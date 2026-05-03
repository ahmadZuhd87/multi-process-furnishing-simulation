#include "furnishing.h"
#include "viz_protocol.h"
#include <stdarg.h>
#include <sys/select.h>

/* ──────────────────────────────────────────────────────────────────────
 * SIGNAL HANDLING
 * ────────────────────────────────────────────────────────────────────── */
static volatile sig_atomic_t g_stop = 0;
static void handle_sigterm(int sig) { (void)sig; g_stop = 1; }

/* Coordinator signal handlers — one per team. */
static volatile sig_atomic_t g_round_won_by = -1;
static void handle_sigusr1(int sig) { (void)sig; g_round_won_by = 0; }
static void handle_sigusr2(int sig) { (void)sig; g_round_won_by = 1; }

/* ──────────────────────────────────────────────────────────────────────
 * VISUALIZER HOOK
 *   - Each child opens the FIFO once (lazy) and writes short text events.
 *   - O_NONBLOCK so we never block when no visualizer is connected.
 * ────────────────────────────────────────────────────────────────────── */
static int g_viz_fd = -2;   /* -2 = not yet tried, -1 = unavailable */

static void viz_emit(const char *fmt, ...)
{
    if (g_viz_fd == -2) {
        g_viz_fd = open(VIZ_FIFO_PATH, O_WRONLY | O_NONBLOCK);
        /* If the visualizer isn't running, we just disable hooks. */
    }
    if (g_viz_fd < 0) return;

    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) {
        ssize_t w = write(g_viz_fd, buf, (size_t)n);
        (void)w;  /* ignore short writes / EAGAIN */
    }
}

static char team_char(int team_id) { return (team_id == 0) ? 'A' : 'B'; }

/* ──────────────────────────────────────────────────────────────────────
 * SOURCE PROCESS  (member index 0)
 * Sends one piece, waits for ACCEPT or REJECT, then sends the next.
 * ────────────────────────────────────────────────────────────────────── */
static void run_source(int team_id, int num_pieces,
                       int fwd_write_fd, int bwd_read_fd,
                       int min_delay, int max_delay, int fatigue)
{
    signal(SIGTERM, handle_sigterm);

    /* Build a shuffled deck of piece serials. */
    int deck[MAX_PIECES];
    for (int i = 0; i < num_pieces; i++) deck[i] = i + 1;
    shuffle(deck, num_pieces);

    /* available[] = pieces that may currently be picked
       last_rejected = the piece we shouldn't pick again immediately      */
    int available[MAX_PIECES];
    int nav = num_pieces;
    for (int i = 0; i < num_pieces; i++) available[i] = deck[i];

    int placed        = 0;
    int fatigue_ms    = 0;
    int last_rejected = -1;

    while (!g_stop && placed < num_pieces) {

        if (nav == 0) { ms_sleep(20); continue; }

        /* Pick a piece, but try to avoid the most recently rejected one. */
        int idx, serial, attempts = 0;
        do {
            idx    = rand_range(0, nav - 1);
            serial = available[idx];
            attempts++;
        } while (serial == last_rejected && nav > 1 && attempts < 10);

        available[idx] = available[--nav];

        /* Simulate picking up the piece. */
        ms_sleep(rand_range(min_delay, max_delay) + fatigue_ms);

        FurnitureMsg msg = { serial, team_id, MSG_FORWARD, 0 };
        log_event(team_id, 0, "→ Sending piece #%d forward to Member1.", serial);
        viz_emit("MOVE %c 0 1 F %d\n", team_char(team_id), serial);

        if (write(fwd_write_fd, &msg, sizeof(msg)) != sizeof(msg)) break;

        /* Block on the backward pipe until we hear back about this piece. */
        FurnitureMsg reply;
        ssize_t n = read(bwd_read_fd, &reply, sizeof(reply));
        if (n <= 0) break;
        fatigue_ms += fatigue;
        if (reply.type == MSG_ACCEPT) {
            placed++;
            last_rejected = -1;
            log_event(team_id, 0, GREEN "✓ Confirmed: piece #%d placed (%d/%d). [fatigue=%dms]" RESET,
            reply.serial, placed, num_pieces, fatigue_ms);
          } else {
            log_event(team_id, 0,
                YELLOW "← Piece #%d REJECTED, returned to deck." RESET,
                reply.serial);
            available[nav++] = reply.serial;
            last_rejected    = reply.serial;
        }
    }

    close(fwd_write_fd);
    close(bwd_read_fd);
    log_event(team_id, 0, "Source done. Exiting.");
    exit(0);
}

