/**
 * @file debug_monitor.c
 * @brief Debug monitoring to see actual TE samples
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include "sw_clock.h"

int main(void) {
    printf("Debug: Monitoring TE Sample Collection\n");
    printf("========================================\n\n");
    
    // Create SwClock
    SwClock* clock = swclock_create();
    if (!clock) {
        fprintf(stderr, "Failed to create clock\n");
        return 1;
    }
    
    // Enable monitoring
    if (swclock_enable_monitoring(clock, true) != 0) {
        fprintf(stderr, "Failed to enable monitoring\n");
        swclock_destroy(clock);
        return 1;
    }
    
    printf("Monitoring enabled. Collecting samples for 15 seconds...\n\n");
    
    // Wait and check metrics periodically
    for (int i = 0; i < 3; i++) {
        sleep(5);
        
        swclock_metrics_snapshot_t metrics;
        if (swclock_get_metrics(clock, &metrics) == 0) {
            printf("After %d seconds:\n", (i+1)*5);
            printf("  Samples:  %u\n", metrics.sample_count);
            printf("  Mean TE:  %.2f ns\n", metrics.mean_te_ns);
            printf("  Min TE:   %.2f ns\n", metrics.min_te_ns);
            printf("  Max TE:   %.2f ns\n", metrics.max_te_ns);
            printf("  Std Dev:  %.2f ns\n", metrics.std_te_ns);
            printf("\n");
        } else {
            printf("After %d seconds: No metrics yet\n\n", (i+1)*5);
        }
    }
    
    swclock_enable_monitoring(clock, false);
    swclock_destroy(clock);
    
    return 0;
}
