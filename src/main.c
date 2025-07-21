#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "server.h"
#include "config.h"
#include "log.h"

int main(int argc, char **argv) {
    ServerConfig config;
    char *config_path = "config.yaml";

    // allow --config <file>
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config_path = argv[++i];
        }
    }

    if (load_config(&config, config_path) != 0) {
        fprintf(stderr, "Error loading configuration from %s\n", config_path);
        return 1;
    }

    if (log_init(&config.logging) != 0) {
        fprintf(stderr, "Error initializing logging");
        exit(EXIT_FAILURE);
    }

    log_message(LOG_LEVEL_INFO, "Starting server on port %d",
        config.port, config.max_connections);

    if (start_server(&config) != 0) {
        log_message(LOG_LEVEL_ERROR, "Error starting server");
        log_shutdown();
        exit(EXIT_FAILURE);
    }

    log_shutdown();
    return EXIT_SUCCESS;
}
