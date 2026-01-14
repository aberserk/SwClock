/**
 * @file test_jsonld_logger.c
 * @brief Test SwClock JSON-LD logger implementation
 */

#include "../src/sw_clock/swclock_jsonld.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static uint64_t get_timestamp_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

int main(void)
{
    printf("SwClock JSON-LD Logger Test\n");
    printf("============================\n\n");

    /* Create test log directory */
    system("mkdir -p logs/test");

    /* Initialize logger with rotation */
    swclock_log_rotation_t rotation = {
        .enabled = true,
        .max_size_mb = 1,      /* Small for testing */
        .max_age_hours = 0,     /* Age-based disabled */
        .max_files = 3,
        .compress = false       /* Disable for easy inspection */
    };

    swclock_jsonld_logger_t* logger = swclock_jsonld_init(
        "logs/test/swclock_test.jsonl",
        &rotation,
        NULL  /* Auto-detect system context */
    );

    if (!logger) {
        fprintf(stderr, "Failed to initialize logger\n");
        return 1;
    }

    printf("✓ Logger initialized\n");

    uint64_t ts = get_timestamp_ns();

    /* Test 1: System event (startup) */
    printf("Testing SystemEvent... ");
    if (swclock_jsonld_log_system(
            logger, ts, "swclock_start", 
            "{\"version\":\"v2.0.0\",\"servo\":{\"kp\":200,\"ki\":8}}") == 0) {
        printf("✓\n");
    } else {
        printf("✗\n");
    }

    /* Test 2: Servo state update */
    printf("Testing ServoStateUpdate... ");
    if (swclock_jsonld_log_servo(
            logger, ts + 1000000, 
            0.0234,      /* freq_ppm */
            -125,        /* phase_error_ns */
            3420,        /* time_error_ns */
            0.0234,      /* pi_freq_ppm */
            0.00000342,  /* pi_int_error_s */
            true         /* servo_enabled */
        ) == 0) {
        printf("✓\n");
    } else {
        printf("✗\n");
    }

    /* Test 3: Time adjustment */
    printf("Testing TimeAdjustment... ");
    if (swclock_jsonld_log_adjustment(
            logger, ts + 2000000,
            "frequency_adjust",
            0.025,       /* value */
            3500,        /* before_offset_ns */
            120          /* after_offset_ns */
        ) == 0) {
        printf("✓\n");
    } else {
        printf("✗\n");
    }

    /* Test 4: PI update */
    printf("Testing PIUpdate... ");
    if (swclock_jsonld_log_pi_update(
            logger, ts + 3000000,
            200.0,       /* kp */
            8.0,         /* ki */
            0.00000125,  /* error_s */
            0.025,       /* output_ppm */
            0.0000034    /* integral_state */
        ) == 0) {
        printf("✓\n");
    } else {
        printf("✗\n");
    }

    /* Test 5: Threshold alert */
    printf("Testing ThresholdAlert... ");
    if (swclock_jsonld_log_alert(
            logger, ts + 4000000,
            "mtie_1s",
            125000.0,    /* value */
            100000.0,    /* thres_ns */
            100000.0,    /* threshold_ns */
            "warning",
            "ITU-T G.8260
        printf("✓\n");
    } else {
        printf("✗\n");
    }

    /* Test 6: Metrics snapshot */
    printf("Testing MetricsSnapshot... ");
    if (swclock_jsonld_log_metrics(
            logger, ts + 5000000,
            822,         /* sample_count */
            9.99,        /* window_duration_s */
            4056.25,     /* mean_te_ns */
            2521.08,     /* std_te_ns */
            -1042.0,     /* min_te_ns */
            8625.0,      /* max_te_ns */
            7917.0,      /* p95_te_ns */
            8375.0,      /* p99_te_ns */
            5625.0,      /* mtie_1s_ns */
            0.0,         /* mtie_10s_ns */
            0.0,         /* mtie_30s_ns */
            0.0,         /* mtie_60s_ns */
            375.48,      /* tdev_0_1s_ns */
            409.28,      /* tdev_1s_ns */
            0.0,         /* tdev_10s_ns */
            true         /* itu_g8260_pass */
        ) == 0) {
        printf("✓\n");
    } else {
        printf("✗\n");
    }

    /* Test 7: Test result */
    printf("Testing TestResult... ");
    if (swclock_jsonld_log_test(
            logger, ts + 6000000,
            "SmallAdjustment",
            "PASSED",
            45000,       /* duration_ms */
            "logs.0,     /* duration_ms */
            "logs/20260113-163249-SmallAdjustment.csv",
            "{\"mean_te_ns\":4056.25,\"mtie_1s_ns\":5625.0,\"tdev_1s_ns\":409.28}",
            true,        /* verified */
            0.34         /* 
        printf("✓\n");
    } else {
        printf("✗\n");
    }

    /* Flush and get stats */
    swclock_jsonld_flush(logger);
    
    printf("\nLogger Statistics:\n");
    printf("  Entries written: %lu\n", (unsigned long)swclock_jsonld_get_count(logger));
    printf("  File size:       %lu bytes\n", (unsigned long)swclock_jsonld_get_size(logger));

    /* Close logger */
    swclock_jsonld_close(logger);
    printf("\n✓ Logger closed\n");

    /* Validate the output */
    printf("\nValidating JSON-LD output...\n");
    int ret = system("python3 tools/sif_validate.py logs/test/swclock_test.jsonl");
    
    if (ret == 0) {
        printf("\n✓ All tests passed!\n");
        printf("\nInspect the log file:\n");
        printf("  cat logs/test/swclock_test.jsonl | jq\n");
    } else {
        printf("\n✗ Validation failed\n");
    }

    return ret;
}
