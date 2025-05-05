#ifndef HTTP2_RESPONSE_H
#define HTTP2_RESPONSE_H

#include <nghttp2/nghttp2.h>
#include "server.h"

#define MAKE_NV(NAME, VALUE) (nghttp2_nv){(uint8_t *)(NAME), (uint8_t *)(VALUE), strlen(NAME), strlen(VALUE), NGHTTP2_NV_FLAG_NONE}

typedef struct {
    nghttp2_nv headers[16];
    size_t num_headers;
    char body[BUFFER_SIZE];
    size_t body_len;
    int status_code;
    char status_text[32];
    char content_type[64];
} Http2Response;

#endif // HTTP2_RESPONSE_H