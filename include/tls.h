#ifndef TLS_H
#define TLS_H

#include <openssl/ssl.h>

SSL_CTX *create_ssl_context(const char *cert_file, const char *key_file);

void cleanup_ssl_context(SSL_CTX *ctx);

#endif
