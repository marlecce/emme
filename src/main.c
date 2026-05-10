#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "server.h"
#include "config.h"
#include "log.h"
#include "metrics.h"

#define DEFAULT_METRICS_PORT 9090
#define MAX_PORT_NUMBER 65535

int main(int argc, char **argv) {
    ServerConfig config;
    char *config_path = "config.yaml";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config_path = argv[++i];
        }
    }

    const char *env_config = getenv("EMME_CONFIG_PATH");
    if (env_config) {
        config_path = (char *)env_config;
    }

    if (load_config(&config, config_path) != 0) {
        fprintf(stderr, "Error loading configuration from %s\n", config_path);
        return 1;
    }

    apply_env_overrides(&config);

    if (log_init(&config.logging) != 0) {
        fprintf(stderr, "Error initializing logging\n");
        exit(EXIT_FAILURE);
    }

    metrics_init();
    
    const char *env_metrics_port = getenv("EMME_METRICS_PORT");
    int metrics_port = DEFAULT_METRICS_PORT;
    if (env_metrics_port) {
        char *endptr;
        long port = strtol(env_metrics_port, &endptr, 10);
        if (*endptr == '\0' && port > 0 && port <= MAX_PORT_NUMBER) {
            metrics_port = (int)port;
        } else {
            log_message(LOG_LEVEL_WARN, "Invalid EMME_METRICS_PORT value '%s', using default %d", 
                       env_metrics_port, metrics_port);
        }
    }
    
    if (metrics_start_server(metrics_port) != 0) {
        log_message(LOG_LEVEL_ERROR, "Failed to start metrics server on port %d", metrics_port);
    }

    log_message(LOG_LEVEL_INFO, "Starting server on port %d (max_connections=%d)",
        config.port, config.max_connections);

    if (start_server(&config) != 0) {
        log_message(LOG_LEVEL_ERROR, "Error starting server");
        metrics_shutdown();
        log_shutdown();
        exit(EXIT_FAILURE);
    }

    metrics_shutdown();
    log_shutdown();
    return EXIT_SUCCESS;
}
