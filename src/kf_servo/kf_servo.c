#include "kf_servo.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* Optional debug hooks */
#ifndef DBG
#define DBG(...)  do {} while (0)
#endif
#ifndef DBGV
#define DBGV(...) do {} while (0)
#endif

struct KalmanFilter {
    /* State and covariance */
    double x[2];       /* [offset, drift] (s, s/s) */
    double P[2][2];    /* covariance */
    double Q[2][2];    /* process noise */
    double R;          /* nominal measurement noise */
    double adaptiveR;  /* adapted measurement noise */

    /* Scalars updated each step */
    double K[2];       /* gains */
    double innovation; /* z - H x_pred */
    double S;          /* innovation covariance */
    double dt;         /* last dt used */

    /* Adaptation helpers */
    double alpha, beta;
    double avgInnovation;
    double innovationVar;
    double baseQ;      /* baseline for Q11 adaptation */
    double prevDrift;

    unsigned long updateCount;
    int initialized;
};

/* ---------- tiny 2x2 helpers ---------- */
static inline void mm2(const double A[2][2], const double B[2][2], double R[2][2]) {
    R[0][0] = A[0][0]*B[0][0] + A[0][1]*B[1][0];
    R[0][1] = A[0][0]*B[0][1] + A[0][1]*B[1][1];
    R[1][0] = A[1][0]*B[0][0] + A[1][1]*B[1][0];
    R[1][1] = A[1][0]*B[0][1] + A[1][1]*B[1][1];
}
static inline void ma2(const double A[2][2], const double B[2][2], double R[2][2]) {
    R[0][0]=A[0][0]+B[0][0]; R[0][1]=A[0][1]+B[0][1];
    R[1][0]=A[1][0]+B[1][0]; R[1][1]=A[1][1]+B[1][1];
}

/* ---------- adaptation ---------- */
static void kf_adapt(KalmanFilter* kf) {
    if (kf->updateCount <= 8) return;
    const double beta = 0.85;
    double e2 = kf->innovation * kf->innovation;
    kf->innovationVar = beta * kf->innovationVar + (1.0 - beta) * e2;

    double theo = kf->S + 1e-12;
    double ratio = kf->innovationVar / theo;

    /* adapt R gently */
    if      (ratio > 2.5) kf->adaptiveR = 0.8  * kf->adaptiveR + 0.2 * kf->innovationVar;
    else if (ratio > 1.5) kf->adaptiveR = 0.9  * kf->adaptiveR + 0.1 * kf->innovationVar;
    else if (ratio < 0.4 && kf->adaptiveR > kf->R * 0.08)
                          kf->adaptiveR = 0.95 * kf->adaptiveR + 0.05* kf->innovationVar;

    if (kf->adaptiveR < kf->R * 0.01) kf->adaptiveR = kf->R * 0.01;
    if (kf->adaptiveR > kf->R * 20.0) kf->adaptiveR = kf->R * 20.0;

    /* slow Q11 adaptation (drift) */
    if (kf->updateCount > 30) {
        double d = fabs(kf->x[1] - kf->prevDrift);
        kf->prevDrift = kf->x[1];
        if      (d > 5e-9)  kf->Q[1][1] = fmin(kf->Q[1][1] * 1.02, kf->baseQ * 10.0);
        else if (d < 1e-10) kf->Q[1][1] = fmax(kf->Q[1][1] * 0.99, kf->baseQ * 0.5);
    }
}

/* ---------- lifecycle ---------- */
KalmanFilter* kf_create(void) {
    KalmanFilter* kf = (KalmanFilter*)calloc(1, sizeof(KalmanFilter));
    if (!kf) return NULL;
    /* defaults chosen to be safe; caller should call kf_init next */
    kf->R = 1e-6;
    kf->adaptiveR = kf->R;
    kf->Q[0][0] = 1e-9;  kf->Q[1][1] = 1e-10;
    kf->baseQ = kf->Q[1][1];
    kf->alpha = 0.95; kf->beta = 0.99;
    kf->P[0][0] = 1000.0; kf->P[1][1] = 100.0;
    return kf;
}

void kf_destroy(KalmanFilter* kf) {
    free(kf);
}

void kf_init(KalmanFilter* kf, double processNoise, double measurementNoise) {
    if (!kf) return;
    memset(kf->x, 0, sizeof(kf->x));
    kf->P[0][0] = 1000.0; kf->P[0][1] = 0.0;
    kf->P[1][0] = 0.0;    kf->P[1][1] = 100.0;

    kf->Q[0][0] = processNoise;
    kf->Q[0][1] = 0.0;
    kf->Q[1][0] = 0.0;
    kf->Q[1][1] = processNoise * 0.1;

    kf->R         = measurementNoise;
    kf->adaptiveR = measurementNoise;

    kf->alpha = 0.95;
    kf->beta  = 0.99;
    kf->baseQ = kf->Q[1][1];

    kf->innovationVar = measurementNoise;
    kf->avgInnovation = 0.0;
    kf->prevDrift = 0.0;
    kf->dt = 1.0;
    kf->updateCount = 0;
    kf->initialized = 0;

    DBG("kf_init: Q0=%.3g R0=%.3g\n", processNoise, measurementNoise);
}

void kf_reset(KalmanFilter* kf) {
    if (!kf) return;
    double Q00 = kf->Q[0][0], Q11 = kf->Q[1][1], R = kf->R, baseQ = kf->baseQ;
    kf_init(kf, Q00, R);
    kf->Q[1][1] = Q11;
    kf->baseQ   = baseQ;
}

