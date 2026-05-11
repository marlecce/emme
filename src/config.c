#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <limits.h>
#include <yaml.h>
#include "log.h"
#include "config.h"

static yaml_node_t *find_yaml_node(yaml_document_t *doc, yaml_node_t *node, const char *key)
{
    if (!node || node->type != YAML_MAPPING_NODE)
        return NULL;

    for (yaml_node_pair_t *pair = node->data.mapping.pairs.start;
         pair < node->data.mapping.pairs.top; pair++) {
        yaml_node_t *key_node = yaml_document_get_node(doc, pair->key);
        if (key_node && key_node->type == YAML_SCALAR_NODE &&
            strcmp((char *)key_node->data.scalar.value, key) == 0) {
            return yaml_document_get_node(doc, pair->value);
        }
    }
    return NULL;
}

static int get_node_line(yaml_node_t *node)
{
    if (!node)
        return 0;
    return (int)node->start_mark.line + 1;
}

typedef struct {
    yaml_document_t *document;
    yaml_node_t *root;
    ServerConfig *config;
    const char *section_name;
} ConfigParser;

#define PARSE_FIELD(field_name, parser_func, min_val, max_val, dest) \
    do { \
        yaml_node_t *_node = find_yaml_node(ctx->document, node, field_name); \
        if (_node) { \
            char _full_name[128]; \
            snprintf(_full_name, sizeof(_full_name), "%s.%s", ctx->section_name, field_name); \
            if (parser_func(_node, _full_name, min_val, max_val, dest) != 0) { \
                fprintf(stderr, "  at line %d\n", get_node_line(_node)); \
                return -1; \
            } \
        } \
    } while(0)

#define PARSE_STRING(field_name, dest, size) \
    do { \
        yaml_node_t *_node = find_yaml_node(ctx->document, node, field_name); \
        if (_node) { \
            char _full_name[128]; \
            snprintf(_full_name, sizeof(_full_name), "%s.%s", ctx->section_name, field_name); \
            if (get_yaml_string(_node, _full_name, dest, size) != 0) { \
                fprintf(stderr, "  at line %d\n", get_node_line(_node)); \
                return -1; \
            } \
        } \
    } while(0)

#define PARSE_BOOL(field_name, dest) \
    do { \
        yaml_node_t *_node = find_yaml_node(ctx->document, node, field_name); \
        if (_node) { \
            char _full_name[128]; \
            snprintf(_full_name, sizeof(_full_name), "%s.%s", ctx->section_name, field_name); \
            if (get_yaml_bool(_node, _full_name, dest) != 0) { \
                fprintf(stderr, "  at line %d\n", get_node_line(_node)); \
                return -1; \
            } \
        } \
    } while(0)

static void set_config_defaults(ServerConfig *config)
{
    memset(config, 0, sizeof(*config));
    config->port = 8443;
    config->max_connections = 100;
    config->shutdown_timeout_seconds = 30;
    snprintf(config->log_level, sizeof(config->log_level), "info");

    config->logging.level = LOG_LEVEL_DEBUG;
    config->logging.format = LOG_FORMAT_PLAIN;
    config->logging.buffer_size = 16384;
    config->logging.rollover_size = 10485760;
    config->logging.rollover_daily = 1;
    config->logging.appender_flags = APPENDER_FILE | APPENDER_CONSOLE;
    snprintf(config->logging.file, sizeof(config->logging.file), "emme.log");

    snprintf(config->ssl.certificate, sizeof(config->ssl.certificate), "certs/dev.crt");
    snprintf(config->ssl.private_key, sizeof(config->ssl.private_key), "certs/dev.key");
    config->ssl.session_cache_size = 100000;
    config->ssl.session_timeout = 300;
    config->ssl.session_ticket_key[0] = '\0';
    config->ssl.read_buffer_size = 32768;
    config->ssl.enable_partial_write = 1;
    config->ssl.release_buffers = 1;

    config->http2.keepalive_timeout = 60;
    config->http2.max_requests_per_connection = 1000;
    config->http2.max_concurrent_streams = 100;
    
    config->request_timeout_ms = 30000;
    config->tls_handshake_timeout_ms = 10000;
    
    config->security_headers.enabled = true;
    config->security_headers.header_count = 0;
}

