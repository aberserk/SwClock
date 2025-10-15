#pragma once
#ifdef __cplusplus
extern "C" {
#endif
typedef struct AdaptiveKalmanFilter AdaptiveKalmanFilter;
AdaptiveKalmanFilter* akf_create(void);
void akf_destroy(AdaptiveKalmanFilter*);
void akf_init(AdaptiveKalmanFilter*, double q, double r);
void akf_reset(AdaptiveKalmanFilter*);
double akf_update(AdaptiveKalmanFilter*, double measurement, double dt_s);
void akf_set_noise(AdaptiveKalmanFilter*, double q_offset, double q_drift, double r_measure);
void akf_set_adaptation(AdaptiveKalmanFilter*, double baseQ_drift, double alpha, double beta);
double akf_get_offset(const AdaptiveKalmanFilter*);
double akf_get_drift(const AdaptiveKalmanFilter*);
double akf_get_drift_ppb(const AdaptiveKalmanFilter*);
double akf_get_innovation(const AdaptiveKalmanFilter*);
double akf_get_gain_offset(const AdaptiveKalmanFilter*);
double akf_get_gain_drift(const AdaptiveKalmanFilter*);
unsigned long akf_get_update_count(const AdaptiveKalmanFilter*);
int akf_is_initialized(const AdaptiveKalmanFilter*);
double akf_get_R_adapt(const AdaptiveKalmanFilter*);
double akf_get_Q_offset(const AdaptiveKalmanFilter*);
double akf_get_Q_drift(const AdaptiveKalmanFilter*);
#ifdef __cplusplus
}
#endif

#ifdef DBG
/* DBG accessors */
double akf_dbg_R_floor(const AdaptiveKalmanFilter*);
double akf_dbg_gate(const AdaptiveKalmanFilter*);
double akf_dbg_sigma(const AdaptiveKalmanFilter*);
double akf_dbg_nsig(const AdaptiveKalmanFilter*);

#endif /* DBG */
