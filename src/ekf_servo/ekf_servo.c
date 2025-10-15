#include "ekf_servo.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

#ifndef DBG
#define DBG(...)  do {} while(0)
#endif
#ifndef DBGV
#define DBGV(...) do {} while(0)
#endif

static inline double clampd(double v,double lo,double hi){ return v<lo?lo:(v>hi?hi:v); }

/* Default linear model */
static void default_f(const double x_in[2], double dt, double x_out[2]){
    x_out[0] = x_in[0] + dt * x_in[1];
    x_out[1] = x_in[1];
}
static void default_h(const double x_in[2], double* z_out){
    *z_out = x_in[0];
}
static void default_F(const double x_in[2], double dt, double F[2][2]){
    (void)x_in;
    F[0][0]=1.0; F[0][1]=dt;
    F[1][0]=0.0; F[1][1]=1.0;
}
static void default_H(const double x_in[2], double H[2]){
    (void)x_in;
    H[0]=1.0; H[1]=0.0;
}

struct ExtendedKalmanFilter {
#ifdef DBG
    /* DBG vars */
    double dbg_R_eff, dbg_gate, dbg_sigma, dbg_nsig;
#endif
    /* State */
    double x[2];       /* [offset, drift] */
    double P[2][2];    /* covariance */
    double Q[2][2];    /* process noise */
    double R;          /* measurement noise */

    /* gains & scalars */
    double K[2];
    double innovation;
    double S;
    double dt;

    /* quantization & gating helpers */
    double z_prev, qstep_est_s, R_floor;
    double dt_ewma;
    int    miss_streak;

    /* model callbacks */
    void (*f)(const double x_in[2], double dt, double x_out[2]);
    void (*h)(const double x_in[2], double* z_out);
    void (*jacF)(const double x_in[2], double dt, double F[2][2]);
    void (*jacH)(const double x_in[2], double H[2]);

    unsigned long updateCount;
    int initialized;
};

static inline void mm2(const double A[2][2], const double B[2][2], double Rr[2][2]){
    Rr[0][0]=A[0][0]*B[0][0]+A[0][1]*B[1][0];
    Rr[0][1]=A[0][0]*B[0][1]+A[0][1]*B[1][1];
    Rr[1][0]=A[1][0]*B[0][0]+A[1][1]*B[1][0];
    Rr[1][1]=A[1][0]*B[0][1]+A[1][1]*B[1][1];
}
static inline void ma2(const double A[2][2], const double B[2][2], double Rr[2][2]){
    Rr[0][0]=A[0][0]+B[0][0]; Rr[0][1]=A[0][1]+B[0][1];
    Rr[1][0]=A[1][0]+B[1][0]; Rr[1][1]=A[1][1]+B[1][1];
}

ExtendedKalmanFilter* ekf_create(void){
    ExtendedKalmanFilter* e=(ExtendedKalmanFilter*)calloc(1,sizeof(*e));
    if(!e) return NULL;
    e->Q[0][0]=1e-9; e->Q[1][1]=1e-10; e->R=1e-6;
    e->P[0][0]=1000.0; e->P[1][1]=100.0;
    e->f=default_f; e->h=default_h; e->jacF=default_F; e->jacH=default_H;
    e->qstep_est_s=0.0; e->R_floor=(0.0005*0.0005)/12.0; e->dt_ewma=0.01;
    return e;
}
void ekf_destroy(ExtendedKalmanFilter* e){ free(e); }

void ekf_init(ExtendedKalmanFilter* e,double q,double r){
    if(!e) return;
    memset(e->x,0,sizeof(e->x));
    e->P[0][0]=1000.0; e->P[0][1]=0.0; e->P[1][0]=0.0; e->P[1][1]=100.0;
    e->Q[0][0]=q; e->Q[0][1]=0.0; e->Q[1][0]=0.0; e->Q[1][1]=q*0.1;
    e->R=r; e->dt=1.0; e->updateCount=0; e->initialized=0;
    e->qstep_est_s=0.0; e->R_floor=fmax((0.0005*0.0005)/12.0, r*0.05); e->dt_ewma=0.01; e->miss_streak=0;
}

