//
// sw_clock.c (v2.0 - slewed phase correction with PI + background poll thread)
//
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <inttypes.h> // for PRId64

#include "sw_clock.h"



// ================= Helpers =================
void swclock_log(SwClock* c);

static inline double scaledppm_to_ppm(long scaled) {
    return ((double)scaled) / 65536.0;
}

// ================= Core state =================

struct SwClock {
    pthread_mutex_t lock;

    // Reference epoch from hardware raw time
    struct timespec ref_mono_raw;

    // Software clock bases at reference
    int64_t base_rt_ns;    // REALTIME
    int64_t base_mono_ns;  // MONOTONIC

    // Base frequency bias set by ADJ_FREQUENCY (scaled-ppm)
    long    freq_scaled_ppm;

    // PI controller state (produces an additional freq correction in ppm)
    double  pi_freq_ppm;           // output
    double  pi_int_error_s;        // integral of error (seconds)
    bool    pi_servo_enabled;      // whether PI servo is active

    // Outstanding phase error to slew out (nanoseconds). Sign indicates direction.
    long long remaining_phase_ns;

    // Timex-ish fields
    int     status;
    long    maxerror;
    long    esterror;
    long    constant;
    long    tick;
    int     tai;

    // Background poll thread
    pthread_t poll_thread;
    bool      poll_thread_running;
    bool      stop_flag;

    // Logging support
    FILE* log_fp;         // CSV file handle
    bool  is_logging;     // true if logging is active
};

// Forward declaration
static void* swclock_poll_thread_main(void* arg);

// Compute total rate factor from base freq + PI freq correction (in ppm)
static inline double total_factor(const struct SwClock* c) {
    double base_ppm = scaledppm_to_ppm(c->freq_scaled_ppm);
    double total_ppm = base_ppm + c->pi_freq_ppm;
    return 1.0 + total_ppm / 1.0e6;
}

// Advance time bases to now using current total factor and update remaining phase bookkeeping.
static void swclock_rebase_now_and_update(SwClock* c) {
    struct timespec now_raw;
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &now_raw) != 0) return;

    int64_t elapsed_raw_ns = ts_to_ns(&now_raw) - ts_to_ns(&c->ref_mono_raw);
    if (elapsed_raw_ns < 0) elapsed_raw_ns = 0;

    double factor = total_factor(c);
    int64_t adj_elapsed_ns = (int64_t)((double)elapsed_raw_ns * factor);

    // Update bases with total factor
    c->base_rt_ns   += adj_elapsed_ns;
    c->base_mono_ns += adj_elapsed_ns;

    // Bookkeeping: determine how much of that advancement was due to PI frequency
    double    base_factor      = scaledppm_to_factor(c->freq_scaled_ppm);
    double    delta_factor     = factor - base_factor;
    long long applied_phase_ns = (long long)((double)elapsed_raw_ns * delta_factor);

    // Reduce remaining phase by what PI rate has effectively corrected
    if (c->remaining_phase_ns != 0) {
        if (llabs(c->remaining_phase_ns) <= llabs(applied_phase_ns)) {
            c->remaining_phase_ns = 0;
        } else {
            // Sign-aware subtraction
            if (c->remaining_phase_ns > 0)
                c->remaining_phase_ns -= (applied_phase_ns > 0 ? applied_phase_ns : 0);
            else
                c->remaining_phase_ns += (applied_phase_ns < 0 ? applied_phase_ns : 0);
        }
    }

    c->ref_mono_raw = now_raw;
}



// One PI control step. dt_s is the elapsed RAW time since last poll in seconds.
static void swclock_pi_step(SwClock* c, double dt_s) {
    // Error is the remaining phase (seconds). Positive error => need faster time (positive ppm).
    double err_s = (double)c->remaining_phase_ns / 1e9;

    // Integrator
    c->pi_int_error_s += err_s * dt_s;

    // PI output in ppm
    double u_ppm = (SWCLOCK_PI_KP_PPM_PER_S * err_s) + (SWCLOCK_PI_KI_PPM_PER_S2 * c->pi_int_error_s);

    // Clamp
    if (u_ppm > SWCLOCK_PI_MAX_PPM)  u_ppm = SWCLOCK_PI_MAX_PPM;
    if (u_ppm < -SWCLOCK_PI_MAX_PPM) u_ppm = -SWCLOCK_PI_MAX_PPM;

    c->pi_freq_ppm = u_ppm;

    // Anti-windup: if close enough, zero everything
    if (llabs(c->remaining_phase_ns) <= SWCLOCK_PHASE_EPS_NS) {
        c->remaining_phase_ns = 0;
        c->pi_int_error_s     = 0.0;
        c->pi_freq_ppm        = 0.0;
    }
}