/* ──────────────────────────────────────────────────────────────────────
 * MIDDLE MEMBER PROCESS
 * Listens on both forward (incoming) and backward (incoming) pipes.
 * Backward direction has priority so rejects/ACKs travel fast.
 * ────────────────────────────────────────────────────────────────────── */
static void run_member(int team_id, int idx,
                       int fwd_in_fd,   /* read  from member idx-1 */
                       int fwd_out_fd,  /* write to   member idx+1 */
                       int bwd_in_fd,   /* read  from member idx+1 */
                       int bwd_out_fd,  /* write to   member idx-1 */
                       int min_delay, int max_delay, int fatigue)
{
    signal(SIGTERM, handle_sigterm);
    int fatigue_ms = 0;

    while (!g_stop) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fwd_in_fd, &rfds);
        FD_SET(bwd_in_fd, &rfds);
        int maxfd = (fwd_in_fd > bwd_in_fd) ? fwd_in_fd : bwd_in_fd;

        int rv = select(maxfd + 1, &rfds, NULL, NULL, NULL);
        if (rv < 0) {
            if (errno == EINTR) continue;
            break;
        }

        FurnitureMsg msg;

        /* ── Backward priority: handle rejects/ACKs first. ── */
        if (FD_ISSET(bwd_in_fd, &rfds)) {
            ssize_t n = read(bwd_in_fd, &msg, sizeof(msg));
            if (n <= 0) break;

            ms_sleep(rand_range(min_delay, max_delay) + fatigue_ms);
            fatigue_ms += fatigue;

            const char *kind = (msg.type == MSG_REJECT) ? "rejected" : "ACK";
            log_event(team_id, idx,
                "← Returning %s piece #%d back toward Source.",
                kind, msg.serial);
            viz_emit("MOVE %c %d %d B %d\n",
                     team_char(team_id), idx, idx - 1, msg.serial);

            msg.transfers++;
            if (write(bwd_out_fd, &msg, sizeof(msg)) != sizeof(msg)) break;
            continue;   /* re-check backward before forward */
        }

        /* ── Forward only when backward is empty. ── */
        if (FD_ISSET(fwd_in_fd, &rfds)) {
            ssize_t n = read(fwd_in_fd, &msg, sizeof(msg));
            if (n <= 0) break;

            ms_sleep(rand_range(min_delay, max_delay) + fatigue_ms);
            fatigue_ms += fatigue;

            log_event(team_id, idx,
                "→ Passing piece #%d forward to Member%d.",
                msg.serial, idx + 1);
            viz_emit("MOVE %c %d %d F %d\n",
                     team_char(team_id), idx, idx + 1, msg.serial);

            msg.transfers++;
            if (write(fwd_out_fd, &msg, sizeof(msg)) != sizeof(msg)) break;
        }
    }

    close(fwd_in_fd);  close(fwd_out_fd);
    close(bwd_in_fd);  close(bwd_out_fd);
    exit(0);
}

/* ──────────────────────────────────────────────────────────────────────
 * SINK PROCESS  (last member)
 * Accepts pieces in strict numeric order; rejects others by sending
 * MSG_REJECT backward. Sends MSG_ACCEPT backward as ACK on success.
 * ────────────────────────────────────────────────────────────────────── */
