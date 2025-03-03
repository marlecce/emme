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

    while (fgets(line, sizeof(line), file)) {
        // Rimuove newline e ritorno a capo
        line[strcspn(line, "\r\n")] = '\0';

        // Determina la sezione corrente in base alla riga letta
        if (strstr(line, "server:") != NULL) {
            in_routes = 0;
            in_logging = 0;
            continue;
        }
        else if (strstr(line, "routes:") != NULL) {
            in_routes = 1;
            in_logging = 0;
            continue;
        }
        else if (strstr(line, "logging:") != NULL) {
            in_logging = 1;
            in_routes = 0;
            continue;
        }

        // Parsing delle impostazioni globali (fuori dalle sezioni specifiche)
        if (!in_routes && !in_logging) {
            if (strstr(line, "port:") != NULL) {
                sscanf(line, " port: %d", &config->port);
            }
            else if (strstr(line, "max_connections:") != NULL) {
                sscanf(line, " max_connections: %d", &config->max_connections);
            }
            else if (strstr(line, "log_level:") != NULL) {
                sscanf(line, " log_level: %15s", config->log_level);
            }
        }
        // Parsing della sezione routes (codice già presente, da adattare se necessario)
        else if (in_routes) {
            // Ad esempio, se la riga contiene "- path:" controlla che non si superi il numero massimo di route
            if (strstr(line, "- path:") != NULL) {
                if (config->route_count < MAX_ROUTES) {
                    sscanf(line, " - path: %127s", config->routes[config->route_count].path);
                }
            }
            else if (strstr(line, "technology:") != NULL) {
                if (config->route_count < MAX_ROUTES) {
                    sscanf(line, "   technology: %31s", config->routes[config->route_count].technology);
                }
            }
            else if (strstr(line, "document_root:") != NULL) {
                if (config->route_count < MAX_ROUTES) {
                    sscanf(line, "   document_root: %255s", config->routes[config->route_count].document_root);
                }
            }
            else if (strstr(line, "backend:") != NULL) {
                if (config->route_count < MAX_ROUTES) {
                    sscanf(line, "   backend: %63s", config->routes[config->route_count].backend);
                    config->route_count++;
                }
            }
        }
        // Parsing della sezione logging
        else if (in_logging) {
            if (strstr(line, "file:") != NULL) {
                sscanf(line, " file: %255s", config->logging.file);
            }
            else if (strstr(line, "level:") != NULL) {
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
            }
            else if (strstr(line, "format:") != NULL) {
                char format_str[16];
                sscanf(line, " format: %15s", format_str);
                if (strcmp(format_str, "json") == 0)
                    config->logging.format = LOG_FORMAT_JSON;
                else
                    config->logging.format = LOG_FORMAT_PLAIN;
            }
            else if (strstr(line, "buffer_size:") != NULL) {
                sscanf(line, " buffer_size: %zu", &config->logging.buffer_size);
            }
            else if (strstr(line, "rollover_size:") != NULL) {
                sscanf(line, " rollover_size: %zu", &config->logging.rollover_size);
            }
            else if (strstr(line, "rollover_daily:") != NULL) {
                char daily_str[8];
                sscanf(line, " rollover_daily: %7s", daily_str);
                config->logging.rollover_daily = (strcmp(daily_str, "true") == 0) ? 1 : 0;
            }
            else if (strstr(line, "appender_flags:") != NULL) {
                char flags_str[32];
                sscanf(line, " appender_flags: %31s", flags_str);
                int flags = 0;
                if (strstr(flags_str, "file") != NULL)
                    flags |= APPENDER_FILE;
                if (strstr(flags_str, "console") != NULL)
                    flags |= APPENDER_CONSOLE;
                config->logging.appender_flags = flags;
            }
        }
    }

    fclose(file);
    return 0;
}
