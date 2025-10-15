#pragma once
#ifdef __cplusplus
extern "C" {
#endif
typedef struct AdaptiveExtendedKalmanFilter AdaptiveExtendedKalmanFilter;
AdaptiveExtendedKalmanFilter* aekf_create(void);
void aekf_destroy(AdaptiveExtendedKalmanFilter*);
void aekf_init(AdaptiveExtendedKalmanFilter*, double q, double r);
void aekf_reset(AdaptiveExtendedKalmanFilter*);
double aekf_update(AdaptiveExtendedKalmanFilter*, double measurement, double dt_s);
void aekf_set_noise(AdaptiveExtendedKalmanFilter*, double q_offset, double q_drift, double r_measure);
void aekf_set_adaptation(AdaptiveExtendedKalmanFilter*, double baseQ_drift, double alpha, double beta);
void aekf_set_model(AdaptiveExtendedKalmanFilter*,
                    void (*state_fn)(const double x_in[2], double dt, double x_out[2]),
                    void (*meas_fn)(const double x_in[2], double* z_out),
                    void (*jacobian_F)(const double x_in[2], double dt, double F[2][2]),
                    void (*jacobian_H)(const double x_in[2], double H[2]));
double aekf_get_offset(const AdaptiveExtendedKalmanFilter*);
double aekf_get_drift(const AdaptiveExtendedKalmanFilter*);
double aekf_get_drift_ppb(const AdaptiveExtendedKalmanFilter*);
double aekf_get_innovation(const AdaptiveExtendedKalmanFilter*);
double aekf_get_gain_offset(const AdaptiveExtendedKalmanFilter*);
double aekf_get_gain_drift(const AdaptiveExtendedKalmanFilter*);
unsigned long aekf_get_update_count(const AdaptiveExtendedKalmanFilter*);
int aekf_is_initialized(const AdaptiveExtendedKalmanFilter*);
double aekf_get_R_adapt(const AdaptiveExtendedKalmanFilter*);
double aekf_get_Q_offset(const AdaptiveExtendedKalmanFilter*);
double aekf_get_Q_drift(const AdaptiveExtendedKalmanFilter*);
#ifdef __cplusplus
}
#endif

#ifdef DBG
/* DBG accessors */
double aekf_dbg_R_floor(const AdaptiveExtendedKalmanFilter*);
double aekf_dbg_gate(const AdaptiveExtendedKalmanFilter*);
double aekf_dbg_sigma(const AdaptiveExtendedKalmanFilter*);
double aekf_dbg_nsig(const AdaptiveExtendedKalmanFilter*);

#endif /* DBG */
