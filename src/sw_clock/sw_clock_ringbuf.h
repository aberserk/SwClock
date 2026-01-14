/**
 * @file sw_clock_ringbuf.h
 * @brief Lock-free ring buffer for event logging
 * 
 * Single-producer, single-consumer (SPSC) lock-free ring buffer
 * optimized for low-latency event logging from servo thread.
 * 
 * Design:
 * - Producer (servo thread): Pushes events without blocking
 * - Consumer (logger thread): Pops events and writes to disk
 * - No locks in hot path (atomic operations only)
 * - Overrun detection with flag
 * 
 * Part of Priority 1 implementation (Recommendation 2).
 * 
 * @author SwClock Development Team
 * @date 2026-01-13
 */

#ifndef SWCLOCK_RINGBUF_H
#define SWCLOCK_RINGBUF_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Ring buffer size (1 MB default)
 * 
 * At ~80 bytes per event, this holds ~12,800 events.
 * At 100 Hz servo rate, this provides ~128 seconds of buffering.
 */
#ifndef SWCLOCK_RINGBUF_SIZE
#define SWCLOCK_RINGBUF_SIZE (1024 * 1024)
#endif

/**
 * @brief Lock-free ring buffer structure
 * 
 * IMPORTANT: Producer writes to write_pos, consumer reads from read_pos.
 * These must be accessed atomically to ensure thread safety.
 */
typedef struct {
    uint8_t buffer[SWCLOCK_RINGBUF_SIZE]; /**< Circular buffer data */
    _Atomic uint64_t write_pos;           /**< Producer write position */
    _Atomic uint64_t read_pos;            /**< Consumer read position */
    _Atomic bool overrun_flag;            /**< Set on buffer full */
    uint64_t events_written;              /**< Total events written */
    uint64_t events_read;                 /**< Total events read */
    uint64_t overrun_count;               /**< Number of overruns */
} swclock_ringbuf_t;

/**
 * @brief Initialize ring buffer
 * 
 * @param rb Ring buffer to initialize
 */
void swclock_ringbuf_init(swclock_ringbuf_t* rb);

/**
 * @brief Push data to ring buffer (producer side)
 * 
 * Non-blocking operation. If buffer is full, sets overrun flag
 * and returns false.
 * 
 * Thread safety: Safe to call from single producer thread.
 * 
 * @param rb Ring buffer
 * @param data Data to write
 * @param size Data size in bytes
 * @return true if written, false if buffer full
 */
bool swclock_ringbuf_push(
    swclock_ringbuf_t* rb,
    const void* data,
    size_t size
);

/**
 * @brief Pop data from ring buffer (consumer side)
 * 
 * Non-blocking operation. If no data available, returns false.
 * 
 * Thread safety: Safe to call from single consumer thread.
 * 
 * @param rb Ring buffer
 * @param data Output buffer
 * @param max_size Maximum bytes to read
 * @param actual_size Output: actual bytes read (can be NULL)
 * @return true if data read, false if buffer empty
 */
bool swclock_ringbuf_pop(
    swclock_ringbuf_t* rb,
    void* data,
    size_t max_size,
    size_t* actual_size
);

/**
 * @brief Check if ring buffer is empty
 * 
 * @param rb Ring buffer
 * @return true if empty
 */
bool swclock_ringbuf_is_empty(const swclock_ringbuf_t* rb);

/**
 * @brief Get available space in ring buffer
 * 
 * @param rb Ring buffer
 * @return Available bytes
 */
size_t swclock_ringbuf_available(const swclock_ringbuf_t* rb);

/**
 * @brief Get used space in ring buffer
 * 
 * @param rb Ring buffer
 * @return Used bytes
 */
size_t swclock_ringbuf_used(const swclock_ringbuf_t* rb);

/**
 * @brief Clear overrun flag and return previous state
 * 
 * @param rb Ring buffer
 * @return true if overrun had occurred
 */
bool swclock_ringbuf_clear_overrun(swclock_ringbuf_t* rb);

/**
 * @brief Get ring buffer statistics
 * 
 * @param rb Ring buffer
 * @param events_written Output: total events written (can be NULL)
 * @param events_read Output: total events read (can be NULL)
 * @param overrun_count Output: number of overruns (can be NULL)
 */
void swclock_ringbuf_stats(
    const swclock_ringbuf_t* rb,
    uint64_t* events_written,
    uint64_t* events_read,
    uint64_t* overrun_count
);

#ifdef __cplusplus
}
#endif

#endif /* SWCLOCK_RINGBUF_H */
