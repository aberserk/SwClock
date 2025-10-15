#include "aekf_servo.h"
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
static inline double maxd(double a,double b){ return a>b?a:b; }
static inline double mind(double a,double b){ return a<b?a:b; }

/* Default linear model, EKF style */
static void default_f(const double x_in[2], double dt, double x_out[2]){
    x_out[0] = x_in[0] + dt * x_in[1];
    x_out[1] = x_in[1];
}
static void default_h(const double x_in[2], double* z_out){
    *z_out = x_in[0];
}
static void default_F(const double x_in[2], double dt, double F[2][2]){
    (void)x_in; F[0][0]=1.0; F[0][1]=dt; F[1][0]=0.0; F[1][1]=1.0;
}
static void default_H(const double x_in[2], double H[2]){
    (void)x_in; H[0]=1.0; H[1]=0.0;
}

struct AdaptiveExtendedKalmanFilter {
#ifdef DBG
    /* DBG vars */
    double dbg_gate, dbg_sigma, dbg_nsig;
#endif
    double x[2]; double P[2][2]; double Q[2][2];
    double R, R_adapt;
    double K[2]; double innovation; double S; double dt;

    /* adaptation state */
    double alpha, beta;
    double e_mean_fast, e_var_fast;
    double e_mean_slow, e_var_slow;
    double e_prev, corr_lag1, baseQ, prevDrift;

    /* quantization & gating */
    double z_prev, qstep_est_s, R_floor;
    double dt_ewma; int miss_streak;
    int k1_satur_count;

    void (*f)(const double[2], double, double[2]);
    void (*h)(const double[2], double*);
    void (*jacF)(const double[2], double, double[2][2]);
    void (*jacH)(const double[2], double[2]);
    unsigned long updateCount; int initialized;
};

static inline void mm2(const double A[2][2], const double B[2][2], double Rr[2][2]){
    Rr[0][0]=A[0][0]*B[0][0]+A[0][1]*B[1][0];
    Rr[0][1]=A[0][0]*B[0][1]+A[0][1]*B[1][1];
    Rr[1][0]=A[1][0]*B[0][0]+A[1][1]*B[1][0];
    Rr[1][1]=A[1][0]*B[0][1]+A[1][1]*B[1][1];
}
static inline void ma2(const double A[2][2], const double B[2][2], double Rr[2][2]){
    Rr[0][0]=A[0][0]+B[0][0]; Rr[0][1]=A[0][1]+B[0][1]; Rr[1][0]=A[1][0]+B[1][0]; Rr[1][1]=A[1][1]+B[1][1];
}

AdaptiveExtendedKalmanFilter* aekf_create(void){
    AdaptiveExtendedKalmanFilter* e=(AdaptiveExtendedKalmanFilter*)calloc(1,sizeof(*e));
    if(!e) return NULL;
    e->Q[0][0]=1e-9; e->Q[1][1]=1e-10; e->R=1e-6; e->R_adapt=e->R;
    e->P[0][0]=1000.0; e->P[1][1]=100.0;
    e->f=default_f; e->h=default_h; e->jacF=default_F; e->jacH=default_H;
    e->alpha=0.95; e->beta=0.98; e->baseQ=e->Q[1][1];
    e->e_var_fast=e->R; e->e_var_slow=e->R;
    e->qstep_est_s=0.0; e->R_floor=(0.0005*0.0005)/12.0; e->dt_ewma=0.01;
    return e;
}
void aekf_destroy(AdaptiveExtendedKalmanFilter* e){ free(e); }

void aekf_init(AdaptiveExtendedKalmanFilter* e,double q,double r){
    if(!e) return;
    memset(e->x,0,sizeof(e->x));
    e->P[0][0]=1000.0; e->P[0][1]=0.0; e->P[1][0]=0.0; e->P[1][1]=100.0;
    e->Q[0][0]=q; e->Q[0][1]=0.0; e->Q[1][0]=0.0; e->Q[1][1]=q*0.1;
    e->R=r; e->R_adapt=r; e->e_var_fast=r; e->e_var_slow=r;
    e->dt=1.0; e->updateCount=0; e->initialized=0;
    e->e_mean_fast=0.0; e->e_mean_slow=0.0; e->e_prev=0.0; e->corr_lag1=0.0; e->prevDrift=0.0;
    e->z_prev=0.0; e->qstep_est_s=0.0; e->R_floor=fmax((0.0005*0.0005)/12.0, r*0.05);
    e->dt_ewma=0.01; e->miss_streak=0; e->k1_satur_count=0;
}

