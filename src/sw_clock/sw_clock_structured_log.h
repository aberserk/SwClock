/**
 * @file sw_clock_structured_log.h
 * @brief SwClock Structured Logging API
 * 
 * Provides structured logging infrastructure for performance tests and validation.
 * Supports multiple output formats (JSONL, CSV) with comprehensive metadata.
 * 
 * Part of Priority 1 implementation (Recommendation 1).
 * 
 * @author SwClock Development Team
 * @date 2026-01-13
 */

#ifndef SWCLOCK_STRUCTURED_LOG_H
#define SWCLOCK_STRUCTURED_LOG_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Supported log output formats
 */
typedef enum {
    SWCLOCK_LOG_FORMAT_LEGACY_CSV,  /**< Legacy CSV format (backward compat) */
    SWCLOCK_LOG_FORMAT_JSONL,       /**< JSON Lines format (recommended) */
    SWCLOCK_LOG_FORMAT_MSGPACK,     /**< Binary MessagePack (future) */
    SWCLOCK_LOG_FORMAT_PROTOBUF     /**< Protocol Buffers (future) */
} swclock_log_format_t;

/**
 * @brief SwClock configuration snapshot
 */
typedef struct {
    double   kp_ppm_per_s;     /**< Proportional gain (ppm/s) */
    double   ki_ppm_per_s2;    /**< Integral gain (ppm/s²) */
    double   max_ppm;          /**< Maximum frequency adjustment (ppm) */
    int64_t  poll_ns;          /**< Poll interval (nanoseconds) */
    int64_t  phase_eps_ns;     /**< Phase epsilon threshold (nanoseconds) */
} swclock_config_snapshot_t;

/**
 * @brief Structured logger context
 * 
 * Maintains state for a single test run's structured log output.
 * Supports concurrent logging of samples and metadata.
 */
typedef struct swclock_structured_logger swclock_structured_logger_t;

/**
 * @brief Create a new structured logger
 * 
 * @param test_name Name of the test (used for log filename)
 * @param format Output format (JSONL recommended)
 * @param output_dir Directory for log output (NULL = current dir)
 * @return Logger handle, or NULL on error
 */
swclock_structured_logger_t* swclock_logger_create(
    const char* test_name,
    swclock_log_format_t format,
    const char* output_dir
);

/**
 * @brief Write test configuration metadata
 * 
 * Should be called once after logger creation before writing samples.
 * 
 * @param logger Logger handle
 * @param config SwClock configuration snapshot
 * @return 0 on success, -1 on error
 */
int swclock_logger_write_config(
    swclock_structured_logger_t* logger,
    const swclock_config_snapshot_t* config
);

/**
 * @brief Write custom metadata key-value pair
 * 
 * @param logger Logger handle
 * @param key Metadata key
 * @param value Metadata value (as string)
 * @return 0 on success, -1 on error
 */
int swclock_logger_write_metadata(
    swclock_structured_logger_t* logger,
    const char* key,
    const char* value
);

/**
 * @brief Write a time error sample
 * 
 * @param logger Logger handle
 * @param timestamp_ns Sample timestamp (nanoseconds)
 * @param te_ns Time error (nanoseconds)
 * @return 0 on success, -1 on error
 */
int swclock_logger_write_sample(
    swclock_structured_logger_t* logger,
    uint64_t timestamp_ns,
    int64_t te_ns
);

/**
 * @brief Finalize logger and close output
 * 
 * Writes footer/checksum if applicable and closes file handle.
 * 
 * @param logger Logger handle
 */
void swclock_logger_finalize(swclock_structured_logger_t* logger);

/**
 * @brief Get the output file path
 * 
 * @param logger Logger handle
 * @return File path string (valid until logger is finalized)
 */
const char* swclock_logger_get_path(const swclock_structured_logger_t* logger);

/**
 * @brief Get sample count written
 * 
 * @param logger Logger handle
 * @return Number of samples written
 */
uint64_t swclock_logger_get_sample_count(const swclock_structured_logger_t* logger);

#ifdef __cplusplus
}
#endif

#endif /* SWCLOCK_STRUCTURED_LOG_H */
