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

static void set_config_defaults(ServerConfig *config)
{
    memset(config, 0, sizeof(*config));
    config->port = 8443;
    config->max_connections = 100;
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

    config->http2.keepalive_timeout = 60;
    config->http2.max_requests_per_connection = 1000;
    config->http2.max_concurrent_streams = 100;
}

static int get_yaml_string(yaml_node_t *node, const char *field, char *buffer, size_t size)
{
    int written;
    const char *value;

    if (!node || node->type != YAML_SCALAR_NODE) {
        fprintf(stderr, "Invalid '%s': expected a scalar string\n", field);
        return -1;
    }

    value = (const char *)node->data.scalar.value;
    written = snprintf(buffer, size, "%s", value);
    if (written < 0 || (size_t)written >= size) {
        fprintf(stderr, "Invalid '%s': value too long\n", field);
        return -1;
    }
    return 0;
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

static int get_yaml_int_in_range(yaml_node_t *node, const char *field,
                                 int min, int max, int *result)
{
    if (!node || node->type != YAML_SCALAR_NODE) {
        fprintf(stderr, "Invalid '%s': expected integer scalar\n", field);
        return -1;
    }
    if (parse_int_in_range((const char *)node->data.scalar.value, min, max, result) != 0) {
        fprintf(stderr, "Invalid '%s': expected integer in range [%d, %d]\n",
                field, min, max);
        return -1;
    }
    return 0;
}

static int get_yaml_size_in_range(yaml_node_t *node, const char *field,
                                  size_t min, size_t max, size_t *result)
{
    if (!node || node->type != YAML_SCALAR_NODE) {
        fprintf(stderr, "Invalid '%s': expected integer scalar\n", field);
        return -1;
    }
    if (parse_size_in_range((const char *)node->data.scalar.value, min, max, result) != 0) {
        fprintf(stderr, "Invalid '%s': expected integer in range [%zu, %zu]\n",
                field, min, max);
        return -1;
    }
    return 0;
}

static int get_yaml_bool(yaml_node_t *node, const char *field, int *result)
{
    const char *value;

    if (!node || node->type != YAML_SCALAR_NODE) {
        fprintf(stderr, "Invalid '%s': expected boolean scalar\n", field);
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

    fprintf(stderr, "Invalid '%s': expected true/false\n", field);
    return -1;
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

static int validate_backend(const char *backend)
{
    char host[64];
    int port;
    char trailing;

    if (!backend || backend[0] == '\0')
        return -1;
    if (sscanf(backend, "%63[^:]:%d%c", host, &port, &trailing) != 2)
        return -1;
    if (host[0] == '\0')
        return -1;
    if (port < 1 || port > 65535)
        return -1;
    return 0;
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

int load_config(ServerConfig *config, const char *file_path)
{
    FILE *fh = NULL;
    yaml_parser_t parser;
    yaml_document_t document;
    yaml_node_t *root;
    yaml_node_t *node;
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

    node = find_yaml_node(&document, root, "server");
    if (node) {
        if (node->type != YAML_MAPPING_NODE) {
            fprintf(stderr, "Invalid 'server': expected mapping\n");
            goto cleanup;
        }

        yaml_node_t *port_node = find_yaml_node(&document, node, "port");
        if (port_node && get_yaml_int_in_range(port_node, "server.port", 1, 65535, &config->port) != 0)
            goto cleanup;

        yaml_node_t *max_conn_node = find_yaml_node(&document, node, "max_connections");
        if (max_conn_node &&
            get_yaml_int_in_range(max_conn_node, "server.max_connections", 1, 1000000,
                                  &config->max_connections) != 0)
            goto cleanup;

        yaml_node_t *log_level_node = find_yaml_node(&document, node, "log_level");
        if (log_level_node &&
            get_yaml_string(log_level_node, "server.log_level",
                            config->log_level, sizeof(config->log_level)) != 0)
            goto cleanup;
    }

    node = find_yaml_node(&document, root, "logging");
    if (node) {
        if (node->type != YAML_MAPPING_NODE) {
            fprintf(stderr, "Invalid 'logging': expected mapping\n");
            goto cleanup;
        }

        yaml_node_t *file_node = find_yaml_node(&document, node, "file");
        if (file_node &&
            get_yaml_string(file_node, "logging.file",
                            config->logging.file, sizeof(config->logging.file)) != 0)
            goto cleanup;

        yaml_node_t *level_node = find_yaml_node(&document, node, "level");
        if (level_node) {
            char level_str[16];
            if (get_yaml_string(level_node, "logging.level", level_str, sizeof(level_str)) != 0)
                goto cleanup;
            if (parse_log_level(level_str, &config->logging.level) != 0) {
                fprintf(stderr, "Invalid 'logging.level': expected debug/info/warn/error\n");
                goto cleanup;
            }
        }

        yaml_node_t *format_node = find_yaml_node(&document, node, "format");
        if (format_node) {
            char format_str[16];
            if (get_yaml_string(format_node, "logging.format", format_str, sizeof(format_str)) != 0)
                goto cleanup;
            if (parse_log_format(format_str, &config->logging.format) != 0) {
                fprintf(stderr, "Invalid 'logging.format': expected plain/json\n");
                goto cleanup;
            }
        }

        yaml_node_t *buffer_size_node = find_yaml_node(&document, node, "buffer_size");
        if (buffer_size_node &&
            get_yaml_size_in_range(buffer_size_node, "logging.buffer_size", 1, 1048576,
                                   &config->logging.buffer_size) != 0)
            goto cleanup;

        yaml_node_t *rollover_size_node = find_yaml_node(&document, node, "rollover_size");
        if (rollover_size_node &&
            get_yaml_size_in_range(rollover_size_node, "logging.rollover_size", 0, 1099511627776ULL,
                                   &config->logging.rollover_size) != 0)
            goto cleanup;

        yaml_node_t *rollover_daily_node = find_yaml_node(&document, node, "rollover_daily");
        if (rollover_daily_node &&
            get_yaml_bool(rollover_daily_node, "logging.rollover_daily",
                          &config->logging.rollover_daily) != 0)
            goto cleanup;

        yaml_node_t *appender_flags_node = find_yaml_node(&document, node, "appender_flags");
        if (parse_appender_flags(&document, appender_flags_node, &config->logging) != 0)
            goto cleanup;
    }

    node = find_yaml_node(&document, root, "ssl");
    if (node) {
        if (node->type != YAML_MAPPING_NODE) {
            fprintf(stderr, "Invalid 'ssl': expected mapping\n");
            goto cleanup;
        }

        yaml_node_t *cert_node = find_yaml_node(&document, node, "certificate");
        if (cert_node &&
            get_yaml_string(cert_node, "ssl.certificate",
                            config->ssl.certificate, sizeof(config->ssl.certificate)) != 0)
            goto cleanup;

        yaml_node_t *key_node = find_yaml_node(&document, node, "private_key");
        if (key_node &&
            get_yaml_string(key_node, "ssl.private_key",
                            config->ssl.private_key, sizeof(config->ssl.private_key)) != 0)
            goto cleanup;

        yaml_node_t *cache_size_node = find_yaml_node(&document, node, "session_cache_size");
        if (cache_size_node &&
            get_yaml_int_in_range(cache_size_node, "ssl.session_cache_size", 1000, 1000000,
                                  &config->ssl.session_cache_size) != 0)
            goto cleanup;

        yaml_node_t *timeout_node = find_yaml_node(&document, node, "session_timeout");
        if (timeout_node &&
            get_yaml_int_in_range(timeout_node, "ssl.session_timeout", 60, 3600,
                                  &config->ssl.session_timeout) != 0)
            goto cleanup;

        yaml_node_t *ticket_key_node = find_yaml_node(&document, node, "session_ticket_key");
        if (ticket_key_node &&
            get_yaml_string(ticket_key_node, "ssl.session_ticket_key",
                            config->ssl.session_ticket_key, sizeof(config->ssl.session_ticket_key)) != 0)
            goto cleanup;
    }

    node = find_yaml_node(&document, root, "http2");
    if (node) {
        if (node->type != YAML_MAPPING_NODE) {
            fprintf(stderr, "Invalid 'http2': expected mapping\n");
            goto cleanup;
        }

        yaml_node_t *keepalive_node = find_yaml_node(&document, node, "keepalive_timeout");
        if (keepalive_node &&
            get_yaml_int_in_range(keepalive_node, "http2.keepalive_timeout", 10, 300,
                                  &config->http2.keepalive_timeout) != 0)
            goto cleanup;

        yaml_node_t *max_requests_node = find_yaml_node(&document, node, "max_requests_per_connection");
        if (max_requests_node &&
            get_yaml_int_in_range(max_requests_node, "http2.max_requests_per_connection", 1, 100000,
                                  &config->http2.max_requests_per_connection) != 0)
            goto cleanup;

        yaml_node_t *max_streams_node = find_yaml_node(&document, node, "max_concurrent_streams");
        if (max_streams_node &&
            get_yaml_int_in_range(max_streams_node, "http2.max_concurrent_streams", 1, 1000,
                                  &config->http2.max_concurrent_streams) != 0)
            goto cleanup;
    }

    node = find_yaml_node(&document, root, "routes");
    if (node) {
        if (node->type != YAML_SEQUENCE_NODE) {
            fprintf(stderr, "Invalid 'routes': expected sequence\n");
            goto cleanup;
        }

        for (yaml_node_item_t *item = node->data.sequence.items.start;
             item < node->data.sequence.items.top; item++) {
            yaml_node_t *route_node = yaml_document_get_node(&document, *item);
            Route *route;
            yaml_node_t *route_field;

            if (config->route_count >= MAX_ROUTES) {
                fprintf(stderr, "Too many routes: maximum supported is %d\n", MAX_ROUTES);
                goto cleanup;
            }
            if (!route_node || route_node->type != YAML_MAPPING_NODE) {
                fprintf(stderr, "Invalid route entry: expected mapping\n");
                goto cleanup;
            }

            route = &config->routes[config->route_count];
            memset(route, 0, sizeof(*route));

            route_field = find_yaml_node(&document, route_node, "path");
            if (!route_field ||
                get_yaml_string(route_field, "routes[].path",
                                route->path, sizeof(route->path)) != 0) {
                fprintf(stderr, "Invalid route: missing or invalid 'path'\n");
                goto cleanup;
            }

            route_field = find_yaml_node(&document, route_node, "technology");
            if (!route_field ||
                get_yaml_string(route_field, "routes[].technology",
                                route->technology, sizeof(route->technology)) != 0) {
                fprintf(stderr, "Invalid route: missing or invalid 'technology'\n");
                goto cleanup;
            }

            route_field = find_yaml_node(&document, route_node, "document_root");
            if (route_field &&
                get_yaml_string(route_field, "routes[].document_root",
                                route->document_root, sizeof(route->document_root)) != 0)
                goto cleanup;

            route_field = find_yaml_node(&document, route_node, "backend");
            if (route_field &&
                get_yaml_string(route_field, "routes[].backend",
                                route->backend, sizeof(route->backend)) != 0)
                goto cleanup;

            if (route->document_root[0] != '\0') {
                if (realpath(route->document_root, route->document_root_real)) {
                    route->document_root_resolved = 1;
                } else {
                    route->document_root_resolved = 0;
                    strncpy(route->document_root_real, route->document_root,
                            sizeof(route->document_root_real) - 1);
                    route->document_root_real[sizeof(route->document_root_real) - 1] = '\0';
                }
            } else {
                route->document_root_resolved = 0;
            }

            config->route_count++;
        }
    }

    if (config->ssl.certificate[0] == '\0' || config->ssl.private_key[0] == '\0') {
        fprintf(stderr, "Invalid SSL config: certificate and private_key are required\n");
        goto cleanup;
    }
    if (config->logging.appender_flags == 0) {
        fprintf(stderr, "Invalid logging config: at least one appender must be enabled\n");
        goto cleanup;
    }
    if ((config->logging.appender_flags & APPENDER_FILE) && config->logging.file[0] == '\0') {
        fprintf(stderr, "Invalid logging config: file appender requires 'logging.file'\n");
        goto cleanup;
    }
    if (validate_routes(config) != 0)
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
