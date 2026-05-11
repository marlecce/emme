#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "server.h"
#include "config.h"
#include "log.h"
#include "metrics.h"
#include "backend_pool.h"

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

    for (int i = 0; i < config.route_count; i++) {
        Route *route = &config.routes[i];
        if (route->http2_enabled && route->backend[0] != '\0') {
            char host[256];
            int port;
            if (parse_backend_url(route->backend, host, sizeof(host), &port) == 0) {
                route->pool = backend_pool_create(host, port, route->tls_enabled, 
                                                   route->tls_verify, 
                                                   route->connection_pool.size);
                if (route->pool) {
                    log_message(LOG_LEVEL_INFO, "Created connection pool for %s (size=%d)",
                               route->backend, route->connection_pool.size);
                    
                    if (route->health_check.enabled) {
                        if (backend_pool_start_health_checker(route->pool, 
                                                              &route->health_check) == 0) {
                            log_message(LOG_LEVEL_INFO, "Health checker started for %s",
                                       route->backend);
                        }
                    }
                    
                    if (route->circuit_breaker.enabled) {
                        if (backend_pool_init_circuit_breaker(route->pool, 
                                                              &route->circuit_breaker) == 0) {
                            log_message(LOG_LEVEL_INFO, "Circuit breaker initialized for %s",
                                       route->backend);
                        }
                    }
                } else {
                    log_message(LOG_LEVEL_ERROR, "Failed to create pool for %s", route->backend);
                }
            }
        }
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
