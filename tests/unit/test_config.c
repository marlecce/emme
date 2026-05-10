#include <criterion/criterion.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "config.h"
#include "log.h"

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

Test(config, parse_ssl_performance_settings)
{
    const char *temp_filename = "temp_config_ssl_perf.yaml";

    write_config_file(
        temp_filename,
        "ssl:\n"
        "  certificate: certs/dev.crt\n"
        "  private_key: certs/dev.key\n"
        "  read_buffer_size: 32768\n"
        "  enable_partial_write: 1\n"
        "  release_buffers: 1\n");

    ServerConfig config;
    int ret = load_config(&config, temp_filename);
    cr_assert_eq(ret, 0, "Config with SSL performance settings should load");
    cr_assert_eq(config.ssl.read_buffer_size, 32768, "Read buffer should be 32KB");
    cr_assert_eq(config.ssl.enable_partial_write, 1, "Partial write should be enabled");
    cr_assert_eq(config.ssl.release_buffers, 1, "Release buffers should be enabled");

    unlink(temp_filename);
}

Test(config, reject_ssl_read_buffer_out_of_range)
{
    const char *temp_filename = "temp_config_ssl_bad_buffer.yaml";

    write_config_file(
        temp_filename,
        "ssl:\n"
        "  certificate: certs/dev.crt\n"
        "  private_key: certs/dev.key\n"
        "  read_buffer_size: 100\n");

    ServerConfig config;
    cr_assert_eq(load_config(&config, temp_filename), -1,
                 "Read buffer size below minimum should be rejected");
    unlink(temp_filename);
}

Test(config, parse_http2_keepalive_settings)
{
    const char *temp_filename = "temp_config_http2.yaml";

    write_config_file(
        temp_filename,
        "ssl:\n"
        "  certificate: certs/dev.crt\n"
        "  private_key: certs/dev.key\n"
        "http2:\n"
        "  keepalive_timeout: 120\n"
        "  max_requests_per_connection: 5000\n"
        "  max_concurrent_streams: 200\n");

    ServerConfig config;
    int ret = load_config(&config, temp_filename);
    cr_assert_eq(ret, 0, "Config with HTTP/2 settings should load");
    cr_assert_eq(config.http2.keepalive_timeout, 120, "Keepalive timeout should be 120s");
    cr_assert_eq(config.http2.max_requests_per_connection, 5000, "Max requests should be 5000");
    cr_assert_eq(config.http2.max_concurrent_streams, 200, "Max streams should be 200");

    unlink(temp_filename);
}

Test(config, reject_http2_invalid_keepalive)
{
    const char *temp_filename = "temp_config_http2_bad.yaml";

    write_config_file(
        temp_filename,
        "ssl:\n"
        "  certificate: certs/dev.crt\n"
        "  private_key: certs/dev.key\n"
        "http2:\n"
        "  keepalive_timeout: 5\n");

    ServerConfig config;
    cr_assert_eq(load_config(&config, temp_filename), -1,
                 "Keepalive timeout below minimum should be rejected");
    unlink(temp_filename);
}

Test(config, parse_logging_settings)
{
    const char *temp_filename = "temp_config_logging.yaml";

    write_config_file(
        temp_filename,
        "logging:\n"
        "  file: /var/log/emme.log\n"
        "  level: warn\n"
        "  format: json\n"
        "  buffer_size: 32768\n"
        "  rollover_size: 52428800\n"
        "  rollover_daily: 0\n"
        "  appender_flags:\n"
        "    - file\n"
        "ssl:\n"
        "  certificate: certs/dev.crt\n"
        "  private_key: certs/dev.key\n");

    ServerConfig config;
    int ret = load_config(&config, temp_filename);
    cr_assert_eq(ret, 0, "Config with logging settings should load");
    cr_assert_str_eq(config.logging.file, "/var/log/emme.log", "Log file path mismatch");
    cr_assert_eq(config.logging.level, LOG_LEVEL_WARN, "Log level should be warn");
    cr_assert_eq(config.logging.format, LOG_FORMAT_JSON, "Log format should be JSON");
    cr_assert_eq(config.logging.buffer_size, 32768, "Buffer size should be 32KB");
    cr_assert_eq(config.logging.rollover_size, 52428800, "Rollover size should be 50MB");
    cr_assert_eq(config.logging.rollover_daily, 0, "Daily rollover should be disabled");

    unlink(temp_filename);
}

