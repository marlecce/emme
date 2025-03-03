#include <stdio.h>
#include <stdlib.h>
#include "server.h"
#include "config.h"
#include "log.h"

int main() {
    ServerConfig config;
    char *config_path = "config.yaml";

    if (load_config(&config, config_path) != 0) {
        fprintf(stderr, "Errore nel caricamento della configurazione\n");
        return 1;
    }

    if (log_init(&config.logging) != 0) {
        fprintf(stderr, "Errore nell'inizializzazione del logging\n");
        exit(EXIT_FAILURE);
    }

    log_message(LOG_LEVEL_INFO, "Avvio del server sulla porta %d con %d connessioni massime",
        config.port, config.max_connections);

    if (start_server(&config) != 0) {
        log_message(LOG_LEVEL_ERROR, "Errore nell'avvio del server");
        log_shutdown();
        exit(EXIT_FAILURE);
    }

    log_shutdown();
    return EXIT_SUCCESS;
}