/* ---------- configuration ---------- */
void kf_set_noise(KalmanFilter* kf, double q_offset, double q_drift, double r_measure) {
    if (!kf) return;
    kf->Q[0][0] = q_offset;
    kf->Q[1][1] = q_drift;
    kf->R       = r_measure;
    /* keep adaptiveR reasonable after changes */
    if (kf->adaptiveR < 0.01 * kf->R) kf->adaptiveR = 0.01 * kf->R;
    if (kf->adaptiveR > 20.0 * kf->R) kf->adaptiveR = 20.0 * kf->R;
}

void kf_set_adaptation(KalmanFilter* kf, double baseQ_drift, double alpha, double beta) {
    if (!kf) return;
    kf->baseQ = baseQ_drift;
    kf->alpha = alpha;
    kf->beta  = beta;
}

/* ---------- filtering ---------- */
double kf_update(KalmanFilter* kf, double z, double dt) {
    if (!kf) return 0.0;
    kf->dt = dt;
    kf->updateCount++;

    if (!kf->initialized) {
        kf->x[0] = z;  /* trust first sample for offset */
        kf->x[1] = 0.0;
        kf->initialized = 1;
        return kf->x[0];
    }

    /* predict */
    const double F[2][2]  = { {1.0, dt}, {0.0, 1.0} };
    const double FT[2][2] = { {1.0, 0.0}, {dt, 1.0} };

    double x_pred0 = kf->x[0] + dt * kf->x[1];
    double x_pred1 = kf->x[1];

    double FP[2][2], FPFT[2][2];
    mm2(F, kf->P, FP);
    mm2(FP, FT, FPFT);
    ma2(FPFT, kf->Q, kf->P); /* P = FPF' + Q */

    /* update */
    kf->innovation = z - x_pred0;           /* H=[1 0] */
    kf->S = kf->P[0][0] + kf->adaptiveR;

    if (fabs(kf->S) > 1e-16) {
        kf->K[0] = kf->P[0][0] / kf->S;
        kf->K[1] = kf->P[1][0] / kf->S;

        /* gentle early shaping */
        if (kf->updateCount < 30) {
            double boost = 1.1 - 0.003 * kf->updateCount; /* 1.1 -> 1.0 */
            kf->K[0] *= boost;
            kf->K[1] *= boost * 0.9;
        }

        double a = fabs(kf->innovation);
        if (a > 200e-6)        kf->K[0] *= 1.05;
        else if (a < 5e-6) {   kf->K[0] *= 0.95; kf->K[1] *= 0.98; }

        if (kf->K[0] < 0.0) kf->K[0] = 0.0;
        if (kf->K[0] > 0.6) kf->K[0] = 0.6;
        if (kf->K[1] < 0.0) kf->K[1] = 0.0;
        if (kf->K[1] > 0.2) kf->K[1] = 0.2;
    } else {
        kf->K[0] = kf->K[1] = 0.0;
    }

    kf->x[0] = x_pred0 + kf->K[0] * kf->innovation;
    kf->x[1] = x_pred1 + kf->K[1] * kf->innovation;

    /* gentle drift decay and bounds */
    const double MAX_DRIFT = 50e-9;   /* 50 ppb in s/s */
    const double DECAY     = 0.995;
    if (kf->updateCount > 50) kf->x[1] *= DECAY;

    double ad = fabs(kf->x[1]);
    if (ad > MAX_DRIFT) {
        if (ad > 200e-9) {
            DBGV("Kalman: extreme drift -> reset (%.1f ppb)\n", kf->x[1]*1e9);
            kf->x[1] = 0.0;
            kf->P[1][1] = 10.0;
        } else {
            kf->x[1] = (kf->x[1] > 0.0) ? MAX_DRIFT : -MAX_DRIFT;
            DBGV("Kalman: drift clamped to %.1f ppb\n", kf->x[1]*1e9);
        }
    }

    /* covariance update: P = (I - K H) P, with H=[1 0] */
    double KH00 = kf->K[0], KH10 = kf->K[1];
    double I_KH[2][2] = { {1.0 - KH00, 0.0}, {-KH10, 1.0} };
    double newP[2][2];
    mm2(I_KH, kf->P, newP);
    kf->P[0][0]=newP[0][0]; kf->P[0][1]=newP[0][1];
    kf->P[1][0]=newP[1][0]; kf->P[1][1]=newP[1][1];

    /* adapt after applying update */
    kf_adapt(kf);

    /* running stats */
    if (kf->updateCount == 1) kf->avgInnovation = kf->innovation;
    else kf->avgInnovation = 0.95*kf->avgInnovation + 0.05*kf->innovation;

    DBGV("KF: z=%g pred=%g filt=%g innov=%g K=[%g,%g] drift=%g\n",
         z, x_pred0, kf->x[0], kf->innovation, kf->K[0], kf->K[1], kf->x[1]);

    return kf->x[0];
}

/* ---------- accessors ---------- */
double        kf_get_offset(const KalmanFilter* kf){ return kf ? kf->x[0] : 0.0; }
double        kf_get_drift(const KalmanFilter* kf) { return kf ? kf->x[1] : 0.0; }
double        kf_get_drift_ppb(const KalmanFilter* kf){ return kf ? kf->x[1]*1e9 : 0.0; }
double        kf_get_innovation(const KalmanFilter* kf){ return kf ? kf->innovation : 0.0; }
double        kf_get_gain_offset(const KalmanFilter* kf){ return kf ? kf->K[0] : 0.0; }
double        kf_get_gain_drift (const KalmanFilter* kf){ return kf ? kf->K[1] : 0.0; }
unsigned long kf_get_update_count(const KalmanFilter* kf){ return kf ? kf->updateCount : 0UL; }
int           kf_is_initialized (const KalmanFilter* kf){ return kf ? kf->initialized : 0; }