static void run_sink(int team_id, int num_pieces,
                     int fwd_in_fd, int bwd_out_fd,
                     int min_delay, int max_delay, int fatigue,
                     pid_t coordinator_pid)
{
    signal(SIGTERM, handle_sigterm);

    int next_expected = 1;
    int placed        = 0;
    int fatigue_ms    = 0;

    while (!g_stop && placed < num_pieces) {
        FurnitureMsg msg;
        ssize_t n = read(fwd_in_fd, &msg, sizeof(msg));
        if (n <= 0) break;

        ms_sleep(rand_range(min_delay, max_delay) + fatigue_ms);
        fatigue_ms += fatigue;

        if (msg.serial == next_expected) {
            placed++;
            next_expected++;
            log_event(team_id, -1,
                GREEN "✓ Piece #%d ACCEPTED! (%d/%d placed)" RESET,
                msg.serial, placed, num_pieces);
            viz_emit("ACCEPT %c %d\n", team_char(team_id), msg.serial);
            viz_emit("SCORE %c %d %d\n",
                     team_char(team_id), placed, num_pieces);

            msg.type = MSG_ACCEPT;
            write(bwd_out_fd, &msg, sizeof(msg));
        } else {
            log_event(team_id, -1,
                RED "✗ Piece #%d REJECTED (expected #%d), sending back." RESET,
                msg.serial, next_expected);
            viz_emit("REJECT %c %d\n", team_char(team_id), msg.serial);

            msg.type = MSG_REJECT;
            write(bwd_out_fd, &msg, sizeof(msg));
        }
    }

    if (placed == num_pieces) {
        printf(BOLD GREEN
            "\n🏆  Team %c finished the round! Notifying coordinator...\n"
            RESET, team_char(team_id));
        kill(coordinator_pid,
             (team_id == 0) ? SIGUSR1 : SIGUSR2);
    }

    close(fwd_in_fd);
    close(bwd_out_fd);
    exit(0);
}

/* ──────────────────────────────────────────────────────────────────────
 * TEAM SETUP — create pipes, fork all members, wire fds correctly
 * ────────────────────────────────────────────────────────────────────── */
static void setup_team(TeamState *t, const Config *cfg, pid_t coord_pid)
{
    int N = cfg->num_members;

    /* Create N-1 forward and N-1 backward pipes. */
    for (int i = 0; i < N - 1; i++) {
        if (pipe(t->fwd_pipes[i]) == -1) { perror("pipe fwd"); exit(1); }
        if (pipe(t->bwd_pipes[i]) == -1) { perror("pipe bwd"); exit(1); }
    }

    for (int i = 0; i < N; i++) {
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); exit(1); }

        if (pid == 0) {
            /* ── CHILD ── */
            srand((unsigned)(time(NULL) ^ (getpid() << 8)));

            if (i == 0) {
                /* SOURCE: writes fwd[0], reads bwd[0]. */
                int fwd_w = t->fwd_pipes[0][1];
                int bwd_r = t->bwd_pipes[0][0];

                for (int j = 0; j < N - 1; j++) {
                    close(t->fwd_pipes[j][0]);
                    if (j != 0) close(t->fwd_pipes[j][1]);
                    if (j != 0) close(t->bwd_pipes[j][0]);
                    close(t->bwd_pipes[j][1]);
                }
                run_source(t->team_id, cfg->num_pieces, fwd_w, bwd_r,
                           cfg->min_delay_ms, cfg->max_delay_ms,
                           cfg->fatigue_factor);

            } else if (i == N - 1) {
                /* SINK: reads fwd[N-2], writes bwd[N-2]. */
                int fwd_r = t->fwd_pipes[N - 2][0];
                int bwd_w = t->bwd_pipes[N - 2][1];

                for (int j = 0; j < N - 1; j++) {
                    if (j != N - 2) close(t->fwd_pipes[j][0]);
                    close(t->fwd_pipes[j][1]);
                    close(t->bwd_pipes[j][0]);
                    if (j != N - 2) close(t->bwd_pipes[j][1]);
                }
                run_sink(t->team_id, cfg->num_pieces, fwd_r, bwd_w,
                         cfg->min_delay_ms, cfg->max_delay_ms,
                         cfg->fatigue_factor, coord_pid);

            } else {
                /* MIDDLE i:
                 *   fwd_in  = fwd[i-1][0]   (from member i-1)
                 *   fwd_out = fwd[i][1]     (to   member i+1)
                 *   bwd_in  = bwd[i][0]     (from member i+1)
                 *   bwd_out = bwd[i-1][1]   (to   member i-1)
                 */
                int fwd_in  = t->fwd_pipes[i - 1][0];
                int fwd_out = t->fwd_pipes[i][1];
                int bwd_in  = t->bwd_pipes[i][0];
                int bwd_out = t->bwd_pipes[i - 1][1];

                for (int j = 0; j < N - 1; j++) {
                    if (j != i - 1) close(t->fwd_pipes[j][0]);
                    if (j != i)     close(t->fwd_pipes[j][1]);
                    if (j != i)     close(t->bwd_pipes[j][0]);
                    if (j != i - 1) close(t->bwd_pipes[j][1]);
                }

                run_member(t->team_id, i, fwd_in, fwd_out, bwd_in, bwd_out,
                           cfg->min_delay_ms, cfg->max_delay_ms,
                           cfg->fatigue_factor);
            }
      
        }

        /* ── PARENT ── */
        t->member_pids[i] = pid;
    }

    /* Parent closes ALL pipe ends — children own them now. */
    for (int i = 0; i < N - 1; i++) {
        close(t->fwd_pipes[i][0]); close(t->fwd_pipes[i][1]);
        close(t->bwd_pipes[i][0]); close(t->bwd_pipes[i][1]);
    }
}

