//
// tests_sw_clock.cpp
// Comprehensive GoogleTest suite for sw_clock
// Build example (macOS):
// clang++ -std=c++11 -O2 -Wall -Wextra tests_sw_clock.cpp sw_clock.c -o sw_clock_tests -lpthread -lgtest
//
#include <gtest/gtest.h>
#include <ctime>
#include <unistd.h>
#include <cerrno>
extern "C" {
#include "sw_clock.h"
}


static inline void sleep_ns(long ns) {
    struct timespec ts = {
        .tv_sec = ns / NS_PER_SEC,
        .tv_nsec = ns % NS_PER_SEC
    };
    nanosleep(&ts, nullptr);
}

TEST(SwClockV1, CreateDestroy) {
    SwClock* c = swclock_create();
    ASSERT_NE(c, nullptr);
    swclock_destroy(c);
}

TEST(SwClockV1, PrintTime) {
    SwClock* c = swclock_create();
    ASSERT_NE(c, nullptr);

    struct timespec ts;
    swclock_gettime(c, CLOCK_REALTIME, &ts);
    printf("\nSwClock CURRENT TIME:\n\n");
    printf(" UTC Time    : ");
    print_timespec_as_datetime(&ts);

    printf(" TAI Time    : ");
    print_timespec_as_TAI(&ts);

    printf(" Local Time  : ");
    print_timespec_as_localtime(&ts);
    printf("\n");

    swclock_destroy(c);
}

TEST(SwClockV1, OffsetImmediateStep) {
    SwClock* c = swclock_create();
    ASSERT_NE(c, nullptr);
    
    struct timespec rt0, rt1;
    swclock_gettime(c, CLOCK_REALTIME, &rt0);
    
    struct timex tx = {0};
    tx.modes  = ADJ_OFFSET | ADJ_MICRO;
    tx.offset = 500000;
    swclock_adjtime(c, &tx);
    
    swclock_gettime(c, CLOCK_REALTIME, &rt1);

    int64_t d_rt_ns  = ts_to_ns(&rt1)  - ts_to_ns(&rt0);

    printf("\n");
    printf("\tInitial Time     : %10.9f[s]\n", (double)rt0.tv_sec + (double)rt0.tv_nsec * SEC_PER_NS);
    printf("\tFinal   Time     : %10.9f[s]\n", (double)rt1.tv_sec + (double)rt1.tv_nsec * SEC_PER_NS);
    printf("\tDelta adjtime.   : %10lld [ns]\n", (long long)d_rt_ns);
    printf("\tdesired offset   : %10lld [ns]\n", tx.offset * NS_PER_US);
    printf("\tTolerance allowed: %10lld [ns]\n", NS_PER_US/2);
    printf("\n");

    swclock_destroy(c);

    ASSERT_NEAR(d_rt_ns, tx.offset * NS_PER_US, NS_PER_US/2); // Allow 1/1 microsecond tolerance
}

TEST(SwClockV1, FrequencyAdjust) {
    SwClock* clk1 = swclock_create();   // baseline (0 ppm)
    SwClock* clk2 = swclock_create();   // will run +100 ppm

    ASSERT_NE(clk1, nullptr);
    ASSERT_NE(clk2, nullptr);

    // Apply +100 ppm to clk2 using ntp-style scaling (ppm << 16)
    struct timex tx = {0};
    tx.modes = ADJ_FREQUENCY;
    tx.freq  = ppm_to_ntp_freq(100.0);
    swclock_adjtime(clk2, &tx);

    // Capture start
    struct timespec clk1_t0, clk2_t0;
    swclock_gettime(clk1, CLOCK_REALTIME, &clk1_t0);
    swclock_gettime(clk2, CLOCK_REALTIME, &clk2_t0);

    // Let ~200 ms elapse (any reasonable value works; longer = better SNR)
    const int64_t SLEEP_NS = 200000000LL; // 200 ms
    sleep_ns(SLEEP_NS);

    // Capture end
    struct timespec clk1_tn, clk2_tn;
    swclock_gettime(clk1, CLOCK_REALTIME, &clk1_tn);
    swclock_gettime(clk2, CLOCK_REALTIME, &clk2_tn);

    // Deltas in nanoseconds
    const int64_t d1_ns = diff_ns(&clk1_t0, &clk1_tn); // baseline elapsed
    const int64_t d2_ns = diff_ns(&clk2_t0, &clk2_tn); // +100 ppm elapsed

    // Sanity: we actually advanced and both measured roughly the sleep duration
    EXPECT_GT(d1_ns, SLEEP_NS / 2);
    EXPECT_GT(d2_ns, SLEEP_NS / 2);

    // Expected extra time on clk2 due to +100 ppm over the same interval
    // extra_ns_expected = d1_ns * (100 ppm) = d1_ns * 100 / 1e6
    const int64_t extra_ns_measured  = d2_ns - d1_ns;
    const double  expected_ppm       = 100.0;
    const double  measured_ppm       = (double)extra_ns_measured * 1e6 / (double)d1_ns;

    printf("\n");
    printf("\tSleep duration       : %10.9f [s]\n", (double)SLEEP_NS / 1e9);
    printf("\tclk1 delta           : %10.9f [s]\n", (double)d1_ns / 1e9);
    printf("\tclk2 delta           : %10.9f [s]\n", (double)d2_ns / 1e9);
    printf("\tclk2 extra (meas.)   : %10.9f [s]\n", (double)extra_ns_measured / 1e9);
    printf("\tclk2 extra (expect.) : %10.9f [s]\n", (double)(d1_ns * 100 / 1e6) / 1e9);
    printf("\tExpected ppm         : %10.3f [ppm]\n", expected_ppm);
    printf("\tMeasured ppm         : %10.3f [ppm]\n", measured_ppm);
    printf("\n");

    // Use a small tolerance to accommodate scheduling jitter & quantization.
    // 5 ppm tolerance over 200 ms ≈ 1,000 ns wiggle room.
    const double  ppm_tolerance = 5.0;

    EXPECT_NEAR(measured_ppm, expected_ppm, ppm_tolerance)
        << "d1_ns=" << d1_ns
        << " d2_ns=" << d2_ns
        << " extra_ns=" << extra_ns_measured;

    swclock_destroy(clk1);
    swclock_destroy(clk2);
}

