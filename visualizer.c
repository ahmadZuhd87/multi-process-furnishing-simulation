#include <GL/glut.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pthread.h>
#include <time.h>

#include "viz_protocol.h"
#include <math.h>
#include <errno.h>

/* ──────────────────────────────────────────────────────────────────
 *  CONFIGURATION & STATE
 * ────────────────────────────────────────────────────────────────── */

#define MAX_NODES        20      /* members per team in the picture   */
#define MAX_PIECES       50      /* concurrent moving pieces          */
#define WIN_W            900
#define WIN_H            500

/* Layout: y-coordinate of each team's pipeline (in window pixels). */
#define TEAM_A_Y         350
#define TEAM_B_Y         180
#define NODE_RADIUS      22

/* One animated piece travelling between two nodes. */
typedef struct {
    int    active;
    char   team;        /* 'A' or 'B' */
    int    serial;
    int    from, to;    /* node indices */
    float  t;           /* 0..1 progress along the segment */
    float  speed;       /* per-frame increment             */
    int    flash;       /* 0=normal, 1=accepted (green), 2=rejected (red) */
    int    flash_ttl;   /* frames remaining for flash      */
} Piece;

/* Global state — guarded by a mutex because the FIFO reader runs
   in a separate thread. */
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;

static int   g_num_nodes  = 4;        /* updated when first MOVE arrives */
static Piece g_pieces[MAX_PIECES];

static int   g_round      = 0;
static int   g_wins_a     = 0;
static int   g_wins_b     = 0;
static int   g_score_a    = 0;
static int   g_score_b    = 0;
static int   g_total      = 6;
static char  g_winner     = '?';
static int   g_finished   = 0;

/* ──────────────────────────────────────────────────────────────────
 *  COLOR PALETTE — 4 colors only, kept minimal
 * ────────────────────────────────────────────────────────────────── */
static void col_bg     (void) { glColor3f(0.10f, 0.12f, 0.16f); }
static void col_team_a (void) { glColor3f(0.30f, 0.65f, 0.95f); } /* blue   */
static void col_team_b (void) { glColor3f(0.95f, 0.45f, 0.40f); } /* red    */
static void col_accept (void) { glColor3f(0.40f, 0.85f, 0.45f); } /* green  */
static void col_text   (void) { glColor3f(0.92f, 0.92f, 0.95f); } /* white  */

/* ──────────────────────────────────────────────────────────────────
 *  GEOMETRY HELPERS
 * ────────────────────────────────────────────────────────────────── */

static float node_x(int idx) {
    /* Nodes are spread evenly between x=80 and x=820. */
    if (g_num_nodes <= 1) return 450.0f;
    float left = 100.0f, right = 800.0f;
    return left + (right - left) * idx / (float)(g_num_nodes - 1);
}

static float node_y(char team) {
    return (team == 'A') ? TEAM_A_Y : TEAM_B_Y;
}

/* Draw filled circle (the OpenGL standard polygon-approximation way). */
static void draw_circle(float cx, float cy, float r, int segments) {
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx, cy);
    for (int i = 0; i <= segments; i++) {
        float a = i * 2.0f * 3.14159f / segments;
        glVertex2f(cx + r * cosf(a), cy + r * sinf(a));
    }
    glEnd();
}

/* Bitmap text using GLUT's built-in font. Simple and dependency-free. */
static void draw_text(float x, float y, const char *str) {
    glRasterPos2f(x, y);
    for (const char *p = str; *p; p++)
        glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, *p);
}

static void draw_text_big(float x, float y, const char *str) {
    glRasterPos2f(x, y);
    for (const char *p = str; *p; p++)
        glutBitmapCharacter(GLUT_BITMAP_HELVETICA_18, *p);
}

/* ──────────────────────────────────────────────────────────────────
 *  RENDERING
 * ────────────────────────────────────────────────────────────────── */

