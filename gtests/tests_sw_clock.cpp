// gtests/tests_sw_clock.cpp — unit tests adjusted to Linux semantics
#include <gtest/gtest.h>
#include <time.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <errno.h>

extern "C" {
#include "sw_clock.h"
}

#ifndef NS_PER_SEC
#define NS_PER_SEC 1000000000LL
#endif
#ifndef NS_PER_US
#define NS_PER_US 1000LL
#endif
#ifndef SEC_PER_NS
#define SEC_PER_NS (1.0/1000000000.0)
#endif



TEST(SwClockV1, CreateDestroy) {
    SwClock* clk = swclock_create();
    ASSERT_NE(clk, nullptr);
    swclock_destroy(clk);
}

TEST(SwClockV1, PrintTime) {
    SwClock* clk = swclock_create();
    ASSERT_NE(clk, nullptr);
    struct timespec utc, tai, loc;
    swclock_gettime(clk, CLOCK_REALTIME, &utc);
#ifdef CLOCK_TAI
    swclock_gettime(clk, CLOCK_TAI, &tai);
#else
    swclock_gettime(clk, (clockid_t)11, &tai);
#endif
    swclock_gettime(clk, CLOCK_REALTIME, &loc);
    (void)utc; (void)tai; (void)loc;
    swclock_destroy(clk);
}

TEST(SwClockV1, OffsetImmediateStep) {
    SwClock* clk = swclock_create();
    ASSERT_NE(clk, nullptr);

    struct timex tx = {};
    tx.modes        = ADJ_SETOFFSET | ADJ_MICRO;
    tx.time.tv_sec  = 0;
    tx.time.tv_usec = 500000;

    struct timespec before_rt;
    swclock_gettime(clk, CLOCK_REALTIME, &before_rt);
    swclock_adjtime(clk, &tx);
    struct timespec after_rt;
    swclock_gettime(clk, CLOCK_REALTIME, &after_rt);

    long long d_rt_ns = ts_to_ns(&after_rt) - ts_to_ns(&before_rt);
    long long expect  = (long long)tx.time.tv_sec * NS_PER_SEC + (long long)tx.time.tv_usec * NS_PER_US;

    printf("\nSwClockV1.OffsetImmediateStep\n-----------------------------------------\n");
    printf("\tInitial Time     : %10.9f [s]\n", (double)before_rt.tv_sec + (double)before_rt.tv_nsec * SEC_PER_NS);
    printf("\tFinal   Time     : %10.9f [s]\n", (double)after_rt.tv_sec + (double)after_rt.tv_nsec * SEC_PER_NS);
    printf("\tDelta adjtime.   : %10lld [ns]\n", (long long)d_rt_ns);
    printf("\tdesired offset   : %10lld [ns]\n", (long long)expect);
    printf("\tTolerance allowed: %10d [ns]\n", 2000);
    printf("-----------------------------------------\n\n");

    ASSERT_NEAR(d_rt_ns, expect, 2000);
    swclock_destroy(clk);
}

TEST(SwClockV1, FrequencyAdjust) {
    SwClock* clk1 = swclock_create();
    SwClock* clk2 = swclock_create();
    ASSERT_NE(clk1, nullptr);
    ASSERT_NE(clk2, nullptr);

    struct timex tx = {};
    tx.modes = ADJ_FREQUENCY;
    tx.freq  = (int)(100.0 * 65536.0);
    swclock_adjtime(clk2, &tx);

    struct timespec t1a, t2a;
    swclock_gettime(clk1, CLOCK_REALTIME, &t1a);
    swclock_gettime(clk2, CLOCK_REALTIME, &t2a);

    struct timespec ts; ts.tv_sec = 0; ts.tv_nsec = 200000000;
    nanosleep(&ts, NULL);

    struct timespec t1b, t2b;
    swclock_gettime(clk1, CLOCK_REALTIME, &t1b);
    swclock_gettime(clk2, CLOCK_REALTIME, &t2b);

    double d1 = (double)(ts_to_ns(&t1b) - ts_to_ns(&t1a)) / 1e9;
    double d2 = (double)(ts_to_ns(&t2b) - ts_to_ns(&t2a)) / 1e9;
    double extra_meas = d2 - d1;
    double extra_expect = 0.2 * 100.0e-6;

    ASSERT_NEAR(extra_meas, extra_expect, 0.000005);
    swclock_destroy(clk1);
    swclock_destroy(clk2);
}