Test(config, parse_bool_variations)
{
    const char *temp_filename = "temp_config_bools.yaml";

    write_config_file(
        temp_filename,
        "logging:\n"
        "  level: info\n"
        "  format: plain\n"
        "  rollover_daily: yes\n"
        "  appender_flags:\n"
        "    - console\n"
        "ssl:\n"
        "  certificate: certs/dev.crt\n"
        "  private_key: certs/dev.key\n"
        "  enable_partial_write: true\n"
        "  release_buffers: false\n");

    ServerConfig config;
    int ret = load_config(&config, temp_filename);
    cr_assert_eq(ret, 0, "Config with boolean variations should load");
    cr_assert_eq(config.logging.rollover_daily, 1, "Rollover daily should be 1 (yes)");
    cr_assert_eq(config.ssl.enable_partial_write, 1, "Partial write should be 1 (true)");
    cr_assert_eq(config.ssl.release_buffers, 0, "Release buffers should be 0 (false)");

    unlink(temp_filename);
}

Test(config, reject_invalid_boolean)
{
    const char *temp_filename = "temp_config_bad_bool.yaml";

    write_config_file(
        temp_filename,
        "ssl:\n"
        "  certificate: certs/dev.crt\n"
        "  private_key: certs/dev.key\n"
        "  enable_partial_write: maybe\n");

    ServerConfig config;
    cr_assert_eq(load_config(&config, temp_filename), -1,
                 "Invalid boolean value should be rejected");
    unlink(temp_filename);
}

Test(config, parse_server_settings)
{
    const char *temp_filename = "temp_config_server.yaml";

    write_config_file(
        temp_filename,
        "server:\n"
        "  port: 9443\n"
        "  max_connections: 500\n"
        "  log_level: error\n"
        "ssl:\n"
        "  certificate: certs/dev.crt\n"
        "  private_key: certs/dev.key\n");

    ServerConfig config;
    int ret = load_config(&config, temp_filename);
    cr_assert_eq(ret, 0, "Config with server settings should load");
    cr_assert_eq(config.port, 9443, "Port should be 9443");
    cr_assert_eq(config.max_connections, 500, "Max connections should be 500");
    cr_assert_str_eq(config.log_level, "error", "Log level should be error");

    unlink(temp_filename);
}

Test(config, reject_port_out_of_range)
{
    const char *temp_filename = "temp_config_bad_port.yaml";

    write_config_file(
        temp_filename,
        "server:\n"
        "  port: 70000\n"
        "ssl:\n"
        "  certificate: certs/dev.crt\n"
        "  private_key: certs/dev.key\n");

    ServerConfig config;
    cr_assert_eq(load_config(&config, temp_filename), -1,
                 "Port above 65535 should be rejected");
    unlink(temp_filename);
}

Test(config, parse_ssl_session_settings)
{
    const char *temp_filename = "temp_config_ssl_session.yaml";

    write_config_file(
        temp_filename,
        "ssl:\n"
        "  certificate: certs/dev.crt\n"
        "  private_key: certs/dev.key\n"
        "  session_cache_size: 50000\n"
        "  session_timeout: 600\n"
        "  session_ticket_key: /etc/ssl/ticket.key\n");

    ServerConfig config;
    int ret = load_config(&config, temp_filename);
    cr_assert_eq(ret, 0, "Config with SSL session settings should load");
    cr_assert_eq(config.ssl.session_cache_size, 50000, "Session cache size should be 50K");
    cr_assert_eq(config.ssl.session_timeout, 600, "Session timeout should be 600s");
    cr_assert_str_eq(config.ssl.session_ticket_key, "/etc/ssl/ticket.key", "Ticket key path mismatch");

    unlink(temp_filename);
}

