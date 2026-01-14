/**
 * @file test_te_calc.c
 * @brief Debug tool to understand TE calculation
 */

#include <stdio.h>
#include <time.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include "sw_clock.h"

int main(void) {
    printf("Testing Time Error Calculation\n");
    printf("================================\n\n");
    
    // Create SwClock
    SwClock* clock = swclock_create();
    if (!clock) {
        fprintf(stderr, "Failed to create clock\n");
        return 1;
    }
    
    printf("SwClock created. Waiting 2 seconds for servo to stabilize...\n");
    sleep(2);
    
    // Take multiple samples
    for (int i = 0; i < 5; i++) {
        struct timespec sys_rt, sw_rt;
        
        clock_gettime(CLOCK_REALTIME, &sys_rt);
        swclock_gettime(clock, CLOCK_REALTIME, &sw_rt);
        
        int64_t sys_rt_ns = ts_to_ns(&sys_rt);
        int64_t sw_rt_ns = ts_to_ns(&sw_rt);
        int64_t te_ns = sys_rt_ns - sw_rt_ns;
        
        printf("\nSample %d:\n", i+1);
        printf("  System REALTIME:  %ld.%09ld = %lld ns\n", 
               sys_rt.tv_sec, sys_rt.tv_nsec, (long long)sys_rt_ns);
        printf("  SwClock REALTIME: %ld.%09ld = %lld ns\n", 
               sw_rt.tv_sec, sw_rt.tv_nsec, (long long)sw_rt_ns);
        printf("  Time Error:       %lld ns (%.3f µs, %.6f ms)\n", 
               (long long)te_ns, te_ns / 1000.0, te_ns / 1000000.0);
        
        if (i < 4) {
            usleep(500000);  // 0.5 second
        }
    }
    
    printf("\n");
    swclock_destroy(clock);
    
    return 0;
}