/* Draw one team's pipeline: nodes + connecting line + label. */
static void draw_team_pipeline(char team) {
    float y = node_y(team);
    if (team == 'A') col_team_a(); else col_team_b();

    /* Connecting line through all nodes. */
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    glVertex2f(node_x(0),               y);
    glVertex2f(node_x(g_num_nodes - 1), y);
    glEnd();

    /* Each node as a circle. Source and Sink slightly larger / labelled. */
    for (int i = 0; i < g_num_nodes; i++) {
        float x = node_x(i);
        if (team == 'A') col_team_a(); else col_team_b();
        draw_circle(x, y, NODE_RADIUS, 24);

        /* Inner darker circle for contrast */
        glColor3f(0.10f, 0.12f, 0.16f);
        draw_circle(x, y, NODE_RADIUS - 5, 24);

        /* Label inside */
        col_text();
        char lbl[16];
        if (i == 0)                       snprintf(lbl, sizeof(lbl), "S");
        else if (i == g_num_nodes - 1)    snprintf(lbl, sizeof(lbl), "H");
        else                              snprintf(lbl, sizeof(lbl), "%d", i % 99);
        draw_text(x - 4, y - 5, lbl);
    }

    /* Team label & score on the left */
    col_text();
    char info[64];
    snprintf(info, sizeof(info), "TEAM %c   %d / %d  pieces",
             team, (team == 'A') ? g_score_a : g_score_b, g_total);
    draw_text(20, y + 40, info);
}

/* Draw all currently-active moving pieces. */
static void draw_pieces(void) {
    for (int i = 0; i < MAX_PIECES; i++) {
        Piece *p = &g_pieces[i];
        if (!p->active) continue;

        float x1 = node_x(p->from);
        float x2 = node_x(p->to);
        float y  = node_y(p->team);
        float x  = x1 + (x2 - x1) * p->t;

        /* Choose color based on state */
        if (p->flash == 1) col_accept();
        else if (p->flash == 2) glColor3f(0.95f, 0.30f, 0.30f);
        else if (p->team == 'A') col_team_a();
        else col_team_b();

        /* Draw a small filled square = "furniture piece" */
        glBegin(GL_QUADS);
        glVertex2f(x - 9, y - 9);
        glVertex2f(x + 9, y - 9);
        glVertex2f(x + 9, y + 9);
        glVertex2f(x - 9, y + 9);
        glEnd();

        /* Serial number on top */
        col_text();
        char num[16];
        snprintf(num, sizeof(num), "%d", p->serial);
        draw_text(x - 4, y - 4, num);
    }
}

/* HUD: round, wins, winner banner. */
static void draw_hud(void) {
    col_text();
    char buf[128];

    snprintf(buf, sizeof(buf),
             "Round %d     Wins:  A = %d    B = %d",
             g_round, g_wins_a, g_wins_b);
    draw_text_big(280, 470, buf);

    if (g_winner != '?') {
        if (g_winner == 'A') col_team_a(); else col_team_b();
        snprintf(buf, sizeof(buf), ">>>  TEAM %c WINS THE COMPETITION  <<<",
                 g_winner);
        draw_text_big(220, 30, buf);
    } else {
        col_text();
        draw_text(20, 20, "Press ESC to quit.  Start the simulation in another terminal.");
    }
}

/* GLUT display callback — called every frame. */
static void display(void) {
    col_bg();
    glClear(GL_COLOR_BUFFER_BIT);

    pthread_mutex_lock(&g_mu);
    draw_team_pipeline('A');
    draw_team_pipeline('B');
    draw_pieces();
    draw_hud();
    pthread_mutex_unlock(&g_mu);

    glutSwapBuffers();
}

/* ──────────────────────────────────────────────────────────────────
 *  ANIMATION TICK — advances each piece's position
 * ────────────────────────────────────────────────────────────────── */
static void timer_tick(int v __attribute__((unused))) {
    pthread_mutex_lock(&g_mu);
    for (int i = 0; i < MAX_PIECES; i++) {
        Piece *p = &g_pieces[i];
        if (!p->active) continue;
        p->t += p->speed;
        if (p->t >= 1.0f) {
            /* arrived at destination — clear it. flash pieces stay a bit */
            if (p->flash && p->flash_ttl > 0) {
                p->flash_ttl--;
                p->t = 1.0f;
            } else {
                p->active = 0;
            }
        }
    }
    pthread_mutex_unlock(&g_mu);

    glutPostRedisplay();
    glutTimerFunc(16, timer_tick, 0);   /* ~60 fps */
}

