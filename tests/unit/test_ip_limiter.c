#include <criterion/criterion.h>
#include <criterion/logging.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <stdatomic.h>
#include "ip_limiter.h"

Test(ip_limiter, init_destroy)
{
    ip_limiter_t limiter;
    
    int ret = ip_limiter_init(&limiter, 10);
    cr_assert_eq(ret, 0, "IP limiter should initialize successfully");
    
    cr_assert_eq(limiter.default_limit, 10, "Default limit should be set");
    cr_assert_eq(ip_limiter_get_total_entries(&limiter), 0, "Initial entries should be 0");
    cr_assert_eq(ip_limiter_get_rejections_total(&limiter), 0, "Initial rejections should be 0");
    
    ip_limiter_destroy(&limiter);
}

Test(ip_limiter, init_invalid_params)
{
    ip_limiter_t limiter;
    
    int ret = ip_limiter_init(NULL, 10);
    cr_assert_eq(ret, -1, "NULL limiter should fail");
    
    ret = ip_limiter_init(&limiter, 0);
    cr_assert_eq(ret, -1, "Zero limit should fail");
    
    ret = ip_limiter_init(&limiter, 20000);
    cr_assert_eq(ret, -1, "Limit > 10000 should fail");
}

Test(ip_limiter, check_and_increment_basic)
{
    ip_limiter_t limiter;
    uint32_t ip = 0x01020304;
    uint32_t current_count;
    
    ip_limiter_init(&limiter, 10);
    
    ip_limiter_result_t ret = ip_limiter_check_and_increment(&limiter, ip, &current_count);
    cr_assert_eq(ret, IP_LIMITER_OK, "First connection should be allowed");
    cr_assert_eq(current_count, 1, "Count should be 1");
    
    ret = ip_limiter_check_and_increment(&limiter, ip, &current_count);
    cr_assert_eq(ret, IP_LIMITER_OK, "Second connection should be allowed");
    cr_assert_eq(current_count, 2, "Count should be 2");
    
    ip_limiter_destroy(&limiter);
}

Test(ip_limiter, decrement_basic)
{
    ip_limiter_t limiter;
    uint32_t ip = 0x01020304;
    uint32_t current_count;
    
    ip_limiter_init(&limiter, 10);
    
    ip_limiter_check_and_increment(&limiter, ip, &current_count);
    ip_limiter_check_and_increment(&limiter, ip, &current_count);
    cr_assert_eq(current_count, 2, "Count should be 2");
    
    ip_limiter_decrement(&limiter, ip);
    ip_limiter_check_and_increment(&limiter, ip, &current_count);
    cr_assert_eq(current_count, 2, "Count should still be 2 after decrement and increment");
    
    ip_limiter_destroy(&limiter);
}

Test(ip_limiter, reject_over_limit)
{
    ip_limiter_t limiter;
    uint32_t ip = 0x01020304;
    uint32_t current_count;
    
    ip_limiter_init(&limiter, 3);
    
    for (int i = 0; i < 3; i++) {
        ip_limiter_result_t ret = ip_limiter_check_and_increment(&limiter, ip, &current_count);
        cr_assert_eq(ret, IP_LIMITER_OK, "Connection %d should be allowed", i + 1);
    }
    
    ip_limiter_result_t ret = ip_limiter_check_and_increment(&limiter, ip, &current_count);
    cr_assert_eq(ret, IP_LIMITER_REJECTED, "Fourth connection should be rejected");
    cr_assert_eq(current_count, 3, "Count should still be 3");
    cr_assert_eq(ip_limiter_get_rejections_total(&limiter), 1, "Rejections should be 1");
    
    ip_limiter_destroy(&limiter);
}

Test(ip_limiter, multiple_ips)
{
    ip_limiter_t limiter;
    uint32_t ip1 = 0x01020304;
    uint32_t ip2 = 0x05060708;
    uint32_t ip3 = 0x090A0B0C;
    uint32_t current_count;
    
    ip_limiter_init(&limiter, 5);
    
    ip_limiter_check_and_increment(&limiter, ip1, &current_count);
    ip_limiter_check_and_increment(&limiter, ip2, &current_count);
    ip_limiter_check_and_increment(&limiter, ip3, &current_count);
    
    cr_assert_eq(ip_limiter_get_total_entries(&limiter), 3, "Should track 3 unique IPs");
    
    ip_limiter_destroy(&limiter);
}

