#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "log.h"

int load_config(ServerConfig *config, const char *file_path) {
    FILE *file = fopen(file_path, "r");
    if (!file) {
        perror("Errore nell'apertura del file di configurazione");
        return -1;
    }

    memset(config, 0, sizeof(ServerConfig));
    char line[256];
    int in_routes = 0;
    int in_logging = 0;
    int in_appender_flags = 0;

    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\r\n")] = '\0';

        if (strstr(line, "server:") != NULL) {
            in_routes = 0;
            in_logging = 0;
            in_appender_flags = 0;
            continue;
        } else if (strstr(line, "routes:") != NULL) {
            in_routes = 1;
            in_logging = 0;
            in_appender_flags = 0;
            continue;
        } else if (strstr(line, "logging:") != NULL) {
            in_logging = 1;
            in_routes = 0;
            in_appender_flags = 0;
            continue;
        }

        if (in_logging && strstr(line, "appender_flags:") != NULL) {
            in_appender_flags = 1;
            config->logging.appender_flags = 0;
            continue;
        }
        
        if (in_logging && in_appender_flags) {
            char *ptr = line;
            while (*ptr == ' ' || *ptr == '\t') ptr++; 
            if (*ptr == '-' ) {
                ptr++;
                while (*ptr == ' ' || *ptr == '\t') ptr++;
                if (strcmp(ptr, "file") == 0)
                    config->logging.appender_flags |= APPENDER_FILE;
                else if (strcmp(ptr, "console") == 0)
                    config->logging.appender_flags |= APPENDER_CONSOLE;
                continue;
            } else {
                in_appender_flags = 0;
            }
        }

        if (!in_routes && !in_logging) {
            if (strstr(line, "port:") != NULL) {
                sscanf(line, " port: %d", &config->port);
            } else if (strstr(line, "max_connections:") != NULL) {
                sscanf(line, " max_connections: %d", &config->max_connections);
            } else if (strstr(line, "log_level:") != NULL) {
                sscanf(line, " log_level: %15s", config->log_level);
            }
        }
        else if (in_routes) {
            if (strstr(line, "- path:") != NULL) {
                if (config->route_count < MAX_ROUTES) {
                    sscanf(line, " - path: %127s", config->routes[config->route_count].path);
                }
            } else if (strstr(line, "technology:") != NULL) {
                if (config->route_count < MAX_ROUTES) {
                    sscanf(line, "   technology: %31s", config->routes[config->route_count].technology);
                }
            } else if (strstr(line, "document_root:") != NULL) {
                if (config->route_count < MAX_ROUTES) {
                    sscanf(line, "   document_root: %255s", config->routes[config->route_count].document_root);
                }
            } else if (strstr(line, "backend:") != NULL) {
                if (config->route_count < MAX_ROUTES) {
                    sscanf(line, "   backend: %63s", config->routes[config->route_count].backend);
                    config->route_count++;
                }
            }
        }
        else if (in_logging && !in_appender_flags) {
            if (strstr(line, "file:") != NULL) {
                sscanf(line, " file: %255s", config->logging.file);
            } else if (strstr(line, "level:") != NULL) {
                char level_str[16];
                sscanf(line, " level: %15s", level_str);
                if (strcmp(level_str, "debug") == 0)
                    config->logging.level = LOG_LEVEL_DEBUG;
                else if (strcmp(level_str, "info") == 0)
                    config->logging.level = LOG_LEVEL_INFO;
                else if (strcmp(level_str, "warn") == 0)
                    config->logging.level = LOG_LEVEL_WARN;
                else if (strcmp(level_str, "error") == 0)
                    config->logging.level = LOG_LEVEL_ERROR;
            } else if (strstr(line, "format:") != NULL) {
                char format_str[16];
                sscanf(line, " format: %15s", format_str);
                config->logging.format = (strcmp(format_str, "json") == 0) ? LOG_FORMAT_JSON : LOG_FORMAT_PLAIN;
            } else if (strstr(line, "buffer_size:") != NULL) {
                sscanf(line, " buffer_size: %zu", &config->logging.buffer_size);
            } else if (strstr(line, "rollover_size:") != NULL) {
                sscanf(line, " rollover_size: %zu", &config->logging.rollover_size);
            } else if (strstr(line, "rollover_daily:") != NULL) {
                char daily_str[8];
                sscanf(line, " rollover_daily: %7s", daily_str);
                config->logging.rollover_daily = (strcmp(daily_str, "true") == 0) ? 1 : 0;
            }
        }
    }

    fclose(file);
    return 0;
}
