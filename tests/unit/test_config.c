#include <criterion/criterion.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"

Test(config, load_full_config_yaml)
{
    const char *temp_filename = "temp_config.yaml";
    FILE *f = fopen(temp_filename, "w");
    cr_assert_not_null(f, "Unable to open temporary configuration file");

    // Write a more complete configuration
    fprintf(f, "server:\n");
    fprintf(f, "  port: 8443\n");
    fprintf(f, "  max_connections: 100\n");
    fprintf(f, "  log_level: DEBUG\n");
    fprintf(f, "ssl:\n");
    fprintf(f, "  certificate: /etc/ssl/certs/server.crt\n");
    fprintf(f, "  private_key: /etc/ssl/private/server.key\n");
    fprintf(f, "routes:\n");
    fprintf(f, "  - path: /static/\n");
    fprintf(f, "    technology: static\n");
    fprintf(f, "    document_root: /var/www/html\n");
    fprintf(f, "  - path: /api/\n");
    fprintf(f, "    technology: reverse_proxy\n");
    fprintf(f, "    backend: 127.0.0.1:5000\n");
    fprintf(f, "  - path: /ws/\n");
    fprintf(f, "    technology: websocket\n");
    fprintf(f, "    backend: 127.0.0.1:6000\n");
    fclose(f);

    ServerConfig config;
    int ret = load_config(&config, temp_filename);
    cr_assert_eq(ret, 0, "Config should load successfully");
    cr_assert_eq(config.port, 8443, "Port should be 8443");
    cr_assert_eq(config.max_connections, 100, "Max connections should be 100");
    cr_assert_str_eq(config.log_level, "DEBUG", "Log level should be DEBUG");

    // SSL section
    cr_assert_str_eq(config.ssl.certificate, "/etc/ssl/certs/server.crt", "SSL certificate mismatch");
    cr_assert_str_eq(config.ssl.private_key, "/etc/ssl/private/server.key", "SSL private_key mismatch");

    // Routes
    cr_assert_eq(config.route_count, 3, "Route count should be 3");

    // First route
    cr_assert_str_eq(config.routes[0].path, "/static/", "First route path mismatch");
    cr_assert_str_eq(config.routes[0].technology, "static", "First route technology mismatch");
    cr_assert_str_eq(config.routes[0].document_root, "/var/www/html", "First route document_root mismatch");

    // Second route
    cr_assert_str_eq(config.routes[1].path, "/api/", "Second route path mismatch");
    cr_assert_str_eq(config.routes[1].technology, "reverse_proxy", "Second route technology mismatch");
    cr_assert_str_eq(config.routes[1].backend, "127.0.0.1:5000", "Second route backend mismatch");

    // Third route
    cr_assert_str_eq(config.routes[2].path, "/ws/", "Third route path mismatch");
    cr_assert_str_eq(config.routes[2].technology, "websocket", "Third route technology mismatch");
    cr_assert_str_eq(config.routes[2].backend, "127.0.0.1:6000", "Third route backend mismatch");

    remove(temp_filename);
}