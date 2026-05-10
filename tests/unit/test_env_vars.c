#include <criterion/criterion.h>
#include <criterion/logging.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "config.h"

static char *create_test_temp_dir(char *buffer, size_t buffer_size)
{
    snprintf(buffer, buffer_size, "/tmp/emme_test_XXXXXX");
    if (mkdtemp(buffer) == NULL) {
        return NULL;
    }
    return buffer;
}

static void cleanup_test_dir(const char *dir)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
    system(cmd);
}

static const char *get_full_test_config(void)
{
    return
        "server:\n"
        "  port: 8443\n"
        "  max_connections: 100\n"
        "  log_level: error\n"
        "\n"
        "logging:\n"
        "  file: test_env.log\n"
        "  level: error\n"
        "  format: plain\n"
        "  buffer_size: 16384\n"
        "  rollover_size: 10485760\n"
        "  rollover_daily: true\n"
        "  appender_flags:\n"
        "    - console\n"
        "\n"
        "ssl:\n"
        "  certificate: \"certs/dev.crt\"\n"
        "  private_key: \"certs/dev.key\"\n"
        "  session_cache_size: 100000\n"
        "  session_timeout: 300\n"
        "  read_buffer_size: 32768\n"
        "  enable_partial_write: true\n"
        "  release_buffers: true\n"
        "\n"
        "http2:\n"
        "  keepalive_timeout: 60\n"
        "  max_requests_per_connection: 1000\n"
        "  max_concurrent_streams: 100\n"
        "\n"
        "routes:\n"
        "  - path: \"/api/\"\n"
        "    technology: \"reverse_proxy\"\n"
        "    backend: \"127.0.0.1:8081\"\n";
}

Test(env_vars, max_connections_override)
{
    char tmpdir[256];
    cr_assert_not_null(create_test_temp_dir(tmpdir, sizeof(tmpdir)), "Failed to create temp dir");
    
    char config_path[512];
    snprintf(config_path, sizeof(config_path), "%s/config.yaml", tmpdir);
    
    FILE *f = fopen(config_path, "w");
    cr_assert_not_null(f, "Failed to create test config");
    
    fprintf(f, "%s", get_full_test_config());
    fclose(f);
    
    ServerConfig config;
    setenv("EMME_MAX_CONNECTIONS", "500", 1);
    
    int result = load_config(&config, config_path);
    cr_assert_eq(result, 0, "Config load should succeed, got %d", result);
    
    apply_env_overrides(&config);
    
    cr_assert_eq(config.max_connections, 500, 
                 "EMME_MAX_CONNECTIONS should override config value, got %d", 
                 config.max_connections);
    
    unsetenv("EMME_MAX_CONNECTIONS");
    cleanup_test_dir(tmpdir);
}

Test(env_vars, max_connections_override_invalid)
{
    char tmpdir[256];
    cr_assert_not_null(create_test_temp_dir(tmpdir, sizeof(tmpdir)), "Failed to create temp dir");
    
    char config_path[512];
    snprintf(config_path, sizeof(config_path), "%s/config.yaml", tmpdir);
    
    FILE *f = fopen(config_path, "w");
    cr_assert_not_null(f, "Failed to create test config");
    
    fprintf(f, "%s", get_full_test_config());
    fclose(f);
    
    ServerConfig config;
    setenv("EMME_MAX_CONNECTIONS", "invalid", 1);
    
    int result = load_config(&config, config_path);
    cr_assert_eq(result, 0, "Config load should succeed");
    
    apply_env_overrides(&config);
    
    cr_assert_eq(config.max_connections, 100, 
                 "Invalid EMME_MAX_CONNECTIONS should use config value");
    
    unsetenv("EMME_MAX_CONNECTIONS");
    cleanup_test_dir(tmpdir);
}

Test(env_vars, max_connections_override_out_of_range)
{
    char tmpdir[256];
    cr_assert_not_null(create_test_temp_dir(tmpdir, sizeof(tmpdir)), "Failed to create temp dir");
    
    char config_path[512];
    snprintf(config_path, sizeof(config_path), "%s/config.yaml", tmpdir);
    
    FILE *f = fopen(config_path, "w");
    cr_assert_not_null(f, "Failed to create test config");
    
    fprintf(f, "%s", get_full_test_config());
    fclose(f);
    
    ServerConfig config;
    setenv("EMME_MAX_CONNECTIONS", "2000000", 1);
    
    int result = load_config(&config, config_path);
    cr_assert_eq(result, 0, "Config load should succeed");
    
    apply_env_overrides(&config);
    
    cr_assert_eq(config.max_connections, 100, 
                 "Out-of-range EMME_MAX_CONNECTIONS should use config value");
    
    unsetenv("EMME_MAX_CONNECTIONS");
    cleanup_test_dir(tmpdir);
}

