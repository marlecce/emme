#include "http_parser.h"
#include <string.h>
#include <ctype.h>

int parse_http_request(char *buffer, size_t len, HttpRequest *req) {
    char *cursor = buffer;
    char *end = buffer + len;
    char *line_end;

    // Parse della Request-Line: METHOD SP PATH SP VERSION CRLF
    line_end = strstr(cursor, "\r\n");
    if (!line_end) return -1;
    *line_end = '\0';
    // Estrai il metodo
    char *method = cursor;
    char *space = strchr(method, ' ');
    if (!space) return -1;
    *space = '\0';
    req->method = method;
    
    // Estrai il path
    char *path = space + 1;
    space = strchr(path, ' ');
    if (!space) return -1;
    *space = '\0';
    req->path = path;
    
    // Il resto è la versione
    char *version = space + 1;
    req->version = version;

    // Sposta il cursore oltre il CRLF
    cursor = line_end + 2;

    // Parse degli header
    req->header_count = 0;
    while (cursor < end && !(cursor[0] == '\r' && cursor[1] == '\n')) {
        line_end = strstr(cursor, "\r\n");
        if (!line_end) break;
        *line_end = '\0';

        // Trova il separatore ':'
        char *colon = strchr(cursor, ':');
        if (!colon) return -1;
        *colon = '\0';
        char *field = cursor;
        char *value = colon + 1;
        
        // Rimuove eventuali spazi iniziali nel value
        while (*value && isspace((unsigned char)*value)) value++;

        if (req->header_count < MAX_HEADERS) {
            req->headers[req->header_count].field = field;
            req->headers[req->header_count].value = value;
            req->header_count++;
        }
        // Altrimenti potresti decidere di ignorare header extra

        cursor = line_end + 2;
    }

    // Il parser ha letto anche la linea vuota che separa header e body
    return 0;
}
