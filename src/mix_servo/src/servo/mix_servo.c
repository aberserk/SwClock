#include "mix_servo.h"
#include "akf_servo.h"
#include "pi_servo.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ===== Compile-time knobs =====
 * Default PI gains inherit from pi_servo unless overridden here.
 * You can also enable drift hint mixing by defining MIX_USE_DRIFT_HINT and MIX_DRIFT_HINT_GAIN.
 */
#ifndef MIX_DRIFT_HINT_GAIN
#define MIX_DRIFT_HINT_GAIN   (0.0)     /* set >0.0 only if MIX_USE_DRIFT_HINT is defined */
#endif

typedef struct MixServo {
    AdaptiveKalmanFilter* akf;
    PIServo*              pi;
} MixServo;

static inline double clampd(double v,double lo,double hi){ return v<lo?lo:(v>hi?hi:v); }

MixServo* mix_create(void){
    MixServo* s = (MixServo*)calloc(1, sizeof(MixServo));
    if(!s) return NULL;
    s->akf = akf_create();
    s->pi  = pi_create();
    if(!s->akf || !s->pi){ mix_destroy(s); return NULL; }
    return s;
}

void mix_destroy(MixServo* s){
    if(!s) return;
    if(s->akf) akf_destroy(s->akf);
    if(s->pi)  pi_destroy(s->pi);
    free(s);
}

void mix_init(MixServo* s, double q, double r){
    if(!s) return;
    akf_init(s->akf, q, r);
    pi_init_default_ptpd(s->pi);
}

void mix_reset(MixServo* s){
    if(!s) return;
    akf_reset(s->akf);
    /* Re-init PI to defaults to avoid stale integrator */
    pi_init_default_ptpd(s->pi);
}

void mix_set_noise(MixServo* s, double q_offset, double q_drift, double r_measure){
    if(!s) return;
    akf_set_noise(s->akf, q_offset, q_drift, r_measure);
}
void mix_set_adaptation(MixServo* s, double baseQ_drift, double alpha, double beta){
    if(!s) return;
    akf_set_adaptation(s->akf, baseQ_drift, alpha, beta);
}
void mix_set_pi_gains(MixServo* s, double kp, double ki){
    if(!s) return;
    pi_set_gains(s->pi, kp, ki);
}

double mix_update(MixServo* s, double z, double dt){
    if(!s) return 0.0;
    /* Estimation */
    double xhat = akf_update(s->akf, z, dt);
    double e = xhat;
#ifdef MIX_USE_DRIFT_HINT
    /* Optional: subtract a fraction of AKF drift (in s/s) from the PI control error.
       The hint reduces steady-state bias if the AKF drift is confident. */
    double drift_hint = akf_get_drift(s->akf);
    e -= (MIX_DRIFT_HINT_GAIN) * drift_hint * dt; /* transform to seconds */
#endif
    /* Control */
    (void)pi_update(s->pi, e, dt);
    return xhat;
}

/* Accessors */
double        mix_get_offset(const MixServo* s){ return s? akf_get_offset(s->akf) : 0.0; }
double        mix_get_drift (const MixServo* s){ return s? pi_get_drift(s->pi) : 0.0; }
double        mix_get_drift_ppb(const MixServo* s){ return s? pi_get_drift_ppb(s->pi) : 0.0; }
double        mix_get_innovation(const MixServo* s){ return s? akf_get_innovation(s->akf) : 0.0; }
double        mix_get_gain_offset(const MixServo* s){ return s? akf_get_gain_offset(s->akf) : 0.0; }
double        mix_get_gain_drift (const MixServo* s){ return s? akf_get_gain_drift(s->akf) : 0.0; }
unsigned long mix_get_update_count(const MixServo* s){ return s? akf_get_update_count(s->akf) : 0UL; }
int           mix_is_initialized (const MixServo* s){ return s? akf_is_initialized(s->akf) : 0; }