Test(config, minimal_config_with_defaults)
{
    const char *temp_filename = "temp_config_minimal.yaml";

    write_config_file(
        temp_filename,
        "ssl:\n"
        "  certificate: certs/dev.crt\n"
        "  private_key: certs/dev.key\n");

    ServerConfig config;
    int ret = load_config(&config, temp_filename);
    cr_assert_eq(ret, 0, "Minimal config should load with defaults");
    cr_assert_eq(config.port, 8443, "Default port should be 8443");
    cr_assert_eq(config.max_connections, 100, "Default max connections should be 100");
    cr_assert_eq(config.ssl.session_cache_size, 100000, "Default session cache should be 100K");
    cr_assert_eq(config.ssl.read_buffer_size, 32768, "Default read buffer should be 32KB");
    cr_assert_eq(config.ssl.enable_partial_write, 1, "Default partial write should be enabled");
    cr_assert_eq(config.ssl.release_buffers, 1, "Default release buffers should be enabled");
    cr_assert_eq(config.http2.keepalive_timeout, 60, "Default keepalive should be 60s");

    unlink(temp_filename);
}

Test(config, ssl_certificate_defaults_when_missing)
{
    const char *temp_filename = "temp_config_no_cert.yaml";

    write_config_file(
        temp_filename,
        "ssl:\n"
        "  private_key: certs/custom.key\n");

    ServerConfig config;
    int ret = load_config(&config, temp_filename);
    cr_assert_eq(ret, 0, "Config should load with default certificate");
    cr_assert_str_eq(config.ssl.certificate, "certs/dev.crt", "Should use default certificate");
    cr_assert_str_eq(config.ssl.private_key, "certs/custom.key", "Should use custom key");
    unlink(temp_filename);
}

Test(config, ssl_private_key_defaults_when_missing)
{
    const char *temp_filename = "temp_config_no_key.yaml";

    write_config_file(
        temp_filename,
        "ssl:\n"
        "  certificate: certs/custom.crt\n");

    ServerConfig config;
    int ret = load_config(&config, temp_filename);
    cr_assert_eq(ret, 0, "Config should load with default private key");
    cr_assert_str_eq(config.ssl.certificate, "certs/custom.crt", "Should use custom certificate");
    cr_assert_str_eq(config.ssl.private_key, "certs/dev.key", "Should use default key");
    unlink(temp_filename);
}

Test(config, logging_file_only_with_console_appender)
{
    const char *temp_filename = "temp_config_log_console_only.yaml";

    write_config_file(
        temp_filename,
        "logging:\n"
        "  file: emme.log\n"
        "  level: info\n"
        "  format: plain\n"
        "  appender_flags:\n"
        "    - console\n"
        "ssl:\n"
        "  certificate: certs/dev.crt\n"
        "  private_key: certs/dev.key\n");

    ServerConfig config;
    int ret = load_config(&config, temp_filename);
    cr_assert_eq(ret, 0, "Config with console-only appender should load");
    cr_assert_eq(config.logging.appender_flags & APPENDER_CONSOLE, APPENDER_CONSOLE, "Should have console appender");
    cr_assert_eq(config.logging.appender_flags & APPENDER_FILE, 0, "Should not have file appender");
    unlink(temp_filename);
}

Test(config, parse_routes_with_document_root_resolution)
{
    const char *temp_filename = "temp_config_routes.yaml";
    const char *temp_dir = "/tmp/emme_test_routes";

    mkdir(temp_dir, 0755);

    write_config_file(
        temp_filename,
        "ssl:\n"
        "  certificate: certs/dev.crt\n"
        "  private_key: certs/dev.key\n"
        "routes:\n"
        "  - path: /static/\n"
        "    technology: static\n"
        "    document_root: /tmp/emme_test_routes\n");

    ServerConfig config;
    int ret = load_config(&config, temp_filename);
    cr_assert_eq(ret, 0, "Config with routes should load");
    cr_assert_eq(config.route_count, 1, "Should have 1 route");
    cr_assert_eq(config.routes[0].document_root_resolved, 1, "Document root should be resolved");

    rmdir(temp_dir);
    unlink(temp_filename);
}