static int get_yaml_string_ext(yaml_node_t *node, const char *field, char *buffer, size_t size, int line)
{
    int written;
    const char *value;

    if (!node || node->type != YAML_SCALAR_NODE) {
        fprintf(stderr, "Invalid '%s' (line %d): expected a scalar string\n", field, line);
        return -1;
    }

    value = (const char *)node->data.scalar.value;
    written = snprintf(buffer, size, "%s", value);
    if (written < 0 || (size_t)written >= size) {
        fprintf(stderr, "Invalid '%s' (line %d): value too long\n", field, line);
        return -1;
    }
    return 0;
}

static int get_yaml_string(yaml_node_t *node, const char *field, char *buffer, size_t size)
{
    return get_yaml_string_ext(node, field, buffer, size, get_node_line(node));
}

static int parse_int_in_range(const char *text, int min, int max, int *result)
{
    char *end = NULL;
    long value;

    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0')
        return -1;
    if (value < min || value > max)
        return -1;

    *result = (int)value;
    return 0;
}

static int parse_size_in_range(const char *text, size_t min, size_t max, size_t *result)
{
    char *end = NULL;
    unsigned long long value;

    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0')
        return -1;
    if (value < min || value > max)
        return -1;

    *result = (size_t)value;
    return 0;
}

static int get_yaml_int(yaml_node_t *node, const char *field, int *result)
{
    if (!node || node->type != YAML_SCALAR_NODE) {
        fprintf(stderr, "Invalid '%s': expected integer scalar\n", field);
        return -1;
    }
    char *endptr;
    long val = strtol((const char *)node->data.scalar.value, &endptr, 10);
    if (*endptr != '\0') {
        fprintf(stderr, "Invalid '%s': expected integer, got '%s'\n", field, (const char *)node->data.scalar.value);
        return -1;
    }
    *result = (int)val;
    return 0;
}

static int get_yaml_int_in_range_ext(yaml_node_t *node, const char *field,
                                     int min, int max, int *result, int line)
{
    if (!node || node->type != YAML_SCALAR_NODE) {
        fprintf(stderr, "Invalid '%s' (line %d): expected integer scalar\n", field, line);
        return -1;
    }
    if (parse_int_in_range((const char *)node->data.scalar.value, min, max, result) != 0) {
        fprintf(stderr, "Invalid '%s' (line %d): expected integer in range [%d, %d]\n",
                field, line, min, max);
        return -1;
    }
    return 0;
}

static int get_yaml_int_in_range(yaml_node_t *node, const char *field,
                                 int min, int max, int *result)
{
    return get_yaml_int_in_range_ext(node, field, min, max, result, get_node_line(node));
}

static int get_yaml_size_in_range_ext(yaml_node_t *node, const char *field,
                                      size_t min, size_t max, size_t *result, int line)
{
    if (!node || node->type != YAML_SCALAR_NODE) {
        fprintf(stderr, "Invalid '%s' (line %d): expected integer scalar\n", field, line);
        return -1;
    }
    if (parse_size_in_range((const char *)node->data.scalar.value, min, max, result) != 0) {
        fprintf(stderr, "Invalid '%s' (line %d): expected integer in range [%zu, %zu]\n",
                field, line, min, max);
        return -1;
    }
    return 0;
}

static int get_yaml_size_in_range(yaml_node_t *node, const char *field,
                                  size_t min, size_t max, size_t *result)
{
    return get_yaml_size_in_range_ext(node, field, min, max, result, get_node_line(node));
}

static int get_yaml_bool_ext(yaml_node_t *node, const char *field, int *result, int line)
{
    const char *value;

    if (!node || node->type != YAML_SCALAR_NODE) {
        fprintf(stderr, "Invalid '%s' (line %d): expected boolean scalar\n", field, line);
        return -1;
    }

    value = (const char *)node->data.scalar.value;
    if (strcasecmp(value, "true") == 0 || strcmp(value, "1") == 0 || strcasecmp(value, "yes") == 0) {
        *result = 1;
        return 0;
    }
    if (strcasecmp(value, "false") == 0 || strcmp(value, "0") == 0 || strcasecmp(value, "no") == 0) {
        *result = 0;
        return 0;
    }

    fprintf(stderr, "Invalid '%s' (line %d): expected true/false\n", field, line);
    return -1;
}

static int get_yaml_bool(yaml_node_t *node, const char *field, int *result)
{
    return get_yaml_bool_ext(node, field, result, get_node_line(node));
}

static int parse_log_level(const char *value, LogLevel *level)
{
    if (strcasecmp(value, "debug") == 0)
        *level = LOG_LEVEL_DEBUG;
    else if (strcasecmp(value, "info") == 0)
        *level = LOG_LEVEL_INFO;
    else if (strcasecmp(value, "warn") == 0)
        *level = LOG_LEVEL_WARN;
    else if (strcasecmp(value, "error") == 0)
        *level = LOG_LEVEL_ERROR;
    else
        return -1;
    return 0;
}

