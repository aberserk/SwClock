#include "pi_servo.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ======= PTPd-like defaults & behavioral knobs (override via -D...) ======= */
#ifndef PTPD_PI_KP
#define PTPD_PI_KP           (0.1)      /* proportional (per second) */
#endif
#ifndef PTPD_PI_KI
#define PTPD_PI_KI           (0.001)    /* integral (per second) */
#endif
#ifndef PTPD_PI_DEADZONE_US
#define PTPD_PI_DEADZONE_US  (20.0)     /* ignore tiny offsets (±20 µs) */
#endif
#ifndef PTPD_PI_MAX_PPB_STEP
#define PTPD_PI_MAX_PPB_STEP (50.0)     /* max freq change per update (ppb) */
#endif
#ifndef PTPD_PI_MAX_PPB_ABS
#define PTPD_PI_MAX_PPB_ABS  (200.0)    /* absolute freq clamp (ppb) */
#endif
#ifndef PTPD_PI_SYNC_REF_S
#define PTPD_PI_SYNC_REF_S   (1.0)      /* sync interval reference (s) */
#endif
#ifndef PTPD_PI_LOCK_TAU_S
#define PTPD_PI_LOCK_TAU_S   (15.0)     /* after lock time, reduce gains */
#endif
#ifndef PTPD_PI_LOCK_SCALE
#define PTPD_PI_LOCK_SCALE   (0.6)      /* scale gains after lock */
#endif
#ifndef PTPD_PI_INT_CLAMP_S
#define PTPD_PI_INT_CLAMP_S  (0.25)     /* integral accumulator clamp (seconds) */
#endif
#ifndef PTPD_PI_HOLD_DECAY
#define PTPD_PI_HOLD_DECAY   (0.998)    /* drift decay during holdover */
#endif

typedef struct PIServo {
    double kp;
    double ki;
    double x_offset;      /* estimated offset (s) */
    double x_drift;       /* estimated drift (s/s) */
    double integ;         /* integral of offset (s) */
    double t_locked;      /* time under deadzone (s) */
    int    initialized;

    /* dt statistics for holdover detection */
    double dt_ewma;
} PIServo;

static inline double clampd(double v, double lo, double hi){
    return v<lo?lo:(v>hi?hi:v);
}

PIServo* pi_create(void){
    PIServo* s = (PIServo*)calloc(1, sizeof(PIServo));
    return s;
}
void pi_destroy(PIServo* s){ free(s); }

void pi_init(PIServo* s, double kp, double ki){
    if(!s) return;
    s->kp = kp;
    s->ki = ki;
    s->x_offset = 0.0;
    s->x_drift = 0.0;
    s->integ = 0.0;
    s->t_locked = 0.0;
    s->dt_ewma = 0.01;
    s->initialized = 0;
}

void pi_init_default_ptpd(PIServo* s){
    if(!s) return;
    pi_init(s, PTPD_PI_KP, PTPD_PI_KI);
}

void pi_set_gains(PIServo* s, double kp, double ki){
    if(!s) return;
    s->kp = kp;
    s->ki = ki;
}

/* PI update with PTPd-inspired protections:
 * - deadzone on small offsets
 * - sync-interval scaling (dt / PTPD_PI_SYNC_REF_S)
 * - step limit per update and absolute clamp in ppb
 * - anti-windup integral clamp and back-calculation when saturated
 * - holdover: if dt spikes (missed sync), decay drift slightly
 */
double pi_update(PIServo* s, double z, double dt){
    if(!s) return 0.0;
    if (dt <= 0.0) dt = 1e-3;
    if(!s->initialized){
        s->x_offset = z;
        s->x_drift = 0.0;
        s->integ = 0.0;
        s->t_locked = 0.0;
        s->dt_ewma = dt;
        s->initialized = 1;
        return s->x_offset;
    }

    /* EWMA dt for holdover detection */
    s->dt_ewma = 0.98*s->dt_ewma + 0.02*dt;
    int holdover = (dt > 1.8*s->dt_ewma) ? 1 : 0;

    /* Deadzone */
    double e = z;
    const double deadzone = PTPD_PI_DEADZONE_US * 1e-6;
    if (fabs(e) < deadzone) {
        e = 0.0;
        s->t_locked += dt;
    } else {
        s->t_locked = 0.0;
    }

    /* Sync-interval scaling for gains */
    double scale = dt / PTPD_PI_SYNC_REF_S;
    double kp_eff = s->kp * scale;
    double ki_eff = s->ki * scale;

    /* Reduce gains after stable lock duration */
    if (s->t_locked > PTPD_PI_LOCK_TAU_S) {
        kp_eff *= PTPD_PI_LOCK_SCALE;
        ki_eff *= PTPD_PI_LOCK_SCALE;
    }

    /* Integrate with clamp (anti-windup) */
    s->integ += e * dt;
    s->integ = clampd(s->integ, -PTPD_PI_INT_CLAMP_S, +PTPD_PI_INT_CLAMP_S);

    /* Control effort in drift units (s/s) */
    double u = kp_eff * e + ki_eff * s->integ;

    /* Step limit per update (convert ppb step to s/s) */
    double max_step = PTPD_PI_MAX_PPB_STEP * 1e-9;
    double before = s->x_drift;
    double after  = s->x_drift + u;
    double step   = clampd(after - before, -max_step, +max_step);
    double u_sat  = step; /* actual applied change in s/s */

    /* Anti-windup back-calculation: if saturated, bleed integral a bit */
    double sat_err = (u - u_sat);
    if (fabs(sat_err) > 0.0 && ki_eff > 0.0){
        /* reduce integral so that ki_eff * integ compensates sat_err gradually */
        double bleed = sat_err / fmax(ki_eff, 1e-12);
        /* don't over-correct integral; apply small fraction */
        s->integ -= 0.2 * bleed;
        s->integ = clampd(s->integ, -PTPD_PI_INT_CLAMP_S, +PTPD_PI_INT_CLAMP_S);
    }

    s->x_drift += u_sat;

    /* Absolute ppb clamp */
    double abs_ppb = s->x_drift * 1e9;
    if (abs_ppb >  PTPD_PI_MAX_PPB_ABS) s->x_drift =  PTPD_PI_MAX_PPB_ABS * 1e-9;
    if (abs_ppb < -PTPD_PI_MAX_PPB_ABS) s->x_drift = -PTPD_PI_MAX_PPB_ABS * 1e-9;

    /* Holdover: decay drift slowly if dt indicates missed syncs */
    if (holdover) s->x_drift *= PTPD_PI_HOLD_DECAY;

    /* Report latest measured offset */
    s->x_offset = z;
    return s->x_offset;
}

double pi_get_offset(const PIServo* s){ return s ? s->x_offset : 0.0; }
double pi_get_drift (const PIServo* s){ return s ? s->x_drift  : 0.0; }
double pi_get_drift_ppb(const PIServo* s){ return s ? s->x_drift*1e9 : 0.0; }
