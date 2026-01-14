/**
 * @file sw_clock_monitor.c
 * @brief Real-time monitoring implementation
 * 
 * Implements circular buffer, sliding window metrics, and threshold monitoring.
 * Part of Priority 2 Recommendation 7.
 * 
 * @author SwClock Development Team
 * @date 2026-01-13
 */

#include "sw_clock_monitor.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <errno.h>
#include <stdio.h>

/**
 * @brief Initialize circular buffer
 */
static int circular_buffer_init(
    swclock_circular_buffer_t* buffer,
    uint32_t capacity,
    double sample_rate_hz
) {
    buffer->samples = calloc(capacity, sizeof(swclock_te_sample_t));
    if (!buffer->samples) {
        return -1;
    }
    
    buffer->capacity = capacity;
    buffer->head = 0;
    buffer->count = 0;
    buffer->sample_rate_hz = sample_rate_hz;
    
    if (pthread_mutex_init(&buffer->lock, NULL) != 0) {
        free(buffer->samples);
        buffer->samples = NULL;
        return -1;
    }
    
    return 0;
}

/**
 * @brief Destroy circular buffer
 */
static void circular_buffer_destroy(swclock_circular_buffer_t* buffer) {
    if (buffer->samples) {
        free(buffer->samples);
        buffer->samples = NULL;
    }
    pthread_mutex_destroy(&buffer->lock);
}

/**
 * @brief Add sample to circular buffer
 */
static void circular_buffer_add(
    swclock_circular_buffer_t* buffer,
    uint64_t timestamp_ns,
    int64_t te_ns
) {
    pthread_mutex_lock(&buffer->lock);
    
    buffer->samples[buffer->head].timestamp_ns = timestamp_ns;
    buffer->samples[buffer->head].te_ns = te_ns;
    
    buffer->head = (buffer->head + 1) % buffer->capacity;
    
    if (buffer->count < buffer->capacity) {
        buffer->count++;
    }
    
    pthread_mutex_unlock(&buffer->lock);
}

/**
 * @brief Get samples from circular buffer (most recent first)
 * 
 * @param buffer Circular buffer
 * @param output Output array (caller must allocate)
 * @param max_samples Maximum samples to retrieve
 * @return Number of samples copied
 */
static uint32_t circular_buffer_get_samples(
    swclock_circular_buffer_t* buffer,
    swclock_te_sample_t* output,
    uint32_t max_samples
) {
    pthread_mutex_lock(&buffer->lock);
    
    uint32_t available = buffer->count;
    uint32_t to_copy = (available < max_samples) ? available : max_samples;
    
    if (to_copy == 0) {
        pthread_mutex_unlock(&buffer->lock);
        return 0;
    }
    
    // Copy samples in reverse chronological order (newest first)
    for (uint32_t i = 0; i < to_copy; i++) {
        uint32_t idx = (buffer->head + buffer->capacity - 1 - i) % buffer->capacity;
        output[i] = buffer->samples[idx];
    }
    
    pthread_mutex_unlock(&buffer->lock);
    return to_copy;
}

/**
 * @brief Compute basic TE statistics from samples
 */
