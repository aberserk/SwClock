/**
 * @file sw_clock_commercial_log.h
 * @brief Commercial-Grade Logging Configuration for SwClock
 * 
 * Production-ready logging with:
 * - Always-on structured logging (no environment variables required)
 * - Automatic log integrity protection (SHA-256)
 * - Comprehensive metadata capture
 * - Audit-compliant traceability
 * - Performance-optimized buffering
 * 
 * Designed for commercial deployment and regulatory compliance.
 * 
 * @author SwClock Development Team
 * @date 2026-02-10
 */

#ifndef SWCLOCK_COMMERCIAL_LOG_H
#define SWCLOCK_COMMERCIAL_LOG_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Commercial logging configuration
 * 
 * All options enabled by default for production deployment.
 * Can be disabled for specific use cases via API.
 */
typedef struct {
    // Core logging features (always enabled in production)
    bool binary_event_log;         /**< Lock-free binary event logging */
    bool jsonld_structured_log;    /**< JSON-LD interchange format */
    bool servo_state_log;          /**< Continuous servo state capture */
    
    // Audit compliance features
    bool automatic_integrity;      /**< Auto SHA-256 on log rotation */
    bool tamper_detection;         /**< Detect log modifications */
    bool comprehensive_metadata;   /**< Extended CSV/JSON headers */
    
    // Performance tuning
    size_t buffer_size_kb;         /**< Write buffer size (default: 1024) */
    int flush_interval_ms;         /**< Auto-flush interval (default: 1000) */
    
    // Log rotation
    bool auto_rotation;            /**< Rotate logs automatically */
    size_t max_size_mb;            /**< Max log size before rotation (default: 100) */
    int max_files;                 /**< Keep N recent logs (default: 10) */
    bool compress_rotated;         /**< gzip old logs */
    
    // Output paths (NULL = use defaults)
    const char* log_directory;     /**< Base directory (default: ./logs/) */
    const char* run_id;            /**< Unique run identifier (default: UUID) */
    
} swclock_commercial_config_t;

/**
 * @brief Get default commercial logging configuration
 * 
 * Returns production-ready configuration with all features enabled.
 * Suitable for deployment in regulated industries.
 * 
 * @return Default configuration
 */
swclock_commercial_config_t swclock_commercial_get_defaults(void);

/**
 * @brief Initialize commercial logging system
 * 
 * Must be called before swclock_start(). Initializes all logging
 * subsystems with commercial-grade defaults.
 * 
 * @param config Configuration (NULL = use defaults)
 * @return 0 on success, -1 on error
 */
int swclock_commercial_logging_init(const swclock_commercial_config_t* config);

/**
 * @brief Finalize commercial logging system
 * 
 * Flushes all buffers, computes integrity hashes, writes manifests.
 * Call before application exit.
 * 
 * @return 0 on success, -1 on error
 */
int swclock_commercial_logging_finalize(void);

/**
 * @brief Write comprehensive test metadata header
 * 
 * Generates extended CSV header with:
 * - Test UUID and timestamp
 * - SwClock version and configuration
 * - System information (OS, CPU, kernel)
 * - Compliance targets (IEEE 1588, ITU-T G.8260)
 * - Environmental conditions
 * 
 * @param fp Output file handle
 * @param test_name Test name
 * @param clock SwClock instance
 * @return 0 on success, -1 on error
 */
int swclock_write_commercial_csv_header(
    FILE* fp,
    const char* test_name,
    void* clock  // SwClock* (avoid circular dependency)
);

/**
 * @brief Compute and append integrity hash to log file
 * 
 * Computes SHA-256 over entire log file and appends signature block.
 * Format: "# SHA256: <hex_hash>\n# TIMESTAMP: <iso8601>\n"
 * 
 * @param filepath Path to log file
 * @return 0 on success, -1 on error
 */
int swclock_seal_log_file(const char* filepath);

/**
 * @brief Verify log file integrity
 * 
 * Checks SHA-256 signature against file contents.
 * 
 * @param filepath Path to log file
 * @param out_valid Set to true if valid, false if tampered
 * @return 0 on success, -1 on error
 */
int swclock_verify_log_integrity(const char* filepath, bool* out_valid);

/**
 * @brief Generate log manifest for test run
 * 
 * Creates manifest.json with:
 * - All log files and their hashes
 * - Test configuration and parameters
 * - Compliance validation results
 * - Cross-references between logs
 * 
 * @param run_id Unique run identifier
 * @param log_directory Base log directory
 * @return 0 on success, -1 on error
 */
int swclock_generate_manifest(const char* run_id, const char* log_directory);

#ifdef __cplusplus
}
#endif

#endif // SWCLOCK_COMMERCIAL_LOG_H
