#include "akf_servo.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

#ifndef DBG
#define DBG(...)  do {} while(0)
#endif
#ifndef DBGV
#define DBGV(...) do {} while(0)
#endif

/* Utility */
static inline double clampd(double v, double lo, double hi){ return v<lo?lo:(v>hi?hi:v); }
static inline double maxd(double a,double b){ return a>b?a:b; }
static inline double mind(double a,double b){ return a<b?a:b; }

struct AdaptiveKalmanFilter {
#ifdef DBG
    /* DBG vars */
    double dbg_gate, dbg_sigma, dbg_nsig;
#endif
    /* State and covariance */
    double x[2]; double P[2][2]; double Q[2][2];
    /* Measurement noise (nominal and adaptive) */
    double R; double R_adapt;
    /* Gains */
    double K[2]; double innovation; double S; double dt;

    /* Adaptation state */
    double alpha, beta;
    double e_mean_fast, e_var_fast;
    double e_mean_slow, e_var_slow;
    double e_prev, corr_lag1;
    double baseQ, prevDrift;

    /* Quantization & floor */
    double z_prev, qstep_est_s;   /* EWMA of observed quantization step */
    double R_floor;               /* Derived from quantization */

    /* Miss/holdover detection */
    double dt_ewma;
    int    miss_streak;

    /* Saturation tracking */
    int k1_satur_count;

    unsigned long updateCount;
    int initialized;
};

/* Small 2x2 helpers */
static inline void mm2(const double A[2][2], const double B[2][2], double R[2][2]){
    R[0][0]=A[0][0]*B[0][0]+A[0][1]*B[1][0]; R[0][1]=A[0][0]*B[0][1]+A[0][1]*B[1][1];
    R[1][0]=A[1][0]*B[0][0]+A[1][1]*B[1][0]; R[1][1]=A[1][0]*B[0][1]+A[1][1]*B[1][1];
}
static inline void ma2(const double A[2][2], const double B[2][2], double R[2][2]){
    R[0][0]=A[0][0]+B[0][0]; R[0][1]=A[0][1]+B[0][1]; R[1][0]=A[1][0]+B[1][0]; R[1][1]=A[1][1]+B[1][1];
}

AdaptiveKalmanFilter* akf_create(void){
    AdaptiveKalmanFilter* a=(AdaptiveKalmanFilter*)calloc(1,sizeof(*a)); if(!a) return NULL;
    a->R=1e-6; a->R_adapt=a->R; a->Q[0][0]=1e-9; a->Q[1][1]=1e-10; a->baseQ=a->Q[1][1];
    a->alpha=0.95; a->beta=0.98; a->P[0][0]=1000.0; a->P[1][1]=100.0;
    a->e_var_slow=a->R; a->e_var_fast=a->R;
    a->dt_ewma=0.01; a->R_floor=1e-10;
    return a;
}
void akf_destroy(AdaptiveKalmanFilter* a){ free(a); }

void akf_init(AdaptiveKalmanFilter* a,double q,double r){
    if(!a) return;
    memset(a->x,0,sizeof(a->x));
    a->P[0][0]=1000.0; a->P[0][1]=0.0; a->P[1][0]=0.0; a->P[1][1]=100.0;
    a->Q[0][0]=q; a->Q[0][1]=0.0; a->Q[1][0]=0.0; a->Q[1][1]=q*0.1;
    a->R=r; a->R_adapt=r;
    a->alpha=0.95; a->beta=0.98;
    a->e_mean_fast=0.0; a->e_var_fast=r;
    a->e_mean_slow=0.0; a->e_var_slow=r;
    a->e_prev=0.0; a->corr_lag1=0.0; a->prevDrift=0.0;
    a->z_prev=0.0; a->qstep_est_s=0.0;
    a->dt=1.0; a->updateCount=0; a->initialized=0;
    a->dt_ewma=0.01; a->miss_streak=0; a->k1_satur_count=0;
    /* provisional quantization-aware floor ≈ (0.0005 s)^2 / 12 for 0.5 ms if we can't infer yet */
    a->R_floor = mind(30.0*r, maxd(1e-12, (0.0005*0.0005)/12.0));
}

void akf_reset(AdaptiveKalmanFilter* a){
    if(!a) return; double Q00=a->Q[0][0], Q11=a->Q[1][1], R=a->R, baseQ=a->baseQ;
    akf_init(a,Q00,R); a->Q[1][1]=Q11; a->baseQ=baseQ;
}

void akf_set_noise(AdaptiveKalmanFilter* a,double q0,double q1,double r){
    if(!a) return; a->Q[0][0]=q0; a->Q[1][1]=q1; a->R=r;
    if(a->R_adapt<0.01*a->R) a->R_adapt=0.01*a->R; if(a->R_adapt>30.0*a->R) a->R_adapt=30.0*a->R;
    /* Update floor bound as well */
    a->R_floor = mind(30.0*a->R, maxd(a->R*0.05, a->R_floor));
}

