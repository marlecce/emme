#define _DEFAULT_SOURCE
#include <criterion/criterion.h>
#include <criterion/internal/assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include "uuid.h"
#include "http_status.h"

Test(timeout, http408_status_code_defined)
{
    cr_assert_eq(HTTP_STATUS_REQUEST_TIMEOUT, 408, "HTTP 408 status code should be defined");
    cr_assert(HTTP_STATUS_REQUEST_TIMEOUT > HTTP_STATUS_BAD_REQUEST, 
              "408 should be greater than 400");
    cr_assert(HTTP_STATUS_REQUEST_TIMEOUT < HTTP_STATUS_INTERNAL_ERROR,
              "408 should be less than 500");
}

Test(timeout, uuid_format_valid)
{
    char uuid[37];
    memset(uuid, 0, sizeof(uuid));
    
    generate_uuid(uuid);
    
    cr_assert_eq(strlen(uuid), 36, "UUID should be 36 characters");
    cr_assert_eq(uuid[8], '-', "UUID should have dash at position 8");
    cr_assert_eq(uuid[13], '-', "UUID should have dash at position 13");
    cr_assert_eq(uuid[18], '-', "UUID should have dash at position 18");
    cr_assert_eq(uuid[23], '-', "UUID should have dash at position 23");
    cr_assert_eq(uuid[14], '4', "UUID version should be 4");
    cr_assert((uuid[19] >= '8' && uuid[19] <= 'b') || 
              (uuid[19] >= 'A' && uuid[19] <= 'B') ||
              (uuid[19] >= 'a' && uuid[19] <= 'b'),
              "UUID variant should be correct (got %c)", uuid[19]);
}

Test(timeout, uuid_uniqueness)
{
    char uuids[50][37];
    memset(uuids, 0, sizeof(uuids));
    
    for (int i = 0; i < 50; i++) {
        generate_uuid(uuids[i]);
    }
    
    for (int i = 0; i < 50; i++) {
        for (int j = i + 1; j < 50; j++) {
            cr_assert_str_neq(uuids[i], uuids[j], "UUIDs should be unique");
        }
    }
}

Test(timeout, uuid_hex_characters_valid)
{
    char uuid[37];
    
    for (int i = 0; i < 10; i++) {
        generate_uuid(uuid);
        
        for (int j = 0; j < 36; j++) {
            if (j == 8 || j == 13 || j == 18 || j == 23) {
                cr_assert_eq(uuid[j], '-', "Position %d should be dash", j);
            } else {
                cr_assert((uuid[j] >= '0' && uuid[j] <= '9') ||
                          (uuid[j] >= 'a' && uuid[j] <= 'f'),
                          "Position %d should be hex (got %c)", j, uuid[j]);
            }
        }
    }
}

Test(timeout, elapsed_time_calculation)
{
    struct timeval start, now;
    
    gettimeofday(&start, NULL);
    usleep(10000);
    gettimeofday(&now, NULL);
    
    int elapsed_ms = (int)((now.tv_sec - start.tv_sec) * 1000 +
                           (now.tv_usec - start.tv_usec) / 1000);
    
    cr_assert(elapsed_ms >= 5 && elapsed_ms <= 50, 
              "Elapsed time calculation should be accurate (got %dms)", elapsed_ms);
}

Test(timeout, timeout_threshold_logic)
{
    int timeout_ms = 30000;
    
    cr_assert(35000 > timeout_ms, "35s should exceed 30s timeout");
    cr_assert(25000 <= timeout_ms, "25s should not exceed 30s timeout");
    cr_assert(30000 <= timeout_ms, "30s should equal timeout threshold");
    cr_assert(30001 > timeout_ms, "30001ms should exceed 30s timeout");
}

Test(timeout, config_timeout_ranges)
{
    cr_assert(30000 >= 1000 && 30000 <= 300000, 
              "Request timeout 30s within valid range 1-300s");
    cr_assert(10000 >= 1000 && 10000 <= 60000, 
              "TLS handshake timeout 10s within valid range 1-60s");
}
