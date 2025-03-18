#include "tls.h"
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <stdio.h>
#include <stdlib.h>

SSL_CTX *create_ssl_context(const char *cert_file, const char *key_file) {
    const SSL_METHOD *method;
    SSL_CTX *ctx;

    OpenSSL_add_ssl_algorithms();
    method = TLS_server_method();
    ctx = SSL_CTX_new(method);
    if (!ctx) {
        fprintf(stderr, "SSL context creation failed\n");
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }

    if (SSL_CTX_use_certificate_file(ctx, cert_file, SSL_FILETYPE_PEM) <= 0) {
        fprintf(stderr, "Certification loading failed\n");
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }

    if (SSL_CTX_use_PrivateKey_file(ctx, key_file, SSL_FILETYPE_PEM) <= 0) {
        fprintf(stderr, "Private key loading failed\n");
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }

    return ctx;
}

void cleanup_ssl_context(SSL_CTX *ctx) {
    SSL_CTX_free(ctx);
    EVP_cleanup();
}
