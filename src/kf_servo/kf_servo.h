#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque type — implementation detail hidden from callers */
typedef struct KalmanFilter KalmanFilter;

/* Lifecycle */
KalmanFilter* kf_create(void);           /* mallocs & sets safe defaults */
void          kf_destroy(KalmanFilter*); /* frees; safe on NULL */

/* Initialize / reset */
void kf_init(KalmanFilter* kf, double processNoise, double measurementNoise);
/* Resets state & covariance; preserves current Q/R/adaptation knobs */
void kf_reset(KalmanFilter* kf);

/* One step (predict + update); returns filtered offset estimate */
double kf_update(KalmanFilter* kf, double measurement, double dt_s);

/* Configuration (optional) */
void kf_set_noise(KalmanFilter* kf,
                  double q_offset,     /* Q00 */
                  double q_drift,      /* Q11 */
                  double r_measure);   /* R (nominal) */

void kf_set_adaptation(KalmanFilter* kf,
                       double baseQ_drift, /* baseline Q11 for slow adaptation */
                       double alpha,       /* kept for API compatibility */
                       double beta);       /* kept for API compatibility */

/* Read-only accessors (no internal fields exposed) */
double        kf_get_offset(const KalmanFilter* kf);        /* seconds */
double        kf_get_drift(const KalmanFilter* kf);         /* s/s */
double        kf_get_drift_ppb(const KalmanFilter* kf);     /* ppb */
double        kf_get_innovation(const KalmanFilter* kf);    /* seconds */
double        kf_get_gain_offset(const KalmanFilter* kf);   /* K0 */
double        kf_get_gain_drift(const KalmanFilter* kf);    /* K1 */
unsigned long kf_get_update_count(const KalmanFilter* kf);
int           kf_is_initialized(const KalmanFilter* kf);    /* 0/1 */

#ifdef __cplusplus
}
#endif