void akf_set_adaptation(AdaptiveKalmanFilter* a,double baseQ,double alpha,double beta){
    if(!a) return; a->baseQ=baseQ; a->alpha=alpha; a->beta=beta;
}

/* --- Adaptive helpers --- */
static void update_quant_floor(AdaptiveKalmanFilter* a, double z){
    if(a->updateCount<=1){ a->z_prev=z; return; }
    double dz = fabs(z - a->z_prev);
    a->z_prev = z;
    /* EWMA of smallest deltas; clip to 20 ms to ignore huge jumps */
    double clipped = dz;
    if (clipped > 0.02) clipped = 0.02;
    if (a->qstep_est_s == 0.0) a->qstep_est_s = clipped;
    else a->qstep_est_s = 0.98*a->qstep_est_s + 0.02*clipped;
    double floor_from_quant = (a->qstep_est_s*a->qstep_est_s)/12.0;
    /* Keep a modest relationship to nominal R; never exceed 30*R */
    a->R_floor = clampd(maxd(floor_from_quant, a->R*0.05), 1e-12, 30.0*a->R);
}

static void adapt_R(AdaptiveKalmanFilter* a){
    /* Two-time-scale innovation variance tracking */
    double e = a->innovation;
    /* Fast tracker */
    double alpha_f=0.7, beta_f=0.85;
    a->e_mean_fast = alpha_f*a->e_mean_fast + (1.0-alpha_f)*e;
    double devf = e - a->e_mean_fast;
    a->e_var_fast  = beta_f*a->e_var_fast + (1.0-beta_f)*(devf*devf);
    /* Slow tracker */
    double alpha_s=a->alpha, beta_s=a->beta;
    a->e_mean_slow = alpha_s*a->e_mean_slow + (1.0-alpha_s)*e;
    double devs = e - a->e_mean_slow;
    a->e_var_slow  = beta_s*a->e_var_slow + (1.0-beta_s)*(devs*devs);
    /* Blend and clamp with floor */
    double blended = 0.7*a->e_var_slow + 0.3*a->e_var_fast;
    a->R_adapt = clampd(blended, a->R_floor, 30.0*a->R);
}

/* correlation + Q schedule + saturation feedback */
static void adapt_Q_and_corr(AdaptiveKalmanFilter* a){
    double e=a->innovation;
    double denom = sqrt((a->e_var_slow+1e-18)*(a->e_var_slow+1e-18));
    double corr_inst = (denom>0)? (a->e_prev*e)/denom : 0.0;
    a->corr_lag1 = 0.95*a->corr_lag1 + 0.05*corr_inst;
    a->e_prev = e;

    /* Saturation feedback for K1 */
    int saturated = (a->K[1] >= 0.25-1e-9);
    if (saturated) a->k1_satur_count++; else a->k1_satur_count = (a->k1_satur_count>0)?a->k1_satur_count-1:0;

    if(a->updateCount>20){
        double ddrift=fabs(a->x[1]-a->prevDrift); a->prevDrift=a->x[1];
        if(a->corr_lag1>0.25 || ddrift>5e-9 || a->k1_satur_count>6){
            a->Q[1][1]=mind(a->Q[1][1]*1.05 + 0.5*a->baseQ, a->baseQ*20.0);
        } else if(a->corr_lag1<0.05 && ddrift<1e-10 && a->k1_satur_count==0){
            a->Q[1][1]=maxd(a->Q[1][1]*0.995, a->baseQ*0.25);
        }
        if(a->corr_lag1>0.35) a->Q[0][0]=mind(a->Q[0][0]*1.02 + 0.2*a->Q[1][1], 50.0*a->R);
        else a->Q[0][0]=maxd(a->Q[0][0]*0.997, 0.1*a->R);
    }
}

