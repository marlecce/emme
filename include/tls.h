#ifndef TLS_H
#define TLS_H

#include <openssl/ssl.h>
#include "config.h"

SSL_CTX *create_ssl_context(const char *cert_file, const char *key_file, ServerConfig *config);

void cleanup_ssl_context(SSL_CTX *ctx);

void log_session_stats(SSL_CTX *ctx);

#endif
