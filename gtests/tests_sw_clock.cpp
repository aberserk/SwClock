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

static inline int64_t ts_to_ns(const struct timespec* t) {
    return (int64_t)t->tv_sec * NS_PER_SEC + t->tv_nsec;
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

TEST(SwClockV1, OffsetImmediateStep) {
    SwClock* c = swclock_create();
    ASSERT_NE(c, nullptr);
    
    struct timespec rt0, rt1;
    swclock_gettime(c, CLOCK_REALTIME, &rt0);
    
    struct timex tx = {0};
    tx.modes = ADJ_OFFSET | ADJ_MICRO;
    tx.offset = 500000;
    swclock_adjtime(c, &tx);
    
    swclock_gettime(c, CLOCK_REALTIME, &rt1);
    
    ASSERT_GT(ts_to_ns(&rt1) - ts_to_ns(&rt0), 400000000LL);
    swclock_destroy(c);
}

TEST(SwClockV1, FrequencyAdjust) {
    SwClock* c = swclock_create();

    ASSERT_NE(c, nullptr);
    
    struct timex tx = {0};
    tx.modes = ADJ_FREQUENCY;
    tx.freq = (long)(100.0 * 65536.0);
    swclock_adjtime(c, &tx);
    
    struct timespec rt0, rt1, raw0, raw1;
    swclock_gettime(c, CLOCK_REALTIME, &rt0);
    clock_gettime(CLOCK_MONOTONIC_RAW, &raw0);
    
    sleep_ns(200000000);
    
    swclock_gettime(c, CLOCK_REALTIME, &rt1);
    clock_gettime(CLOCK_MONOTONIC_RAW, &raw1);

    double t_secs_raw0 = (double)raw0.tv_sec + (double)raw0.tv_nsec / 1e9;
    double t_secs_raw1 = (double)raw1.tv_sec + (double)raw1.tv_nsec / 1e9;
    double t_secs_rt0  = (double)rt0.tv_sec  + (double)rt0.tv_nsec  / 1e9;
    double t_secs_rt1  = (double)rt1.tv_sec  + (double)rt1.tv_nsec  / 1e9;

    int64_t d_rt_ns  = ts_to_ns(&rt1)  - ts_to_ns(&rt0);
    int64_t d_raw_ns = ts_to_ns(&raw1) - ts_to_ns(&raw0);
  
    int64_t d_rt_ns2 =
    (int64_t)(rt1.tv_sec - rt0.tv_sec) * NS_PER_SEC +
    (int64_t)(rt1.tv_nsec - rt0.tv_nsec);

    int64_t d_raw_ns2 =
    (int64_t)(raw1.tv_sec - raw0.tv_sec) * NS_PER_SEC +
    (int64_t)(raw1.tv_nsec - raw0.tv_nsec);

    printf("\n");
    printf("\tInitial clock_gettime  : %10.9f[s]\n", t_secs_raw0);
    printf("\tFinal   clock_gettime  : %10.9f[s]\n", t_secs_raw1);
    printf("\tElapsed clock_gettime  :   %lld [ns]\n", (long long)d_raw_ns);
    printf("\tElapsed clock_gettime  : %10.9f[s]\n", (double)d_raw_ns2 * SEC_PER_NS);
    printf("\n");
    printf("\tInitial swclock_gettime: %10.9f[s]\n", t_secs_rt0);
    printf("\tFinal   swclock_gettime: %10.9f[s]\n", t_secs_rt1);
    printf("\tElapsed swclock_gettime:   %lld [ns]\n", (long long)d_rt_ns);
    printf("\tElapsed swclock_gettime: %10.9f[s]\n", (double)d_rt_ns2 * SEC_PER_NS);
    printf("\n");

    ASSERT_GT(d_rt_ns, d_raw_ns);
    ASSERT_GT(d_rt_ns2, d_raw_ns2);

    swclock_destroy(c);
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
    printf("\tInitial difference     :   %lld [ns]\n", (long long)dt_ns);

    // Allow small tolerance due to time between calls (should be < 1 microsecond)
    int64_t initial_diff = abs(dt_ns);
    ASSERT_LT(initial_diff, US_PER_SEC); // Within 1µs tolerance
    ASSERT_NEAR(t_secs_rt0, t_secs_raw0, US_PER_SEC); // Within 1µs tolerance

    sleep(1); // Ensure at least 1 second passes for more noticeable difference
    
    swclock_gettime(c, CLOCK_REALTIME, &rt1);
    clock_gettime(CLOCK_REALTIME, &raw1);

    double t_secs_rt1  = (double)rt1.tv_sec  + (double)rt1.tv_nsec * SEC_PER_NS;
    double t_secs_raw1 = (double)raw1.tv_sec + (double)raw1.tv_nsec * SEC_PER_NS;
    dt_ns = ts_to_ns(&rt1) - ts_to_ns(&raw1);

    printf("\n");
    printf("\tFinal   swclock_gettime: %10.9f[s]\n", t_secs_rt1);
    printf("\tFinal   clock_gettime  : %10.9f[s]\n", t_secs_raw1);
    printf("\tFinal   difference     :   %lld [ns]\n", (long long)dt_ns);
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
    
    ASSERT_GT(ts_to_ns(&after), ts_to_ns(&now) - NS_PER_SEC);
    swclock_destroy(c);
}

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