double akf_update(AdaptiveKalmanFilter* a,double z,double dt){
    if(!a) return 0.0;
    a->dt = dt; a->updateCount++;

    /* Miss/holdover heuristic from dt anomalies */
    if (a->updateCount==1) a->dt_ewma = dt>0?dt:0.01;
    a->dt_ewma = 0.98*a->dt_ewma + 0.02*(dt>0?dt:a->dt_ewma);
    int miss = (dt > 1.8*a->dt_ewma) ? 1 : 0;
    a->miss_streak = miss ? (a->miss_streak+1) : 0;

    /* First-time init */
    if(!a->initialized){ a->x[0]=z; a->x[1]=0.0; a->initialized=1; a->e_prev=0.0; a->z_prev=z; return a->x[0]; }

    /* Prediction */
    const double F[2][2]={{1.0,dt},{0.0,1.0}}; const double FT[2][2]={{1.0,0.0},{dt,1.0}};
    double x0=a->x[0]+dt*a->x[1]; double x1=a->x[1];
    double FP[2][2], FPFT[2][2]; mm2(F,a->P,FP); mm2(FP,FT,FPFT); ma2(FPFT,a->Q,a->P);

    /* Innovation and dynamic R */
    double z_pred = x0;
    a->innovation = z - z_pred;

    update_quant_floor(a, z);
    adapt_R(a);
    /* Escalate R if we experienced a gap (holdover) */
    if (a->miss_streak>0) {
        for (int i=0;i<a->miss_streak;i++) a->R_adapt = mind(a->R_adapt*1.3, 30.0*a->R);
    }

    a->S = a->P[0][0] + a->R_adapt;

    /* Dynamic gating */
    double base_gate = 3.5;
    if (a->miss_streak>0) base_gate += 1.0;                /* be more tolerant after gaps */
    else if (a->corr_lag1<0.03) base_gate = 3.0;           /* tighten when very white */
    #ifdef DBG
    a->dbg_sigma = sqrt(fabs(a->S));
#endif
    double sigma = sqrt(fabs(a->S)); double gscale=1.0;
    if(sigma>0){ double nsig=fabs(a->innovation)/sigma;
#ifdef DBG
        a->dbg_nsig = nsig; a->dbg_gate = base_gate;
#endif
        if(nsig>base_gate) gscale=clampd(base_gate/nsig, 0.2, 1.0); 
    }

    /* Gains with asymmetric clamp (offset) and bounded drift gain */
    if(fabs(a->S)>1e-18){
        a->K[0]=(a->P[0][0]/a->S)*gscale; a->K[1]=(a->P[1][0]/a->S)*gscale;
        /* Asymmetric clamp on K0 depending on innovation sign to reduce overshoot */
        double K0max_pos=0.45, K0max_neg=0.60;
        double k0max = (a->innovation>=0.0)?K0max_pos:K0max_neg;
        a->K[0]=clampd(a->K[0], 0.0, k0max);
        a->K[1]=clampd(a->K[1], 0.0, 0.25);
    } else { a->K[0]=a->K[1]=0.0; }

    /* Offset-first update: apply offset, recompute residual for drift */
    double x0_upd = x0 + a->K[0]*a->innovation;
    double innov2 = z - x0_upd;
    double x1_upd = x1 + a->K[1]*innov2;

    a->x[0]=x0_upd; a->x[1]=x1_upd;

    /* Conditional drift decay during prolonged gaps or after long run */
    if (a->miss_streak>0 || a->updateCount>80) {
        const double DECAY = 0.998; a->x[1]*=DECAY;
    }

    /* Drift safety clamp */
    const double MAX_DRIFT=80e-9;
    double ad=fabs(a->x[1]);
    if(ad>MAX_DRIFT){
        if(ad>300e-9){ a->x[1]=0.0; a->P[1][1]=10.0; }
        else a->x[1]=(a->x[1]>0)?MAX_DRIFT:-MAX_DRIFT;
    }

    /* Joseph-form like stabilization (I-KH)P */
    double KH00=a->K[0]; double KH10=a->K[1];
    double I_KH[2][2]={{1.0-KH00,0.0},{-KH10,1.0}};
    double newP[2][2];
    mm2(I_KH,a->P,newP); a->P[0][0]=newP[0][0]; a->P[0][1]=newP[0][1]; a->P[1][0]=newP[1][0]; a->P[1][1]=newP[1][1];

    adapt_Q_and_corr(a);

    return a->x[0];
}

/* Accessors */
double        akf_get_offset(const AdaptiveKalmanFilter* a){ return a?a->x[0]:0.0; }
double        akf_get_drift (const AdaptiveKalmanFilter* a){ return a?a->x[1]:0.0; }
double        akf_get_drift_ppb(const AdaptiveKalmanFilter* a){ return a?a->x[1]*1e9:0.0; }
double        akf_get_innovation(const AdaptiveKalmanFilter* a){ return a?a->innovation:0.0; }
double        akf_get_gain_offset(const AdaptiveKalmanFilter* a){ return a?a->K[0]:0.0; }
double        akf_get_gain_drift (const AdaptiveKalmanFilter* a){ return a?a->K[1]:0.0; }
unsigned long akf_get_update_count(const AdaptiveKalmanFilter* a){ return a?a->updateCount:0UL; }
int           akf_is_initialized (const AdaptiveKalmanFilter* a){ return a?a->initialized:0; }
double akf_get_R_adapt(const AdaptiveKalmanFilter* a){ return a?a->R_adapt:0.0; }
double akf_get_Q_offset(const AdaptiveKalmanFilter* a){ return a?a->Q[0][0]:0.0; }
double akf_get_Q_drift (const AdaptiveKalmanFilter* a){ return a?a->Q[1][1]:0.0; }

#ifdef DBG
double akf_dbg_R_floor(const AdaptiveKalmanFilter* a){ return a?a->R_floor:0.0; }
double akf_dbg_gate(const AdaptiveKalmanFilter* a){ return a?a->dbg_gate:0.0; }
double akf_dbg_sigma(const AdaptiveKalmanFilter* a){ return a?a->dbg_sigma:0.0; }
double akf_dbg_nsig(const AdaptiveKalmanFilter* a){ return a?a->dbg_nsig:0.0; }
#endif