Test(env_vars, ssl_cert_path_override)
{
    char tmpdir[256];
    cr_assert_not_null(create_test_temp_dir(tmpdir, sizeof(tmpdir)), "Failed to create temp dir");
    
    char config_path[512];
    snprintf(config_path, sizeof(config_path), "%s/config.yaml", tmpdir);
    
    FILE *f = fopen(config_path, "w");
    cr_assert_not_null(f, "Failed to create test config");
    
    fprintf(f, "%s", get_full_test_config());
    fclose(f);
    
    ServerConfig config;
    setenv("EMME_SSL_CERT_PATH", "/custom/path/to/cert.pem", 1);
    
    int result = load_config(&config, config_path);
    cr_assert_eq(result, 0, "Config load should succeed");
    
    apply_env_overrides(&config);
    
    cr_assert_str_eq(config.ssl.certificate, "/custom/path/to/cert.pem",
                     "EMME_SSL_CERT_PATH should override config value");
    
    unsetenv("EMME_SSL_CERT_PATH");
    cleanup_test_dir(tmpdir);
}

Test(env_vars, ssl_key_path_override)
{
    char tmpdir[256];
    cr_assert_not_null(create_test_temp_dir(tmpdir, sizeof(tmpdir)), "Failed to create temp dir");
    
    char config_path[512];
    snprintf(config_path, sizeof(config_path), "%s/config.yaml", tmpdir);
    
    FILE *f = fopen(config_path, "w");
    cr_assert_not_null(f, "Failed to create test config");
    
    fprintf(f, "%s", get_full_test_config());
    fclose(f);
    
    ServerConfig config;
    setenv("EMME_SSL_KEY_PATH", "/custom/path/to/key.pem", 1);
    
    int result = load_config(&config, config_path);
    cr_assert_eq(result, 0, "Config load should succeed");
    
    apply_env_overrides(&config);
    
    cr_assert_str_eq(config.ssl.private_key, "/custom/path/to/key.pem",
                     "EMME_SSL_KEY_PATH should override config value");
    
    unsetenv("EMME_SSL_KEY_PATH");
    cleanup_test_dir(tmpdir);
}

Test(env_vars, multiple_env_vars_override)
{
    char tmpdir[256];
    cr_assert_not_null(create_test_temp_dir(tmpdir, sizeof(tmpdir)), "Failed to create temp dir");
    
    char config_path[512];
    snprintf(config_path, sizeof(config_path), "%s/config.yaml", tmpdir);
    
    FILE *f = fopen(config_path, "w");
    cr_assert_not_null(f, "Failed to create test config");
    
    fprintf(f, "%s", get_full_test_config());
    fclose(f);
    
    ServerConfig config;
    setenv("EMME_PORT", "9443", 1);
    setenv("EMME_MAX_CONNECTIONS", "750", 1);
    setenv("EMME_SHUTDOWN_TIMEOUT", "60", 1);
    setenv("EMME_LOG_LEVEL", "debug", 1);
    setenv("EMME_SSL_CERT_PATH", "/env/cert.pem", 1);
    setenv("EMME_SSL_KEY_PATH", "/env/key.pem", 1);
    
    int result = load_config(&config, config_path);
    cr_assert_eq(result, 0, "Config load should succeed");
    
    apply_env_overrides(&config);
    
    cr_assert_eq(config.port, 9443, "EMME_PORT should override");
    cr_assert_eq(config.max_connections, 750, "EMME_MAX_CONNECTIONS should override");
    cr_assert_eq(config.shutdown_timeout_seconds, 60, "EMME_SHUTDOWN_TIMEOUT should override");
    cr_assert_str_eq(config.log_level, "debug", "EMME_LOG_LEVEL should override");
    cr_assert_str_eq(config.ssl.certificate, "/env/cert.pem", "EMME_SSL_CERT_PATH should override");
    cr_assert_str_eq(config.ssl.private_key, "/env/key.pem", "EMME_SSL_KEY_PATH should override");
    
    unsetenv("EMME_PORT");
    unsetenv("EMME_MAX_CONNECTIONS");
    unsetenv("EMME_SHUTDOWN_TIMEOUT");
    unsetenv("EMME_LOG_LEVEL");
    unsetenv("EMME_SSL_CERT_PATH");
    unsetenv("EMME_SSL_KEY_PATH");
    cleanup_test_dir(tmpdir);
}

Test(env_vars, ssl_paths_with_spaces)
{
    char tmpdir[256];
    cr_assert_not_null(create_test_temp_dir(tmpdir, sizeof(tmpdir)), "Failed to create temp dir");
    
    char config_path[512];
    snprintf(config_path, sizeof(config_path), "%s/config.yaml", tmpdir);
    
    FILE *f = fopen(config_path, "w");
    cr_assert_not_null(f, "Failed to create test config");
    
    fprintf(f, "%s", get_full_test_config());
    fclose(f);
    
    ServerConfig config;
    setenv("EMME_SSL_CERT_PATH", "/path with spaces/cert.pem", 1);
    setenv("EMME_SSL_KEY_PATH", "/path with spaces/key.pem", 1);
    
    int result = load_config(&config, config_path);
    cr_assert_eq(result, 0, "Config load should succeed");
    
    apply_env_overrides(&config);
    
    cr_assert_str_eq(config.ssl.certificate, "/path with spaces/cert.pem",
                     "EMME_SSL_CERT_PATH with spaces should work");
    cr_assert_str_eq(config.ssl.private_key, "/path with spaces/key.pem",
                     "EMME_SSL_KEY_PATH with spaces should work");
    
    unsetenv("EMME_SSL_CERT_PATH");
    unsetenv("EMME_SSL_KEY_PATH");
    cleanup_test_dir(tmpdir);
}
