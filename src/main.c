#include <stdio.h>
#include <stdlib.h>
#include "server.h"
#include "config.h"

int main() {
    ServerConfig config;
    char *config_path = "config.yaml"; 
    if (load_config(&config, config_path) != 0) {
        fprintf(stderr, "Errore nel caricamento della configurazione\n");
        return 1;
    }

    printf("Avvio del server sulla porta %d con %d connessioni massime...\n", 
           config.port, config.max_connections);

    if (start_server(&config) != 0) {
        fprintf(stderr, "Errore nell'avvio del server\n");
        return 1;
    }

    return 0;
}