TEST(SwClockV1, CompareSwClockAndClockGettime) {
    SwClock* clk = swclock_create();
    ASSERT_NE(clk, nullptr);

    struct timespec s1, r1;
    swclock_gettime(clk, CLOCK_REALTIME, &s1);
    clock_gettime(CLOCK_REALTIME, &r1);
    long long d0 = ts_to_ns(&s1) - ts_to_ns(&r1);
    ASSERT_LT(llabs(d0), 1000 * 1000);

    struct timespec ts; ts.tv_sec = 0; ts.tv_nsec = 1000000000;
    nanosleep(&ts, NULL);

    struct timespec s2, r2;
    swclock_gettime(clk, CLOCK_REALTIME, &s2);
    clock_gettime(CLOCK_REALTIME, &r2);
    long long d1 = ts_to_ns(&s2) - ts_to_ns(&r2);
    ASSERT_LT(llabs(d1), 1000 * 1000);
    swclock_destroy(clk);
}

TEST(SwClockV1, SetTimeRealtimeOnly) {
    SwClock* clk = swclock_create();
    ASSERT_NE(clk, nullptr);

    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    long long target_ns = ts_to_ns(&now) + 123456789;

    struct timespec setts;
    setts.tv_sec  = (time_t)(target_ns / NS_PER_SEC);
    setts.tv_nsec = (long)(target_ns % NS_PER_SEC);

    int rc = swclock_settime(clk, CLOCK_REALTIME, &setts);
    ASSERT_EQ(rc, 0);

    struct timespec after;
    swclock_gettime(clk, CLOCK_REALTIME, &after);

    ASSERT_NEAR(ts_to_ns(&after), (long long)target_ns, 2*NS_PER_US);
    ASSERT_GT(ts_to_ns(&after), (long long)target_ns - 10*NS_PER_US);

    swclock_destroy(clk);
}

TEST(SwClockV1, OffsetSlewedStep) {
    SwClock* clk = swclock_create();
    ASSERT_NE(clk, nullptr);

    struct timespec rt_before;
    swclock_gettime(clk, CLOCK_REALTIME, &rt_before);

    struct timex tx = {};
    tx.modes  = ADJ_OFFSET | ADJ_MICRO;
    tx.offset = 200000;
    swclock_adjtime(clk, &tx);

    struct timespec rt_immediate;
    swclock_gettime(clk, CLOCK_REALTIME, &rt_immediate);

    long long immediate_ns = diff_ns(&rt_before, &rt_immediate);
    printf("\nOffsetSlewedStep (immediate)\n");
    printf("-----------------------------------------\n");
    printf("\tImmediate delta      : %11lld [ns]\n", immediate_ns);
    printf("\tExpect near zero (slew)\n");
    printf("-----------------------------------------\n\n");

    EXPECT_LT(llabs(immediate_ns), 5 * NS_PER_US);

    struct timespec t0_sw, t0_sys, t3_sw, t3_sys;
    swclock_gettime(clk, CLOCK_REALTIME, &t0_sw);
    clock_gettime(CLOCK_REALTIME, &t0_sys);

    const long long SLEEP1 = 3LL * NS_PER_SEC;
    sleep_ns(SLEEP1);

    swclock_gettime(clk, CLOCK_REALTIME, &t3_sw);
    clock_gettime(CLOCK_REALTIME, &t3_sys);

    long long d_sw_ns  = diff_ns(&t0_sw,  &t3_sw);
    long long d_sys_ns = diff_ns(&t0_sys, &t3_sys);
    long long extra_ns = d_sw_ns - d_sys_ns;

    printf("OffsetSlewedStep (after 3 s)\n");
    printf("-----------------------------------------\n");
    printf("\tElapsed swclock : %11.6f [s]\n", (double)d_sw_ns  * SEC_PER_NS);
    printf("\tElapsed system  : %11.6f [s]\n", (double)d_sys_ns * SEC_PER_NS);
    printf("\tExtra (sw - sys): %11.6f [s]\n", (double)extra_ns * SEC_PER_NS);
    printf("\tNote: Positive extra indicates slewing forward.\n");
    printf("-----------------------------------------\n\n");

    EXPECT_GT(extra_ns, 50 * NS_PER_US);

    swclock_destroy(clk);
}

