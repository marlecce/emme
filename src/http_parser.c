#include "http_parser.h"
#include <string.h>
#include <ctype.h>

int parse_http_request(char *buffer, size_t len, HttpRequest *req) {
    char *cursor = buffer;
    char *end = buffer + len;
    char *line_end;

    // Parse the Request-Line: METHOD SP PATH SP VERSION CRLF
    line_end = strstr(cursor, "\r\n");
    if (!line_end) return -1;
    *line_end = '\0';
    
    // Extract the method
    char *method = cursor;
    char *space = strchr(method, ' ');
    if (!space) return -1;
    *space = '\0';
    req->method = method;
    
    // Extract the path
    char *path = space + 1;
    space = strchr(path, ' ');
    if (!space) return -1;
    *space = '\0';
    req->path = path;
    
    // The rest is the HTTP version
    char *version = space + 1;
    req->version = version;

    // Move the cursor past the CRLF
    cursor = line_end + 2;

    // Parse headers
    req->header_count = 0;
    while (cursor < end && !(cursor[0] == '\r' && cursor[1] == '\n')) {
        line_end = strstr(cursor, "\r\n");
        if (!line_end) break;
        *line_end = '\0';

        // Find the ':' separator
        char *colon = strchr(cursor, ':');
        if (!colon) return -1;
        *colon = '\0';
        char *field = cursor;
        char *value = colon + 1;
        
        // Remove any leading spaces in the value
        while (*value && isspace((unsigned char)*value)) value++;

        if (req->header_count < MAX_HEADERS) {
            req->headers[req->header_count].field = field;
            req->headers[req->header_count].value = value;
            req->header_count++;
        }
        // Otherwise, you might decide to ignore extra headers

        cursor = line_end + 2;
    }

    // The parser has also read the empty line separating headers and body
    return 0;
}
