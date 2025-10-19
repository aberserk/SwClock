//
// sw_clock.h
// Software clock for macOS driven by CLOCK_MONOTONIC_RAW.
// Exposes Linux-like gettime/settime/adjtime semantics for PTPd-style use.
//
// Build (macOS):
//   clang -O2 -Wall -Wextra -pedantic -std=c11 sw_clock.c main.c -o swclock_demo
//
#ifndef SW_CLOCK_UTILITIES_H
#define SW_CLOCK_UTILITIES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <time.h>
#include <sys/time.h> // timeval
#include "sw_clock_constants.h"

// -------- timex compatibility (for macOS) -------------------
// Prevent system timex.h from being included to avoid conflicts
#ifndef __SYS_TIMEX_H__
#define __SYS_TIMEX_H__
#endif

/**
 * Convert nanoseconds to a timespec structure.
 * @param ns The time in nanoseconds
 * @return The equivalent timespec structure
 */
static inline struct timespec ns_to_ts(int64_t ns) {
    int64_t sec  = ns / NS_PER_SEC;   // truncates toward zero
    int64_t nsec = ns % NS_PER_SEC;   // same sign as ns

    if (nsec < 0) {                 // fix negative remainder
        nsec += NS_PER_SEC;
        --sec;
    }

    struct timespec t;
    t.tv_sec  = (time_t)sec;
    t.tv_nsec = (long)nsec;         // guaranteed < NS_PER_S
    return t;
}

/**
 * Convert ppm (parts-per-million) to NTP frequency format (scaled ppm).
 * @param ppm Frequency in parts-per-million (ppm)
 * @return Frequency in NTP format (scaled ppm)
 */
static inline long ppm_to_ntp_freq(double ppm)
{
    return (long)(ppm * (double)NTP_SCALE_FACTOR);
}

/**
 * Convert NTP frequency format (scaled ppm) back to ppm.
 * @param ntp_freq Frequency in NTP format (scaled ppm)
 * @return Frequency in parts-per-million (ppm)
 */
static inline double ntp_freq_to_ppm(long ntp_freq)
{
    return (double)ntp_freq / (double)NTP_SCALE_FACTOR;
}

/**
 * Convert a timespec structure to nanoseconds.
 * @param t The timespec to convert
 * @return The equivalent time in nanoseconds
 */
static inline int64_t ts_to_ns(const struct timespec* t) {
    return (int64_t)t->tv_sec * NS_PER_SEC + (int64_t)t->tv_nsec;
}

/**
 * Calculate the difference between two timespec structures in nanoseconds.
 * @param a The first timespec
 * @param b The second timespec
 * @return The difference in nanoseconds (b - a)
 */
static inline int64_t diff_ns(const struct timespec* a, const struct timespec* b) {
    return ts_to_ns(b) - ts_to_ns(a);
}

/**
 * Convert scaled ppm (parts-per-million * 2^16) to a multiplicative factor.
 * E.g., 100 ppm -> 1.0001
 * @param scaled_ppm Frequency adjustment in scaled ppm (parts-per-million * 2^16)
 * @return Multiplicative factor (e.g., 1.0001 for +100 ppm)
 */
static inline double scaledppm_to_factor(long scaled_ppm) {
    return 1.0 + ((double)scaled_ppm) / (65536.0 * 1.0e6);
}

static inline void sleep_ns(long long ns)
{
    if (ns <= 0) return;

    struct timespec ts;
    ts.tv_sec  = (time_t)(ns / NS_PER_SEC);
    ts.tv_nsec = (long)(ns % NS_PER_SEC);

    /* Retry if interrupted by a signal */
    while (nanosleep(&ts, NULL) == -1 && errno == EINTR) {
        /* ts is updated with remaining time */
    }
}

/**
 * Print a timespec structure as a formatted date/time string (UTC).
 * @param ts The timespec to print
 */
void print_timespec_as_datetime(const struct timespec *ts);

/**
 * Print a timespec structure as a formatted date/time string (local time).
 * @param ts The timespec to print  
 */
void print_timespec_as_localtime(const struct timespec *ts);

/**
 * Print a timespec structure as a formatted date/time string (TAI).
 * @param ts The timespec to print  
 */
void print_timespec_as_TAI(const struct timespec *ts);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // SW_CLOCK_H
