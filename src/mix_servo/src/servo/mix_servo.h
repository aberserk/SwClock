#pragma once
#ifdef __cplusplus
extern "C" {
#endif

typedef struct MixServo MixServo;

/* Lifecycle */
MixServo* mix_create(void);
void      mix_destroy(MixServo* s);

/* Initialize with AKF (q,r) like other filters; PI uses ptpd-like defaults (compile-time overridable). */
void      mix_init(MixServo* s, double q, double r);
void      mix_reset(MixServo* s);

/* Update with a new offset measurement (seconds) and elapsed time dt (seconds).
 * Returns the AKF-estimated offset after the update.
 */
double    mix_update(MixServo* s, double measurement_s, double dt_s);

/* Noise/adaptation (pass-through to AKF) */
void      mix_set_noise(MixServo* s, double q_offset, double q_drift, double r_measure);
void      mix_set_adaptation(MixServo* s, double baseQ_drift, double alpha, double beta);

/* Optional: runtime PI tuning (kept to one call to stay API-lite) */
void      mix_set_pi_gains(MixServo* s, double kp, double ki);

/* Accessors: offset from AKF; drift from PI */
double        mix_get_offset(const MixServo* s);
double        mix_get_drift(const MixServo* s);        /* seconds/second */
double        mix_get_drift_ppb(const MixServo* s);    /* parts-per-billion */
double        mix_get_innovation(const MixServo* s);   /* AKF innovation */
double        mix_get_gain_offset(const MixServo* s);  /* AKF K0 */
double        mix_get_gain_drift (const MixServo* s);  /* AKF K1 */
unsigned long mix_get_update_count(const MixServo* s);
int           mix_is_initialized (const MixServo* s);

#ifdef __cplusplus
}
#endif