static int parse_log_format(const char *value, LogFormat *format)
{
    if (strcasecmp(value, "plain") == 0)
        *format = LOG_FORMAT_PLAIN;
    else if (strcasecmp(value, "json") == 0)
        *format = LOG_FORMAT_JSON;
    else
        return -1;
    return 0;
}

static int parse_appender_flags(yaml_document_t *doc, yaml_node_t *node, LoggingConfig *log_cfg)
{
    if (!node)
        return 0;
    if (node->type != YAML_SEQUENCE_NODE) {
        fprintf(stderr, "Invalid 'logging.appender_flags': expected sequence\n");
        return -1;
    }

    log_cfg->appender_flags = 0;
    for (yaml_node_item_t *item = node->data.sequence.items.start;
         item < node->data.sequence.items.top; item++) {
        yaml_node_t *flag_node = yaml_document_get_node(doc, *item);
        if (!flag_node || flag_node->type != YAML_SCALAR_NODE) {
            fprintf(stderr, "Invalid 'logging.appender_flags': entries must be scalars\n");
            return -1;
        }
        const char *flag = (const char *)flag_node->data.scalar.value;
        if (strcasecmp(flag, "file") == 0)
            log_cfg->appender_flags |= APPENDER_FILE;
        else if (strcasecmp(flag, "console") == 0)
            log_cfg->appender_flags |= APPENDER_CONSOLE;
        else {
            fprintf(stderr, "Invalid appender flag '%s': expected 'file' or 'console'\n", flag);
            return -1;
        }
    }

    if (log_cfg->appender_flags == 0) {
        fprintf(stderr, "Invalid 'logging.appender_flags': at least one appender required\n");
        return -1;
    }
    return 0;
}

static int parse_health_check_config(yaml_document_t *doc, yaml_node_t *node, HealthCheckConfig *hc)
{
    if (!node || node->type != YAML_MAPPING_NODE) {
        return 0; // Optional, missing is OK
    }
    
    hc->enabled = false;
    snprintf(hc->path, sizeof(hc->path), "/health");
    hc->interval_seconds = 10;
    hc->timeout_seconds = 5;
    hc->unhealthy_threshold = 3;
    hc->healthy_threshold = 2;
    
    yaml_node_t *field;
    
    field = find_yaml_node(doc, node, "enabled");
    if (field) {
        int val;
        if (get_yaml_bool(field, "health_check.enabled", &val) == 0) {
            hc->enabled = (bool)val;
        }
    }
    
    field = find_yaml_node(doc, node, "path");
    if (field) {
        get_yaml_string(field, "health_check.path", hc->path, sizeof(hc->path));
    }
    
    field = find_yaml_node(doc, node, "interval_seconds");
    if (field) {
        int val;
        if (get_yaml_int(field, "health_check.interval_seconds", &val) == 0) {
            hc->interval_seconds = val;
        }
    }
    
    field = find_yaml_node(doc, node, "timeout_seconds");
    if (field) {
        int val;
        if (get_yaml_int(field, "health_check.timeout_seconds", &val) == 0) {
            hc->timeout_seconds = val;
        }
    }
    
    field = find_yaml_node(doc, node, "unhealthy_threshold");
    if (field) {
        int val;
        if (get_yaml_int(field, "health_check.unhealthy_threshold", &val) == 0) {
            hc->unhealthy_threshold = val;
        }
    }
    
    field = find_yaml_node(doc, node, "healthy_threshold");
    if (field) {
        int val;
        if (get_yaml_int(field, "health_check.healthy_threshold", &val) == 0) {
            hc->healthy_threshold = val;
        }
    }
    
    return 0;
}

static int parse_connection_pool_config(yaml_document_t *doc, yaml_node_t *node, ConnectionPoolConfig *cp)
{
    if (!node || node->type != YAML_MAPPING_NODE) {
        cp->size = BACKEND_POOL_DEFAULT_SIZE;
        cp->idle_timeout_seconds = BACKEND_POOL_IDLE_TIMEOUT_SEC;
        return 0;
    }
    
    cp->size = BACKEND_POOL_DEFAULT_SIZE;
    cp->idle_timeout_seconds = BACKEND_POOL_IDLE_TIMEOUT_SEC;
    
    yaml_node_t *field;
    
    field = find_yaml_node(doc, node, "size");
    if (field) {
        int val;
        if (get_yaml_int(field, "connection_pool.size", &val) == 0) {
            cp->size = val;
        }
    }
    
    field = find_yaml_node(doc, node, "idle_timeout_seconds");
    if (field) {
        int val;
        if (get_yaml_int(field, "connection_pool.idle_timeout_seconds", &val) == 0) {
            cp->idle_timeout_seconds = val;
        }
    }
    
    return 0;
}

