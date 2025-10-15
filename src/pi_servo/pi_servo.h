#pragma once
#ifdef __cplusplus
extern "C" {
#endif

typedef struct PIServo PIServo;

/* Create / destroy */
PIServo* pi_create(void);
void     pi_destroy(PIServo* s);

/* Initialize with proportional (kp) and integral (ki) gains.
 * Gains are per-second; controller integrates over dt internally.
 * If unsure, use pi_init_default_ptpd() to apply PTPd-like defaults.
 */
void     pi_init(PIServo* s, double kp, double ki);

/* Apply typical PTPd defaults (KP, KI), scaled by dt internally.
 * Defaults here use commonly deployed PTPd-like gains:
 *   KP = 0.1, KI = 0.001 (per second)
 * You can override at runtime with pi_set_gains or at compile time by defining:
 *   -DPTPD_PI_KP=<value> -DPTPD_PI_KI=<value>
 */
void     pi_init_default_ptpd(PIServo* s);

/* Optional runtime tuning */
void     pi_set_gains(PIServo* s, double kp, double ki);

/* Update with a new offset measurement z (seconds), and elapsed dt (seconds).
 * Returns the estimated offset after applying the correction term internally.
 */
double   pi_update(PIServo* s, double z, double dt);

/* Accessors */
double   pi_get_offset(const PIServo* s);
double   pi_get_drift(const PIServo* s);       /* seconds/second */
double   pi_get_drift_ppb(const PIServo* s);   /* parts-per-billion */

#ifdef __cplusplus
}
#endif