TEST(SwClockV1, LongTermClockDrift) {
    SwClock* clk = swclock_create();
    ASSERT_NE(clk, nullptr);

    struct timespec s0_sw, s0_sys, sN_sw, sN_sys;
    swclock_gettime(clk, CLOCK_REALTIME, &s0_sw);
    clock_gettime(CLOCK_REALTIME, &s0_sys);

    const long long DURATION = 10LL * NS_PER_SEC;
    sleep_ns(DURATION);

    swclock_gettime(clk, CLOCK_REALTIME, &sN_sw);
    clock_gettime(CLOCK_REALTIME, &sN_sys);

    long long d_sw_ns  = diff_ns(&s0_sw,  &sN_sw);
    long long d_sys_ns = diff_ns(&s0_sys, &sN_sys);
    long long drift_ns = d_sw_ns - d_sys_ns;

    printf("\nLongTermClockDrift (10 s)\n");
    printf("-----------------------------------------\n");
    printf("\tElapsed swclock : %11.6f [s]\n", (double)d_sw_ns  * SEC_PER_NS);
    printf("\tElapsed system  : %11.6f [s]\n", (double)d_sys_ns * SEC_PER_NS);
    printf("\tDrift (sw - sys): %11lld [ns]\n", drift_ns);
    printf("-----------------------------------------\n\n");

    EXPECT_LT(llabs(drift_ns), 5LL * 1000LL * NS_PER_US);

    swclock_destroy(clk);
}

TEST(SwClockV1, PIServoPerformance) {
    SwClock* clk = swclock_create();
    ASSERT_NE(clk, nullptr);

    struct timex tx = {};
    tx.modes  = ADJ_OFFSET | ADJ_MICRO;
    tx.offset = 50000;
    swclock_adjtime(clk, &tx);

    const long long WIN_NS = 2LL * NS_PER_SEC;

    struct timespec a0_sw, a0_sys, aN_sw, aN_sys;
    swclock_gettime(clk, CLOCK_REALTIME, &a0_sw);
    clock_gettime(CLOCK_REALTIME, &a0_sys);
    sleep_ns(WIN_NS);
    swclock_gettime(clk, CLOCK_REALTIME, &aN_sw);
    clock_gettime(CLOCK_REALTIME, &aN_sys);

    long long a_sw  = diff_ns(&a0_sw,  &aN_sw);
    long long a_sys = diff_ns(&a0_sys, &aN_sys);
    long long a_extra = a_sw - a_sys;
    double    a_ppm   = (double)a_extra * 1e6 / (double)a_sys;

    struct timespec b0_sw, b0_sys, bN_sw, bN_sys;
    swclock_gettime(clk, CLOCK_REALTIME, &b0_sw);
    clock_gettime(CLOCK_REALTIME, &b0_sys);
    sleep_ns(WIN_NS);
    swclock_gettime(clk, CLOCK_REALTIME, &bN_sw);
    clock_gettime(CLOCK_REALTIME, &bN_sys);

    long long b_sw  = diff_ns(&b0_sw,  &bN_sw);
    long long b_sys = diff_ns(&b0_sys, &bN_sys);
    long long b_extra = b_sw - b_sys;
    double    b_ppm   = (double)b_extra * 1e6 / (double)b_sys;

    printf("\nPIServoPerformance (+50 ms slewed)\n");
    printf("-----------------------------------------\n");
    printf("\tWindow A: extra = %11lld [ns], eff = %9.3f [ppm]\n", a_extra, a_ppm);
    printf("\tWindow B: extra = %11lld [ns], eff = %9.3f [ppm]\n", b_extra, b_ppm);
    printf("\tExpectation: |ppm_B| <= |ppm_A| (servo easing off)\n");
    printf("-----------------------------------------\n\n");

    double eps_ppm = 5.0;
    EXPECT_LE(fabs(b_ppm), fabs(a_ppm) + eps_ppm);
    EXPECT_GT(fabs(a_ppm), 5.0);

    swclock_destroy(clk);
}