static int parse_circuit_breaker_config(yaml_document_t *doc, yaml_node_t *node, CircuitBreakerConfig *cb)
{
    if (!node || node->type != YAML_MAPPING_NODE) {
        cb->enabled = false;
        cb->failure_threshold = 5;
        cb->recovery_timeout_seconds = 30;
        return 0;
    }
    
    cb->enabled = false;
    cb->failure_threshold = 5;
    cb->recovery_timeout_seconds = 30;
    
    yaml_node_t *field;
    
    field = find_yaml_node(doc, node, "enabled");
    if (field) {
        int val;
        if (get_yaml_bool(field, "circuit_breaker.enabled", &val) == 0) {
            cb->enabled = (bool)val;
        }
    }
    
    field = find_yaml_node(doc, node, "failure_threshold");
    if (field) {
        int val;
        if (get_yaml_int(field, "circuit_breaker.failure_threshold", &val) == 0) {
            cb->failure_threshold = val;
        }
    }
    
    field = find_yaml_node(doc, node, "recovery_timeout_seconds");
    if (field) {
        int val;
        if (get_yaml_int(field, "circuit_breaker.recovery_timeout_seconds", &val) == 0) {
            cb->recovery_timeout_seconds = val;
        }
    }
    
    return 0;
}

static int parse_security_headers_config(yaml_document_t *doc, yaml_node_t *node, SecurityHeadersConfig *shc)
{
    if (!node || node->type != YAML_MAPPING_NODE) {
        shc->enabled = false;
        shc->header_count = 0;
        return 0;
    }
    
    shc->enabled = false;
    shc->header_count = 0;
    
    yaml_node_t *field;
    
    field = find_yaml_node(doc, node, "enabled");
    if (field) {
        int val;
        if (get_yaml_bool(field, "security_headers.enabled", &val) == 0) {
            shc->enabled = (bool)val;
        }
    }
    
    yaml_node_t *headers_node = find_yaml_node(doc, node, "headers");
    if (headers_node && headers_node->type == YAML_SEQUENCE_NODE) {
        for (yaml_node_item_t *item = headers_node->data.sequence.items.start;
             item < headers_node->data.sequence.items.top && shc->header_count < MAX_SECURITY_HEADERS;
             item++) {
            yaml_node_t *header_node = yaml_document_get_node(doc, *item);
            if (!header_node || header_node->type != YAML_MAPPING_NODE)
                continue;
            
            yaml_node_t *name_node = find_yaml_node(doc, header_node, "name");
            yaml_node_t *value_node = find_yaml_node(doc, header_node, "value");
            
            if (name_node && value_node) {
                SecurityHeader *header = &shc->headers[shc->header_count];
                if (get_yaml_string(name_node, "security_headers[].name", header->name, sizeof(header->name)) == 0 &&
                    get_yaml_string(value_node, "security_headers[].value", header->value, sizeof(header->value)) == 0) {
                    shc->header_count++;
                }
            }
        }
    }
    
    return 0;
}

static int parse_cors_config(yaml_document_t *doc, yaml_node_t *node, CORSConfig *cors)
{
    if (!node || node->type != YAML_MAPPING_NODE) {
        cors->enabled = false;
        cors->allow_credentials = false;
        cors->max_age_seconds = 86400;
        return 0;
    }
    
    cors->enabled = false;
    cors->allow_credentials = false;
    cors->max_age_seconds = 86400;
    
    yaml_node_t *field;
    
    field = find_yaml_node(doc, node, "enabled");
    if (field) {
        int val;
        if (get_yaml_bool(field, "cors.enabled", &val) == 0) {
            cors->enabled = (bool)val;
        }
    }
    
    field = find_yaml_node(doc, node, "allow_origin");
    if (field) {
        get_yaml_string(field, "cors.allow_origin", cors->allow_origin, sizeof(cors->allow_origin));
    }
    
    field = find_yaml_node(doc, node, "allow_methods");
    if (field) {
        get_yaml_string(field, "cors.allow_methods", cors->allow_methods, sizeof(cors->allow_methods));
    }
    
    field = find_yaml_node(doc, node, "allow_headers");
    if (field) {
        get_yaml_string(field, "cors.allow_headers", cors->allow_headers, sizeof(cors->allow_headers));
    }
    
    field = find_yaml_node(doc, node, "allow_credentials");
    if (field) {
        int val;
        if (get_yaml_bool(field, "cors.allow_credentials", &val) == 0) {
            cors->allow_credentials = (bool)val;
        }
    }
    
    field = find_yaml_node(doc, node, "max_age_seconds");
    if (field) {
        int val;
        if (get_yaml_int(field, "cors.max_age_seconds", &val) == 0) {
            cors->max_age_seconds = val;
        }
    }
    
    return 0;
}

