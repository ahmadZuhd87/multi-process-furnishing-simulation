/*
 * furnishing.h  –  Shared types, constants, and declarations.
 *
 * IPC Design:
 *   PIPES  → forward path  (source → member_1 → ... → sink)
 *   FIFOs  → backward path (sink → source) for rejected pieces
 *   SIGNALS→ round-level events (SIGUSR1 = round won, SIGTERM = stop)
 */
#ifndef FURNISHING_H
#define FURNISHING_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <stdarg.h>

/* ── Limits ──────────────────────────────────────────────────────────── */
#define MAX_MEMBERS   20
#define MAX_PIECES    1000
#define FIFO_PATH_LEN 64

/* ── Message passed through pipes and FIFOs ──────────────────────────── */
/* Message types */
#define MSG_FORWARD 0  
#define MSG_REJECT  1   
#define MSG_ACCEPT  2   

typedef struct {
    int serial;
    int team_id;
    int type;         /* MSG_FORWARD | MSG_REJECT | MSG_ACCEPT */
    int transfers;
} FurnitureMsg;


/* ── Configuration (loaded from config.txt) ──────────────────────────── */
typedef struct {
    int num_members;    /* processes per team (includes source+sink) */
    int num_pieces;     /* furniture pieces per team                 */
    int min_delay_ms;
    int max_delay_ms;
    int fatigue_factor; /* extra ms added per accepted transfer      */
    int wins_to_win;    /* rounds to win the competition             */
} Config;

/* ── Per-team state (lives in parent/coordinator process) ────────────── */
typedef struct {
    int   team_id;
    int   wins;
    pid_t member_pids[MAX_MEMBERS];
    int   fwd_pipes[MAX_MEMBERS][2];  /* fwd[i] = member i → i+1   */
    int   bwd_pipes[MAX_MEMBERS][2];  /* bwd[i] = member i+1 → i   */
  
} TeamState;

/* ── ANSI colours ────────────────────────────────────────────────────── */
#define RESET   "\033[0m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define CYAN    "\033[36m"
#define BOLD    "\033[1m"

/* ── Function declarations ───────────────────────────────────────────── */
int  load_config(const char *path, Config *cfg);
void print_config(const Config *cfg);
void ms_sleep(int ms);
int  rand_range(int lo, int hi);
void shuffle(int *arr, int n);
void log_event(int team_id, int member_idx, const char *fmt, ...);
void run_simulation(Config *cfg);

#endif /* FURNISHING_H */