// Public poll: advance to now, then do one PI update based on elapsed dt.
void swclock_poll(SwClock* c) {
    if (!c) return;
    pthread_mutex_lock(&c->lock);

    struct timespec before = c->ref_mono_raw;
    
    swclock_rebase_now_and_update(c);

    int64_t dt_ns = ts_to_ns(&c->ref_mono_raw) - ts_to_ns(&before);
    double dt_s = (dt_ns > 0) ? (double)dt_ns / 1e9 : (double)SWCLOCK_POLL_NS / 1e9;

    if (c->pi_servo_enabled) {
        swclock_pi_step(c, dt_s);
    }

    pthread_mutex_unlock(&c->lock);
}

// ================= Public API =================

SwClock* swclock_create(void) {
    SwClock* c = (SwClock*)calloc(1, sizeof(SwClock));
    if (!c) return NULL;

    pthread_mutex_init(&c->lock, NULL);

    clock_gettime(CLOCK_MONOTONIC_RAW, &c->ref_mono_raw);

    struct timespec sys_rt = {0}, sys_mono = {0};
    clock_gettime(CLOCK_REALTIME, &sys_rt);
    clock_gettime(CLOCK_MONOTONIC, &sys_mono);

    c->base_rt_ns   = ts_to_ns(&sys_rt);
    c->base_mono_ns = ts_to_ns(&sys_mono);

    c->freq_scaled_ppm    = 0;
    c->pi_freq_ppm        = 0.0;
    c->pi_int_error_s     = 0.0;
    c->pi_servo_enabled   = true;
    c->remaining_phase_ns = 0;

    c->status   = 0; 
    c->maxerror = 0; 
    c->esterror = 0; 
    c->constant = 0; 
    c->tick     = 0; 
    c->tai      = 0;

    c->stop_flag           = false;
    c->poll_thread_running = true;

    if (pthread_create(&c->poll_thread, NULL, swclock_poll_thread_main, c) != 0) {
        c->poll_thread_running = false;
    }

    return c;
}

void swclock_destroy(SwClock* c) {
    if (!c) return;

    if (c->poll_thread_running) {
        // First, signal the thread to stop
        pthread_mutex_lock(&c->lock);
        c->stop_flag = true;
        pthread_mutex_unlock(&c->lock);
        
        // Wait for thread to exit
        pthread_join(c->poll_thread, NULL);
        
        // Now safely close the log
        swclock_close_log(c);
    }
    
    pthread_mutex_destroy(&c->lock);
    
    free(c);
}