int parse_backend_url(const char *backend, char *host, size_t host_size, int *port)
{
    char temp_host[256];
    int temp_port;
    char trailing;

    if (!backend || backend[0] == '\0')
        return -1;
    if (sscanf(backend, "%255[^:]:%d%c", temp_host, &temp_port, &trailing) != 2)
        return -1;
    if (temp_host[0] == '\0')
        return -1;
    if (temp_port < 1 || temp_port > 65535)
        return -1;
    
    snprintf(host, host_size, "%s", temp_host);
    *port = temp_port;
    return 0;
}

static int validate_backend(const char *backend)
{
    char host[64];
    int port;

    return parse_backend_url(backend, host, sizeof(host), &port);
}

static int validate_routes(const ServerConfig *config)
{
    for (int i = 0; i < config->route_count; i++) {
        const Route *route = &config->routes[i];

        if (route->path[0] == '\0' || route->path[0] != '/') {
            fprintf(stderr, "Invalid routes[%d].path: must start with '/'\n", i);
            return -1;
        }
        if (route->technology[0] == '\0') {
            fprintf(stderr, "Invalid routes[%d].technology: required\n", i);
            return -1;
        }

        if (strcmp(route->technology, "static") == 0) {
            if (route->document_root[0] == '\0') {
                fprintf(stderr, "Invalid routes[%d].document_root: required for static route\n", i);
                return -1;
            }
        } else if (strcmp(route->technology, "reverse_proxy") == 0) {
            if (validate_backend(route->backend) != 0) {
                fprintf(stderr, "Invalid routes[%d].backend: expected host:port\n", i);
                return -1;
            }
        } else {
            fprintf(stderr, "Invalid routes[%d].technology '%s': unsupported\n", i, route->technology);
            return -1;
        }
    }

    return 0;
}

static int parse_server_section(ConfigParser *ctx, yaml_node_t *node)
{
    ctx->section_name = "server";

    if (node->type != YAML_MAPPING_NODE) {
        fprintf(stderr, "Invalid 'server' (line %d): expected mapping\n",
                get_node_line(node));
        return -1;
    }

    PARSE_FIELD("port", get_yaml_int_in_range, 1, 65535, &ctx->config->port);
    PARSE_FIELD("max_connections", get_yaml_int_in_range, 1, 1000000, &ctx->config->max_connections);
    PARSE_FIELD("shutdown_timeout_seconds", get_yaml_int_in_range, 1, 300, &ctx->config->shutdown_timeout_seconds);
    PARSE_FIELD("request_timeout_ms", get_yaml_int_in_range, 1000, 300000, &ctx->config->request_timeout_ms);
    PARSE_FIELD("tls_handshake_timeout_ms", get_yaml_int_in_range, 1000, 60000, &ctx->config->tls_handshake_timeout_ms);
    PARSE_STRING("log_level", ctx->config->log_level, sizeof(ctx->config->log_level));

    return 0;
}