/* ──────────────────────────────────────────────────────────────────
 *  EVENT INGESTION — runs in a background thread
 * ────────────────────────────────────────────────────────────────── */

static void spawn_piece(char team, int from, int to, int serial, int flash) {
    /* Find a free slot */
    for (int i = 0; i < MAX_PIECES; i++) {
        if (!g_pieces[i].active) {
            Piece *p = &g_pieces[i];
            p->active    = 1;
            p->team      = team;
            p->from      = from;
            p->to        = to;
            p->serial    = serial;
            p->t         = 0.0f;
            p->speed     = 0.04f;       /* covers segment in ~25 frames */
            p->flash     = flash;
            p->flash_ttl = (flash) ? 30 : 0;
            return;
        }
    }
}

static void *fifo_reader_thread(void *arg) {
    (void)arg;
    /* Open blocking — will wait until the simulation opens write end */
    int fd = open(VIZ_FIFO_PATH, O_RDONLY);
    if (fd < 0) { perror("open fifo"); return NULL; }

    FILE *f = fdopen(fd, "r");
    if (!f) { perror("fdopen"); return NULL; }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char team;
        int  a, b, c;
        char dir;

        pthread_mutex_lock(&g_mu);

        if (sscanf(line, "MOVE %c %d %d %c %d", &team, &a, &b, &dir, &c) == 5) {
            /* update node count if this team has more members than we knew */
            if (b + 1 > g_num_nodes) g_num_nodes = b + 1;
            if (a + 1 > g_num_nodes) g_num_nodes = a + 1;
            spawn_piece(team, a, b, c, 0);
        }
        else if (sscanf(line, "ACCEPT %c %d", &team, &a) == 2) {
            /* flash a green piece at the sink position */
            spawn_piece(team, g_num_nodes - 1, g_num_nodes - 1, a, 1);
        }
        else if (sscanf(line, "REJECT %c %d", &team, &a) == 2) {
            spawn_piece(team, g_num_nodes - 1, g_num_nodes - 1, a, 2);
        }
        else if (sscanf(line, "SCORE %c %d %d", &team, &a, &b) == 3) {
            if (team == 'A') g_score_a = a; else g_score_b = a;
            g_total = b;
        }
        else if (sscanf(line, "ROUND %d %d %d", &a, &b, &c) == 3) {
            g_round  = a;
            g_wins_a = b;
            g_wins_b = c;
            g_score_a = g_score_b = 0;
        }
        else if (sscanf(line, "WINNER %c", &team) == 1) {
            g_winner = team;
        }
        else if (strncmp(line, "END", 3) == 0) {
            g_finished = 1;
        }

        pthread_mutex_unlock(&g_mu);
    }
    fclose(f);
    return NULL;
}

/* ──────────────────────────────────────────────────────────────────
 *  SETUP
 * ────────────────────────────────────────────────────────────────── */
static void init_gl(void) {
    glClearColor(0.10f, 0.12f, 0.16f, 1.0f);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    /* 2D ortho — origin bottom-left, in pixels */
    gluOrtho2D(0, WIN_W, 0, WIN_H);
    glMatrixMode(GL_MODELVIEW);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

static void keyboard(unsigned char k, int x, int y) {
    (void)x; (void)y;
    if (k == 27) exit(0);   /* ESC */
}

int main(int argc, char **argv) {
    /* Make sure the FIFO exists before the simulation starts */
    unlink(VIZ_FIFO_PATH);
    if (mkfifo(VIZ_FIFO_PATH, 0666) < 0 && errno != EEXIST) {
        perror("mkfifo");
        return 1;
    }

    /* GLUT setup */
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB);
    glutInitWindowSize(WIN_W, WIN_H);
    glutCreateWindow("Furnishing Competition — Visualizer");

    init_gl();
    glutDisplayFunc(display);
    glutKeyboardFunc(keyboard);
    glutTimerFunc(16, timer_tick, 0);

    /* Spawn FIFO reader thread */
    pthread_t tid;
    pthread_create(&tid, NULL, fifo_reader_thread, NULL);
    pthread_detach(tid);

    printf("Visualizer ready. Now run:  make run\n");
    glutMainLoop();
    return 0;
}