TEST(SwClockV1, CompareSwClockAndClockGettime) {
    SwClock* c = swclock_create();

    ASSERT_NE(c, nullptr);

    struct timespec rt0, rt1, raw0, raw1;
    swclock_gettime(c, CLOCK_REALTIME, &rt0);
    clock_gettime(CLOCK_REALTIME, &raw0);

    double t_secs_rt0  = (double)rt0.tv_sec  + (double)rt0.tv_nsec  * SEC_PER_NS;
    double t_secs_raw0 = (double)raw0.tv_sec + (double)raw0.tv_nsec * SEC_PER_NS;
    int64_t dt_ns    = ts_to_ns(&rt0)  - ts_to_ns(&raw0);

    printf("\n");
    printf("\tInitial swclock_gettime: %10.9f[s]\n", t_secs_rt0);
    printf("\tInitial clock_gettime  : %10.9f[s]\n", t_secs_raw0);
    printf("\tInitial difference     : %10lld [ns]\n", (long long)dt_ns);
    printf("\tAllowed difference     : %10lld [ns]\n", NS_PER_US/2);

    // Allow small tolerance due to time between calls (should be < 1 microsecond)
    int64_t initial_diff = abs(dt_ns);
    ASSERT_LT(initial_diff, US_PER_SEC/2); // Within 1/2 µs tolerance
    ASSERT_NEAR(t_secs_rt0, t_secs_raw0, US_PER_SEC/2); // Within 1/2 µs tolerance

    sleep(10); // Ensure at least 1 second passes for more noticeable difference
    
    swclock_gettime(c, CLOCK_REALTIME, &rt1);
    clock_gettime(CLOCK_REALTIME, &raw1);

    double t_secs_rt1  = (double)rt1.tv_sec  + (double)rt1.tv_nsec * SEC_PER_NS;
    double t_secs_raw1 = (double)raw1.tv_sec + (double)raw1.tv_nsec * SEC_PER_NS;
    dt_ns = ts_to_ns(&rt1) - ts_to_ns(&raw1);

    printf("\n");
    printf("\tFinal   swclock_gettime: %10.9f[s]\n", t_secs_rt1);
    printf("\tFinal   clock_gettime  : %10.9f[s]\n", t_secs_raw1);
    printf("\tFinal   difference     : %10lld [ns]\n", (long long)dt_ns);
    printf("\tAllowed difference     : %10lld [ns]\n", US_PER_SEC);
    printf("\n");
    
    // Allow small tolerance due to time between calls and potential drift
    int64_t final_diff = abs(dt_ns);
    ASSERT_LT(final_diff, US_PER_SEC);           // Within 1µs tolerance
    ASSERT_NEAR(t_secs_rt1, t_secs_raw1, US_PER_SEC); // Within 1µs tolerance

    swclock_destroy(c);
}

TEST(SwClockV1, SetTimeRealtimeOnly) {
    SwClock* c = swclock_create();
    ASSERT_NE(c, nullptr);
    
    struct timespec now;
    swclock_gettime(c, CLOCK_REALTIME, &now);
    now.tv_sec += 1;
    swclock_settime(c, CLOCK_REALTIME, &now);
    
    struct timespec after;
    swclock_gettime(c, CLOCK_REALTIME, &after);
    
    printf("\n");
    printf("\tSettime requested   : %10lld [ns]\n", (long long)ts_to_ns(&now));
    printf("\tSettime actual      : %10lld [ns]\n", (long long)ts_to_ns(&after));
    printf("\tExpected difference : %10lld [ns]\n", (long long)ts_to_ns(&after) - (long long)ts_to_ns(&now));
    printf("\tTolerance           : %10lld [ns]\n", NS_PER_US/2);
    printf("\n");

    ASSERT_NEAR(ts_to_ns(&after), ts_to_ns(&now), 2 * NS_PER_US); // Allow 2 microsecond tolerance
    ASSERT_GT(ts_to_ns(&after), ts_to_ns(&now));
    swclock_destroy(c);
}

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