static int parse_logging_section(ConfigParser *ctx, yaml_node_t *node)
{
    ctx->section_name = "logging";

    if (node->type != YAML_MAPPING_NODE) {
        fprintf(stderr, "Invalid 'logging' (line %d): expected mapping\n",
                get_node_line(node));
        return -1;
    }

    PARSE_STRING("file", ctx->config->logging.file, sizeof(ctx->config->logging.file));
    PARSE_FIELD("buffer_size", get_yaml_size_in_range, 1, 1048576, &ctx->config->logging.buffer_size);

    yaml_node_t *level_node = find_yaml_node(ctx->document, node, "level");
    if (level_node) {
        char level_str[16];
        int line = get_node_line(level_node);
        if (get_yaml_string(level_node, "logging.level", level_str, sizeof(level_str)) != 0) {
            fprintf(stderr, "  at line %d\n", line);
            return -1;
        }
        if (parse_log_level(level_str, &ctx->config->logging.level) != 0) {
            fprintf(stderr, "Invalid 'logging.level' (line %d): expected debug/info/warn/error\n", line);
            return -1;
        }
    }

    yaml_node_t *format_node = find_yaml_node(ctx->document, node, "format");
    if (format_node) {
        char format_str[16];
        int line = get_node_line(format_node);
        if (get_yaml_string(format_node, "logging.format", format_str, sizeof(format_str)) != 0) {
            fprintf(stderr, "  at line %d\n", line);
            return -1;
        }
        if (parse_log_format(format_str, &ctx->config->logging.format) != 0) {
            fprintf(stderr, "Invalid 'logging.format' (line %d): expected plain/json\n", line);
            return -1;
        }
    }

    PARSE_FIELD("buffer_size", get_yaml_size_in_range, 1, 1048576, &ctx->config->logging.buffer_size);
    PARSE_FIELD("rollover_size", get_yaml_size_in_range, 0, 1099511627776ULL, &ctx->config->logging.rollover_size);
    PARSE_BOOL("rollover_daily", &ctx->config->logging.rollover_daily);

    yaml_node_t *appender_flags_node = find_yaml_node(ctx->document, node, "appender_flags");
    if (appender_flags_node) {
        if (parse_appender_flags(ctx->document, appender_flags_node, &ctx->config->logging) != 0) {
            fprintf(stderr, "  at line %d\n", get_node_line(appender_flags_node));
            return -1;
        }
    }

    return 0;
}

static int parse_ssl_section(ConfigParser *ctx, yaml_node_t *node)
{
    ctx->section_name = "ssl";

    if (node->type != YAML_MAPPING_NODE) {
        fprintf(stderr, "Invalid 'ssl' (line %d): expected mapping\n",
                get_node_line(node));
        return -1;
    }

    PARSE_STRING("certificate", ctx->config->ssl.certificate, sizeof(ctx->config->ssl.certificate));
    PARSE_STRING("private_key", ctx->config->ssl.private_key, sizeof(ctx->config->ssl.private_key));
    PARSE_FIELD("session_cache_size", get_yaml_int_in_range, 1000, 1000000, &ctx->config->ssl.session_cache_size);
    PARSE_FIELD("session_timeout", get_yaml_int_in_range, 60, 3600, &ctx->config->ssl.session_timeout);
    PARSE_STRING("session_ticket_key", ctx->config->ssl.session_ticket_key, sizeof(ctx->config->ssl.session_ticket_key));
    PARSE_FIELD("read_buffer_size", get_yaml_int_in_range, 4096, 65536, &ctx->config->ssl.read_buffer_size);
    PARSE_BOOL("enable_partial_write", &ctx->config->ssl.enable_partial_write);
    PARSE_BOOL("release_buffers", &ctx->config->ssl.release_buffers);

    return 0;
}

static int parse_http2_section(ConfigParser *ctx, yaml_node_t *node)
{
    ctx->section_name = "http2";

    if (node->type != YAML_MAPPING_NODE) {
        fprintf(stderr, "Invalid 'http2' (line %d): expected mapping\n",
                get_node_line(node));
        return -1;
    }

    PARSE_FIELD("keepalive_timeout", get_yaml_int_in_range, 10, 300, &ctx->config->http2.keepalive_timeout);
    PARSE_FIELD("max_requests_per_connection", get_yaml_int_in_range, 1, 100000, &ctx->config->http2.max_requests_per_connection);
    PARSE_FIELD("max_concurrent_streams", get_yaml_int_in_range, 1, 1000, &ctx->config->http2.max_concurrent_streams);

    return 0;
}

