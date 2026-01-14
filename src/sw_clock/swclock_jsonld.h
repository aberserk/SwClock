/**
 * @file swclock_jsonld.h
 * @brief JSON-LD structured logging for SwClock (IEEE Audit Recommendation 10)
 * 
 * Implements SwClock Interchange Format (SIF) v1.0.0:
 * - JSON-LD format with semantic web compatibility
 * - IEEE 1588 and ITU-T standards compliance
 * - Schema versioning (semantic versioning)
 * - Thread-safe buffered I/O
 * - Log rotation and compression
 * - Multi-vendor interoperability
 * 
 * @author SwClock Development Team
 * @date 2026-01-13
 */

#ifndef SWCLOCK_JSONLD_H
#define SWCLOCK_JSONLD_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief SwClock Interchange Format (SIF) version
 */
#define SWCLOCK_SIF_VERSION "1.0.0"

/**
 * @brief Maximum JSON entry size (64KB for complex events)
 */
#define SWCLOCK_JSONLD_MAX_SIZE 65536

/**
 * @brief Write buffer size (1MB for batching)
 */
#define SWCLOCK_JSONLD_BUFFER_SIZE (1024 * 1024)

/**
 * @brief JSON-LD logger context
 */
typedef struct swclock_jsonld_logger swclock_jsonld_logger_t;

/**
 * @brief Log rotation configuration
 */
typedef struct {
    bool enabled;
    size_t max_size_mb;        // Rotate when file exceeds this size
    int max_age_hours;          // Rotate after this time (0 = disabled)
    int max_files;              // Keep this many rotated logs (0 = unlimited)
    bool compress;              // gzip compress rotated logs
} swclock_log_rotation_t;

/**
 * @brief System context for log entries
 */
typedef struct {
    char hostname[256];
    char os[64];
    char kernel[128];
    char arch[32];
    char swclock_version[16];
} swclock_system_context_t;

/**
 * @brief Initialize JSON-LD logger
 * 
 * @param log_path Path to log file (JSONL format)
 * @param rotation Log rotation configuration (NULL for no rotation)
 * @param system_ctx System context (NULL to auto-detect)
 * @return Logger handle or NULL on error
 */
swclock_jsonld_logger_t* swclock_jsonld_init(
    const char* log_path,
    const swclock_log_rotation_t* rotation,
    const swclock_system_context_t* system_ctx
);

/**
 * @brief Close and cleanup JSON-LD logger
 * @param logger Logger handle
 */
void swclock_jsonld_close(swclock_jsonld_logger_t* logger);

/**
 * @brief Flush buffered entries to disk
 * @param logger Logger handle
 * @return 0 on success, -1 on error
 */
int swclock_jsonld_flush(swclock_jsonld_logger_t* logger);

/**
 * @brief Log servo state update
 * 
 * Maps to @type: "ServoStateUpdate"
 */
int swclock_jsonld_log_servo(
    swclock_jsonld_logger_t* logger,
    uint64_t timestamp_mono_ns,
    double freq_ppm,
    int64_t phase_error_ns,
    int64_t time_error_ns,
    double pi_freq_ppm,
    double pi_int_error_s,
    bool servo_enabled
);

/**
 * @brief Log time adjustment event
 * 
 * Maps to @type: "TimeAdjustment"
 */
int swclock_jsonld_log_adjustment(
    swclock_jsonld_logger_t* logger,
    uint64_t timestamp_mono_ns,
    const char* adjustment_type,  // "phase_step", "frequency_adjust", "slew"
    double value,
    int64_t before_offset_ns,
    int64_t after_offset_ns
);

/**
 * @brief Log PI controller update
 * 
 * Maps to @type: "PIUpdate"
 */
int swclock_jsonld_log_pi_update(
    swclock_jsonld_logger_t* logger,
    uint64_t timestamp_mono_ns,
    double kp,
    double ki,
    double error_s,
    double output_ppm,
    double integral_state
);

/**
 * @brief Log threshold alert
 * 
 * Maps to @type: "ThresholdAlert"
 */
int swclock_jsonld_log_alert(
    swclock_jsonld_logger_t* logger,
    uint64_t timestamp_mono_ns,
    const char* metric_name,
    double value_ns,
    double threshold_ns,
    const char* severity,  // "warning" or "critical"
    const char* standard   // e.g., "ITU-T G.8260 Class C"
);

/**
 * @brief Log system event
 * 
 * Maps to @type: "SystemEvent"
 */
int swclock_jsonld_log_system(
    swclock_jsonld_logger_t* logger,
    uint64_t timestamp_mono_ns,
    const char* event_type,
    const char* details_json
);

/**
 * @brief Log metrics snapshot
 * 
 * Maps to @type: "MetricsSnapshot"
 * Includes TE statistics, MTIE, TDEV, and compliance status
 */
int swclock_jsonld_log_metrics(
    swclock_jsonld_logger_t* logger,
    uint64_t timestamp_mono_ns,
    uint32_t sample_count,
    double window_duration_s,
    double mean_te_ns,
    double std_te_ns,
    double min_te_ns,
    double max_te_ns,
    double p95_te_ns,
    double p99_te_ns,
    double mtie_1s_ns,
    double mtie_10s_ns,
    double mtie_30s_ns,
    double mtie_60s_ns,
    double tdev_0_1s_ns,
    double tdev_1s_ns,
    double tdev_10s_ns,
    bool itu_g8260_pass
);

/**
 * @brief Log test result with validation data
 * 
 * Maps to @type: "TestResult"
 */
int swclock_jsonld_log_test(
    swclock_jsonld_logger_t* logger,
    uint64_t timestamp_mono_ns,
    const char* test_name,
    const char* status,  // "PASSED", "FAILED", "SKIPPED", "TIMEOUT"
    double duration_ms,
    const char* csv_file,
    const char* metrics_json,
    bool verified,
    double max_error_percent
);

/**
 * @brief Rotate log file manually
 * 
 * Renames current log to .1, compresses if configured
 * @param logger Logger handle
 * @return 0 on success, -1 on error
 */
int swclock_jsonld_rotate(swclock_jsonld_logger_t* logger);

/**
 * @brief Get current log file size
 * @param logger Logger handle
 * @return Size in bytes
 */
size_t swclock_jsonld_get_size(swclock_jsonld_logger_t* logger);

/**
 * @brief Get number of entries written
 * @param logger Logger handle
 * @return Entry count
 */
uint64_t swclock_jsonld_get_count(swclock_jsonld_logger_t* logger);

#ifdef __cplusplus
}
#endif

#endif /* SWCLOCK_JSONLD_H */
