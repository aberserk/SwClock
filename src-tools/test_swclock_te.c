/**
 * @file test_swclock_te.c
 * @brief Test if swclock_gettime returns correct values
 */

#include <stdio.h>
#include <time.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include "sw_clock.h"

int main(void) {
    printf("Testing swclock_gettime behavior\n");
    printf("==================================\n\n");
    
    SwClock* clock = swclock_create();
    if (!clock) {
        fprintf(stderr, "Failed to create clock\n");
        return 1;
    }
    
    sleep(2);  // Let servo stabilize
    
    for (int i = 0; i < 3; i++) {
        struct timespec sys_rt, sw_rt, mono_raw;
        
        clock_gettime(CLOCK_REALTIME, &sys_rt);
        clock_gettime(CLOCK_MONOTONIC_RAW, &mono_raw);
        int ret = swclock_gettime(clock, CLOCK_REALTIME, &sw_rt);
        
        if (ret != 0) {
            fprintf(stderr, "swclock_gettime failed: %d\n", ret);
            continue;
        }
        
        int64_t sys_rt_ns = (int64_t)sys_rt.tv_sec * 1000000000LL + sys_rt.tv_nsec;
        int64_t sw_rt_ns = (int64_t)sw_rt.tv_sec * 1000000000LL + sw_rt.tv_nsec;
        int64_t mono_ns = (int64_t)mono_raw.tv_sec * 1000000000LL + mono_raw.tv_nsec;
        int64_t te_ns = sys_rt_ns - sw_rt_ns;
        
        printf("\nSample %d:\n", i+1);
        printf("  CLOCK_REALTIME:     %ld.%09ld\n", sys_rt.tv_sec, sys_rt.tv_nsec);
        printf("  SwClock REALTIME:   %ld.%09ld\n", sw_rt.tv_sec, sw_rt.tv_nsec);
        printf("  MONOTONIC_RAW:      %ld.%09ld\n", mono_raw.tv_sec, mono_raw.tv_nsec);
        printf("  TE (sys - sw):      %lld ns (%.3f µs)\n", 
               (long long)te_ns, te_ns / 1000.0);
        printf("  Mono timestamp:     %lld ns\n", (long long)mono_ns);
        
        sleep(1);
    }
    
    printf("\n");
    swclock_destroy(clock);
    
    return 0;
}
