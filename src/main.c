/*
 * main.c  –  Entry point. Reads config, seeds RNG, starts simulation.
 */
#include "furnishing.h"

int main(int argc, char *argv[])
{
    const char *config_path = (argc > 1) ? argv[1] : "config.txt";

    Config cfg;
    if (load_config(config_path, &cfg) != 0) {
        fprintf(stderr, "Fatal: bad configuration.\n");
        return EXIT_FAILURE;
    }

    print_config(&cfg);

    /* Each process re-seeds after fork; this seeds the parent */
    srand((unsigned)time(NULL));

    run_simulation(&cfg);

    return EXIT_SUCCESS;
}