static void compute_te_statistics(
    const swclock_te_sample_t* samples,
    uint32_t count,
    swclock_metrics_snapshot_t* metrics
) {
    if (count == 0) return;
    
    // Compute mean
    double sum = 0.0;
    double min_val = (double)samples[0].te_ns;
    double max_val = (double)samples[0].te_ns;
    
    for (uint32_t i = 0; i < count; i++) {
        double te = (double)samples[i].te_ns;
        sum += te;
        if (te < min_val) min_val = te;
        if (te > max_val) max_val = te;
    }
    
    double mean = sum / count;
    
    // Compute standard deviation
    double var_sum = 0.0;
    for (uint32_t i = 0; i < count; i++) {
        double diff = (double)samples[i].te_ns - mean;
        var_sum += diff * diff;
    }
    double std = sqrt(var_sum / count);
    
    // Compute percentiles (simple method - sort copy of data)
    double* sorted_te = malloc(count * sizeof(double));
    if (sorted_te) {
        for (uint32_t i = 0; i < count; i++) {
            sorted_te[i] = (double)samples[i].te_ns;
        }
        
        // Simple bubble sort for percentiles (good enough for monitoring)
        for (uint32_t i = 0; i < count - 1; i++) {
            for (uint32_t j = 0; j < count - i - 1; j++) {
                if (sorted_te[j] > sorted_te[j + 1]) {
                    double temp = sorted_te[j];
                    sorted_te[j] = sorted_te[j + 1];
                    sorted_te[j + 1] = temp;
                }
            }
        }
        
        uint32_t p95_idx = (uint32_t)(0.95 * count);
        uint32_t p99_idx = (uint32_t)(0.99 * count);
        if (p95_idx >= count) p95_idx = count - 1;
        if (p99_idx >= count) p99_idx = count - 1;
        
        metrics->p95_te_ns = sorted_te[p95_idx];
        metrics->p99_te_ns = sorted_te[p99_idx];
        
        free(sorted_te);
    } else {
        metrics->p95_te_ns = 0.0;
        metrics->p99_te_ns = 0.0;
    }
    
    metrics->mean_te_ns = mean;
    metrics->std_te_ns = std;
    metrics->max_te_ns = max_val;
    metrics->min_te_ns = min_val;
}

/**
 * @brief Compute MTIE for given observation interval
 */
static double compute_mtie_tau(
    const swclock_te_sample_t* samples,
    uint32_t count,
    double sample_dt_s,
    double tau_s
) {
    uint32_t tau_samples = (uint32_t)(tau_s / sample_dt_s);
    
    if (tau_samples >= count || tau_samples == 0) {
        return 0.0;
    }
    
    double max_diff = 0.0;
    
    for (uint32_t i = 0; i <= count - tau_samples; i++) {
        double te_start = (double)samples[i].te_ns;
        double te_end = (double)samples[i + tau_samples].te_ns;
        double diff = fabs(te_end - te_start);
        
        if (diff > max_diff) {
            max_diff = diff;
        }
    }
    
    return max_diff;
}

/**
 * @brief Compute TDEV for given observation interval
 */
static double compute_tdev_tau(
    const swclock_te_sample_t* samples,
    uint32_t count,
    double sample_dt_s,
    double tau_s
) {
    uint32_t tau_samples = (uint32_t)(tau_s / sample_dt_s);
    
    if (tau_samples * 3 >= count || tau_samples == 0) {
        return 0.0;
    }
    
    // TDEV uses second-difference formula
    double sum_sq = 0.0;
    uint32_t n_estimates = 0;
    
    for (uint32_t i = 0; i <= count - 2 * tau_samples; i++) {
        double te0 = (double)samples[i].te_ns;
        double te1 = (double)samples[i + tau_samples].te_ns;
        double te2 = (double)samples[i + 2 * tau_samples].te_ns;
        
        double second_diff = te2 - 2.0 * te1 + te0;
        sum_sq += second_diff * second_diff;
        n_estimates++;
    }
    
    if (n_estimates == 0) {
        return 0.0;
    }
    
    // TDEV formula: sqrt(sum(second_diff^2) / (6 * n))
    return sqrt(sum_sq / (6.0 * n_estimates));
}

/**
 * @brief Compute all metrics from current buffer
 */