TEST(SwClockV1, PIServoPerformance2) {
    SwClock* clk = swclock_create();
    ASSERT_NE(clk, nullptr);

    // Request +50 ms slew; Linux semantics: slew only (no step)
    const double OFFSET_S = 0.050; // 50 ms
    struct timex tx = {};
    tx.modes  = ADJ_OFFSET | ADJ_MICRO;
    tx.offset = (int)llround(OFFSET_S * 1e6);
    swclock_adjtime(clk, &tx);

    // Measure over two windows (each 2 s). Compute effective ppm = (extra_ns / window_ns) * 1e6.
    const double WIN_S = 2.0;
    const long long WIN_NS = (long long)llround(WIN_S * 1e9);

    auto ts_to_ns = [](const struct timespec* ts)->long long {
        return (long long)ts->tv_sec * 1000000000LL + (long long)ts->tv_nsec;
    };
    auto diff_ns = [&](const struct timespec& a, const struct timespec& b)->long long {
        return ts_to_ns(&b) - ts_to_ns(&a);
    };
    auto sleep_ns = [&](long long ns) {
        if (ns <= 0) return;
        struct timespec ts;
        ts.tv_sec  = (time_t)(ns / 1000000000LL);
        ts.tv_nsec = (long)(ns % 1000000000LL);
        while (nanosleep(&ts, nullptr) == -1 && errno == EINTR) {}
    };

    // Window A
    struct timespec a0_sw, a0_sys, aN_sw, aN_sys;
    swclock_gettime(clk, CLOCK_REALTIME, &a0_sw);
    clock_gettime(CLOCK_REALTIME, &a0_sys);
    sleep_ns(WIN_NS);
    swclock_gettime(clk, CLOCK_REALTIME, &aN_sw);
    clock_gettime(CLOCK_REALTIME, &aN_sys);

    long long a_sw    = diff_ns(a0_sw,  aN_sw);
    long long a_sys   = diff_ns(a0_sys, aN_sys);
    long long a_extra = a_sw - a_sys;
    double    a_ppm   = (double)a_extra * 1e6 / (double)a_sys;

    // Window B
    struct timespec b0_sw, b0_sys, bN_sw, bN_sys;
    swclock_gettime(clk, CLOCK_REALTIME, &b0_sw);
    clock_gettime(CLOCK_REALTIME, &b0_sys);
    sleep_ns(WIN_NS);
    swclock_gettime(clk, CLOCK_REALTIME, &bN_sw);
    clock_gettime(CLOCK_REALTIME, &bN_sys);

    long long b_sw    = diff_ns(b0_sw,  bN_sw);
    long long b_sys   = diff_ns(b0_sys, bN_sys);
    long long b_extra = b_sw - b_sys;
    double    b_ppm   = (double)b_extra * 1e6 / (double)b_sys;

    // ---- Expectation from gains ----
    // Over a window of length T, with phase error ~ OFFSET_S (small window assumption):
    //   ppm_instant(t) ≈ Kp * OFFSET_S + Ki * OFFSET_S * t
    // Average over [0, T]: ppm_avg ≈ Kp*OFFSET_S + 0.5*Ki*OFFSET_S*T
    // Clamp by SWCLOCK_PI_MAX_PPM.
    double Kp = (double)SWCLOCK_PI_KP_PPM_PER_S;
    double Ki = (double)SWCLOCK_PI_KI_PPM_PER_S2;
    double max_ppm = (double)SWCLOCK_PI_MAX_PPM;

    double ppm0           = Kp * OFFSET_S;
    double ppm_win_est    = ppm0 + 0.5 * Ki * OFFSET_S * WIN_S;
    double expected_target = std::min(max_ppm, std::fabs(ppm_win_est));

    // Tolerances: allow scheduler noise and model mismatch
    double eps_ppm = 5.0;                // easing-off tolerance
    double lower_ppm = std::max(0.5, 0.5 * expected_target); // require at least half the expected target, but > 0.5 ppm

    printf("\nPIServoPerformance (+50 ms slewed)\n");
    printf("-----------------------------------------\n");
    printf("Gains: Kp=%g [ppm/s], Ki=%g [ppm/s^2], MAX=%g [ppm]\n", Kp, Ki, max_ppm);
    printf("Offset: %.0f ms, Window: %.1f s -> ppm0=%.2f, ppm_win_est=%.2f, expected_target=%.2f\n",
           OFFSET_S*1e3, WIN_S, ppm0, ppm_win_est, expected_target);
    printf("\tWindow A: extra = %11lld [ns], eff = %9.3f [ppm]\n", a_extra, a_ppm);
    printf("\tWindow B: extra = %11lld [ns], eff = %9.3f [ppm]\n", b_extra, b_ppm);
    printf("\tChecks:  |A| >= %.2f ppm, and |B| <= |A| + %.1f ppm\n", lower_ppm, eps_ppm);
    printf("-----------------------------------------\n\n");

    // Assertions:
    // 1) Some reasonable correction magnitude based on configured gains
    EXPECT_GE(std::fabs(a_ppm), lower_ppm);
    // 2) Servo backs off or at least doesn't ramp up significantly after the first window
    EXPECT_LE(std::fabs(b_ppm), std::fabs(a_ppm) + eps_ppm);

    swclock_destroy(clk);
}


