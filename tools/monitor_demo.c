/**
 * @file monitor_demo.c
 * @brief Demo program for SwClock Real-Time Monitoring (Recommendation 7)
 * 
 * This program demonstrates the real-time monitoring API for continuous
 * MTIE/TDEV computation. It:
 * 1. Creates a SwClock instance
 * 2. Enables real-time monitoring
 * 3. Runs the servo for a period
 * 4. Periodically queries and displays metrics
 * 5. Demonstrates threshold alerting
 * 
 * Usage:
 *   SWCLOCK_MONITOR=1 ./monitor_demo [duration_seconds]
 * 
 * IEEE Audit Recommendation 7: Real-Time Monitoring Mode
 * Priority 2 - Est: 80 hours
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include "sw_clock.h"

static volatile int keep_running = 1;

void signal_handler(int signum) {
    (void)signum;
    keep_running = 0;
}

void threshold_callback(const char* metric, double value, double threshold) {
    printf("\n⚠️  THRESHOLD ALERT ⚠️\n");
    printf("  %s: %.2f ns (threshold: %.0f ns)\n", metric, value, threshold);
    printf("\n");
}

void print_metrics(const swclock_metrics_snapshot_t* metrics) {
    printf("\n┌─────────────────────────────────────────────────────────────┐\n");
    printf("│ Real-Time Monitoring Metrics                                │\n");
    printf("├─────────────────────────────────────────────────────────────┤\n");
    
    // Time Error Statistics
    printf("│ Time Error Statistics                                       │\n");
    printf("│   Mean:        %10.2f ns                              │\n", metrics->mean_te_ns);
    printf("│   Std Dev:     %10.2f ns                              │\n", metrics->std_te_ns);
    printf("│   Min:         %10.2f ns                              │\n", metrics->min_te_ns);
    printf("│   Max:         %10.2f ns                              │\n", metrics->max_te_ns);
    printf("│   P95:         %10.2f ns                              │\n", metrics->p95_te_ns);
    printf("│   P99:         %10.2f ns                              │\n", metrics->p99_te_ns);
    printf("├─────────────────────────────────────────────────────────────┤\n");
    
    // MTIE (Maximum Time Interval Error)
    printf("│ MTIE (Maximum Time Interval Error)                         │\n");
    printf("│   τ = 1s:      %10.2f ns                              │\n", metrics->mtie_1s_ns);
    printf("│   τ = 10s:     %10.2f ns                              │\n", metrics->mtie_10s_ns);
    printf("│   τ = 30s:     %10.2f ns                              │\n", metrics->mtie_30s_ns);
    printf("│   τ = 60s:     %10.2f ns                              │\n", metrics->mtie_60s_ns);
    printf("├─────────────────────────────────────────────────────────────┤\n");
    
    // TDEV (Time Deviation)
    printf("│ TDEV (Time Deviation)                                      │\n");
    printf("│   τ = 0.1s:    %10.2f ns                              │\n", metrics->tdev_0_1s_ns);
    printf("│   τ = 1.0s:    %10.2f ns                              │\n", metrics->tdev_1s_ns);
    printf("│   τ = 10.0s:   %10.2f ns                              │\n", metrics->tdev_10s_ns);
    printf("├─────────────────────────────────────────────────────────────┤\n");
    
    // Sample information
    printf("│ Sample Information                                          │\n");
    printf("│   Count:       %10u samples                           │\n", metrics->sample_count);
    printf("│   Window:      %10.2f seconds                         │\n", metrics->window_duration_s);
    printf("│   Timestamp:   %10llu ns                               │\n", 
           (unsigned long long)metrics->timestamp_ns);
    printf("└─────────────────────────────────────────────────────────────┘\n");
}

void print_usage(const char* program) {
    printf("Usage: %s [options]\n", program);
    printf("\nOptions:\n");
    printf("  -d DURATION    Run duration in seconds (default: 120)\n");
    printf("  -i INTERVAL    Metrics display interval in seconds (default: 15)\n");
    printf("  -h             Show this help message\n");
    printf("\nEnvironment Variables:\n");
    printf("  SWCLOCK_MONITOR=1     Enable monitoring (required)\n");
    printf("\nExample:\n");
    printf("  SWCLOCK_MONITOR=1 %s -d 60 -i 10\n", program);
    printf("\n");
}

int main(int argc, char* argv[]) {
    int duration_sec = 120;
    int interval_sec = 15;
    int opt;
    
    // Parse command line arguments
    while ((opt = getopt(argc, argv, "d:i:h")) != -1) {
        switch (opt) {
            case 'd':
                duration_sec = atoi(optarg);
                if (duration_sec < 10) {
                    fprintf(stderr, "Error: Duration must be at least 10 seconds\n");
                    return 1;
                }
                break;
            case 'i':
                interval_sec = atoi(optarg);
                if (interval_sec < 5) {
                    fprintf(stderr, "Error: Interval must be at least 5 seconds\n");
                    return 1;
                }
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    // Check if monitoring is enabled via environment variable
    const char* monitor_env = getenv("SWCLOCK_MONITOR");
    if (!monitor_env || atoi(monitor_env) != 1) {
        fprintf(stderr, "Error: Real-time monitoring not enabled\n");
        fprintf(stderr, "Set SWCLOCK_MONITOR=1 to enable monitoring\n");
        return 1;
    }
    
    printf("═════════════════════════════════════════════════════════════\n");
    printf(" SwClock Real-Time Monitoring Demo\n");
    printf(" IEEE Audit Recommendation 7: Real-Time Monitoring Mode\n");
    printf("═════════════════════════════════════════════════════════════\n");
    printf("\n");
    printf("Configuration:\n");
    printf("  Duration:    %d seconds\n", duration_sec);
    printf("  Interval:    %d seconds\n", interval_sec);
    printf("  Buffer Size: %d samples (%.1f seconds @ 100 Hz)\n", 
           SWCLOCK_MONITOR_BUFFER_SIZE, 
           SWCLOCK_MONITOR_BUFFER_SIZE / 100.0);
    printf("\n");
    
    // Install signal handler for graceful shutdown
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Create SwClock instance
    printf("Creating SwClock instance...\n");
    SwClock* clock = swclock_create();
    if (!clock) {
        fprintf(stderr, "Error: Failed to create SwClock\n");
        return 1;
    }
    
    // Enable real-time monitoring
    printf("Enabling real-time monitoring...\n");
    if (swclock_enable_monitoring(clock, true) != 0) {
        fprintf(stderr, "Error: Failed to enable monitoring\n");
        swclock_destroy(clock);
        return 1;
    }
    
    // Configure thresholds (ITU-T G.8260 Class C defaults)
    printf("Configuring alert thresholds (ITU-T G.8260 Class C):\n");
    printf("  MTIE(1s):  100 µs\n");
    printf("  MTIE(10s): 200 µs\n");
    printf("  TDEV(1s):  40 µs\n");
    printf("  Max TE:    300 µs\n");
    printf("\n");
    
    swclock_threshold_config_t thresholds = {
        .enabled = true,
        .mtie_1s_threshold_ns = 100000,    // 100 µs
        .mtie_10s_threshold_ns = 200000,   // 200 µs
        .tdev_1s_threshold_ns = 40000,     // 40 µs
        .max_te_threshold_ns = 300000,     // 300 µs
        .alert_callback = threshold_callback
    };
    swclock_set_thresholds(clock, &thresholds);
    
    printf("\nServo running automatically. Collecting samples...\n");
    printf("(Press Ctrl+C to stop)\n");
    
    // Monitor metrics periodically
    time_t start_time = time(NULL);
    time_t last_display = start_time;
    int iterations = 0;
    
    while (keep_running && (time(NULL) - start_time) < duration_sec) {
        sleep(1);
        
        time_t now = time(NULL);
        if (now - last_display >= interval_sec) {
            iterations++;
            
            // Get current metrics
            swclock_metrics_snapshot_t metrics;
            int ret = swclock_get_metrics(clock, &metrics);
            if (ret == 0) {
                printf("\n[Iteration %d - Elapsed: %ld seconds]\n", 
                       iterations, now - start_time);
                print_metrics(&metrics);
            } else {
                printf("\nWarning: Failed to get metrics (insufficient samples?)\n");
            }
            
            last_display = now;
        }
    }
    
    // Final metrics
    printf("\n");
    printf("═════════════════════════════════════════════════════════════\n");
    printf(" Final Metrics\n");
    printf("═════════════════════════════════════════════════════════════\n");
    
    swclock_metrics_snapshot_t final_metrics;
    if (swclock_get_metrics(clock, &final_metrics) == 0) {
        print_metrics(&final_metrics);
    }
    
    // Cleanup
    printf("\n");
    printf("Disabling monitoring...\n");
    swclock_enable_monitoring(clock, false);
    
    printf("Destroying SwClock instance...\n");
    swclock_destroy(clock);
    
    printf("\n");
    printf("═════════════════════════════════════════════════════════════\n");
    printf(" Demo Complete\n");
    printf("═════════════════════════════════════════════════════════════\n");
    
    return 0;
}
