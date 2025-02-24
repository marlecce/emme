#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"

int load_config(ServerConfig *config, const char *file_path) {
    FILE *file = fopen(file_path, "r");
    if (!file) {
        perror("Error opening the configuration file");
        return -1;
    }

    memset(config, 0, sizeof(ServerConfig));
    char line[256];
    int in_routes = 0;
    
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\r\n")] = '\0';
        
        if (strstr(line, "server:") != NULL) {
            continue;
        }
        else if (strstr(line, "port:") != NULL) {
            sscanf(line, " port: %d", &config->port);
        } 
        else if (strstr(line, "max_connections:") != NULL) {
            sscanf(line, " max_connections: %d", &config->max_connections);
        } 
        else if (strstr(line, "log_level:") != NULL) {
            sscanf(line, " log_level: %15s", config->log_level);
        } 
        else if (strstr(line, "routes:") != NULL) {
            in_routes = 1;
        } 
        else if (in_routes) {
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
    }
    
    fclose(file);
    return 0;
}