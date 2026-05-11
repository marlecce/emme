#ifndef HTTP2_RESPONSE_H
#define HTTP2_RESPONSE_H

#include <nghttp2/nghttp2.h>
#include "server.h"
#include "config.h"

#define MAKE_NV(NAME, VALUE) (nghttp2_nv){(uint8_t *)(NAME), (uint8_t *)(VALUE), strlen(NAME), strlen(VALUE), NGHTTP2_NV_FLAG_NONE}

typedef struct Http2Response {
    nghttp2_nv headers[16];
    size_t num_headers;
    char body[BUFFER_SIZE];
    size_t body_len;
    int status_code;
    char status_code_str[4];
    char content_length_str[32];
    char status_text[32];
    char content_type[64];
} Http2Response;

void h2_response_init(Http2Response *resp);
void h2_response_set_status(Http2Response *resp, int status_code, const char *status_text);
void h2_response_add_header(Http2Response *resp, const char *name, const char *value);
void h2_response_set_body(Http2Response *resp, const char *body, size_t len);
void h2_response_set_body_len(Http2Response *resp, size_t len);
void h2_response_set_content_type(Http2Response *resp, const char *content_type);
void h2_response_finalize(Http2Response *resp);
void h2_response_add_security_headers(Http2Response *resp, SecurityHeadersConfig *config, CORSConfig *cors);

#endif // HTTP2_RESPONSE_H
