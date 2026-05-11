#include "http2_response.h"
#include <string.h>
#include <stdio.h>
#include "metrics.h"

void h2_response_init(Http2Response *resp)
{
    memset(resp, 0, sizeof(Http2Response));
    resp->status_code = 200;
    strcpy(resp->status_text, "OK");
    strcpy(resp->content_type, "text/html");
}

void h2_response_set_status(Http2Response *resp, int status_code, const char *status_text)
{
    resp->status_code = status_code;
    snprintf(resp->status_text, sizeof(resp->status_text), "%s", status_text ? status_text : "OK");
    snprintf(resp->status_code_str, sizeof(resp->status_code_str), "%d", status_code);
}

void h2_response_add_header(Http2Response *resp, const char *name, const char *value)
{
    if (resp->num_headers >= 16)
        return;
    
    resp->headers[resp->num_headers] = MAKE_NV(name, value);
    resp->num_headers++;
}

void h2_response_set_body(Http2Response *resp, const char *body, size_t len)
{
    if (len >= sizeof(resp->body))
        len = sizeof(resp->body) - 1;
    
    memcpy(resp->body, body, len);
    resp->body[len] = '\0';
    resp->body_len = len;
}

void h2_response_set_body_len(Http2Response *resp, size_t len)
{
    resp->body_len = len;
}

void h2_response_set_content_type(Http2Response *resp, const char *content_type)
{
    snprintf(resp->content_type, sizeof(resp->content_type), "%s", content_type);
}

void h2_response_finalize(Http2Response *resp)
{
    snprintf(resp->content_length_str, sizeof(resp->content_length_str), "%zu", resp->body_len);
}

void h2_response_add_security_headers(Http2Response *resp, SecurityHeadersConfig *config, CORSConfig *cors)
{
    int sec_headers_added = 0;
    int cors_headers_added = 0;
    
    if (!resp || !config || !config->enabled)
        return;
    
    for (int i = 0; i < config->header_count && resp->num_headers < 16; i++) {
        const SecurityHeader *header = &config->headers[i];
        h2_response_add_header(resp, header->name, header->value);
        sec_headers_added++;
    }
    
    if (cors && cors->enabled) {
        if (cors->allow_origin[0] != '\0' && resp->num_headers < 16) {
            h2_response_add_header(resp, "Access-Control-Allow-Origin", cors->allow_origin);
            cors_headers_added++;
        }
        if (cors->allow_methods[0] != '\0' && resp->num_headers < 16) {
            h2_response_add_header(resp, "Access-Control-Allow-Methods", cors->allow_methods);
            cors_headers_added++;
        }
        if (cors->allow_headers[0] != '\0' && resp->num_headers < 16) {
            h2_response_add_header(resp, "Access-Control-Allow-Headers", cors->allow_headers);
            cors_headers_added++;
        }
        if (cors->allow_credentials && resp->num_headers < 16) {
            h2_response_add_header(resp, "Access-Control-Allow-Credentials", "true");
            cors_headers_added++;
        }
        if (cors->max_age_seconds > 0 && resp->num_headers < 16) {
            char max_age_str[32];
            snprintf(max_age_str, sizeof(max_age_str), "%d", cors->max_age_seconds);
            h2_response_add_header(resp, "Access-Control-Max-Age", max_age_str);
            cors_headers_added++;
        }
    }
    
    if (sec_headers_added > 0) {
        metrics_increment_security_headers_sent();
    }
    if (cors_headers_added > 0) {
        metrics_increment_cors_headers_sent();
    }
}