void aekf_reset(AdaptiveExtendedKalmanFilter* e){
    if(!e) return; double Q00=e->Q[0][0], Q11=e->Q[1][1], R=e->R, baseQ=e->baseQ;
    aekf_init(e,Q00,R); e->Q[1][1]=Q11; e->baseQ=baseQ;
}
void aekf_set_noise(AdaptiveExtendedKalmanFilter* e,double q0,double q1,double r){
    if(!e) return; e->Q[0][0]=q0; e->Q[1][1]=q1; e->R=r;
    if(e->R_adapt<0.01*e->R) e->R_adapt=0.01*e->R;
    if(e->R_adapt>30.0*e->R) e->R_adapt=30.0*e->R;
    e->R_floor = fmax(e->R_floor, 0.05*e->R);
}
void aekf_set_adaptation(AdaptiveExtendedKalmanFilter* e,double baseQ,double alpha,double beta){
    if(!e) return; e->baseQ=baseQ; e->alpha=alpha; e->beta=beta;
}
void aekf_set_model(AdaptiveExtendedKalmanFilter* e,
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

static void update_quant_floor(struct AdaptiveExtendedKalmanFilter* e, double z){
    if(e->updateCount<=1){ e->z_prev=z; return; }
    double dz = fabs(z - e->z_prev);
    e->z_prev = z;
    if (dz > 0.02) dz = 0.02;
    if (e->qstep_est_s == 0.0) e->qstep_est_s = dz;
    else e->qstep_est_s = 0.98*e->qstep_est_s + 0.02*dz;
    double floor_from_quant = (e->qstep_est_s*e->qstep_est_s)/12.0;
    e->R_floor = fmax(e->R_floor, fmax(floor_from_quant, 0.05*e->R));
}

static void adapt_R(struct AdaptiveExtendedKalmanFilter* e){
    double ev = e->innovation;
    /* fast tracker */
    double alpha_f=0.7, beta_f=0.85;
    e->e_mean_fast = alpha_f*e->e_mean_fast + (1.0-alpha_f)*ev;
    double devf = ev - e->e_mean_fast;
    e->e_var_fast  = beta_f*e->e_var_fast + (1.0-beta_f)*(devf*devf);
    /* slow tracker */
    e->e_mean_slow = e->alpha*e->e_mean_slow + (1.0-e->alpha)*ev;
    double devs = ev - e->e_mean_slow;
    e->e_var_slow  = e->beta*e->e_var_slow + (1.0-e->beta)*(devs*devs);
    double blended = 0.7*e->e_var_slow + 0.3*e->e_var_fast;
    e->R_adapt = clampd(blended, e->R_floor, 30.0*e->R);
}

static void adapt_Q_and_corr(struct AdaptiveExtendedKalmanFilter* e){
    double ev=e->innovation;
    double denom = sqrt((e->e_var_slow+1e-18)*(e->e_var_slow+1e-18));
    double corr_inst = (denom>0)? (e->e_prev*ev)/denom : 0.0;
    e->corr_lag1 = 0.95*e->corr_lag1 + 0.05*corr_inst;
    e->e_prev = ev;

    int saturated = (e->K[1] >= 0.25-1e-9);
    if (saturated) e->k1_satur_count++; else e->k1_satur_count = (e->k1_satur_count>0)?e->k1_satur_count-1:0;

    if(e->updateCount>20){
        double ddrift = fabs(e->x[1]-e->prevDrift); e->prevDrift=e->x[1];
        if(e->corr_lag1>0.25 || ddrift>5e-9 || e->k1_satur_count>6){
            e->Q[1][1]=mind(e->Q[1][1]*1.05 + 0.5*e->baseQ, e->baseQ*20.0);
        } else if(e->corr_lag1<0.05 && ddrift<1e-10 && e->k1_satur_count==0){
            e->Q[1][1]=maxd(e->Q[1][1]*0.995, e->baseQ*0.25);
        }
        if(e->corr_lag1>0.35) e->Q[0][0]=mind(e->Q[0][0]*1.02 + 0.2*e->Q[1][1], 50.0*e->R);
        else e->Q[0][0]=maxd(e->Q[0][0]*0.997, 0.1*e->R);
    }
}

double aekf_update(AdaptiveExtendedKalmanFilter* e,double z_meas,double dt){
    if(!e) return 0.0;
    e->dt=dt; e->updateCount++;

    if (e->updateCount==1) e->dt_ewma = dt>0?dt:0.01;
    e->dt_ewma = 0.98*e->dt_ewma + 0.02*(dt>0?dt:e->dt_ewma);
    int miss = (dt > 1.8*e->dt_ewma) ? 1 : 0;
    e->miss_streak = miss ? (e->miss_streak+1) : 0;

    if(!e->initialized){ e->x[0]=z_meas; e->x[1]=0.0; e->initialized=1; e->e_prev=0.0; e->z_prev=z_meas; return e->x[0]; }

    /* predict */
    double x_pred[2]; e->f(e->x, dt, x_pred);
    double F[2][2]; e->jacF(e->x, dt, F);
    const double FT[2][2]={{F[0][0],F[1][0]},{F[0][1],F[1][1]}};
    double FP[2][2], FPFT[2][2];
    mm2(F, e->P, FP); mm2(FP, FT, FPFT); ma2(FPFT, e->Q, e->P);

    /* update */
    double z_pred; e->h(x_pred, &z_pred);
    double H[2]; e->jacH(x_pred, H);
    e->innovation = z_meas - z_pred;

    update_quant_floor(e, z_meas);
    adapt_R(e);
    if (e->miss_streak>0) {
        for (int i=0;i<e->miss_streak;i++) e->R_adapt = mind(e->R_adapt*1.15, 30.0*e->R);
    }

    e->S = e->P[0][0]*H[0]*H[0] + (e->P[0][1]+e->P[1][0])*H[0]*H[1] + e->P[1][1]*H[1]*H[1] + e->R_adapt;

    /* gating */
    double base_gate = (e->miss_streak>0)?4.0:3.5;
    double sigma=sqrt(fabs(e->S)), gscale=1.0;
    if(sigma>0){ double nsig=fabs(e->innovation)/sigma; if(nsig>base_gate) gscale=clampd(base_gate/nsig, 0.2, 1.0); }

    if (fabs(e->S) > 1e-18){
        double PHt0=e->P[0][0]*H[0] + e->P[0][1]*H[1];
        double PHt1=e->P[1][0]*H[0] + e->P[1][1]*H[1];
        e->K[0]=(PHt0/e->S)*gscale; e->K[1]=(PHt1/e->S)*gscale;
        double K0max=(e->innovation>=0.0)?0.45:0.60;
        e->K[0]=clampd(e->K[0],0.0,K0max);
        e->K[1]=clampd(e->K[1],0.0,0.25);
    } else { e->K[0]=e->K[1]=0.0; }

    /* offset-first update */
    e->x[0] = x_pred[0] + e->K[0]*e->innovation;
    double innov2 = z_meas - e->x[0];
    e->x[1] = x_pred[1] + e->K[1]*innov2;

    /* conditional drift decay */
    if (e->miss_streak>0 || e->updateCount>80) e->x[1]*=0.998;

    /* covariance */
    double KH00=e->K[0]*H[0], KH01=e->K[0]*H[1], KH10=e->K[1]*H[0], KH11=e->K[1]*H[1];
    double I_KH[2][2]={{1.0-KH00,-KH01},{-KH10,1.0-KH11}}; double newP[2][2]; mm2(I_KH,e->P,newP);
    e->P[0][0]=newP[0][0]; e->P[0][1]=newP[0][1]; e->P[1][0]=newP[1][0]; e->P[1][1]=newP[1][1];

    adapt_Q_and_corr(e);
    return e->x[0];
}

/* Accessors */
double        aekf_get_offset(const AdaptiveExtendedKalmanFilter* e){ return e?e->x[0]:0.0; }
double        aekf_get_drift (const AdaptiveExtendedKalmanFilter* e){ return e?e->x[1]:0.0; }
double        aekf_get_drift_ppb(const AdaptiveExtendedKalmanFilter* e){ return e?e->x[1]*1e9:0.0; }
double        aekf_get_innovation(const AdaptiveExtendedKalmanFilter* e){ return e?e->innovation:0.0; }
double        aekf_get_gain_offset(const AdaptiveExtendedKalmanFilter* e){ return e?e->K[0]:0.0; }
double        aekf_get_gain_drift (const AdaptiveExtendedKalmanFilter* e){ return e?e->K[1]:0.0; }
unsigned long aekf_get_update_count(const AdaptiveExtendedKalmanFilter* e){ return e?e->updateCount:0UL; }
int           aekf_is_initialized (const AdaptiveExtendedKalmanFilter* e){ return e?e->initialized:0; }
double aekf_get_R_adapt(const AdaptiveExtendedKalmanFilter* e){ return e?e->R_adapt:0.0; }
double aekf_get_Q_offset(const AdaptiveExtendedKalmanFilter* e){ return e?e->Q[0][0]:0.0; }
double aekf_get_Q_drift (const AdaptiveExtendedKalmanFilter* e){ return e?e->Q[1][1]:0.0; }

#ifdef DBG
double aekf_dbg_R_floor(const AdaptiveExtendedKalmanFilter* e){ return e?e->R_floor:0.0; }
double aekf_dbg_gate(const AdaptiveExtendedKalmanFilter* e){ return e?e->dbg_gate:0.0; }
double aekf_dbg_sigma(const AdaptiveExtendedKalmanFilter* e){ return e?e->dbg_sigma:0.0; }
double aekf_dbg_nsig(const AdaptiveExtendedKalmanFilter* e){ return e?e->dbg_nsig:0.0; }
#endif
