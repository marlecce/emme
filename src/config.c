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
    int in_ssl = 0;

    while (fgets(line, sizeof(line), file)) {
        /* Rimuove newline e ritorno a capo */
        line[strcspn(line, "\r\n")] = '\0';

        /* Determina la sezione corrente in base alla riga letta */
        if (strstr(line, "server:") != NULL) {
            in_routes = 0;
            in_logging = 0;
            in_appender_flags = 0;
            in_ssl = 0;
            continue;
        } else if (strstr(line, "routes:") != NULL) {
            in_routes = 1;
            in_logging = 0;
            in_appender_flags = 0;
            in_ssl = 0;
            continue;
        } else if (strstr(line, "logging:") != NULL) {
            in_logging = 1;
            in_routes = 0;
            in_appender_flags = 0;
            in_ssl = 0;
            continue;
        } else if (strstr(line, "ssl:") != NULL) {
            in_ssl = 1;
            in_logging = 0;
            in_routes = 0;
            in_appender_flags = 0;
            continue;
        }

        /* Parsing della sezione logging per l'array appender_flags */
        if (in_logging && strstr(line, "appender_flags:") != NULL) {
            in_appender_flags = 1;
            config->logging.appender_flags = 0;
            continue;
        }
        if (in_logging && in_appender_flags) {
            char *ptr = line;
            while (*ptr == ' ' || *ptr == '\t') ptr++; // Rimuove spazi iniziali
            if (*ptr == '-') {
                ptr++; // Salta il trattino
                while (*ptr == ' ' || *ptr == '\t') ptr++; // Rimuove spazi dopo il trattino
                if (strcmp(ptr, "file") == 0)
                    config->logging.appender_flags |= APPENDER_FILE;
                else if (strcmp(ptr, "console") == 0)
                    config->logging.appender_flags |= APPENDER_CONSOLE;
                continue;
            } else {
                in_appender_flags = 0;
            }
        }

        /* Parsing delle impostazioni globali (fuori dalle sezioni specifiche) */
        if (!in_routes && !in_logging && !in_ssl) {
            if (strstr(line, "port:") != NULL) {
                sscanf(line, " port: %d", &config->port);
            } else if (strstr(line, "max_connections:") != NULL) {
                sscanf(line, " max_connections: %d", &config->max_connections);
            } else if (strstr(line, "log_level:") != NULL) {
                sscanf(line, " log_level: %15s", config->log_level);
            }
        }
        /* Parsing della sezione routes */
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
        /* Parsing della sezione logging (escluso l'array appender_flags, già gestito) */
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
        /* Parsing della sezione SSL */
        else if (in_ssl) {
            /* Se la riga non è indentata, consideriamo che la sezione SSL sia terminata */
            if (line[0] != ' ' && line[0] != '\t') {
                in_ssl = 0;
                /* Fall-through per permettere il parsing di questa riga nella sezione globale */
            } else {
                /* Rimuovi eventuali spazi iniziali */
                char *ptr = line;
                while (*ptr == ' ' || *ptr == '\t') ptr++;
                if (strstr(ptr, "certificate:") == ptr) {
                    sscanf(ptr, "certificate: %255s", config->ssl.certificate);
                } else if (strstr(ptr, "private_key:") == ptr) {
                    sscanf(ptr, "private_key: %255s", config->ssl.private_key);
                }
                continue; // Continua a leggere le righe della sezione SSL
            }
        }
    }

    fclose(file);
    return 0;
}
