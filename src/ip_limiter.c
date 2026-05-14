#include "ip_limiter.h"
#include "log.h"
#include <string.h>
#include <time.h>
#include <stdatomic.h>

#define CACHE_LINE_SIZE 64
#define KNUTH_MULTIPLICATIVE_CONSTANT 2654435761UL
#define COMPACTION_INTERVAL_NS 5000000000ULL  /* 5 seconds */
#define ENTRY_EXPIRY_NS 60000000000ULL        /* 60 seconds */

static uint32_t hash_ip(uint32_t ip)
{
    ip *= KNUTH_MULTIPLICATIVE_CONSTANT;
    ip ^= (ip >> 16);
    ip *= KNUTH_MULTIPLICATIVE_CONSTANT;
    ip ^= (ip >> 16);
    ip *= KNUTH_MULTIPLICATIVE_CONSTANT;
    return ip;
}

static size_t get_shard_index(uint32_t ip)
{
    return hash_ip(ip) & (IP_LIMITER_SHARDS - 1);
}

static uint64_t get_time_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

int ip_limiter_init(ip_limiter_t *limiter, uint32_t default_limit)
{
    if (!limiter || default_limit == 0 || default_limit > IP_LIMITER_MAX_CONNECTIONS_PER_IP) {
        return -1;
    }

    memset(limiter, 0, sizeof(*limiter));
    limiter->default_limit = default_limit;

    for (size_t i = 0; i < IP_LIMITER_SHARDS; i++) {
        if (pthread_mutex_init(&limiter->shards[i].lock, NULL) != 0) {
            for (size_t j = 0; j < i; j++) {
                pthread_mutex_destroy(&limiter->shards[j].lock);
            }
            return -1;
        }
        limiter->shards[i].count = 0;
    }

    limiter->last_compaction = get_time_ns();
    atomic_store(&limiter->total_entries, 0);
    atomic_store(&limiter->rejections_total, 0);

    log_message(LOG_LEVEL_INFO, "IP limiter initialized: %d shards, %d max entries, default limit %d",
                IP_LIMITER_SHARDS, IP_LIMITER_MAX_ENTRIES, default_limit);

    return 0;
}

void ip_limiter_destroy(ip_limiter_t *limiter)
{
    if (!limiter) {
        return;
    }

    for (size_t i = 0; i < IP_LIMITER_SHARDS; i++) {
        pthread_mutex_destroy(&limiter->shards[i].lock);
    }

    log_message(LOG_LEVEL_INFO, "IP limiter destroyed: final rejections %lu",
                atomic_load(&limiter->rejections_total));
}

