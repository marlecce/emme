#include <criterion/criterion.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "config.h"

static void write_config_file(const char *path, const char *content)
{
    FILE *f = fopen(path, "w");
    cr_assert_not_null(f, "Unable to open temporary configuration file");
    fputs(content, f);
    fclose(f);
}

Test(config, load_full_config_yaml)
{
    const char *temp_filename = "temp_config.yaml";

    write_config_file(
        temp_filename,
        "server:\n"
        "  port: 8443\n"
        "  max_connections: 100\n"
        "  log_level: DEBUG\n"
        "logging:\n"
        "  file: emme.log\n"
        "  level: debug\n"
        "  format: plain\n"
        "  buffer_size: 16384\n"
        "  rollover_size: 10485760\n"
        "  rollover_daily: true\n"
        "  appender_flags:\n"
        "    - file\n"
        "    - console\n"
        "ssl:\n"
        "  certificate: /etc/ssl/certs/server.crt\n"
        "  private_key: /etc/ssl/private/server.key\n"
        "routes:\n"
        "  - path: /static/\n"
        "    technology: static\n"
        "    document_root: /var/www/html\n"
        "  - path: /api/\n"
        "    technology: reverse_proxy\n"
        "    backend: 127.0.0.1:5000\n");

    ServerConfig config;
    int ret = load_config(&config, temp_filename);
    cr_assert_eq(ret, 0, "Config should load successfully");
    cr_assert_eq(config.port, 8443, "Port should be 8443");
    cr_assert_eq(config.max_connections, 100, "Max connections should be 100");
    cr_assert_str_eq(config.log_level, "DEBUG", "Log level should be DEBUG");
    cr_assert_str_eq(config.ssl.certificate, "/etc/ssl/certs/server.crt", "SSL certificate mismatch");
    cr_assert_str_eq(config.ssl.private_key, "/etc/ssl/private/server.key", "SSL private_key mismatch");
    cr_assert_eq(config.route_count, 2, "Route count should be 2");
    cr_assert_str_eq(config.routes[0].path, "/static/", "First route path mismatch");
    cr_assert_str_eq(config.routes[0].technology, "static", "First route technology mismatch");
    cr_assert_str_eq(config.routes[1].path, "/api/", "Second route path mismatch");
    cr_assert_str_eq(config.routes[1].technology, "reverse_proxy", "Second route technology mismatch");

    unlink(temp_filename);
}

Test(config, reject_invalid_port)
{
    const char *temp_filename = "temp_config_invalid_port.yaml";

    write_config_file(
        temp_filename,
        "server:\n"
        "  port: abc\n"
        "ssl:\n"
        "  certificate: certs/dev.crt\n"
        "  private_key: certs/dev.key\n");

    ServerConfig config;
    cr_assert_eq(load_config(&config, temp_filename), -1,
                 "Config with non-numeric port must fail");
    unlink(temp_filename);
}

Test(config, reject_unsupported_route_technology)
{
    const char *temp_filename = "temp_config_invalid_tech.yaml";

    write_config_file(
        temp_filename,
        "ssl:\n"
        "  certificate: certs/dev.crt\n"
        "  private_key: certs/dev.key\n"
        "routes:\n"
        "  - path: /ws/\n"
        "    technology: websocket\n"
        "    backend: 127.0.0.1:6000\n");

    ServerConfig config;
    cr_assert_eq(load_config(&config, temp_filename), -1,
                 "Unsupported technology must be rejected");
    unlink(temp_filename);
}

Test(config, reject_static_route_without_document_root)
{
    const char *temp_filename = "temp_config_missing_docroot.yaml";

    write_config_file(
        temp_filename,
        "ssl:\n"
        "  certificate: certs/dev.crt\n"
        "  private_key: certs/dev.key\n"
        "routes:\n"
        "  - path: /static/\n"
        "    technology: static\n");

    ServerConfig config;
    cr_assert_eq(load_config(&config, temp_filename), -1,
                 "Static route without document_root must fail");
    unlink(temp_filename);
}

Test(config, reject_reverse_proxy_with_invalid_backend)
{
    const char *temp_filename = "temp_config_invalid_backend.yaml";

    write_config_file(
        temp_filename,
        "ssl:\n"
        "  certificate: certs/dev.crt\n"
        "  private_key: certs/dev.key\n"
        "routes:\n"
        "  - path: /api/\n"
        "    technology: reverse_proxy\n"
        "    backend: invalid-backend-value\n");

    ServerConfig config;
    cr_assert_eq(load_config(&config, temp_filename), -1,
                 "Reverse proxy route with malformed backend must fail");
    unlink(temp_filename);
}