void ekf_reset(ExtendedKalmanFilter* e){
    if(!e) return; double Q00=e->Q[0][0], Q11=e->Q[1][1], R=e->R;
    ekf_init(e,Q00,R); e->Q[1][1]=Q11;
}
void ekf_set_noise(ExtendedKalmanFilter* e,double q0,double q1,double r){
    if(!e) return; e->Q[0][0]=q0; e->Q[1][1]=q1; e->R=r; e->R_floor=fmax(e->R_floor, 0.05*r);
}
void ekf_set_model(ExtendedKalmanFilter* e,
                   void (*state_fn)(const double[2], double, double[2]),
                   void (*meas_fn)(const double[2], double*),
                   void (*jacobian_F)(const double[2], double, double[2][2]),
                   void (*jacobian_H)(const double[2], double[2])){
    if(!e) return;
    e->f = state_fn ? state_fn : default_f;
    e->h = meas_fn  ? meas_fn  : default_h;
    e->jacF = jacobian_F ? jacobian_F : default_F;
    e->jacH = jacobian_H ? jacobian_H : default_H;
}

static void update_quant_floor(struct ExtendedKalmanFilter* e, double z){
    if(e->updateCount<=1){ e->z_prev=z; return; }
    double dz = fabs(z - e->z_prev);
    e->z_prev = z;
    if (dz > 0.02) dz = 0.02;
    if (e->qstep_est_s == 0.0) e->qstep_est_s = dz;
    else e->qstep_est_s = 0.98*e->qstep_est_s + 0.02*dz;
    double floor_from_quant = (e->qstep_est_s*e->qstep_est_s)/12.0;
    e->R_floor = fmax(e->R_floor, fmax(floor_from_quant, 0.05*e->R));
}

