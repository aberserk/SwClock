#pragma once
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ExtendedKalmanFilter ExtendedKalmanFilter;
ExtendedKalmanFilter* ekf_create(void);
void ekf_destroy(ExtendedKalmanFilter*);
void ekf_init(ExtendedKalmanFilter*, double q, double r);
void ekf_reset(ExtendedKalmanFilter*);
double ekf_update(ExtendedKalmanFilter*, double measurement, double dt_s);
void ekf_set_noise(ExtendedKalmanFilter*, double q_offset, double q_drift, double r_measure);
void ekf_set_model(ExtendedKalmanFilter*,
                   void (*state_fn)(const double x_in[2], double dt, double x_out[2]),
                   void (*meas_fn)(const double x_in[2], double* z_out),
                   void (*jacobian_F)(const double x_in[2], double dt, double F[2][2]),
                   void (*jacobian_H)(const double x_in[2], double H[2]));
double ekf_get_offset(const ExtendedKalmanFilter*);
double ekf_get_drift(const ExtendedKalmanFilter*);
double ekf_get_drift_ppb(const ExtendedKalmanFilter*);
double ekf_get_innovation(const ExtendedKalmanFilter*);
double ekf_get_gain_offset(const ExtendedKalmanFilter*);
double ekf_get_gain_drift(const ExtendedKalmanFilter*);
unsigned long ekf_get_update_count(const ExtendedKalmanFilter*);
int ekf_is_initialized(const ExtendedKalmanFilter*);
#ifdef __cplusplus
}
#endif

#ifdef DBG
/* DBG accessors */
double ekf_dbg_R_eff(const ExtendedKalmanFilter*);
double ekf_dbg_R_floor(const ExtendedKalmanFilter*);
double ekf_dbg_gate(const ExtendedKalmanFilter*);
double ekf_dbg_sigma(const ExtendedKalmanFilter*);
double ekf_dbg_nsig(const ExtendedKalmanFilter*);

#endif /* DBG */
