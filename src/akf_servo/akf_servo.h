#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct AdaptiveKalmanFilter AdaptiveKalmanFilter;
AdaptiveKalmanFilter* akf_create(void);
void                   akf_destroy(AdaptiveKalmanFilter*);
void akf_init(AdaptiveKalmanFilter* akf, double processNoise, double measurementNoise);
void akf_reset(AdaptiveKalmanFilter* akf);
double akf_update(AdaptiveKalmanFilter* akf, double measurement, double dt_s);
void akf_set_noise(AdaptiveKalmanFilter* akf, double q_offset, double q_drift, double r_measure);
void akf_set_adaptation(AdaptiveKalmanFilter* akf, double baseQ_drift, double alpha, double beta);
double        akf_get_offset(const AdaptiveKalmanFilter* akf);
double        akf_get_drift(const AdaptiveKalmanFilter* akf);
double        akf_get_drift_ppb(const AdaptiveKalmanFilter* akf);
double        akf_get_innovation(const AdaptiveKalmanFilter* akf);
double        akf_get_gain_offset(const AdaptiveKalmanFilter* akf);
double        akf_get_gain_drift(const AdaptiveKalmanFilter* akf);
unsigned long akf_get_update_count(const AdaptiveKalmanFilter* akf);
int           akf_is_initialized(const AdaptiveKalmanFilter* akf);
double akf_get_R_adapt(const AdaptiveKalmanFilter* akf);
double akf_get_Q_offset(const AdaptiveKalmanFilter* akf);
double akf_get_Q_drift (const AdaptiveKalmanFilter* akf);
#ifdef __cplusplus
}
#endif