static int parse_route_entry(ConfigParser *ctx, yaml_node_t *route_node)
{
    if (ctx->config->route_count >= MAX_ROUTES) {
        fprintf(stderr, "Too many routes: maximum supported is %d\n", MAX_ROUTES);
        return -1;
    }

    Route *route = &ctx->config->routes[ctx->config->route_count];
    memset(route, 0, sizeof(*route));

    yaml_node_t *route_field = find_yaml_node(ctx->document, route_node, "path");
    if (!route_field ||
        get_yaml_string(route_field, "routes[].path",
                        route->path, sizeof(route->path)) != 0) {
        fprintf(stderr, "Invalid route: missing or invalid 'path'\n");
        return -1;
    }

    route_field = find_yaml_node(ctx->document, route_node, "technology");
    if (!route_field ||
        get_yaml_string(route_field, "routes[].technology",
                        route->technology, sizeof(route->technology)) != 0) {
        fprintf(stderr, "Invalid route: missing or invalid 'technology'\n");
        return -1;
    }

    route_field = find_yaml_node(ctx->document, route_node, "document_root");
    if (route_field &&
        get_yaml_string(route_field, "routes[].document_root",
                        route->document_root, sizeof(route->document_root)) != 0)
        return -1;

    route_field = find_yaml_node(ctx->document, route_node, "backend");
    if (route_field &&
        get_yaml_string(route_field, "routes[].backend",
                        route->backend, sizeof(route->backend)) != 0)
        return -1;

    // Parse HTTP/2 reverse proxy options
    route->http2_enabled = false;
    route->tls_enabled = true;
    route->tls_verify = false;
    
    route_field = find_yaml_node(ctx->document, route_node, "http2_enabled");
    if (route_field) {
        int val;
        if (get_yaml_bool(route_field, "routes[].http2_enabled", &val) == 0) {
            route->http2_enabled = (bool)val;
        }
    }
    
    route_field = find_yaml_node(ctx->document, route_node, "tls_enabled");
    if (route_field) {
        int val;
        if (get_yaml_bool(route_field, "routes[].tls_enabled", &val) == 0) {
            route->tls_enabled = (bool)val;
        }
    }
    
    route_field = find_yaml_node(ctx->document, route_node, "tls_verify");
    if (route_field) {
        int val;
        if (get_yaml_bool(route_field, "routes[].tls_verify", &val) == 0) {
            route->tls_verify = (bool)val;
        }
    }
    
    // Parse nested config sections
    yaml_node_t *hc_node = find_yaml_node(ctx->document, route_node, "health_check");
    parse_health_check_config(ctx->document, hc_node, &route->health_check);
    
    yaml_node_t *cp_node = find_yaml_node(ctx->document, route_node, "connection_pool");
    parse_connection_pool_config(ctx->document, cp_node, &route->connection_pool);
    
    yaml_node_t *cb_node = find_yaml_node(ctx->document, route_node, "circuit_breaker");
    parse_circuit_breaker_config(ctx->document, cb_node, &route->circuit_breaker);
    
    yaml_node_t *sh_node = find_yaml_node(ctx->document, route_node, "security_headers");
    parse_security_headers_config(ctx->document, sh_node, &route->security_headers);
    
    yaml_node_t *cors_node = find_yaml_node(ctx->document, route_node, "cors");
    parse_cors_config(ctx->document, cors_node, &route->cors);
    
    route->inherit_global_headers = true;
    yaml_node_t *inherit_node = find_yaml_node(ctx->document, route_node, "inherit_global_headers");
    if (inherit_node) {
        int val;
        if (get_yaml_bool(inherit_node, "routes[].inherit_global_headers", &val) == 0) {
            route->inherit_global_headers = (bool)val;
        }
    }

    if (route->document_root[0] != '\0') {
        if (realpath(route->document_root, route->document_root_real)) {
            route->document_root_resolved = 1;
        } else {
            route->document_root_resolved = 0;
            strncpy(route->document_root_real, route->document_root,
                    sizeof(route->document_root_real) - 1);
            route->document_root_real[sizeof(route->document_root_real) - 1] = '\0';
        }
    }

    ctx->config->route_count++;
    return 0;
}

static int parse_routes_section(ConfigParser *ctx, yaml_node_t *node)
{
    if (node->type != YAML_SEQUENCE_NODE) {
        fprintf(stderr, "Invalid 'routes' (line %d): expected sequence\n",
                get_node_line(node));
        return -1;
    }

    for (yaml_node_item_t *item = node->data.sequence.items.start;
         item < node->data.sequence.items.top; item++) {
        yaml_node_t *route_node = yaml_document_get_node(ctx->document, *item);
        if (!route_node || route_node->type != YAML_MAPPING_NODE) {
            fprintf(stderr, "Invalid route entry (line %d): expected mapping\n",
                    get_node_line(route_node));
            return -1;
        }

        if (parse_route_entry(ctx, route_node) != 0)
            return -1;
    }

    return 0;
}

static int validate_config(const ServerConfig *config)
{
    if (config->ssl.certificate[0] == '\0' || config->ssl.private_key[0] == '\0') {
        fprintf(stderr, "Invalid SSL config: certificate and private_key are required\n");
        return -1;
    }

    if (config->logging.appender_flags == 0) {
        fprintf(stderr, "Invalid logging config: at least one appender must be enabled\n");
        return -1;
    }

    if ((config->logging.appender_flags & APPENDER_FILE) && config->logging.file[0] == '\0') {
        fprintf(stderr, "Invalid logging config: file appender requires 'logging.file'\n");
        return -1;
    }

    if (validate_routes(config) != 0)
        return -1;

    return 0;
}

