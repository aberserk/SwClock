/**
 * @file sw_clock_monitor.h
 * @brief Real-time monitoring infrastructure for SwClock
 * 
 * Implements Priority 2 Recommendation 7: Real-Time Monitoring Mode
 * - Circular buffer for continuous TE capture
 * - Sliding window MTIE/TDEV computation
 * - Threshold monitoring and alerts
 * 
 * @author SwClock Development Team
 * @date 2026-01-13
 */

#ifndef SWCLOCK_MONITOR_H
#define SWCLOCK_MONITOR_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Monitoring configuration
 */
#define SWCLOCK_MONITOR_BUFFER_SIZE   36000  /**< 1 hour @ 10 Hz */
#define SWCLOCK_MONITOR_COMPUTE_INTERVAL_S  10  /**< Recompute metrics every 10s */

/**
 * @brief Time Error sample for circular buffer
 */
typedef struct {
    uint64_t timestamp_ns;  /**< CLOCK_MONOTONIC_RAW timestamp */
    int64_t  te_ns;         /**< Time error in nanoseconds */
} swclock_te_sample_t;

/**
 * @brief Circular buffer for TE samples
 */
typedef struct {
    swclock_te_sample_t* samples;  /**< Ring buffer array */
    uint32_t capacity;              /**< Buffer capacity (samples) */
    uint32_t head;                  /**< Write position */
    uint32_t count;                 /**< Number of samples stored */
    pthread_mutex_t lock;           /**< Thread safety */
    double sample_rate_hz;          /**< Expected sample rate */
} swclock_circular_buffer_t;

/**
 * @brief Real-time metrics snapshot
 */
typedef struct {
    uint64_t timestamp_ns;          /**< When metrics were computed */
    uint32_t sample_count;          /**< Number of samples used */
    double   window_duration_s;     /**< Time window of data */
    
    // TE statistics
    double   mean_te_ns;
    double   std_te_ns;
    double   max_te_ns;
    double   min_te_ns;
    double   p95_te_ns;
    double   p99_te_ns;
    
    // MTIE at key observation intervals
    double   mtie_1s_ns;
    double   mtie_10s_ns;
    double   mtie_30s_ns;
    double   mtie_60s_ns;
    
    // TDEV at key observation intervals
    double   tdev_0_1s_ns;
    double   tdev_1s_ns;
    double   tdev_10s_ns;
} swclock_metrics_snapshot_t;

/**
 * @brief Threshold configuration
 */
typedef struct {
    bool     enabled;
    double   mtie_1s_threshold_ns;
    double   mtie_10s_threshold_ns;
    double   tdev_1s_threshold_ns;
    double   max_te_threshold_ns;
    void (*alert_callback)(const char* metric, double value, double threshold);
} swclock_threshold_config_t;

/**
 * @brief Real-time monitoring context
 */
typedef struct {
    swclock_circular_buffer_t buffer;
    swclock_metrics_snapshot_t latest_metrics;
    swclock_threshold_config_t thresholds;
    
    pthread_t compute_thread;
    bool compute_thread_running;
    bool stop_compute_thread;
    
    uint64_t last_compute_time_ns;
    uint64_t compute_count;
} swclock_monitor_t;

/**
 * @brief Initialize monitoring infrastructure
 * 
 * @param monitor Monitor context
 * @param sample_rate_hz Expected sample rate
 * @return 0 on success, -1 on error
 */
int swclock_monitor_init(swclock_monitor_t* monitor, double sample_rate_hz);

/**
 * @brief Destroy monitoring infrastructure
 * 
 * @param monitor Monitor context
 */
void swclock_monitor_destroy(swclock_monitor_t* monitor);

/**
 * @brief Add TE sample to circular buffer
 * 
 * @param monitor Monitor context
 * @param timestamp_ns Sample timestamp
 * @param te_ns Time error in nanoseconds
 */
void swclock_monitor_add_sample(
    swclock_monitor_t* monitor,
    uint64_t timestamp_ns,
    int64_t te_ns
);

/**
 * @brief Get current metrics snapshot
 * 
 * @param monitor Monitor context
 * @param snapshot Output snapshot (will be filled)
 * @return 0 on success, -1 if no metrics available
 */
int swclock_monitor_get_metrics(
    swclock_monitor_t* monitor,
    swclock_metrics_snapshot_t* snapshot
);

/**
 * @brief Configure threshold monitoring
 * 
 * @param monitor Monitor context
 * @param config Threshold configuration
 */
void swclock_monitor_set_thresholds(
    swclock_monitor_t* monitor,
    const swclock_threshold_config_t* config
);

/**
 * @brief Start background metrics computation thread
 * 
 * @param monitor Monitor context
 * @return 0 on success, -1 on error
 */
int swclock_monitor_start_compute_thread(swclock_monitor_t* monitor);

/**
 * @brief Stop background metrics computation thread
 * 
 * @param monitor Monitor context
 */
void swclock_monitor_stop_compute_thread(swclock_monitor_t* monitor);

/**
 * @brief Trigger immediate metrics computation
 * 
 * @param monitor Monitor context
 * @return 0 on success, -1 on error
 */
int swclock_monitor_compute_now(swclock_monitor_t* monitor);

#ifdef __cplusplus
}
#endif

#endif /* SWCLOCK_MONITOR_H */
