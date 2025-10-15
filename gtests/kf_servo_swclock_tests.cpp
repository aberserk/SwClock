#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstdint>
#include <vector>
#include <random>
#include <algorithm>

extern "C" {
#include "kf_servo.h"
#include "swclock.h"
#include "swclock_compat.h"
#include "sw_adjtimex.h"
}

using namespace std::chrono;

/* ---------- utilities ---------- */

static inline int64_t steady_now_ns() {
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

static inline void sleep_for_ms(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

static inline long ppb_to_freq_fixed(long double ppb) {
    // ADJ_FREQUENCY expects scaled-ppm (ppm << 16). ppb -> ppm.
    long double ppm = ppb / 1000.0L;
    long double scaled = ppm * 65536.0L;
    if (scaled > (long double)std::numeric_limits<long>::max())
        return std::numeric_limits<long>::max();
    if (scaled < (long double)std::numeric_limits<long>::min())
        return std::numeric_limits<long>::min();
    return (long)std::llround(scaled);
}

/* Measure offset master - sw in seconds */
static inline double measure_offset_s(SwClock* sw, int64_t master_ns) {
    int64_t sw_ns = swclock_now_ns(sw);
    return (double)( (long double)(master_ns - sw_ns) / 1e9L );
}

struct Harness {
    SwClock* sw{};
    KalmanFilter* kf{};

    int64_t master_start_ns{};
    steady_clock::time_point wall0;

    void init(double init_step_ms = 0.0, double init_freq_ppb = 0.0) {
        wall0 = steady_clock::now();
        master_start_ns = steady_now_ns();
        sw = swclock_create();
        ASSERT_NE(sw, nullptr);
        kf = kf_create();
        ASSERT_NE(kf, nullptr);
        // Reasonable default noises (seconds^2 and seconds^2)
        kf_init(kf, /*Q*/ 1e-8, /*R*/ 1e-6);

        // Synchronize SwClock to master time initially
        swclock_align_now(sw, master_start_ns);

        if (init_freq_ppb != 0.0) {
            swclock_set_freq(sw, init_freq_ppb);
        }
        if (init_step_ms != 0.0) {
            // Slew the software clock away from master by init_step_ms
            int64_t offset_ns = (int64_t)std::llround(init_step_ms * 1e6);
            swclock_adjust(sw, offset_ns, (int64_t)5e8); // 0.5 s window
        }
    }

    void fini() {
        kf_destroy(kf);
        swclock_destroy(sw);
        kf = nullptr;
        sw = nullptr;
    }

    int64_t master_now_ns() const {
        // ideal master clock with perfect rate
        int64_t elapsed = duration_cast<nanoseconds>(steady_clock::now() - wall0).count();
        return master_start_ns + elapsed;
    }

    /* One PTP-like control loop iteration */
    void loop_once(double dt_s, bool apply_freq = true, bool apply_offset = true) {
        // Measure current offset
        double z = measure_offset_s(sw, master_now_ns());

        // KF update
        double filt = kf_update(kf, z, dt_s);

        // Apply drift estimate as frequency correction (scaled-ppm<<16 via sw_adjtimex)
        if (apply_freq) {
            struct timex tx{};
            tx.modes = ADJ_FREQUENCY;
            long freq = ppb_to_freq_fixed(kf_get_drift_ppb(kf));
            tx.freq = freq;
            sw_adjtimex(sw, &tx);
        }

        // Apply filtered offset as slewed time correction (ADJ_OFFSET microseconds)
        if (apply_offset) {
            struct timex tx{};
            tx.modes = ADJ_OFFSET;
            tx.offset = (long)std::llround(filt * 1e6); // seconds -> usec
            sw_adjtimex(sw, &tx);
        }
    }
};

/* ---------- Tests ---------- */

/* 1) Basic convergence from large step + freq error */
TEST(KalmanServo, ConvergesFromStepAndFreqError) {
    Harness H;
    H.init(/*init_step_ms=*/50.0, /*init_freq_ppb=*/+30000.0); // 50 ms step, +30 ppm

    const int iters = 400;
    const int tick_ms = 10;
    for (int i=0; i<iters; ++i) {
        H.loop_once(tick_ms/1000.0, /*apply_freq=*/true, /*apply_offset=*/true);
        sleep_for_ms(tick_ms);
    }

    // After 4 seconds, we expect sub-millisecond lock
    double off_ms = std::fabs(measure_offset_s(H.sw, H.master_now_ns())) * 1e3;
    EXPECT_LT(off_ms, 1.0) << "Offset should be < 1 ms after convergence";

    // And frequency near zero (few ppb)
    double drift_ppb = std::fabs(kf_get_drift_ppb(H.kf));
    EXPECT_LT(drift_ppb, 200.0) << "Drift estimate should be within 200 ppb";

    H.fini();
}

/* 2) Robustness to noisy measurements (zero-mean noise) */
TEST(KalmanServo, RobustToMeasurementNoise) {
    Harness H;
    H.init(/*init_step_ms=*/20.0, /*init_freq_ppb=*/-20000.0); // 20 ms, -20 ppm

    std::mt19937 rng(42);
    std::normal_distribution<double> noise(0.0, 300e-6); // 300 microseconds sigma

    const int iters = 500;
    const int tick_ms = 10;
    for (int i=0; i<iters; ++i) {
        // Inject noise into the measurement by perturbing master_now
        int64_t mn = H.master_now_ns();
        double n = noise(rng);
        mn += (int64_t)std::llround(n * 1e9);
        double z = (double)((long double)(mn - swclock_now_ns(H.sw))/1e9L);

        // KF step with noisy z
        (void)kf_update(H.kf, z, tick_ms/1000.0);

        // Apply frequency & offset each round
        struct timex tf{}; tf.modes = ADJ_FREQUENCY;
        tf.freq = ppb_to_freq_fixed(kf_get_drift_ppb(H.kf));
        sw_adjtimex(H.sw, &tf);

        struct timex to{}; to.modes = ADJ_OFFSET;
        to.offset = (long)std::llround(kf_get_offset(H.kf)*1e6);
        sw_adjtimex(H.sw, &to);

        sleep_for_ms(tick_ms);
    }

    double off_us = std::fabs(measure_offset_s(H.sw, H.master_now_ns())) * 1e6;
    EXPECT_LT(off_us, 500.0) << "Offset should be < 500 us with strong noise";

    H.fini();
}

/* 3) Outlier bursts should not destabilize the servo */
TEST(KalmanServo, HandlesOutliers) {
    Harness H;
    H.init(/*init_step_ms=*/5.0, /*init_freq_ppb=*/+10000.0);

    const int iters = 600;
    const int tick_ms = 10;
    for (int i=0; i<iters; ++i) {
        double z = measure_offset_s(H.sw, H.master_now_ns());

        // Inject heavy-tailed outliers every ~50 ms
        if (i % 5 == 0) {
            z += ( (i % 10 == 0) ? 0.010 : -0.007 ); // +/- 7-10 ms spikes
        }

        (void)kf_update(H.kf, z, tick_ms/1000.0);

        struct timex tf{}; tf.modes = ADJ_FREQUENCY;
        tf.freq = ppb_to_freq_fixed(kf_get_drift_ppb(H.kf));
        sw_adjtimex(H.sw, &tf);

        struct timex to{}; to.modes = ADJ_OFFSET;
        to.offset = (long)std::llround(kf_get_offset(H.kf)*1e6);
        sw_adjtimex(H.sw, &to);

        sleep_for_ms(tick_ms);
    }

    double off_ms = std::fabs(measure_offset_s(H.sw, H.master_now_ns())) * 1e3;
    EXPECT_LT(off_ms, 2.0) << "Offset should remain within a few ms despite outliers";

    H.fini();
}

/* 4) Step change in master time (simulated GM step) */
TEST(KalmanServo, RecoversFromMasterStep) {
    Harness H;
    H.init(/*init_step_ms=*/0.0, /*init_freq_ppb=*/0.0);

    const int tick_ms = 10;

    // Settle briefly
    for (int i=0; i<100; ++i) {
        H.loop_once(tick_ms/1000.0);
        sleep_for_ms(tick_ms);
    }

    // Simulate master jumping forward by +15 ms
    H.master_start_ns += (int64_t)15'000'000; // 15 ms step

    // Run to recover (need ~30s for 15ms at 500ppm slew rate)
    for (int i=0; i<3200; ++i) {  // 32 seconds
        H.loop_once(tick_ms/1000.0);
        sleep_for_ms(tick_ms);
    }

    double off_us = std::fabs(measure_offset_s(H.sw, H.master_now_ns())) * 1e6;
    EXPECT_LT(off_us, 1000.0) << "Should recover to within 1ms after sufficient slew time";

    H.fini();
}

/* 5) Frequency-only discipline (no offset slews) */
TEST(KalmanServo, FrequencyOnlyDiscipline) {
    Harness H;
    H.init(/*init_step_ms=*/0.0, /*init_freq_ppb=*/+40000.0); // +40 ppm bias
    const int iters = 600;
    const int tick_ms = 10;
    for (int i=0; i<iters; ++i) {
        // Only frequency corrections
        H.loop_once(tick_ms/1000.0, /*apply_freq=*/true, /*apply_offset=*/false);
        sleep_for_ms(tick_ms);
    }
    // Offset may not be tiny without slews, but drift should be near zero
    double drift_ppb = std::fabs(kf_get_drift_ppb(H.kf));
    EXPECT_LT(drift_ppb, 500.0) << "Drift should be largely corrected with freq-only control";
    H.fini();
}

/* 6) Offset-only discipline (no frequency corrections) */
TEST(KalmanServo, OffsetOnlyDiscipline) {
    Harness H;
    H.init(/*init_step_ms=*/30.0, /*init_freq_ppb=*/+15000.0);
    const int iters = 600;
    const int tick_ms = 10;
    for (int i=0; i<iters; ++i) {
        // Only offset slews
        H.loop_once(tick_ms/1000.0, /*apply_freq=*/false, /*apply_offset=*/true);
        sleep_for_ms(tick_ms);
    }
    double off_ms = std::fabs(measure_offset_s(H.sw, H.master_now_ns())) * 1e3;
    EXPECT_LT(off_ms, 3.0) << "Offset-only control should keep ms-level lock despite freq bias";
    H.fini();
}