int load_config(ServerConfig *config, const char *file_path)
{
    FILE *fh = NULL;
    yaml_parser_t parser;
    yaml_document_t document;
    yaml_node_t *root;
    ConfigParser ctx;
    int parser_ready = 0;
    int document_ready = 0;
    int rc = -1;

    if (!config || !file_path) {
        fprintf(stderr, "Invalid input: config and file_path are required\n");
        return -1;
    }

    set_config_defaults(config);

    fh = fopen(file_path, "rb");
    if (!fh) {
        fprintf(stderr, "Error opening configuration file: %s\n", file_path);
        return -1;
    }

    if (!yaml_parser_initialize(&parser)) {
        fprintf(stderr, "Unable to initialize YAML parser\n");
        goto cleanup;
    }
    parser_ready = 1;
    yaml_parser_set_input_file(&parser, fh);

    if (!yaml_parser_load(&parser, &document)) {
        fprintf(stderr, "Error parsing YAML configuration file\n");
        goto cleanup;
    }
    document_ready = 1;

    root = yaml_document_get_root_node(&document);
    if (!root || root->type != YAML_MAPPING_NODE) {
        fprintf(stderr, "YAML document is not a mapping\n");
        goto cleanup;
    }

    ctx.document = &document;
    ctx.root = root;
    ctx.config = config;

    yaml_node_t *node = find_yaml_node(&document, root, "server");
    if (node && parse_server_section(&ctx, node) != 0)
        goto cleanup;

    node = find_yaml_node(&document, root, "logging");
    if (node && parse_logging_section(&ctx, node) != 0)
        goto cleanup;

    node = find_yaml_node(&document, root, "ssl");
    if (node && parse_ssl_section(&ctx, node) != 0)
        goto cleanup;

    node = find_yaml_node(&document, root, "http2");
    if (node && parse_http2_section(&ctx, node) != 0)
        goto cleanup;

    node = find_yaml_node(&document, root, "routes");
    if (node && parse_routes_section(&ctx, node) != 0)
        goto cleanup;

    node = find_yaml_node(&document, root, "security_headers");
    parse_security_headers_config(&document, node, &config->security_headers);

    if (validate_config(config) != 0)
        goto cleanup;

    rc = 0;

cleanup:
    if (document_ready)
        yaml_document_delete(&document);
    if (parser_ready)
        yaml_parser_delete(&parser);
    if (fh)
        fclose(fh);
    return rc;
}

static void apply_int_env_override(const char *env_name, int *config_value, int min_val, int max_val, 
                                   const char *unit, int scale)
{
    const char *env_str = getenv(env_name);
    if (!env_str) return;
    
    char *endptr;
    long value = strtol(env_str, &endptr, 10);
    if (*endptr == '\0' && value >= min_val && value <= max_val) {
        *config_value = (int)(value * scale);
    } else {
        fprintf(stderr, "Warning: Invalid %s value '%s', using config value %d%s\n", 
                env_name, env_str, *config_value, unit);
    }
}

static void apply_string_env_override(const char *env_name, char *config_value, size_t max_len)
{
    const char *env_str = getenv(env_name);
    if (!env_str) return;
    
    strncpy(config_value, env_str, max_len - 1);
    config_value[max_len - 1] = '\0';
}

void apply_env_overrides(ServerConfig *config)
{
    apply_int_env_override("EMME_PORT", &config->port, 1, 65535, "", 1);
    apply_string_env_override("EMME_LOG_LEVEL", config->log_level, sizeof(config->log_level));
    apply_int_env_override("EMME_SHUTDOWN_TIMEOUT", &config->shutdown_timeout_seconds, 1, 300, "", 1);
    apply_int_env_override("EMME_MAX_CONNECTIONS", &config->max_connections, 1, 1000000, "", 1);
    apply_string_env_override("EMME_SSL_CERT_PATH", config->ssl.certificate, sizeof(config->ssl.certificate));
    apply_string_env_override("EMME_SSL_KEY_PATH", config->ssl.private_key, sizeof(config->ssl.private_key));
    apply_int_env_override("EMME_REQUEST_TIMEOUT", &config->request_timeout_ms, 1, 300, "ms", 1000);
    apply_int_env_override("EMME_TLS_HANDSHAKE_TIMEOUT", &config->tls_handshake_timeout_ms, 1, 60, "ms", 1000);
}
