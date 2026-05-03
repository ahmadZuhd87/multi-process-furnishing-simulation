
#include "furnishing.h"

int load_config(const char *path, Config *cfg)
{
    /* Defaults */
    cfg->num_members    = 4;
    cfg->num_pieces     = 6;
    cfg->min_delay_ms   = 100;
    cfg->max_delay_ms   = 500;
    cfg->fatigue_factor = 10;
    cfg->wins_to_win    = 3;

    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, YELLOW "[config] '%s' not found – using defaults.\n" RESET, path);
        return 0;
    }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char key[128], val[128];
        if (sscanf(line, "%127[^=]=%127s", key, val) != 2) continue;

        /* Trim trailing whitespace from key */
        char *e = key + strlen(key) - 1;
        while (e > key && (*e == ' ' || *e == '\t')) *e-- = '\0';

        if      (!strcmp(key,"NUM_MEMBERS"))    cfg->num_members    = atoi(val);
        else if (!strcmp(key,"NUM_PIECES"))     cfg->num_pieces     = atoi(val);
        else if (!strcmp(key,"MIN_DELAY"))      cfg->min_delay_ms   = atoi(val);
        else if (!strcmp(key,"MAX_DELAY"))      cfg->max_delay_ms   = atoi(val);
        else if (!strcmp(key,"FATIGUE_FACTOR")) cfg->fatigue_factor = atoi(val);
        else if (!strcmp(key,"WINS_TO_WIN"))    cfg->wins_to_win    = atoi(val);
    }
    fclose(f);

    if (cfg->num_members < 2 || cfg->num_members > MAX_MEMBERS) {
        fprintf(stderr, "ERROR: NUM_MEMBERS must be 2–%d\n", MAX_MEMBERS);
        return -1;
    }
    if (cfg->num_pieces < 1 || cfg->num_pieces > MAX_PIECES) {
        fprintf(stderr, "ERROR: NUM_PIECES must be 1–%d\n", MAX_PIECES);
        return -1;
    }
    return 0;
}

void print_config(const Config *cfg)
{
    printf(BOLD
        "\n╔══════════════════════════════════════╗\n"
          "║     FURNISHING COMPETITION CONFIG    ║\n"
          "╚══════════════════════════════════════╝\n" RESET);
    printf("  Members per team : %d\n",          cfg->num_members);
    printf("  Furniture pieces : %d\n",          cfg->num_pieces);
    printf("  Delay range      : %d – %d ms\n",  cfg->min_delay_ms, cfg->max_delay_ms);
    printf("  Fatigue factor   : %d ms/transfer\n", cfg->fatigue_factor);
    printf("  Wins needed      : %d\n\n",        cfg->wins_to_win);
}

