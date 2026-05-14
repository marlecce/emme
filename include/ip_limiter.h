#ifndef IP_LIMITER_H
#define IP_LIMITER_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#define IP_LIMITER_SHARDS 256
#define IP_LIMITER_MAX_ENTRIES_PER_SHARD 256
#define IP_LIMITER_MAX_ENTRIES (IP_LIMITER_SHARDS * IP_LIMITER_MAX_ENTRIES_PER_SHARD)
#define IP_LIMITER_MAX_CONNECTIONS_PER_IP 10000

typedef struct {
    uint32_t ip;
    _Atomic uint32_t count;
    uint64_t last_seen;
    bool marked_for_deletion;
} ip_entry_t;

typedef struct {
    pthread_mutex_t lock;
    ip_entry_t entries[IP_LIMITER_MAX_ENTRIES_PER_SHARD];
    size_t count;
    char padding[64 - (sizeof(pthread_mutex_t) + sizeof(ip_entry_t) * IP_LIMITER_MAX_ENTRIES_PER_SHARD + sizeof(size_t)) % 64];
} ip_shard_t;

typedef struct {
    ip_shard_t shards[IP_LIMITER_SHARDS];
    uint32_t default_limit;
    _Atomic uint64_t total_entries;
    _Atomic uint64_t rejections_total;
    uint64_t last_compaction;
} ip_limiter_t;

int ip_limiter_init(ip_limiter_t *limiter, uint32_t default_limit);
void ip_limiter_destroy(ip_limiter_t *limiter);

typedef enum {
    IP_LIMITER_OK,
    IP_LIMITER_REJECTED,
    IP_LIMITER_ERROR
} ip_limiter_result_t;

ip_limiter_result_t ip_limiter_check_and_increment(ip_limiter_t *limiter, uint32_t ip, uint32_t *current_count);
void ip_limiter_decrement(ip_limiter_t *limiter, uint32_t ip);
void ip_limiter_compact(ip_limiter_t *limiter);

uint64_t ip_limiter_get_total_entries(ip_limiter_t *limiter);
uint64_t ip_limiter_get_rejections_total(ip_limiter_t *limiter);

#endif /* IP_LIMITER_H */