ip_limiter_result_t ip_limiter_check_and_increment(ip_limiter_t *limiter, uint32_t ip, uint32_t *current_count)
{
    if (!limiter || !current_count) {
        return IP_LIMITER_ERROR;
    }

    size_t shard_idx = get_shard_index(ip);
    ip_shard_t *shard = &limiter->shards[shard_idx];

    pthread_mutex_lock(&shard->lock);

    ip_entry_t *found_entry = NULL;
    ip_entry_t *empty_slot = NULL;

    for (size_t i = 0; i < shard->count; i++) {
        if (shard->entries[i].ip == ip) {
            found_entry = &shard->entries[i];
            break;
        }
        if (shard->entries[i].count == 0 && !empty_slot) {
            empty_slot = &shard->entries[i];
        }
    }

    if (!found_entry && !empty_slot && shard->count < IP_LIMITER_MAX_ENTRIES_PER_SHARD) {
        empty_slot = &shard->entries[shard->count];
        shard->count++;
    }

    if (found_entry) {
        if (found_entry->marked_for_deletion) {
            found_entry->marked_for_deletion = false;
            atomic_store(&found_entry->count, 0);
        }

        uint32_t limit = limiter->default_limit;
        uint32_t current = atomic_load(&found_entry->count);

        if (current >= limit) {
            *current_count = current;
            atomic_fetch_add(&limiter->rejections_total, 1);
            pthread_mutex_unlock(&shard->lock);
            return IP_LIMITER_REJECTED;
        }

        atomic_fetch_add(&found_entry->count, 1);
        found_entry->last_seen = get_time_ns();
        *current_count = current + 1;
        pthread_mutex_unlock(&shard->lock);
        return IP_LIMITER_OK;
    }

    if (empty_slot) {
        empty_slot->ip = ip;
        atomic_store(&empty_slot->count, 1);
        empty_slot->last_seen = get_time_ns();
        empty_slot->marked_for_deletion = false;
        *current_count = 1;
        atomic_fetch_add(&limiter->total_entries, 1);
        pthread_mutex_unlock(&shard->lock);
        return IP_LIMITER_OK;
    }

    ip_entry_t *oldest = NULL;
    uint64_t oldest_time = UINT64_MAX;

    for (size_t i = 0; i < shard->count; i++) {
        if (shard->entries[i].count == 0 && shard->entries[i].last_seen < oldest_time) {
            oldest = &shard->entries[i];
            oldest_time = shard->entries[i].last_seen;
        }
    }

    if (oldest) {
        oldest->ip = ip;
        atomic_store(&oldest->count, 1);
        oldest->last_seen = get_time_ns();
        oldest->marked_for_deletion = false;
        *current_count = 1;
        pthread_mutex_unlock(&shard->lock);
        return IP_LIMITER_OK;
    }

    *current_count = limiter->default_limit;
    atomic_fetch_add(&limiter->rejections_total, 1);
    pthread_mutex_unlock(&shard->lock);
    return IP_LIMITER_REJECTED;
}

void ip_limiter_decrement(ip_limiter_t *limiter, uint32_t ip)
{
    if (!limiter) {
        return;
    }

    size_t shard_idx = get_shard_index(ip);
    ip_shard_t *shard = &limiter->shards[shard_idx];

    pthread_mutex_lock(&shard->lock);

    for (size_t i = 0; i < shard->count; i++) {
        if (shard->entries[i].ip == ip) {
            uint32_t old_count = atomic_fetch_sub(&shard->entries[i].count, 1);
            if (old_count <= 1) {
                shard->entries[i].marked_for_deletion = true;
            }
            break;
        }
    }

    pthread_mutex_unlock(&shard->lock);
}

void ip_limiter_compact(ip_limiter_t *limiter)
{
    if (!limiter) {
        return;
    }

    uint64_t now = get_time_ns();
    uint64_t elapsed_ns = now - limiter->last_compaction;

    if (elapsed_ns < COMPACTION_INTERVAL_NS) {
        return;
    }

    limiter->last_compaction = now;

    size_t total_removed = 0;

    for (size_t i = 0; i < IP_LIMITER_SHARDS; i++) {
        ip_shard_t *shard = &limiter->shards[i];

        pthread_mutex_lock(&shard->lock);

        size_t removed = 0;
        for (size_t j = 0; j < shard->count; j++) {
            if (shard->entries[j].marked_for_deletion && shard->entries[j].count == 0) {
                uint64_t entry_age = now - shard->entries[j].last_seen;
                if (entry_age > ENTRY_EXPIRY_NS) {
                    shard->entries[j].ip = 0;
                    shard->entries[j].last_seen = 0;
                    shard->entries[j].marked_for_deletion = false;
                    removed++;
                }
            }
        }

        if (removed > 0 && removed == shard->count) {
            shard->count = 0;
        }

        total_removed += removed;
        pthread_mutex_unlock(&shard->lock);
    }

    if (total_removed > 0) {
        atomic_fetch_sub(&limiter->total_entries, total_removed);
        log_message(LOG_LEVEL_DEBUG, "IP limiter compacted: removed %zu entries", total_removed);
    }
}

uint64_t ip_limiter_get_total_entries(ip_limiter_t *limiter)
{
    if (!limiter) {
        return 0;
    }
    return atomic_load(&limiter->total_entries);
}

uint64_t ip_limiter_get_rejections_total(ip_limiter_t *limiter)
{
    if (!limiter) {
        return 0;
    }
    return atomic_load(&limiter->rejections_total);
}