Test(ip_limiter, hash_distribution)
{
    ip_limiter_t limiter;
    uint32_t current_count;
    
    ip_limiter_init(&limiter, 100);
    
    for (uint32_t i = 0; i < 1000; i++) {
        ip_limiter_check_and_increment(&limiter, i, &current_count);
    }
    
    cr_assert_eq(ip_limiter_get_total_entries(&limiter), 1000, "Should track 1000 unique IPs");
    
    ip_limiter_destroy(&limiter);
}

Test(ip_limiter, reclaim_after_decrement)
{
    ip_limiter_t limiter;
    uint32_t ip = 0x01020304;
    uint32_t current_count;
    
    ip_limiter_init(&limiter, 2);
    
    ip_limiter_check_and_increment(&limiter, ip, &current_count);
    ip_limiter_check_and_increment(&limiter, ip, &current_count);
    cr_assert_eq(current_count, 2, "Count should be 2");
    
    ip_limiter_decrement(&limiter, ip);
    ip_limiter_decrement(&limiter, ip);
    
    ip_limiter_result_t ret = ip_limiter_check_and_increment(&limiter, ip, &current_count);
    cr_assert_eq(ret, IP_LIMITER_OK, "Should be able to reconnect after decrementing to 0");
    cr_assert_eq(current_count, 1, "Count should be 1");
    
    ip_limiter_destroy(&limiter);
}

Test(ip_limiter, concurrent_access)
{
    ip_limiter_t limiter;
    uint32_t ip = 0x01020304;
    _Atomic int errors = 0;
    _Atomic int successes = 0;
    
    ip_limiter_init(&limiter, 100);
    
    void *thread_func(void *arg) {
        (void)arg;
        uint32_t count;
        for (int i = 0; i < 100; i++) {
            ip_limiter_result_t ret = ip_limiter_check_and_increment(&limiter, ip, &count);
            if (ret == IP_LIMITER_OK) {
                atomic_fetch_add(&successes, 1);
            } else if (ret != IP_LIMITER_REJECTED) {
                atomic_fetch_add(&errors, 1);
            }
            usleep(100);
        }
        return NULL;
    }
    
    pthread_t threads[10];
    for (int i = 0; i < 10; i++) {
        pthread_create(&threads[i], NULL, thread_func, NULL);
    }
    
    for (int i = 0; i < 10; i++) {
        pthread_join(threads[i], NULL);
    }
    
    cr_assert_eq(atomic_load(&errors), 0, "No errors should occur");
    cr_assert_leq(atomic_load(&successes), 100, "Successes should not exceed limit");
    
    ip_limiter_destroy(&limiter);
}

Test(ip_limiter, null_safety)
{
    uint32_t ip = 0x01020304;
    uint32_t current_count;
    
    ip_limiter_result_t ret = ip_limiter_check_and_increment(NULL, ip, &current_count);
    cr_assert_eq(ret, IP_LIMITER_ERROR, "NULL limiter should return error");
    
    ip_limiter_decrement(NULL, ip);
    
    cr_assert_eq(ip_limiter_get_total_entries(NULL), 0, "NULL limiter entries should be 0");
    cr_assert_eq(ip_limiter_get_rejections_total(NULL), 0, "NULL limiter rejections should be 0");
}

Test(ip_limiter, compact_empty)
{
    ip_limiter_t limiter;
    
    ip_limiter_init(&limiter, 10);
    
    ip_limiter_compact(&limiter);
    
    cr_assert_eq(ip_limiter_get_total_entries(&limiter), 0, "Should still have 0 entries");
    
    ip_limiter_destroy(&limiter);
}

Test(ip_limiter, stress_single_ip)
{
    ip_limiter_t limiter;
    uint32_t ip = 0x01020304;
    uint32_t current_count;
    
    ip_limiter_init(&limiter, 1000);
    
    for (int i = 0; i < 1000; i++) {
        ip_limiter_result_t ret = ip_limiter_check_and_increment(&limiter, ip, &current_count);
        cr_assert_eq(ret, IP_LIMITER_OK, "Connection %d should be allowed", i + 1);
        cr_assert_eq(current_count, (uint32_t)(i + 1), "Count should match");
    }
    
    ip_limiter_result_t ret = ip_limiter_check_and_increment(&limiter, ip, &current_count);
    cr_assert_eq(ret, IP_LIMITER_REJECTED, "Connection 1001 should be rejected");
    
    ip_limiter_destroy(&limiter);
}
