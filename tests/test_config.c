#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"

int main() {
    // Create a temporary configuration file
    const char *temp_filename = "temp_config.yaml";
    FILE *f = fopen(temp_filename, "w");
    if (!f) {
        perror("Unable to open temporary configuration file");
        return 1;
    }
    // Write the expected configuration content
    fprintf(f, "server:\n");
    fprintf(f, "  port: 8080\n");
    fprintf(f, "  max_connections: 100\n");
    fprintf(f, "  log_level: INFO\n");
    fprintf(f, "  routes:\n");
    fprintf(f, "    - path: /static/\n");
    fprintf(f, "      technology: static\n");
    fprintf(f, "      document_root: /var/www/html\n");
    fprintf(f, "    - path: /api/\n");
    fprintf(f, "      technology: reverse_proxy\n");
    fprintf(f, "      backend: 127.0.0.1:5000\n");
    fclose(f);

    ServerConfig config;
    int ret = load_config(&config, temp_filename);
    assert(ret == 0);
    assert(config.port == 8080);
    assert(config.max_connections == 100);
    assert(strcmp(config.log_level, "INFO") == 0);
    assert(config.route_count == 2);
    // Verify the first route
    assert(strcmp(config.routes[0].path, "/static/") == 0);
    assert(strcmp(config.routes[0].technology, "static") == 0);
    assert(strcmp(config.routes[0].document_root, "/var/www/html") == 0);
    // Verify the second route
    assert(strcmp(config.routes[1].path, "/api/") == 0);
    assert(strcmp(config.routes[1].technology, "reverse_proxy") == 0);
    assert(strcmp(config.routes[1].backend, "127.0.0.1:5000") == 0);

    printf("Test load_config passed!\n");

    // Remove the temporary file
    remove(temp_filename);
    return 0;
}
