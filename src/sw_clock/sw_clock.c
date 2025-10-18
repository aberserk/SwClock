//
// sw_clock.c
// Software clock for macOS driven by CLOCK_MONOTONIC_RAW.
// Implements Linux-like gettime/settime/adjtime semantics suitable for PTPd.
//

// Prevent system timex.h inclusion to avoid conflicts with our definitions
#ifndef __SYS_TIMEX_H__
#define __SYS_TIMEX_H__
#endif

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sw_clock.h"

// Force IntelliSense to see our definitions (redundant but fixes IDE warnings)
#ifndef ADJ_FREQUENCY
#define ADJ_FREQUENCY   0x0002
#define ADJ_OFFSET      0x0001  
#define ADJ_NANO        0x2000
#define ADJ_SETOFFSET   0x0100
#define ADJ_STATUS      0x0010
#endif

// -------- helpers -------------------------------------------


static inline int64_t ts_to_ns(const struct timespec* t) {
    return (int64_t)t->tv_sec * NS_PER_SEC + (int64_t)t->tv_nsec;
}

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

static inline int get_mono_raw(struct timespec* t) {
    return clock_gettime(CLOCK_MONOTONIC_RAW, t);
}

static inline int get_mono(struct timespec* t) {
    return clock_gettime(CLOCK_MONOTONIC, t);
}

static inline int get_real(struct timespec* t) {
    return clock_gettime(CLOCK_REALTIME, t);
}

// Linux scaled-ppm (2^-16 ppm) -> rate factor
// factor = 1 + freq / (65536 * 1e6)
static inline double scaledppm_to_factor(long scaled_ppm) {
    return 1.0 + ((double)scaled_ppm) / (65536.0 * 1.0e6);
}

// -------- core state ----------------------------------------
struct SwClock {
    pthread_mutex_t lock;

    // Reference epoch for our software clocking
    struct timespec ref_mono_raw;   // monotonic_raw at last update
    int64_t         base_rt_ns;     // software REALTIME at ref (ns)
    int64_t         base_mono_ns;   // software MONOTONIC at ref (ns)

    // Adjustments
    long            freq_scaled_ppm; // ADJ_FREQUENCY (scaled ppm, 2^-16 ppm)
    int             status;          // STA_* bits (stored only)

    // For completeness / reporting
    long            maxerror;
    long            esterror;
    long            constant;
    long            tick;
    int             tai;
};

// Advance bases to "now" using current freq
static void swclock_rebase_now(SwClock* c) {
    struct timespec now_raw;
    if (get_mono_raw(&now_raw) != 0) return;

    int64_t elapsed_ns = ts_to_ns(&now_raw) - ts_to_ns(&c->ref_mono_raw);
    if (elapsed_ns < 0) elapsed_ns = 0;

    double factor = scaledppm_to_factor(c->freq_scaled_ppm);
    int64_t adj_elapsed_ns = (int64_t)((double)elapsed_ns * factor);

    c->base_rt_ns   += adj_elapsed_ns;
    c->base_mono_ns += adj_elapsed_ns;

    c->ref_mono_raw  = now_raw;
}

// -------- public API ----------------------------------------
SwClock* swclock_create(void) {
    SwClock* c = (SwClock*)calloc(1, sizeof(SwClock));
    if (!c) return NULL;

    pthread_mutex_init(&c->lock, NULL);

    // Initialize references from the real system clocks
    get_mono_raw(&c->ref_mono_raw);

    struct timespec sys_rt = {0}, sys_mono = {0};
    get_real(&sys_rt);
    get_mono(&sys_mono);

    c->base_rt_ns   = ts_to_ns(&sys_rt);
    c->base_mono_ns = ts_to_ns(&sys_mono);

    c->freq_scaled_ppm = 0;
    c->status          = 0;
    c->maxerror        = 0;
    c->esterror        = 0;
    c->constant        = 0;
    c->tick            = 0;
    c->tai             = 0;

    return c;
}

void swclock_destroy(SwClock* c) {
    if (!c) return;
    pthread_mutex_destroy(&c->lock);
    free(c);
}