static int compute_metrics(
    swclock_monitor_t* monitor,
    swclock_metrics_snapshot_t* metrics
) {
    // Allocate temporary buffer for samples
    swclock_te_sample_t* samples = malloc(
        SWCLOCK_MONITOR_BUFFER_SIZE * sizeof(swclock_te_sample_t)
    );
    
    if (!samples) {
        return -1;
    }
    
    // Get samples from circular buffer
    uint32_t count = circular_buffer_get_samples(
        &monitor->buffer,
        samples,
        SWCLOCK_MONITOR_BUFFER_SIZE
    );
    
    if (count < 100) {  // Need minimum data
        free(samples);
        return -1;
    }
    
    // Compute timestamp and duration
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC_RAW, &now);
    metrics->timestamp_ns = (uint64_t)now.tv_sec * 1000000000ULL + now.tv_nsec;
    metrics->sample_count = count;
    
    uint64_t duration_ns = samples[0].timestamp_ns - samples[count - 1].timestamp_ns;
    metrics->window_duration_s = duration_ns / 1e9;
    
    // Compute TE statistics
    compute_te_statistics(samples, count, metrics);
    
    // Compute sample interval
    double sample_dt_s = 1.0 / monitor->buffer.sample_rate_hz;
    
    // Compute MTIE at key intervals
    metrics->mtie_1s_ns = compute_mtie_tau(samples, count, sample_dt_s, 1.0);
    metrics->mtie_10s_ns = compute_mtie_tau(samples, count, sample_dt_s, 10.0);
    metrics->mtie_30s_ns = compute_mtie_tau(samples, count, sample_dt_s, 30.0);
    metrics->mtie_60s_ns = compute_mtie_tau(samples, count, sample_dt_s, 60.0);
    
    // Compute TDEV at key intervals
    metrics->tdev_0_1s_ns = compute_tdev_tau(samples, count, sample_dt_s, 0.1);
    metrics->tdev_1s_ns = compute_tdev_tau(samples, count, sample_dt_s, 1.0);
    metrics->tdev_10s_ns = compute_tdev_tau(samples, count, sample_dt_s, 10.0);
    
    free(samples);
    return 0;
}

/**
 * @brief Check thresholds and trigger alerts
 */
static void check_thresholds(
    swclock_monitor_t* monitor,
    const swclock_metrics_snapshot_t* metrics
) {
    if (!monitor->thresholds.enabled || !monitor->thresholds.alert_callback) {
        return;
    }
    
    const swclock_threshold_config_t* cfg = &monitor->thresholds;
    
    if (metrics->mtie_1s_ns > cfg->mtie_1s_threshold_ns) {
        cfg->alert_callback("MTIE(1s)", metrics->mtie_1s_ns, cfg->mtie_1s_threshold_ns);
    }
    
    if (metrics->mtie_10s_ns > cfg->mtie_10s_threshold_ns) {
        cfg->alert_callback("MTIE(10s)", metrics->mtie_10s_ns, cfg->mtie_10s_threshold_ns);
    }
    
    if (metrics->tdev_1s_ns > cfg->tdev_1s_threshold_ns) {
        cfg->alert_callback("TDEV(1s)", metrics->tdev_1s_ns, cfg->tdev_1s_threshold_ns);
    }
    
    if (fabs(metrics->max_te_ns) > cfg->max_te_threshold_ns) {
        cfg->alert_callback("Max TE", metrics->max_te_ns, cfg->max_te_threshold_ns);
    }
}

/**
 * @brief Background computation thread
 */
static void* compute_thread_main(void* arg) {
    swclock_monitor_t* monitor = (swclock_monitor_t*)arg;
    
    struct timespec sleep_time = {
        .tv_sec = SWCLOCK_MONITOR_COMPUTE_INTERVAL_S,
        .tv_nsec = 0
    };
    
    while (!monitor->stop_compute_thread) {
        nanosleep(&sleep_time, NULL);
        
        if (monitor->stop_compute_thread) break;
        
        // Compute metrics
        swclock_metrics_snapshot_t metrics = {0};
        if (compute_metrics(monitor, &metrics) == 0) {
            // Update latest metrics (atomic copy)
            monitor->latest_metrics = metrics;
            monitor->last_compute_time_ns = metrics.timestamp_ns;
            monitor->compute_count++;
            
            // Check thresholds
            check_thresholds(monitor, &metrics);
        }
    }
    
    return NULL;
}

// ============================================================================
// Public API Implementation
// ============================================================================

