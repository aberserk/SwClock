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

#include <time.h>
#include <sys/time.h> // timeval
#include "sw_clock_constants.h"
#include "sw_clock_utilities.h"

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
      long maxerror;        // (unused here)
      long esterror;        // (unused here)
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
  #define ADJ_OFFSET      0x0001
  #define ADJ_FREQUENCY   0x0002
  #define ADJ_MAXERROR    0x0004
  #define ADJ_ESTERROR    0x0008
  #define ADJ_STATUS      0x0010
  #define ADJ_TIMECONST   0x0020
  #define ADJ_TAI         0x0080
  #define ADJ_SETOFFSET   0x0100
  #define ADJ_MICRO       0x1000
  #define ADJ_NANO        0x2000

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

// Opaque handle
typedef struct SwClock SwClock;

// API
SwClock* swclock_create(void);
void     swclock_destroy(SwClock* c);
// Functionally identical to Linux clock_gettime for REALTIME/MONOTONIC/RAW (RAW passthrough)
int      swclock_gettime(SwClock* c, clockid_t clk_id, struct timespec *tp);
// Functionally identical to Linux clock_settime for REALTIME (MONOTONIC cannot be set)
int      swclock_settime(SwClock* c, clockid_t clk_id, const struct timespec *tp);
// Functionally similar to Linux ntp_adjtime (subset): ADJ_FREQUENCY, ADJ_OFFSET, ADJ_SETOFFSET, ADJ_STATUS
int      swclock_adjtime(SwClock* c, struct timex *tptr);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // SW_CLOCK_H
