//
// sw_clock.h
// Software clock for macOS driven by CLOCK_MONOTONIC_RAW.
// Exposes Linux-like gettime/settime/adjtime semantics for PTPd-style use.
//
// Build (macOS):
//   clang -O2 -Wall -Wextra -pedantic -std=c11 sw_clock.c main.c -o swclock_demo
//
#ifndef SW_CLOCK_H
#define SW_CLOCK_H

#ifdef __cplusplus
extern "C" {
#endif

#define SWCLOCK_VERSION "v2.0.0"

#include <time.h>
#include <sys/time.h> // timeval
#include "sw_clock_constants.h"
#include "sw_clock_utilities.h"
#include "sw_clock_events.h"
#include "sw_clock_ringbuf.h"
#include "sw_clock_monitor.h"
#include <stdio.h>

// -------- timex compatibility (for macOS) -------------------
// Prevent system timex.h from being included to avoid conflicts
#ifndef __SYS_TIMEX_H__
#define __SYS_TIMEX_H__
#endif

// Define our own complete timex structure and constants
#ifndef __SWCLOCK_TIMEX_COMPAT__
#define __SWCLOCK_TIMEX_COMPAT__

// Minimal Linux-compatible subset sufficient for PTPd-like usage.
// Values chosen to match Linux uapi where relevant.

  struct timex {
      unsigned int modes;   // input: which fields to set
      long offset;          // phase offset: usec or nsec (with ADJ_NANO)
      long freq;            // frequency offset, scaled ppm (2^-16 ppm units)
      long maxerror;        // maximum error estimate (microseconds)
      long esterror;        // estimated error (microseconds)
      int  status;          // STA_* bitfield; we store but don't interpret
      long constant;        // (unused here)
      long precision;       // (unused here)
      long tolerance;       // (unused here)
      struct timeval time;  // for ADJ_SETOFFSET
      long tick;            // (unused here)
      long ppsfreq;         // (unused)
      long jitter;          // (unused)
      int  shift;           // (unused)
      long stabil;          // (unused)
      long jitcnt;          // (unused)
      long calcnt;          // (unused)
      long errcnt;          // (unused)
      long stbcnt;          // (unused)
      int  tai;             // (unused)
  };

  // Modes
  #define ADJ_OFFSET      0x0001 /* phase offset */
  #define ADJ_FREQUENCY   0x0002 /* frequency offset */
  #define ADJ_MAXERROR    0x0004 /* maximum error estimate */
  #define ADJ_ESTERROR    0x0008 /* estimated error */
  #define ADJ_STATUS      0x0010 /* (unused here) */
  #define ADJ_TIMECONST   0x0020 /* (unused here) */
  #define ADJ_TAI         0x0080 /* Adjust TAI offset*/
  #define ADJ_SETOFFSET   0x0100 /* set time offset */
  #define ADJ_MICRO       0x1000 /* microsec offset */
  #define ADJ_NANO        0x2000 /* nanosec offset */

  #define TIME_OK         0
  #define TIME_BAD        5
  // Status bits (stored only)
  #define STA_PLL         0x0001
  #define STA_UNSYNC      0x0040

  // Return values (Linux ntp_adjtime)
  #define TIME_OK         0
  #define TIME_BAD        5

#endif // __SWCLOCK_TIMEX_COMPAT__

// -------- clock id compatibility for macOS -------------------
#ifndef CLOCK_MONOTONIC_RAW
  #ifdef CLOCK_UPTIME_RAW
    #define CLOCK_MONOTONIC_RAW CLOCK_UPTIME_RAW
  #else
    #define CLOCK_MONOTONIC_RAW CLOCK_MONOTONIC
  #endif
#endif

typedef struct SwClock SwClock; // SwClock opaque type

/**
 * Create a new software clock instance.
 * @return Pointer to the new SwClock instance, or NULL on failure.
 */
SwClock* swclock_create(void);

/** 
 * Destroy a software clock instance. 
 * @param c Pointer to SwClock instance to destroy
 */
void     swclock_destroy(SwClock* c);

/** 
 * Functionally identical to Linux clock_gettime for REALTIME/MONOTONIC/RAW (RAW passthrough) 
 * @param c Pointer to SwClock instance
 * @param clk_id Clock ID (CLOCK_REALTIME, CLOCK_MONOTONIC, CLOCK_MONOTONIC_RAW)
 * @param tp Pointer to timespec structure to receive the time
 * @return 0 on success, -1 on failure (errno set)
 */
int      swclock_gettime(SwClock* c, clockid_t clk_id, struct timespec *tp);

/** 
 * Functionally identical to Linux clock_settime for REALTIME (MONOTONIC cannot be set) 
 * @param c Pointer to SwClock instance
 * @param clk_id Clock ID (CLOCK_REALTIME, CLOCK_MONOTONIC, CLOCK_MONOTONIC_RAW)
 * @param tp Pointer to timespec structure containing the new time
 * @return 0 on success, -1 on failure (errno set)
 */
int      swclock_settime(SwClock* c, clockid_t clk_id, const struct timespec *tp);

/** 
 * Functionally identical to Linux adjtimex 
 * @param c Pointer to SwClock instance
 * @param tptr Pointer to timex structure for adjustment parameters and readback
 * @return TIME_OK on success, TIME_BAD on failure (errno set)
 */
int      swclock_adjtime(SwClock* c, struct timex *tptr);

/**
 * Explicit poll (normally called by the internal thread). Safe to call manually.
 * @param c Pointer to SwClock instance
 */
void     swclock_poll(SwClock* c);

/**
 * Start logging clock state to a file.
 * @param c Pointer to SwClock instance
 * @param filename Name of the log file
 */
void     swclock_start_log(SwClock* c, const char* filename);

/**
 * Close the log file if open.
 * @param c Pointer to SwClock instance
 */
void     swclock_close_log(SwClock* c);

/*
 * Disable the PI servo control loop.
 * @param c Pointer to SwClock instance
 */
void     swclock_disable_pi_servo(SwClock* c);

/**
 * Get the current remaining phase error being corrected by PI servo.
 * @param c Pointer to SwClock instance
 * @return Remaining phase error in nanoseconds (positive or negative)
 */
long long swclock_get_remaining_phase_ns(SwClock* c);

/**
 * Start event logging to binary file.
 * @param c Pointer to SwClock instance
 * @param filename Event log file path
 * @return 0 on success, -1 on failure
 */
int      swclock_start_event_log(SwClock* c, const char* filename);

/**
 * Stop event logging and close file.
 * @param c Pointer to SwClock instance
 */
void     swclock_stop_event_log(SwClock* c);

/**
 * Log a custom event with optional payload.
 * @param c Pointer to SwClock instance
 * @param event_type Event type
 * @param payload Optional payload data (can be NULL)
 * @payload_size Payload size in bytes
 */
void     swclock_log_event(SwClock* c, swclock_event_type_t event_type,
                           const void* payload, size_t payload_size);

/**
 * Enable real-time monitoring mode.
 * @param c Pointer to SwClock instance
 * @param enable True to enable, false to disable
 * @return 0 on success, -1 on failure
 */
int      swclock_enable_monitoring(SwClock* c, bool enable);

/**
 * Get current real-time metrics snapshot.
 * @param c Pointer to SwClock instance
 * @param snapshot Output metrics snapshot
 * @return 0 on success, -1 on failure
 */
int      swclock_get_metrics(SwClock* c, swclock_metrics_snapshot_t* snapshot);

/**
 * Set threshold monitoring configuration.
 * @param c Pointer to SwClock instance
 * @param config Threshold configuration
 */
void     swclock_set_thresholds(SwClock* c, const swclock_threshold_config_t* config);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // SW_CLOCK_H
