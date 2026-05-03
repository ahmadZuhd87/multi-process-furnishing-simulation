#include <omp.h>
#include "furnishing.h"

void ms_sleep(int ms)
{
    if (ms <= 0) return;
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

int rand_range(int lo, int hi)
{
    if (hi <= lo) return lo;
    return lo + rand() % (hi - lo + 1);
}

void shuffle(int *arr, int n)
{
    _Pragma("omp parallel for if(n > 100)") for (int i = n - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int tmp = arr[i]; arr[i] = arr[j]; arr[j] = tmp;
    }
}

/* log_event is defined as log_event_ex in simulation.c;
   this version is used by config.c and any file that includes furnishing.h
   outside of simulation.c */
void log_event(int team_id, int member_idx, const char *fmt, ...)
{
    const char *color = (team_id == 0) ? BLUE : RED;
    const char *tname = (team_id == 0) ? "A"  : "B";

    char prefix[64];
    if      (member_idx ==  0) snprintf(prefix, sizeof(prefix), "[Team %s | SOURCE  ]", tname);
    else if (member_idx == -1) snprintf(prefix, sizeof(prefix), "[Team %s | SINK    ]", tname);
    else                       snprintf(prefix, sizeof(prefix), "[Team %s | Member%-2d]", tname, member_idx);

    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    char out[640];
    int len = snprintf(out, sizeof(out), "%s%s %s%s\n", color, prefix, msg, RESET);
    write(STDOUT_FILENO, out, len);
}