/* ──────────────────────────────────────────────────────────────────────
 * TEARDOWN — terminate all team members and reap them
 * ────────────────────────────────────────────────────────────────────── */
static void teardown_team(TeamState *t, const Config *cfg)
{
    for (int i = 0; i < cfg->num_members; i++)
        if (t->member_pids[i] > 0) kill(t->member_pids[i], SIGTERM);

    for (int i = 0; i < cfg->num_members; i++)
        if (t->member_pids[i] > 0) waitpid(t->member_pids[i], NULL, 0);
}

/* ──────────────────────────────────────────────────────────────────────
 * TOP-LEVEL DRIVER
 * ────────────────────────────────────────────────────────────────────── */
void run_simulation(Config *cfg)
{
    /* Coordinator handles team-win notifications. */
    signal(SIGUSR1, handle_sigusr1);
    signal(SIGUSR2, handle_sigusr2);

    /* Try to connect to the visualizer (no-op if it isn't running). */
    g_viz_fd = open(VIZ_FIFO_PATH, O_WRONLY | O_NONBLOCK);

    TeamState teams[2] = {0};
    teams[0].team_id = 0;
    teams[1].team_id = 1;

    int round = 1;

    printf(BOLD "\n🏠  HOME FURNISHING COMPETITION BEGINS!\n\n" RESET);

    while (teams[0].wins < cfg->wins_to_win &&
           teams[1].wins < cfg->wins_to_win)
    {
        printf(BOLD CYAN
            "\n══════════════════════════════════════\n"
              "  ROUND %d  (A: %d wins | B: %d wins)\n"
              "══════════════════════════════════════\n" RESET,
            round, teams[0].wins, teams[1].wins);

        viz_emit("ROUND %d %d %d\n", round, teams[0].wins, teams[1].wins);

        g_round_won_by = -1;

        pid_t coord_pid = getpid();
        setup_team(&teams[0], cfg, coord_pid);
        setup_team(&teams[1], cfg, coord_pid);

        /* Wait for either SIGUSR1 or SIGUSR2 from a sink. */
        while (g_round_won_by == -1) pause();

        int winner = g_round_won_by;

        /* End the round cleanly for both teams. */
        teardown_team(&teams[0], cfg);
        teardown_team(&teams[1], cfg);

        teams[winner].wins++;
    viz_emit("ROUND %d %d %d\n", round, teams[0].wins, teams[1].wins);

        printf(BOLD GREEN
            "\n🎉  Team %c wins Round %d!  (A: %d | B: %d)\n" RESET,
            (winner == 0) ? 'A' : 'B', round,
            teams[0].wins, teams[1].wins);

        round++;
        sleep(1);
    }

    int champ = (teams[0].wins >= cfg->wins_to_win) ? 0 : 1;

    printf(BOLD YELLOW
        "\n╔══════════════════════════════════════╗\n"
          "║  🏆  COMPETITION WINNER: TEAM %c      ║\n"
          "║  Final score: A=%d  B=%d              ║\n"
          "╚══════════════════════════════════════╝\n" RESET,
        (champ == 0) ? 'A' : 'B',
        teams[0].wins, teams[1].wins);

    viz_emit("WINNER %c\n", (champ == 0) ? 'A' : 'B');
    viz_emit("ROUND %d %d %d\n", round, teams[0].wins, teams[1].wins);
    viz_emit("SCORE A 0 %d\n", cfg->num_pieces);
    viz_emit("SCORE B 0 %d\n", cfg->num_pieces);

    g_round_won_by = -1;
    viz_emit("END\n");

    if (g_viz_fd >= 0) close(g_viz_fd);
}