int swclock_gettime(SwClock* c, clockid_t clk_id, struct timespec *tp) {
    if (!c || !tp) { errno = EINVAL; return -1; }

    // CLOCK_MONOTONIC_RAW is always passthrough (hardware-like)
    if (clk_id == CLOCK_MONOTONIC_RAW) {
        return clock_gettime(CLOCK_MONOTONIC_RAW, tp);
    }

    pthread_mutex_lock(&c->lock);
    swclock_rebase_now(c);

    int64_t ns;
    switch (clk_id) {
        case CLOCK_REALTIME:
            ns = c->base_rt_ns;
            break;
        case CLOCK_MONOTONIC:
            // Like Linux: frequency discipline affects MONOTONIC rate,
            // but settime/offset does NOT step MONOTONIC's epoch.
            ns = c->base_mono_ns;
            break;
        default:
            pthread_mutex_unlock(&c->lock);
            errno = EINVAL;
            return -1;
    }

    *tp = ns_to_ts(ns);
    pthread_mutex_unlock(&c->lock);
    return 0;
}

int swclock_settime(SwClock* c, clockid_t clk_id, const struct timespec *tp) {
    if (!c || !tp) { errno = EINVAL; return -1; }

    // Linux allows setting CLOCK_REALTIME only (not MONOTONIC/RAW).
    if (clk_id != CLOCK_REALTIME) {
        errno = EINVAL;
        return -1;
    }

    pthread_mutex_lock(&c->lock);
    // Rebase first to "now", then step REALTIME epoch to requested tp.
    swclock_rebase_now(c);
    c->base_rt_ns = (tp->tv_sec < 0) ? 0 : ts_to_ns(tp);
    pthread_mutex_unlock(&c->lock);
    return 0;
}

// Emulated subset of Linux ntp_adjtime()
// - ADJ_FREQUENCY: set frequency in scaled-ppm (2^-16 ppm), prospective
// - ADJ_OFFSET:    step phase immediately by 'offset' (µs or ns with ADJ_NANO)
// - ADJ_SETOFFSET: uses tptr->time as a relative step
// - ADJ_STATUS:    store status
// Returns TIME_OK on success, -1 on error with errno set.
int swclock_adjtime(SwClock* c, struct timex *tptr) {
    if (!c || !tptr) { errno = EINVAL; return -1; }

    pthread_mutex_lock(&c->lock);
    // Move bases to now before applying new parameters
    swclock_rebase_now(c);

    unsigned int modes = tptr->modes;

    // Frequency
    if (modes & ADJ_FREQUENCY) {
        c->freq_scaled_ppm = tptr->freq;
    }

    // Phase / offset step
    if (modes & ADJ_OFFSET) {
        // offset units: usec by default, ns if ADJ_NANO
        int64_t delta_ns;
        if (modes & ADJ_NANO) {
            delta_ns = (int64_t)tptr->offset;            // nanoseconds
        } else {
            delta_ns = (int64_t)tptr->offset * 1000LL;   // microseconds -> nanoseconds
        }
        // Step REALTIME epoch only (MONOTONIC keeps its epoch)
        c->base_rt_ns += delta_ns;
    }

    if (modes & ADJ_SETOFFSET) {
        // We accept timeval (microseconds resolution). Treat as step.
        int64_t delta_ns = (int64_t)tptr->time.tv_sec * 1000000000LL
                         + (int64_t)tptr->time.tv_usec * 1000LL;
        c->base_rt_ns += delta_ns;
    }

    if (modes & ADJ_STATUS) {
        c->status = tptr->status;
    }

    // Populate some readback fields (optional / informational)
    tptr->status    = c->status;
    tptr->freq      = c->freq_scaled_ppm;
    tptr->maxerror  = c->maxerror;
    tptr->esterror  = c->esterror;
    tptr->constant  = c->constant;
    tptr->precision = 1; // nominal
    tptr->tick      = c->tick;
    tptr->tai       = c->tai;

    pthread_mutex_unlock(&c->lock);
    return TIME_OK;
}
