#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstdint>
#include <random>
extern "C" {
#include "akf_servo.h"
#include "swclock.h"
#include "swclock_compat.h"
#include "sw_adjtimex.h"
}
using namespace std::chrono;
static inline void msleep(int ms){ std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
static inline int64_t master_now_ns(const steady_clock::time_point& t0, int64_t start_ns){
    int64_t elapsed = duration_cast<nanoseconds>(steady_clock::now() - t0).count();
    return start_ns + elapsed;
}
static inline double measure_offset_s(struct SwClock* sw, int64_t master_ns){
    return (double)((long double)(master_ns - swclock_now_ns(sw))/1e9L);
}
static inline long ppb_to_freq_fixed(long double ppb){
    long double ppm=ppb/1000.0L; long double scaled=ppm*65536.0L;
    if (scaled > (long double)std::numeric_limits<long>::max()) return std::numeric_limits<long>::max();
    if (scaled < (long double)std::numeric_limits<long>::min()) return std::numeric_limits<long>::min();
    return (long)std::llround(scaled);
}
TEST(AKF, ConvergesFromStepAndFreqBias){
    SwClock* sw = swclock_create(); ASSERT_NE(sw,nullptr);
    AdaptiveKalmanFilter* akf = akf_create(); ASSERT_NE(akf,nullptr);
    akf_init(akf,1e-8,1e-6);
    
    int64_t start_ns = duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
    steady_clock::time_point t0 = steady_clock::now();
    
    // Synchronize SwClock to master time initially
    swclock_align_now(sw, start_ns);
    
    swclock_set_freq(sw,+30000.0);
    swclock_adjust(sw,(int64_t)40e6,(int64_t)5e8);
    for (int i=0;i<450;++i){
        double z = measure_offset_s(sw, master_now_ns(t0,start_ns));
        (void)akf_update(akf,z,0.01);
        struct timex tf{}; tf.modes=ADJ_FREQUENCY; tf.freq=ppb_to_freq_fixed(akf_get_drift_ppb(akf)); sw_adjtimex(sw,&tf);
        struct timex to{}; to.modes=ADJ_OFFSET; to.offset=(long)std::llround(akf_get_offset(akf)*1e6); sw_adjtimex(sw,&to);
        msleep(10);
    }
    double off_ms = std::fabs(measure_offset_s(sw, master_now_ns(t0,start_ns)))*1e3;
    EXPECT_LT(off_ms, 1.0);
    EXPECT_LT(std::fabs(akf_get_drift_ppb(akf)), 200.0);
    akf_destroy(akf); swclock_destroy(sw);
}
TEST(AKF, RobustToNoiseAndOutliers){
    SwClock* sw = swclock_create(); ASSERT_NE(sw,nullptr);
    AdaptiveKalmanFilter* akf = akf_create(); ASSERT_NE(akf,nullptr);
    akf_init(akf,1e-8,2e-6);
    
    int64_t start_ns = duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
    steady_clock::time_point t0 = steady_clock::now();
    
    // Synchronize SwClock to master time initially
    swclock_align_now(sw, start_ns);
    
    std::mt19937 rng(9); std::normal_distribution<double> noise(0.0, 300e-6);
    for (int i=0;i<700;++i){
        int64_t mn = master_now_ns(t0,start_ns);
        double z = measure_offset_s(sw, mn) + noise(rng);
        if (i%9==0) z += ( (i%18==0) ? 0.012 : -0.009 );
        (void)akf_update(akf,z,0.01);
        struct timex tf{}; tf.modes=ADJ_FREQUENCY; tf.freq=ppb_to_freq_fixed(akf_get_drift_ppb(akf)); sw_adjtimex(sw,&tf);
        struct timex to{}; to.modes=ADJ_OFFSET; to.offset=(long)std::llround(akf_get_offset(akf)*1e6); sw_adjtimex(sw,&to);
        msleep(10);
    }
    double off_ms = std::fabs(measure_offset_s(sw, master_now_ns(t0,start_ns)))*1e3;
    EXPECT_LT(off_ms, 2.0);
    akf_destroy(akf); swclock_destroy(sw);
}