int swclock_monitor_init(swclock_monitor_t* monitor, double sample_rate_hz) {
    if (!monitor || sample_rate_hz <= 0.0) {
        errno = EINVAL;
        return -1;
    }
    
    memset(monitor, 0, sizeof(*monitor));
    
    if (circular_buffer_init(
            &monitor->buffer,
            SWCLOCK_MONITOR_BUFFER_SIZE,
            sample_rate_hz) != 0) {
        return -1;
    }
    
    monitor->compute_thread_running = false;
    monitor->stop_compute_thread = false;
    monitor->last_compute_time_ns = 0;
    monitor->compute_count = 0;
    
    // Set default thresholds (ITU-T G.8260 Class C)
    monitor->thresholds.enabled = false;
    monitor->thresholds.mtie_1s_threshold_ns = 100000.0;    // 100 µs
    monitor->thresholds.mtie_10s_threshold_ns = 200000.0;   // 200 µs
    monitor->thresholds.tdev_1s_threshold_ns = 40000.0;     // 40 µs
    monitor->thresholds.max_te_threshold_ns = 300000.0;     // 300 µs
    monitor->thresholds.alert_callback = NULL;
    
    return 0;
}

void swclock_monitor_destroy(swclock_monitor_t* monitor) {
    if (!monitor) return;
    
    // Stop compute thread if running
    if (monitor->compute_thread_running) {
        swclock_monitor_stop_compute_thread(monitor);
    }
    
    circular_buffer_destroy(&monitor->buffer);
    memset(monitor, 0, sizeof(*monitor));
}

void swclock_monitor_add_sample(
    swclock_monitor_t* monitor,
    uint64_t timestamp_ns,
    int64_t te_ns
) {
    if (!monitor) return;
    
    circular_buffer_add(&monitor->buffer, timestamp_ns, te_ns);
}

int swclock_monitor_get_metrics(
    swclock_monitor_t* monitor,
    swclock_metrics_snapshot_t* snapshot
) {
    if (!monitor || !snapshot) {
        errno = EINVAL;
        return -1;
    }
    
    // Return cached metrics if recent enough (< 1 second old)
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC_RAW, &now);
    uint64_t now_ns = (uint64_t)now.tv_sec * 1000000000ULL + now.tv_nsec;
    
    if (monitor->last_compute_time_ns > 0 &&
        (now_ns - monitor->last_compute_time_ns) < 1000000000ULL) {
        *snapshot = monitor->latest_metrics;
        return 0;
    }
    
    // Compute fresh metrics
    return compute_metrics(monitor, snapshot);
}

void swclock_monitor_set_thresholds(
    swclock_monitor_t* monitor,
    const swclock_threshold_config_t* config
) {
    if (!monitor || !config) return;
    
    monitor->thresholds = *config;
}

int swclock_monitor_start_compute_thread(swclock_monitor_t* monitor) {
    if (!monitor) {
        errno = EINVAL;
        return -1;
    }
    
    if (monitor->compute_thread_running) {
        return 0;  // Already running
    }
    
    monitor->stop_compute_thread = false;
    
    if (pthread_create(&monitor->compute_thread, NULL, compute_thread_main, monitor) != 0) {
        return -1;
    }
    
    monitor->compute_thread_running = true;
    return 0;
}

void swclock_monitor_stop_compute_thread(swclock_monitor_t* monitor) {
    if (!monitor || !monitor->compute_thread_running) return;
    
    monitor->stop_compute_thread = true;
    pthread_join(monitor->compute_thread, NULL);
    monitor->compute_thread_running = false;
}

int swclock_monitor_compute_now(swclock_monitor_t* monitor) {
    if (!monitor) {
        errno = EINVAL;
        return -1;
    }
    
    swclock_metrics_snapshot_t metrics = {0};
    int result = compute_metrics(monitor, &metrics);
    
    if (result == 0) {
        monitor->latest_metrics = metrics;
        monitor->last_compute_time_ns = metrics.timestamp_ns;
        monitor->compute_count++;
        
        check_thresholds(monitor, &metrics);
    }
    
    return result;
}