int swclock_gettime(SwClock* c, clockid_t clk_id, struct timespec *tp) {
    
    if (!c || !tp) 
    { 
        errno = EINVAL; 
        return -1;
 }

    if (clk_id == CLOCK_MONOTONIC_RAW) {
        return clock_gettime(CLOCK_MONOTONIC_RAW, tp);
    }

    pthread_mutex_lock(&c->lock);
    swclock_rebase_now_and_update(c);

    int64_t ns;
    switch (clk_id) {
        case CLOCK_REALTIME:
            ns = c->base_rt_ns;
            break;
        case CLOCK_MONOTONIC:
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
    if (clk_id != CLOCK_REALTIME) { errno = EINVAL; return -1; }

    pthread_mutex_lock(&c->lock);
    swclock_rebase_now_and_update(c);
    c->base_rt_ns = (tp->tv_sec < 0) ? 0 : ts_to_ns(tp);
    // When the user sets time explicitly, clear leftover corrections
    c->remaining_phase_ns = 0;
    c->pi_int_error_s = 0.0;
    c->pi_freq_ppm = 0.0;
    pthread_mutex_unlock(&c->lock);
    return 0;
}

int swclock_adjtime(SwClock* c, struct timex *tptr) {
    if (!c || !tptr) { errno = EINVAL; return -1; }

    pthread_mutex_lock(&c->lock);
    swclock_rebase_now_and_update(c);

    unsigned int modes = tptr->modes;

    /* Base frequency bias (Darwin uses same scaled units: ppm * 2^-16) */
    if (modes & ADJ_FREQUENCY) {
        c->freq_scaled_ppm = tptr->freq;
    }

    /* ADJ_OFFSET: SLEW the phase via PI (no immediate step) */
    if (modes & ADJ_OFFSET) {
        long long delta_ns;
        if (modes & ADJ_NANO) {
            delta_ns = (long long)tptr->offset;          // already ns
        } else {
            delta_ns = (long long)tptr->offset * 1000LL; // usec -> ns
        }
        c->remaining_phase_ns += delta_ns;               // PI will work this down
    }

    /* ADJ_SETOFFSET: RELATIVE STEP (immediate)
       macOS headers vary; prefer timex.time if nonzero, else fall back to offset. */
    if (modes & ADJ_SETOFFSET) {
        long long delta_ns = 0;

        /* Try to use timex.time if available & nonzero */
        /* Some Darwin SDKs do declare .time; others don't. If present but zero,
           we still want to accept 'offset' so tests pass in both cases. */
        #define TIMEX_TIME_NONZERO ( (tptr->time.tv_sec != 0) || (tptr->time.tv_usec != 0) )
        #ifdef __APPLE__
        if (TIMEX_TIME_NONZERO) {
            long long tv_nsec;
            if (modes & ADJ_NANO) {
                /* With ADJ_NANO, Linux uses tv_usec to carry nanoseconds */
                tv_nsec = (long long)tptr->time.tv_usec;
            } else {
                tv_nsec = (long long)tptr->time.tv_usec * 1000LL; // usec -> ns
            }
            delta_ns = (long long)tptr->time.tv_sec * 1000000000LL + tv_nsec;
        } else
        #endif
        {
            /* Fallback: treat 'offset' as relative step */
            if (modes & ADJ_NANO) {
                delta_ns = (long long)tptr->offset;          // ns
            } else {
                delta_ns = (long long)tptr->offset * 1000LL; // usec -> ns
            }
        }

        /* Immediate step of REALTIME base; keep PI state & remaining_phase_ns intact */
        c->base_rt_ns += delta_ns;
    }

    /* Optional pass-through of status flags */
    if (modes & ADJ_STATUS) {
        c->status = tptr->status;
    }

    /* Optional: TAI-UTC offset if you use it */
    if (modes & ADJ_TAI) {
        c->tai = tptr->constant;
    }

    /* Readback (adjtimex-like) */
    tptr->status    = c->status;
    tptr->freq      = c->freq_scaled_ppm;
    tptr->maxerror  = c->maxerror;
    tptr->esterror  = c->esterror;
    tptr->constant  = c->constant;
    tptr->precision = 1;
    tptr->tick      = c->tick;
    tptr->tai       = c->tai;

    pthread_mutex_unlock(&c->lock);
    return TIME_OK;
}

void swclock_reset(SwClock* c) {

    /* IMPORTANT: Not thread-safe use pthread_mutex_lock(&c->lock); to call this function */
    if (!c) return;

    clock_gettime(CLOCK_MONOTONIC_RAW, &c->ref_mono_raw);

    struct timespec sys_rt = {0}, sys_mono = {0};
    clock_gettime(CLOCK_REALTIME, &sys_rt);
    clock_gettime(CLOCK_MONOTONIC, &sys_mono);

    c->base_rt_ns   = ts_to_ns(&sys_rt);
    c->base_mono_ns = ts_to_ns(&sys_mono);

    c->freq_scaled_ppm    = 0;
    c->pi_freq_ppm        = 0.0;
    c->pi_int_error_s     = 0.0;
    c->remaining_phase_ns = 0;

    c->status   = 0; 
    c->maxerror = 0; 
    c->esterror = 0; 
    c->constant = 0; 
    c->tick     = 0; 
    c->tai      = 0;

    c->stop_flag           = false;
    c->poll_thread_running = true;

    // c->pi_servo_enabled: Not reset, we respect the previous state
}


// ================= Background thread =================

static void* swclock_poll_thread_main(void* arg) {
    SwClock* c = (SwClock*)arg;
    struct timespec ts = { .tv_sec = 0, .tv_nsec = SWCLOCK_POLL_NS };

    while (1) {
        // Sleep first to avoid a busy loop
        nanosleep(&ts, NULL);

        pthread_mutex_lock(&c->lock);
        bool stop = c->stop_flag;
        pthread_mutex_unlock(&c->lock);
        if (stop) break;

        swclock_poll(c);

        pthread_mutex_lock(&c->lock);
        if (c->log_fp && c->is_logging) {
            swclock_log(c);
        }
        pthread_mutex_unlock(&c->lock);
    }
    return NULL;
}

void swclock_enable_PIServo(SwClock* c)
{
    if (!c) return;

    if (true != c->pi_servo_enabled) {
        pthread_mutex_lock(&c->lock);
        c->pi_servo_enabled = true;
        swclock_reset(c);
        pthread_mutex_unlock(&c->lock);
    }
}


void swclock_disable_PIServo(SwClock* c)
{
    if (!c) return;

    if (true == c->pi_servo_enabled) {
        pthread_mutex_lock(&c->lock);
        c->pi_servo_enabled = false;
        // When disabling PI Servo, clear PI state
        swclock_reset(c);
        pthread_mutex_unlock(&c->lock);
    }
}


bool swclock_is_PIServo_enabled(SwClock* c)
{
    if (!c) return false;
    bool is_enabled = true;

    pthread_mutex_lock(&c->lock);
    is_enabled = c->pi_servo_enabled;
    pthread_mutex_unlock(&c->lock);
    
    return is_enabled;
}


// ================= Logging support =================

void swclock_start_log(SwClock* c, const char* filename) {
    if (!c || !filename) return;

    pthread_mutex_lock(&c->lock);

    c->log_fp = fopen(filename, "w");
    if (!c->log_fp) {
        perror("swclock_start_log: fopen");
        pthread_mutex_unlock(&c->lock);
        return;
    }

    // Get current local date and time
    time_t now = time(NULL);
    struct tm* tinfo = localtime(&now);
    char datetime_buf[64];
    strftime(datetime_buf, sizeof(datetime_buf), "%Y-%m-%d %H:%M:%S", tinfo);

    // Write header with version and timestamp
    fprintf(c->log_fp,
        "# SwClock Log (%s)\n"
        "# Version: %s\n"
        "# Started at: %s\n"
        "# Columns:\n"
        "timestamp_ns,"
        "base_rt_ns,"
        "base_mono_ns,"
        "freq_scaled_ppm,"
        "pi_freq_ppm,"
        "pi_int_error_s,"
        "remaining_phase_ns,"
        "pi_servo_enabled,"
        "maxerror,"
        "esterror,"
        "constant,"
        "tick,"
        "tai\n",
        filename,
        SWCLOCK_VERSION,
        datetime_buf
    );

    fflush(c->log_fp);
    c->is_logging = true;

    pthread_mutex_unlock(&c->lock);
}

void swclock_log(SwClock* c) {
    if (!c || !c->is_logging || !c->log_fp) return;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC_RAW, &now);
    long long now_ns = ts_to_ns(&now);

    fprintf(c->log_fp,
        "%lld,"        // timestamp
        "%" PRId64 "," // base_rt_ns
        "%" PRId64 "," // base_mono_ns
        "%ld,"         // freq_scaled_ppm
        "%.9f,"        // pi_freq_ppm
        "%.9f,"        // pi_int_error_s
        "%lld,"        // remaining_phase_ns
        "%d,"          // pi_servo_enabled
        "%ld,"         // maxerror
        "%ld,"         // esterror
        "%ld,"         // constant
        "%ld,"         // tick
        "%d\n",        // tai
        now_ns,
        c->base_rt_ns,
        c->base_mono_ns,
        c->freq_scaled_ppm,
        c->pi_freq_ppm,
        c->pi_int_error_s,
        c->remaining_phase_ns,
        c->pi_servo_enabled ? 1 : 0,
        c->maxerror,
        c->esterror,
        c->constant,
        c->tick,
        c->tai);

    fflush(c->log_fp);
}

void swclock_close_log(SwClock* c) {
    if (!c) return;
    pthread_mutex_lock(&c->lock);
    if (c->log_fp) {
        fclose(c->log_fp);
        c->log_fp = NULL;
    }
    c->is_logging = false;
    pthread_mutex_unlock(&c->lock);
}


