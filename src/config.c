#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"

int load_config(ServerConfig *config, const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("Errore nell'apertura del file di configurazione");
        return -1;
    }
    
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        if (sscanf(line, " port: %d", &config->port) == 1) {
            continue;
        } else if (sscanf(line, " max_connections: %d", &config->max_connections) == 1) {
            continue;
        } else if (sscanf(line, " log_level: %s", config->log_level) == 1) {
            continue;
        }
    }
    
    fclose(file);
    return 0;
}
