// config_yaml.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yaml.h>
#include "log.h"
#include "config.h"

/* Helper function to find a scalar node within a mapping node given a key */
static yaml_node_t *find_yaml_node(yaml_document_t *doc, yaml_node_t *node, const char *key) {
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

/* Helper function to read an integer from a YAML scalar node */
static int get_yaml_int(yaml_document_t *doc, yaml_node_t *node, int *result) {
    (void)doc;  // currently, doc is not directly used here
    if (node && node->type == YAML_SCALAR_NODE) {
        *result = atoi((char *)node->data.scalar.value);
        return 0;
    } else {
        if (node) {
            log_message(LOG_LEVEL_ERROR, "Parsing error: expected scalar for integer but got type %d at line %d, column %d",
                        node->type, node->start_mark.line, node->start_mark.column);
        } else {
            log_message(LOG_LEVEL_ERROR, "Parsing error: received NULL node when expecting a scalar for integer");
        }
        return -1;
    }
}

/* Helper function to read a string from a YAML scalar node */
static int get_yaml_string(yaml_document_t *doc, yaml_node_t *node, char *buffer, size_t size) {
    (void)doc;  // currently, doc is not used here
    if (node && node->type == YAML_SCALAR_NODE) {
        strncpy(buffer, (char *)node->data.scalar.value, size - 1);
        buffer[size - 1] = '\0';
        return 0;
    } else {
        if (node) {
            log_message(LOG_LEVEL_ERROR, "Parsing error: expected scalar for string but got type %d at line %d, column %d",
                        node->type, node->start_mark.line, node->start_mark.column);
        } else {
            log_message(LOG_LEVEL_ERROR, "Parsing error: received NULL node when expecting a scalar for string");
        }
        return -1;
    }
}

/* Parse the logging section for the appender_flags array */
static void parse_appender_flags(yaml_document_t *doc, yaml_node_t *node, LoggingConfig *log_cfg) {
    (void)doc; // doc is not needed beyond node extraction here
    if (!node || node->type != YAML_SEQUENCE_NODE)
        return;

    for (yaml_node_item_t *item = node->data.sequence.items.start;
         item < node->data.sequence.items.top; item++) {
        yaml_node_t *flag_node = yaml_document_get_node(doc, *item);
        if (flag_node && flag_node->type == YAML_SCALAR_NODE) {
            const char *flag = (char *)flag_node->data.scalar.value;
            if (strcmp(flag, "file") == 0)
                log_cfg->appender_flags |= APPENDER_FILE;
            else if (strcmp(flag, "console") == 0)
                log_cfg->appender_flags |= APPENDER_CONSOLE;
        }
    }
}

/* Function to load the configuration from a YAML file */
int load_config(ServerConfig *config, const char *file_path) {
    FILE *fh = fopen(file_path, "rb");
    if (!fh) {
        log_message(LOG_LEVEL_ERROR, "Error opening configuration file: %s", file_path);
        return -1;
    }

    yaml_parser_t parser;
    yaml_document_t document;
    if (!yaml_parser_initialize(&parser)) {
        fclose(fh);
        log_message(LOG_LEVEL_ERROR, "Unable to initialize YAML parser");
        return -1;
    }
    yaml_parser_set_input_file(&parser, fh);

    if (!yaml_parser_load(&parser, &document)) {
        log_message(LOG_LEVEL_ERROR, "Error parsing YAML configuration file");
        yaml_parser_delete(&parser);
        fclose(fh);
        return -1;
    }
    fclose(fh);

    yaml_node_t *root = yaml_document_get_root_node(&document);
    if (!root || root->type != YAML_MAPPING_NODE) {
        log_message(LOG_LEVEL_ERROR, "YAML document is not a mapping");
        yaml_document_delete(&document);
        yaml_parser_delete(&parser);
        return -1;
    }

    memset(config, 0, sizeof(ServerConfig));

    /* Parse global settings under the "server" key */
    yaml_node_t *server_node = find_yaml_node(&document, root, "server");
    if (server_node && server_node->type == YAML_MAPPING_NODE) {
        yaml_node_t *port_node = find_yaml_node(&document, server_node, "port");
        if (port_node)
            get_yaml_int(&document, port_node, &config->port);

        yaml_node_t *max_conn_node = find_yaml_node(&document, server_node, "max_connections");
        if (max_conn_node)
            get_yaml_int(&document, max_conn_node, &config->max_connections);

        yaml_node_t *log_level_node = find_yaml_node(&document, server_node, "log_level");
        if (log_level_node)
            get_yaml_string(&document, log_level_node, config->log_level, sizeof(config->log_level));
    }

    /* Parse the logging section */
    yaml_node_t *logging_node = find_yaml_node(&document, root, "logging");
    if (logging_node && logging_node->type == YAML_MAPPING_NODE) {
        yaml_node_t *file_node = find_yaml_node(&document, logging_node, "file");
        if (file_node)
            get_yaml_string(&document, file_node, config->logging.file, sizeof(config->logging.file));
        
        yaml_node_t *level_node = find_yaml_node(&document, logging_node, "level");
        if (level_node) {
            char level_str[16] = {0};
            get_yaml_string(&document, level_node, level_str, sizeof(level_str));
            if (strcmp(level_str, "debug") == 0)
                config->logging.level = LOG_LEVEL_DEBUG;
            else if (strcmp(level_str, "info") == 0)
                config->logging.level = LOG_LEVEL_INFO;
            else if (strcmp(level_str, "warn") == 0)
                config->logging.level = LOG_LEVEL_WARN;
            else if (strcmp(level_str, "error") == 0)
                config->logging.level = LOG_LEVEL_ERROR;
        }

        yaml_node_t *format_node = find_yaml_node(&document, logging_node, "format");
        if (format_node) {
            char format_str[16] = {0};
            get_yaml_string(&document, format_node, format_str, sizeof(format_str));
            config->logging.format = (strcmp(format_str, "json") == 0) ? LOG_FORMAT_JSON : LOG_FORMAT_PLAIN;
        }

        yaml_node_t *buffer_size_node = find_yaml_node(&document, logging_node, "buffer_size");
        if (buffer_size_node)
            get_yaml_int(&document, buffer_size_node, (int *)&config->logging.buffer_size);

        yaml_node_t *rollover_size_node = find_yaml_node(&document, logging_node, "rollover_size");
        if (rollover_size_node)
            get_yaml_int(&document, rollover_size_node, (int *)&config->logging.rollover_size);

        yaml_node_t *rollover_daily_node = find_yaml_node(&document, logging_node, "rollover_daily");
        if (rollover_daily_node) {
            char daily_str[8] = {0};
            get_yaml_string(&document, rollover_daily_node, daily_str, sizeof(daily_str));
            config->logging.rollover_daily = (strcmp(daily_str, "true") == 0) ? 1 : 0;
        }

        yaml_node_t *appender_flags_node = find_yaml_node(&document, logging_node, "appender_flags");
        if (appender_flags_node)
            parse_appender_flags(&document, appender_flags_node, &config->logging);
    }

    /* Parse the SSL section */
    yaml_node_t *ssl_node = find_yaml_node(&document, root, "ssl");
    if (ssl_node && ssl_node->type == YAML_MAPPING_NODE) {
        yaml_node_t *cert_node = find_yaml_node(&document, ssl_node, "certificate");
        if (cert_node)
            get_yaml_string(&document, cert_node, config->ssl.certificate, sizeof(config->ssl.certificate));
        yaml_node_t *key_node = find_yaml_node(&document, ssl_node, "private_key");
        if (key_node)
            get_yaml_string(&document, key_node, config->ssl.private_key, sizeof(config->ssl.private_key));
    }

    /* Parse the routes section */
    yaml_node_t *routes_node = find_yaml_node(&document, root, "routes");
    if (routes_node && routes_node->type == YAML_SEQUENCE_NODE) {
        for (yaml_node_item_t *item = routes_node->data.sequence.items.start;
             item < routes_node->data.sequence.items.top; item++) {
            if (config->route_count >= MAX_ROUTES)
                break;
            yaml_node_t *route_node = yaml_document_get_node(&document, *item);
            if (route_node && route_node->type == YAML_MAPPING_NODE) {
                yaml_node_t *path_node = find_yaml_node(&document, route_node, "path");
                if (path_node)
                    get_yaml_string(&document, path_node,
                                    config->routes[config->route_count].path,
                                    sizeof(config->routes[config->route_count].path));
                yaml_node_t *tech_node = find_yaml_node(&document, route_node, "technology");
                if (tech_node)
                    get_yaml_string(&document, tech_node,
                                    config->routes[config->route_count].technology,
                                    sizeof(config->routes[config->route_count].technology));
                yaml_node_t *docroot_node = find_yaml_node(&document, route_node, "document_root");
                if (docroot_node)
                    get_yaml_string(&document, docroot_node,
                                    config->routes[config->route_count].document_root,
                                    sizeof(config->routes[config->route_count].document_root));
                yaml_node_t *backend_node = find_yaml_node(&document, route_node, "backend");
                if (backend_node)
                    get_yaml_string(&document, backend_node,
                                    config->routes[config->route_count].backend,
                                    sizeof(config->routes[config->route_count].backend));
                config->route_count++;
            }
        }
    }

    yaml_document_delete(&document);
    yaml_parser_delete(&parser);
    return 0;
}
