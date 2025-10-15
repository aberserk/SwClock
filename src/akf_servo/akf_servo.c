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
struct AdaptiveKalmanFilter {
    double x[2]; double P[2][2]; double Q[2][2];
    double R; double R_adapt;
    double K[2]; double innovation; double S; double dt;
    double alpha, beta, e_mean, e_var, e_prev, corr_lag1, baseQ, prevDrift;
    unsigned long updateCount; int initialized;
};
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
    a->alpha=0.95; a->beta=0.98; a->P[0][0]=1000.0; a->P[1][1]=100.0; a->e_var=a->R; return a;
}
void akf_destroy(AdaptiveKalmanFilter* a){ free(a); }
void akf_init(AdaptiveKalmanFilter* a,double q,double r){
    if(!a) return; memset(a->x,0,sizeof(a->x)); a->P[0][0]=1000.0; a->P[1][1]=100.0; a->P[0][1]=a->P[1][0]=0.0;
    a->Q[0][0]=q; a->Q[0][1]=a->Q[1][0]=0.0; a->Q[1][1]=q*0.1; a->R=r; a->R_adapt=r;
    a->e_mean=0.0; a->e_var=r; a->e_prev=0.0; a->corr_lag1=0.0; a->prevDrift=0.0;
    a->dt=1.0; a->updateCount=0; a->initialized=0;
}
void akf_reset(AdaptiveKalmanFilter* a){
    if(!a) return; double Q00=a->Q[0][0], Q11=a->Q[1][1], R=a->R, baseQ=a->baseQ; akf_init(a,Q00,R); a->Q[1][1]=Q11; a->baseQ=baseQ;
}
void akf_set_noise(AdaptiveKalmanFilter* a,double q0,double q1,double r){
    if(!a) return; a->Q[0][0]=q0; a->Q[1][1]=q1; a->R=r; if(a->R_adapt<0.01*a->R) a->R_adapt=0.01*a->R; if(a->R_adapt>30.0*a->R) a->R_adapt=30.0*a->R;
}
void akf_set_adaptation(AdaptiveKalmanFilter* a,double baseQ,double alpha,double beta){
    if(!a) return; a->baseQ=baseQ; a->alpha=alpha; a->beta=beta;
}
static void adapt(AdaptiveKalmanFilter* a){
    if(a->updateCount<=5) return;
    double e=a->innovation;
    a->e_mean = a->alpha*a->e_mean + (1.0-a->alpha)*e;
    double dev = e - a->e_mean;
    a->e_var  = a->beta*a->e_var + (1.0-a->beta)*(dev*dev);
    double denom = sqrt((a->e_var+1e-18)*(a->e_var+1e-18));
    double corr_inst = (denom>0)? (a->e_prev*e)/denom : 0.0;
    a->corr_lag1 = 0.95*a->corr_lag1 + 0.05*corr_inst;
    a->e_prev = e;
    double theo=a->S+1e-18; double ratio=a->e_var/theo;
    if(ratio>2.0) a->R_adapt = 0.85*a->R_adapt + 0.15*a->e_var;
    else if(ratio>1.2) a->R_adapt = 0.90*a->R_adapt + 0.10*a->e_var;
    else if(ratio<0.6) a->R_adapt = 0.95*a->R_adapt + 0.05*a->e_var;
    if(a->R_adapt<0.01*a->R) a->R_adapt=0.01*a->R;
    if(a->R_adapt>30.0*a->R) a->R_adapt=30.0*a->R;
    if(a->updateCount>20){
        double ddrift=fabs(a->x[1]-a->prevDrift); a->prevDrift=a->x[1];
        if(a->corr_lag1>0.25 || ddrift>5e-9) a->Q[1][1]=fmin(a->Q[1][1]*1.05 + 0.5*a->baseQ, a->baseQ*20.0);
        else if(a->corr_lag1<0.05 && ddrift<1e-10) a->Q[1][1]=fmax(a->Q[1][1]*0.98, a->baseQ*0.25);
        if(a->corr_lag1>0.35) a->Q[0][0]=fmin(a->Q[0][0]*1.03 + 0.2*a->Q[1][1], 50.0*a->R);
        else a->Q[0][0]=fmax(a->Q[0][0]*0.995, 0.1*a->R);
    }
}
double akf_update(AdaptiveKalmanFilter* a,double z,double dt){
    if(!a) return 0.0; a->dt=dt; a->updateCount++;
    if(!a->initialized){ a->x[0]=z; a->x[1]=0.0; a->initialized=1; a->e_prev=0.0; return a->x[0]; }
    const double F[2][2]={{1.0,dt},{0.0,1.0}}; const double FT[2][2]={{1.0,0.0},{dt,1.0}};
    double x0=a->x[0]+dt*a->x[1]; double x1=a->x[1];
    double FP[2][2], FPFT[2][2]; mm2(F,a->P,FP); mm2(FP,FT,FPFT); ma2(FPFT,a->Q,a->P);
    a->innovation = z - x0; a->S = a->P[0][0] + a->R_adapt;
    double gscale=1.0; double gate=3.5; double sigma=sqrt(fabs(a->S));
    if(sigma>0){ double nsig=fabs(a->innovation)/sigma; if(nsig>gate) gscale=fmax(0.2, gate/nsig); }
    if(fabs(a->S)>1e-18){ a->K[0]=(a->P[0][0]/a->S)*gscale; a->K[1]=(a->P[1][0]/a->S)*gscale;
        if(a->K[0]<0.0) a->K[0]=0.0; if(a->K[0]>0.6) a->K[0]=0.6;
        if(a->K[1]<0.0) a->K[1]=0.0; if(a->K[1]>0.25) a->K[1]=0.25;
    } else { a->K[0]=a->K[1]=0.0; }
    a->x[0]=x0 + a->K[0]*a->innovation; a->x[1]=x1 + a->K[1]*a->innovation;
    const double MAX_DRIFT=80e-9; const double DECAY=0.996; if(a->updateCount>80) a->x[1]*=DECAY;
    double ad=fabs(a->x[1]); if(ad>MAX_DRIFT){ if(ad>300e-9){ a->x[1]=0.0; a->P[1][1]=10.0; } else a->x[1]=(a->x[1]>0)?MAX_DRIFT:-MAX_DRIFT; }
    double KH00=a->K[0], KH10=a->K[1]; double I_KH[2][2]={{1.0-KH00,0.0},{-KH10,1.0}}; double newP[2][2];
    mm2(I_KH,a->P,newP); a->P[0][0]=newP[0][0]; a->P[0][1]=newP[0][1]; a->P[1][0]=newP[1][0]; a->P[1][1]=newP[1][1];
    adapt(a);
    return a->x[0];
}
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
