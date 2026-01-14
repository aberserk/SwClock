/**
 * @file test_metadata.h
 * @brief Test metadata collection for comprehensive logging
 * 
 * Provides utilities to collect system information, configuration,
 * and environmental data for test documentation and audit trails.
 * 
 * Part of Priority 1 implementation (Recommendation 4).
 * 
 * @author SwClock Development Team
 * @date 2026-01-13
 */

#ifndef TEST_METADATA_H
#define TEST_METADATA_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Comprehensive test metadata structure
 */
typedef struct {
    // Test identification
    char test_run_id[40];          // UUID v4 string
    char test_name[128];           // Test case name
    char swclock_version[16];      // SwClock version (e.g., "v2.0.0")
    
    // Configuration
    double kp_ppm_per_s;           // Proportional gain (ppm/s)
    double ki_ppm_per_s2;          // Integral gain (ppm/s²)
    double max_ppm;                // Maximum frequency adjustment (ppm)
    int64_t poll_ns;               // Poll interval (nanoseconds)
    int64_t phase_eps_ns;          // Phase epsilon threshold (nanoseconds)
    
    // System information
    char os_name[64];              // Operating system name
    char os_version[32];           // OS version
    char cpu_model[128];           // CPU model string
    char hostname[256];            // System hostname
    
    // Timing reference
    char reference_clock[64];      // Clock source (e.g., "CLOCK_MONOTONIC_RAW")
    
    // Test conditions
    char start_time_iso8601[64];   // Test start time (ISO 8601 format)
    char timezone[16];             // Timezone (typically "UTC")
    
    // Environment (optional)
    double ambient_temp_c;         // Ambient temperature (if available)
    double system_load_avg;        // System load average
    int cpu_count;                 // Number of CPU cores
    
    // Compliance targets (for documentation in CSV)
    char compliance_standard[64];  // Target standard (e.g., "ITU-T G.8260 Class C")
} test_metadata_t;

/**
 * @brief Collect comprehensive test metadata
 * 
 * Gathers system information, configuration, and environmental data.
 * 
 * @param meta Output metadata structure
 * @param test_name Name of the test being run
 * @param kp Proportional gain (ppm/s)
 * @param ki Integral gain (ppm/s²)
 * @param max_ppm Maximum frequency adjustment (ppm)
 * @param poll_ns Poll interval (nanoseconds)
 * @param phase_eps_ns Phase epsilon (nanoseconds)
 */
void collect_test_metadata(
    test_metadata_t* meta,
    const char* test_name,
    double kp,
    double ki,
    double max_ppm,
    int64_t poll_ns,
    int64_t phase_eps_ns
);

/**
 * @brief Generate a UUID v4 string
 * 
 * @param uuid_buf Output buffer (at least 37 bytes)
 * @param buf_size Buffer size
 */
void generate_test_run_uuid(char* uuid_buf, size_t buf_size);

/**
 * @brief Get current timestamp in ISO 8601 format
 * 
 * @param buf Output buffer
 * @param buf_size Buffer size
 */
void get_iso8601_timestamp(char* buf, size_t buf_size);

/**
 * @brief Get system hostname
 * 
 * @param buf Output buffer
 * @param buf_size Buffer size
 * @return 0 on success, -1 on error
 */
int get_system_hostname(char* buf, size_t buf_size);

/**
 * @brief Get CPU model information
 * 
 * @param buf Output buffer
 * @param buf_size Buffer size
 * @return 0 on success, -1 on error
 */
int get_cpu_model(char* buf, size_t buf_size);

/**
 * @brief Get OS name and version
 * 
 * @param name_buf OS name output
 * @param name_size Name buffer size
 * @param version_buf OS version output
 * @param version_size Version buffer size
 * @return 0 on success, -1 on error
 */
int get_os_info(char* name_buf, size_t name_size, 
                char* version_buf, size_t version_size);

/**
 * @brief Get system load average
 * 
 * @return 1-minute load average, or -1.0 on error
 */
double get_system_load();

/**
 * @brief Get number of CPU cores
 * 
 * @return CPU count, or -1 on error
 */
int get_cpu_count();

#ifdef __cplusplus
}
#endif

#endif /* TEST_METADATA_H */
