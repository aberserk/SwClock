/**
 * @file sw_clock_events.h
 * @brief Event type definitions for structured event logging
 * 
 * Defines event types, structures, and interfaces for SwClock's
 * structured event logging system. Provides audit trail capabilities
 * with binary encoding for minimal performance overhead.
 * 
 * Part of Priority 1 implementation (Recommendation 2).
 * 
 * @author SwClock Development Team
 * @date 2026-01-13
 */

#ifndef SWCLOCK_EVENTS_H
#define SWCLOCK_EVENTS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Event type enumeration
 * 
 * Defines all event types that can be logged by SwClock.
 * Values are chosen to be self-documenting in hex dumps.
 */
typedef enum {
    SWCLOCK_EVENT_ADJTIME_CALL       = 0x01,  /**< adjtimex() called */
    SWCLOCK_EVENT_ADJTIME_RETURN     = 0x02,  /**< adjtimex() returned */
    SWCLOCK_EVENT_PI_ENABLE          = 0x10,  /**< PI controller enabled */
    SWCLOCK_EVENT_PI_DISABLE         = 0x11,  /**< PI controller disabled */
    SWCLOCK_EVENT_PI_STEP            = 0x12,  /**< PI controller step executed */
    SWCLOCK_EVENT_PHASE_SLEW_START   = 0x20,  /**< Phase slew started */
    SWCLOCK_EVENT_PHASE_SLEW_DONE    = 0x21,  /**< Phase slew completed */
    SWCLOCK_EVENT_FREQUENCY_CLAMP    = 0x30,  /**< Frequency clamped to max_ppm */
    SWCLOCK_EVENT_THRESHOLD_CROSS    = 0x40,  /**< Phase error threshold crossed */
    SWCLOCK_EVENT_CLOCK_RESET        = 0x50,  /**< Clock state reset */
    SWCLOCK_EVENT_LOG_START          = 0xF0,  /**< Logging started */
    SWCLOCK_EVENT_LOG_STOP           = 0xF1,  /**< Logging stopped */
    SWCLOCK_EVENT_LOG_MARKER         = 0xFF   /**< User-defined marker */
} swclock_event_type_t;

/**
 * @brief Event header structure (fixed 16 bytes)
 * 
 * All events start with this header, followed by optional payload.
 * Little-endian encoding for cross-platform compatibility.
 */
typedef struct __attribute__((packed)) {
    uint64_t sequence_num;      /**< Monotonic event counter */
    uint64_t timestamp_ns;      /**< CLOCK_MONOTONIC_RAW timestamp */
    uint16_t event_type;        /**< swclock_event_type_t */
    uint16_t payload_size;      /**< Variable payload length (bytes) */
    uint32_t reserved;          /**< Reserved for future use */
} swclock_event_header_t;

/**
 * @brief adjtimex() call event payload
 */
typedef struct __attribute__((packed)) {
    uint32_t modes;             /**< ADJ_* flags */
    int64_t  offset_ns;         /**< Phase offset (nanoseconds) */
    int64_t  freq_scaled_ppm;   /**< Frequency adjustment (scaled ppm) */
    int32_t  return_code;       /**< Return value (TIME_OK, etc.) */
    uint32_t padding;           /**< Alignment padding */
} swclock_event_adjtime_payload_t;

/**
 * @brief PI controller step event payload
 */
typedef struct __attribute__((packed)) {
    double   pi_freq_ppm;       /**< Current PI output (ppm) */
    double   pi_int_error_s;    /**< Integral error accumulator (seconds) */
    int64_t  remaining_phase_ns;/**< Outstanding phase correction (ns) */
    int32_t  servo_enabled;     /**< PI enabled flag */
    uint32_t padding;           /**< Alignment padding */
} swclock_event_pi_step_payload_t;

/**
 * @brief Phase slew event payload
 */
typedef struct __attribute__((packed)) {
    int64_t  target_phase_ns;   /**< Target phase correction (ns) */
    int64_t  current_phase_ns;  /**< Current phase offset (ns) */
    double   slew_rate_ns_per_s;/**< Slew rate (ns/s) */
    uint32_t duration_ms;       /**< Expected duration (milliseconds) */
    uint32_t padding;           /**< Alignment padding */
} swclock_event_phase_slew_payload_t;

/**
 * @brief Frequency clamp event payload
 */
typedef struct __attribute__((packed)) {
    double   requested_ppm;     /**< Requested frequency (ppm) */
    double   clamped_ppm;       /**< Clamped frequency (ppm) */
    double   max_ppm;           /**< Maximum allowed (ppm) */
    uint32_t padding;           /**< Alignment padding */
} swclock_event_frequency_clamp_payload_t;

/**
 * @brief Threshold crossing event payload
 */
typedef struct __attribute__((packed)) {
    int64_t  phase_error_ns;    /**< Current phase error (ns) */
    int64_t  threshold_ns;      /**< Threshold value (ns) */
    uint32_t crossing_type;     /**< 0=rising, 1=falling */
    uint32_t padding;           /**< Alignment padding */
} swclock_event_threshold_payload_t;

/**
 * @brief Generic marker event payload
 */
typedef struct __attribute__((packed)) {
    uint32_t marker_id;         /**< User-defined marker ID */
    char     description[60];   /**< Human-readable description */
} swclock_event_marker_payload_t;

/**
 * @brief Maximum event size (header + largest payload)
 */
#define SWCLOCK_EVENT_MAX_SIZE (sizeof(swclock_event_header_t) + 64)

/**
 * @brief Event log file magic number (identifies binary format)
 */
#define SWCLOCK_EVENT_LOG_MAGIC 0x53574556  /* "SWEV" in ASCII */

/**
 * @brief Event log file header
 */
typedef struct __attribute__((packed)) {
    uint32_t magic;             /**< SWCLOCK_EVENT_LOG_MAGIC */
    uint16_t version_major;     /**< Format version (major) */
    uint16_t version_minor;     /**< Format version (minor) */
    uint64_t start_time_ns;     /**< Log start timestamp */
    char     swclock_version[16]; /**< SwClock version string */
    uint32_t reserved[8];       /**< Reserved for future use */
} swclock_event_log_header_t;

/**
 * @brief Get human-readable event type name
 * 
 * @param event_type Event type
 * @return String description (never NULL)
 */
const char* swclock_event_type_name(swclock_event_type_t event_type);

/**
 * @brief Get payload size for event type
 * 
 * @param event_type Event type
 * @return Expected payload size in bytes, or 0 if variable
 */
size_t swclock_event_payload_size(swclock_event_type_t event_type);

#ifdef __cplusplus
}
#endif

#endif /* SWCLOCK_EVENTS_H */
