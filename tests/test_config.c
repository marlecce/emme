#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"

int main()
{
    // Crea un file di configurazione temporaneo
    const char *temp_filename = "temp_config.yaml";
    FILE *f = fopen(temp_filename, "w");
    if (!f)
    {
        perror("Impossibile aprire il file di configurazione temporaneo");
        return 1;
    }
    // Scrive il contenuto di configurazione atteso.
    // Il nostro parser (in config.c) legge linee del tipo:
    // \" port: 8080\"
    // \" max_connections: 100\"
    // \" log_level: INFO\"
    fprintf(f, "server:\n port: 8080\n max_connections: 100\n log_level: INFO\n");
    fclose(f);

    ServerConfig config;
    int ret = load_config(&config, temp_filename);
    assert(ret == 0);
    assert(config.port == 8080);
    assert(config.max_connections == 100);
    assert(strcmp(config.log_level, "INFO") == 0);

    printf("Test load_config passed!\\n");

    // Rimuove il file temporaneo
    remove(temp_filename);

    return 0;
}