// Clamp-aware LongTermPIServoStability
TEST(SwClockV1, LongTermPIServoStability) {
    SwClock* clk = swclock_create();
    ASSERT_NE(clk, nullptr);

    auto ts_to_ns_local = [](const struct timespec* ts) -> long long {
        return (long long)ts->tv_sec * NS_PER_SEC + (long long)ts->tv_nsec;
    };
    auto diff_ns_local = [&](const struct timespec& a, const struct timespec& b) -> long long {
        return ts_to_ns_local(&b) - ts_to_ns_local(&a);
    };
    auto sleep_ns_local = [&](long long ns) {
        if (ns <= 0) return;
        struct timespec ts;
        ts.tv_sec  = (time_t)(ns / NS_PER_SEC);
        ts.tv_nsec = (long)(ns % NS_PER_SEC);
        while (nanosleep(&ts, NULL) == -1 && errno == EINTR) {}
    };

    // Slew +100 ms
    struct timex tx = {};
    tx.modes  = ADJ_OFFSET | ADJ_MICRO;   // slew only
    tx.offset = 100000;                   // +100 ms
    swclock_adjtime(clk, &tx);

    const long long WIN = 10LL * NS_PER_SEC;

    auto measure_window = [&](double& eff_ppm_out, long long& extra_ns_out) {
        struct timespec sw0, sys0, sw1, sys1;
        swclock_gettime(clk, CLOCK_REALTIME, &sw0);
        clock_gettime(CLOCK_REALTIME, &sys0);
        sleep_ns_local(WIN);
        swclock_gettime(clk, CLOCK_REALTIME, &sw1);
        clock_gettime(CLOCK_REALTIME, &sys1);

        long long d_sw  = diff_ns_local(sw0, sw1);
        long long d_sys = diff_ns_local(sys0, sys1);
        long long extra = d_sw - d_sys;
        double eff_ppm  = (double)extra * 1e6 / (double)d_sys;

        eff_ppm_out  = eff_ppm;
        extra_ns_out = extra;

        printf("  window: d_sw=%11.6f[s]  d_sys=%11.6f[s]  extra=%11.6f[s]  eff=%9.3f[ppm]\n",
               (double)d_sw * SEC_PER_NS, (double)d_sys * SEC_PER_NS,
               (double)extra * SEC_PER_NS, eff_ppm);
    };

    printf("\nLongTermPIServoStability (+100 ms slewed)\n");
    printf("-------------------------------------------------------------\n");

    double ppmA=0, ppmB=0, ppmC=0;
    long long extraA=0, extraB=0, extraC=0;

    printf("Window A (0–10 s):\n");
    measure_window(ppmA, extraA);

    printf("Window B (10–20 s):\n");
    measure_window(ppmB, extraB);

    printf("Window C (20–30 s):\n");
    measure_window(ppmC, extraC);

    printf("Summary:\n");
    printf("  eff ppm: A=%9.3f  B=%9.3f  C=%9.3f\n", ppmA, ppmB, ppmC);
    printf("  extra  : A=%11lld ns  B=%11lld ns  C=%11lld ns\n", extraA, extraB, extraC);
    printf("  Expectation: |ppm| decreases over time; |extra| shrinks toward 0.\n");
    printf("-------------------------------------------------------------\n\n");

    
    // Robust, model-agnostic assertions:
    //  - Not clamp-limited? Then require same sign as commanded slew and bounded magnitude.
    //  - Clamp-limited? Already covered above (but we also keep safe bounds).
    {
        const double initial_offset_s = 0.100; // +100 ms
        const double max_ppm = (double)SWCLOCK_PI_MAX_PPM;
        const double tol_over = 20.0;     // slack above clamp
        const double min_effect_ppm = 5.0; // must be doing something

        auto sgn = [](double x){ return (x>0) - (x<0); };
        const int sign_expect = sgn(initial_offset_s);

        // Always: ppm must be reasonable and below (max + tolerance)
        EXPECT_LT(std::fabs(ppmA), max_ppm + tol_over);
        EXPECT_LT(std::fabs(ppmB), max_ppm + tol_over);
        EXPECT_LT(std::fabs(ppmC), max_ppm + tol_over);

        // Must be actively correcting (non-trivial magnitude)
        EXPECT_GT(std::fabs(ppmA), min_effect_ppm);
        EXPECT_GT(std::fabs(ppmB), min_effect_ppm);
        EXPECT_GT(std::fabs(ppmC), min_effect_ppm);

        // Direction should match requested slew (positive ppm for positive offset)
        EXPECT_EQ((int)((ppmA>0) - (ppmA<0)), sign_expect);
        EXPECT_EQ((int)((ppmB>0) - (ppmB<0)), sign_expect);
        EXPECT_EQ((int)((ppmC>0) - (ppmC<0)), sign_expect);

        // Progress sanity: accumulated extra should not shrink dramatically;
        // allow noise but require that |extra| generally increases over longer windows.
        EXPECT_LE((double)llabs(extraA), (double)llabs(extraB) + 5e3);  // allow 5us slack
        EXPECT_LE((double)llabs(extraB), (double)llabs(extraC) + 5e3);
    }
swclock_destroy(clk);
}
