#include <stdio.h>
#include <stdlib.h>
#include "server.h"
#include "config.h"
#include "log.h"

int main() {
    ServerConfig config;
    char *config_path = "config.yaml";

    if (load_config(&config, config_path) != 0) {
        fprintf(stderr, "Error loading configuration");
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
