#include "tls.h"
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <stdio.h>
#include <stdlib.h>
#include <log.h>

static const unsigned char protos[] = "\x02h2\x08http/1.1";
static const unsigned int protos_len = 12;

static int alpn_select_cb(SSL *ssl,
                          const unsigned char **out,
                          unsigned char *outlen,
                          const unsigned char *in,
                          unsigned int inlen,
                          void *arg);

SSL_CTX *create_ssl_context(const char *cert_path, const char *key_path)
{
    SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
    if (!ctx)
    {
        return NULL;
    }

    SSL_CTX_set_options(ctx,
                        SSL_OP_NO_SSLv2 |
                            SSL_OP_NO_SSLv3 |
                            SSL_OP_NO_TLSv1 |
                            SSL_OP_NO_TLSv1_1);

    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);

    if (SSL_CTX_set_alpn_protos(ctx, protos, protos_len) != 0)
    {
        log_message(LOG_LEVEL_ERROR, "Failed to set ALPN protocols");
        SSL_CTX_free(ctx);
        return NULL;
    }

    SSL_CTX_set_alpn_select_cb(ctx, alpn_select_cb, NULL);

    if (SSL_CTX_use_certificate_file(ctx, cert_path, SSL_FILETYPE_PEM) <= 0)
    {
        log_message(LOG_LEVEL_ERROR, "Failed to load certificate file: %s",
                    ERR_error_string(ERR_get_error(), NULL));
        SSL_CTX_free(ctx);
        return NULL;
    }

    if (SSL_CTX_use_PrivateKey_file(ctx, key_path, SSL_FILETYPE_PEM) <= 0)
    {
        log_message(LOG_LEVEL_ERROR, "Failed to load private key file: %s",
                    ERR_error_string(ERR_get_error(), NULL));
        SSL_CTX_free(ctx);
        return NULL;
    }

    SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_SERVER);
    SSL_CTX_set_session_id_context(ctx, (const unsigned char *)"emme", 4);

    return ctx;
}

static int alpn_select_cb(SSL *ssl, const unsigned char **out, unsigned char *outlen,
                          const unsigned char *in, unsigned int inlen, void *arg)
{
    (void)ssl;
    (void)arg;
    if (SSL_select_next_proto((unsigned char **)out, outlen, protos, protos_len, in, inlen) != OPENSSL_NPN_NEGOTIATED)
    {
        return SSL_TLSEXT_ERR_NOACK;
    }
    return SSL_TLSEXT_ERR_OK;
}

void cleanup_ssl_context(SSL_CTX *ctx)
{
    SSL_CTX_free(ctx);
    EVP_cleanup();
}
