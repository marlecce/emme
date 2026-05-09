#include "tls.h"
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include "log.h"

#define SESSION_TICKET_KEY_SIZE 80

static const unsigned char protos[] = "\x02h2\x08http/1.1";
static const unsigned int protos_len = 12;

typedef struct {
    atomic_long new_sessions;
    atomic_long removed_sessions;
    atomic_long cache_full;
} SSLSessionStats;

static SSLSessionStats g_session_stats;

static int alpn_select_cb(SSL *ssl,
                          const unsigned char **out,
                          unsigned char *outlen,
                          const unsigned char *in,
                          unsigned int inlen,
                          void *arg);

static int session_new_callback(SSL *ssl, SSL_SESSION *session);
static void session_remove_callback(SSL_CTX *ctx, SSL_SESSION *session);

SSL_CTX *create_ssl_context(const char *cert_path, const char *key_path, ServerConfig *config)
{
    SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
    if (!ctx)
    {
        log_message(LOG_LEVEL_ERROR, "Unable to create SSL context");
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
    /* Configure session caching for high-performance resumption */
    SSL_CTX_set_session_cache_mode(ctx, 
        SSL_SESS_CACHE_SERVER | SSL_SESS_CACHE_NO_INTERNAL);

    /* Set cache size (100K sessions = ~4MB memory) */
    SSL_CTX_sess_set_cache_size(ctx, config->ssl.session_cache_size);

    /* Set session timeout (5 minutes for security) */
    SSL_CTX_set_timeout(ctx, config->ssl.session_timeout);

    /* Session ID context for virtual host separation */
    unsigned char sess_id_ctx[] = "emme";
    if (SSL_CTX_set_session_id_context(ctx, sess_id_ctx, sizeof(sess_id_ctx)) != 1)
    {
        log_message(LOG_LEVEL_ERROR, "Failed to set session id context");
        SSL_CTX_free(ctx);
        return NULL;
    }

    /* Load session ticket key for stateless resumption */
    if (config->ssl.session_ticket_key[0] != '\0')
    {
        FILE *f = fopen(config->ssl.session_ticket_key, "rb");
        if (f)
        {
            unsigned char key[SESSION_TICKET_KEY_SIZE];
            if (fread(key, 1, SESSION_TICKET_KEY_SIZE, f) == SESSION_TICKET_KEY_SIZE)
            {
                if (SSL_CTX_set_tlsext_ticket_keys(ctx, key, SESSION_TICKET_KEY_SIZE) == 1)
                {
                    log_message(LOG_LEVEL_INFO, "Session tickets enabled from %s",
                                config->ssl.session_ticket_key);
                }
                else
                {
                    log_message(LOG_LEVEL_WARN, "Failed to set session ticket keys");
                }
            }
            else
            {
                log_message(LOG_LEVEL_WARN, "Invalid ticket key file (expected %d bytes)",
                            SESSION_TICKET_KEY_SIZE);
            }
            fclose(f);
        }
        else
        {
            log_message(LOG_LEVEL_WARN, "Cannot open ticket key file %s, using cache-only mode",
                        config->ssl.session_ticket_key);
        }
    }
    else
    {
        log_message(LOG_LEVEL_INFO, "Session tickets not configured, using session cache only");
    }

    /* Register session callbacks for statistics tracking */
    SSL_CTX_sess_set_new_cb(ctx, session_new_callback);
    SSL_CTX_sess_set_remove_cb(ctx, session_remove_callback);

    log_message(LOG_LEVEL_INFO, "TLS session cache configured: size=%d timeout=%ds",
                config->ssl.session_cache_size, config->ssl.session_timeout);

    return ctx;
}

static int alpn_select_cb(SSL *ssl, const unsigned char **out, unsigned char *outlen,
                          const unsigned char *in, unsigned int inlen, void *arg)
{
    (void)ssl;
    (void)arg;
    if (SSL_select_next_proto((unsigned char **)out, outlen, protos, protos_len, in, inlen)
          != OPENSSL_NPN_NEGOTIATED)
    {
        return SSL_TLSEXT_ERR_NOACK;
    }
    return SSL_TLSEXT_ERR_OK;
}

/*
 * Called when a new SSL session is created.
 * Tracks session count and warns when cache reaches capacity.
 */
static int session_new_callback(SSL *ssl, SSL_SESSION *session)
{
    (void)session;
    
    atomic_fetch_add(&g_session_stats.new_sessions, 1);
    
    SSL_CTX *ctx = SSL_get_SSL_CTX(ssl);
    if (ctx)
    {
        long cache_size = SSL_CTX_sess_get_cache_size(ctx);
        long current = SSL_CTX_sess_number(ctx);
        
        if (current >= cache_size)
        {
            atomic_fetch_add(&g_session_stats.cache_full, 1);
            log_message(LOG_LEVEL_WARN, "TLS session cache full: %ld/%ld sessions",
                        current, cache_size);
        }
    }
    return 0;
}

/*
 * Called when a session is removed from the cache.
 * Tracks removal count for observability.
 */
static void session_remove_callback(SSL_CTX *ctx, SSL_SESSION *session)
{
    (void)ctx;
    (void)session;
    atomic_fetch_add(&g_session_stats.removed_sessions, 1);
}

/*
 * Logs TLS session statistics for monitoring and debugging.
 * Called every 60 seconds during server operation and on shutdown.
 */
void log_session_stats(SSL_CTX *ctx)
{
    if (!ctx)
        return;
    
    long num = SSL_CTX_sess_number(ctx);
    long connect = SSL_CTX_sess_connect(ctx);
    long cache_hits = SSL_CTX_sess_hits(ctx);
    long cache_misses = SSL_CTX_sess_misses(ctx);
    long cache_timeouts = SSL_CTX_sess_timeouts(ctx);
    
    double hit_rate = (connect > 0) ? 
        (100.0 * cache_hits / connect) : 0.0;
    
    log_message(LOG_LEVEL_INFO,
        "TLS Session Stats: active=%ld total=%ld hits=%ld misses=%ld "
        "timeouts=%ld hit_rate=%.1f%% new=%ld removed=%ld cache_full=%ld",
        num, connect, cache_hits, cache_misses, cache_timeouts,
        hit_rate,
        atomic_load(&g_session_stats.new_sessions),
        atomic_load(&g_session_stats.removed_sessions),
        atomic_load(&g_session_stats.cache_full));
}

void cleanup_ssl_context(SSL_CTX *ctx)
{
    if (ctx)
    {
        log_session_stats(ctx);
        SSL_CTX_free(ctx);
    }
    EVP_cleanup();
}