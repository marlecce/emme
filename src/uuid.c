#include "uuid.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

static int random_bytes_initialized = 0;
static unsigned int random_state[624];
static int random_index = 625;

static void init_random_state(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    
    random_state[0] = (unsigned int)tv.tv_sec ^ (unsigned int)tv.tv_usec ^ (unsigned int)getpid();
    for (int i = 1; i < 624; i++) {
        random_state[i] = 1812433253UL * (random_state[i-1] ^ (random_state[i-1] >> 30)) + i;
    }
    random_index = 624;
    random_bytes_initialized = 1;
}

static unsigned int generate_random(void)
{
    if (random_index >= 624) {
        for (int i = 0; i < 227; i++) {
            unsigned int y = (random_state[i] & 0x80000000UL) + (random_state[i+1] & 0x7fffffffUL);
            random_state[i] = random_state[i+397] ^ (y >> 1);
            if (y & 1) random_state[i] ^= 0x9908b0dfUL;
        }
        for (int i = 227; i < 623; i++) {
            unsigned int y = (random_state[i] & 0x80000000UL) + (random_state[i+1] & 0x7fffffffUL);
            random_state[i] = random_state[i-227] ^ (y >> 1);
            if (y & 1) random_state[i] ^= 0x9908b0dfUL;
        }
        unsigned int y = (random_state[623] & 0x80000000UL) + (random_state[0] & 0x7fffffffUL);
        random_state[623] = random_state[396] ^ (y >> 1);
        if (y & 1) random_state[623] ^= 0x9908b0dfUL;
        random_index = 0;
    }
    
    unsigned int y = random_state[random_index++];
    y ^= (y >> 11);
    y ^= (y << 7) & 0x9d2c5680UL;
    y ^= (y << 15) & 0xefc60000UL;
    y ^= (y >> 18);
    return y;
}

static unsigned int get_random_uint(void)
{
    if (!random_bytes_initialized) {
        init_random_state();
    }
    return generate_random();
}

void generate_uuid(char *uuid_out)
{
    unsigned int r1 = get_random_uint();
    unsigned int r2 = get_random_uint();
    unsigned int r3 = get_random_uint();
    unsigned int r4 = get_random_uint();
    
    r2 = (r2 & 0xffff0fff) | 0x00004000;
    r3 = (r3 & 0x0fff) | 0x8000;
    
    sprintf(uuid_out, "%08x-%04x-%04x-%04x-%04x%08x",
            r1,
            r2 >> 16,
            r2 & 0xffff,
            r3,
            r4 >> 16,
            r4 & 0xffff);
}