double ekf_update(ExtendedKalmanFilter* e,double z_meas,double dt){
    if(!e) return 0.0;
    e->dt=dt; e->updateCount++;

    /* holdover heuristic via dt */
    if (e->updateCount==1) e->dt_ewma = dt>0?dt:0.01;
    e->dt_ewma = 0.98*e->dt_ewma + 0.02*(dt>0?dt:e->dt_ewma);
    int miss = (dt > 1.8*e->dt_ewma) ? 1 : 0;
    e->miss_streak = miss ? (e->miss_streak+1) : 0;

    if(!e->initialized){
        e->x[0]=z_meas; e->x[1]=0.0; e->initialized=1; e->z_prev=z_meas; return e->x[0];
    }
    /* Predict */
    double x_pred[2]; e->f(e->x, dt, x_pred);
    double F[2][2]; e->jacF(e->x, dt, F);
    const double FT[2][2]={{F[0][0],F[1][0]},{F[0][1],F[1][1]}};
    double FP[2][2], FPFT[2][2];
    mm2(F, e->P, FP); mm2(FP, FT, FPFT); ma2(FPFT, e->Q, e->P);

    /* Update */
    double z_pred; e->h(x_pred, &z_pred);
    double H[2]; e->jacH(x_pred, H);
    e->innovation = z_meas - z_pred;
    update_quant_floor(e, z_meas);

    /* Use quantization-aware floor inside S (no API change) */
    double R_eff = fmax(e->R, e->R_floor);
#ifdef DBG
    e->dbg_R_eff = R_eff;
#endif
    /* Tolerate bursts after gaps by inflating R_eff a bit while in miss_streak */
    for (int i=0;i<e->miss_streak;i++) R_eff = fmin(R_eff*1.3, 30.0*e->R);

    e->S = e->P[0][0]*H[0]*H[0] + (e->P[0][1]+e->P[1][0])*H[0]*H[1] + e->P[1][1]*H[1]*H[1] + R_eff;

    /* Dynamic gating & asymmetric clamp like AKF */
    double base_gate = (e->miss_streak>0)?4.5:3.5;
    #ifdef DBG
    e->dbg_sigma = sqrt(fabs(e->S));
#endif
    double sigma = sqrt(fabs(e->S)), gscale=1.0;
    if (sigma>0){ double nsig=fabs(e->innovation)/sigma;
#ifdef DBG
        e->dbg_nsig = nsig; e->dbg_gate = base_gate;
#endif
        if(nsig>base_gate) gscale=fmax(0.2, base_gate/nsig); 
    }

    if (fabs(e->S) > 1e-18){
        double PHt[2] = { e->P[0][0]*H[0] + e->P[0][1]*H[1],
                          e->P[1][0]*H[0] + e->P[1][1]*H[1] };
        e->K[0] = (PHt[0] / e->S) * gscale;
        e->K[1] = (PHt[1] / e->S) * gscale;
        double K0max = (e->innovation>=0.0)?0.45:0.60;
        e->K[0] = clampd(e->K[0], 0.0, K0max);
        e->K[1] = clampd(e->K[1], 0.0, 0.25);
    } else {
        e->K[0]=e->K[1]=0.0;
    }

    /* Offset-first update */
    e->x[0] = x_pred[0] + e->K[0]*e->innovation;
    double innov2 = z_meas - e->x[0];
    e->x[1] = x_pred[1] + e->K[1]*innov2;

    /* Conditional drift decay on gaps or after long run */
    if (e->miss_streak>0 || e->updateCount>80) e->x[1] *= 0.998;

    /* Covariance */
    double KH00 = e->K[0]*H[0];
    double KH01 = e->K[0]*H[1];
    double KH10 = e->K[1]*H[0];
    double KH11 = e->K[1]*H[1];
    double I_KH[2][2]={{1.0-KH00, -KH01},{-KH10, 1.0-KH11}};
    double newP[2][2];
    mm2(I_KH, e->P, newP);
    e->P[0][0]=newP[0][0]; e->P[0][1]=newP[0][1];
    e->P[1][0]=newP[1][0]; e->P[1][1]=newP[1][1];

    return e->x[0];
}

/* Accessors */
double        ekf_get_offset(const ExtendedKalmanFilter* e){ return e?e->x[0]:0.0; }
double        ekf_get_drift (const ExtendedKalmanFilter* e){ return e?e->x[1]:0.0; }
double        ekf_get_drift_ppb(const ExtendedKalmanFilter* e){ return e?e->x[1]*1e9:0.0; }
double        ekf_get_innovation(const ExtendedKalmanFilter* e){ return e?e->innovation:0.0; }
double        ekf_get_gain_offset(const ExtendedKalmanFilter* e){ return e?e->K[0]:0.0; }
double        ekf_get_gain_drift (const ExtendedKalmanFilter* e){ return e?e->K[1]:0.0; }
unsigned long ekf_get_update_count(const ExtendedKalmanFilter* e){ return e?e->updateCount:0UL; }
int           ekf_is_initialized (const ExtendedKalmanFilter* e){ return e?e->initialized:0; }

#ifdef DBG
double ekf_dbg_R_eff(const ExtendedKalmanFilter* e){ return e?e->dbg_R_eff:0.0; }
double ekf_dbg_R_floor(const ExtendedKalmanFilter* e){ return e?e->R_floor:0.0; }
double ekf_dbg_gate(const ExtendedKalmanFilter* e){ return e?e->dbg_gate:0.0; }
double ekf_dbg_sigma(const ExtendedKalmanFilter* e){ return e?e->dbg_sigma:0.0; }
double ekf_dbg_nsig(const ExtendedKalmanFilter* e){ return e?e->dbg_nsig:0.0; }
#endif
