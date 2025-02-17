#ifndef HTTP_PARSER_H
#define HTTP_PARSER_H

#include <stddef.h>

#define MAX_HEADERS 20

typedef struct {
    const char *field;
    const char *value;
} HttpHeader;

typedef struct {
    const char *method;
    const char *path;
    const char *version;
    int header_count;
    HttpHeader headers[MAX_HEADERS];
} HttpRequest;

int parse_http_request(char *buffer, size_t len, HttpRequest *req);

#endif
