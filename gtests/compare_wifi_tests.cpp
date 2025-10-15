
#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstdint>
#include <vector>
#include <random>
#include <algorithm>
#include <fstream>
#include <string>

extern "C" {
#include "swclock.h"
#include "swclock_compat.h"
#include "sw_adjtimex.h"
#include "kf_servo.h"
#include "akf_servo.h"
#include "ekf_servo.h"
#include "aekf_servo.h"
#include "pi_servo.h"
#include "mix_servo.h"
}

using namespace std::chrono;

static std::string create_timestamped_output_dir(const std::string& base_dir = "logs") {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto tm = *std::localtime(&time_t);
    
    std::ostringstream oss;
    oss << base_dir << "/" << std::put_time(&tm, "%Y-%m-%d_%H-%M-%S");
    std::string dir_path = oss.str();
    
    // Create the directory (cross-platform mkdir)
    std::string mkdir_cmd = "mkdir -p " + dir_path;
    int result = system(mkdir_cmd.c_str());
    if (result != 0) {
        throw std::runtime_error("Failed to create output directory: " + dir_path);
    }
    
    return dir_path;
}

static inline void sleep_for_ms(int ms){ std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
static inline int64_t now_ns(){ return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count(); }
static inline double measure_offset_s(struct SwClock* sw, int64_t master_ns){
    return (double)((long double)(master_ns - swclock_now_ns(sw))/1e9L);
}
static inline long ppb_to_freq_fixed(long double ppb){
    long double ppm=ppb/1000.0L; long double scaled=ppm*65536.0L;
    if (scaled > (long double)std::numeric_limits<long>::max()) return std::numeric_limits<long>::max();
    if (scaled < (long double)std::numeric_limits<long>::min()) return std::numeric_limits<long>::min();
    return (long)std::llround(scaled);
}

struct WifiPreset {
    const char* name;
    double base_min_ms, base_max_ms;
    double jitter_ms;
    double reorder_p, reorder_min_ms, reorder_max_ms;
    double quant_ms;
    double loss_g2b, loss_b2g, loss_pgood, loss_pbad; // Gilbert-Elliot
};

static const WifiPreset PRESETS[] = {
    {"Good", 2.0, 6.0, 0.4, 0.02, 1.0, 8.0, 0.25, 0.002, 0.25, 0.02},
    {"Moderate", 2.0, 10.0, 1.2, 0.06, 2.0, 15.0, 0.35, 0.01, 0.25, 0.1},
    {"Harsh", 3.0, 18.0, 2.2, 0.10, 4.0, 25.0, 0.5, 0.02, 0.35, 0.30},
    {"BurstyLoss", 4.0, 14.0, 1.5, 0.08, 2.0, 20.0, 0.5, 0.005, 0.05, 0.45}
};

struct GEState { bool bad=false; };
static bool ge_loss(std::mt19937& rng, GEState& st, double p_g2b, double p_b2g, double p_good_drop, double p_bad_drop){
    std::bernoulli_distribution g2b(p_g2b), b2g(p_b2g);
    if (!st.bad) { if (g2b(rng)) st.bad = true; }
    else { if (b2g(rng)) st.bad = false; }
    std::bernoulli_distribution drop(st.bad ? p_bad_drop : p_good_drop);
    return drop(rng);
}

struct Channel {
    WifiPreset P;
    std::mt19937 rng{12345};
    std::uniform_real_distribution<double> base_lat_ms{2.0,10.0};
    std::normal_distribution<double> jitter{0.0,1.0};
    std::bernoulli_distribution reorder_flag{0.08};
    std::uniform_real_distribution<double> reorder_extra{2.0,20.0};
    GEState ge{};
    struct Msg { int64_t arrival_ns; double z_meas_s; };
    std::vector<Msg> q;

    Channel(const WifiPreset& p):P(p),
      base_lat_ms(p.base_min_ms,p.base_max_ms),
      jitter(0.0,p.jitter_ms),
      reorder_flag(p.reorder_p),
      reorder_extra(p.reorder_min_ms,p.reorder_max_ms) {}

    void send(int64_t now_ns, double z_true_s){
        if (ge_loss(rng, ge, P.loss_g2b, P.loss_b2g, P.loss_pgood, P.loss_pbad)) return;
        double lat = base_lat_ms(rng) + jitter(rng);
        if (reorder_flag(rng)) lat += reorder_extra(rng);
        lat = std::round(lat / P.quant_ms) * P.quant_ms;
        int64_t arrival = now_ns + (int64_t)std::llround(lat * 1e6);
        q.push_back({arrival, z_true_s + lat/1000.0});
    }

    bool deliver(int64_t now_ns, double& out_z){
        if (q.empty()) return false;
        auto it = std::min_element(q.begin(), q.end(), [](const Msg&a,const Msg&b){return a.arrival_ns<b.arrival_ns;});
        if (it->arrival_ns <= now_ns){ out_z = it->z_meas_s; q.erase(it); return true; }
        return false;
    }
};

struct ServoRunner {
    SwClock* sw{};
    void* kf{}; void* akf{}; void* ekf{}; void* aekf{}; void* pi{}; void* mix{};
    enum Kind { KF, AKF, EKF, AEKF, PI, MIX } kind;

    explicit ServoRunner(Kind k): kind(k) {
        sw = swclock_create();
        if (k==KF)   kf = kf_create();
        if (k==AKF) akf = akf_create();
        if (k==EKF) ekf = ekf_create();
        if (k==AEKF) aekf = aekf_create();
        if (k==PI)   pi = pi_create();
        if (k==MIX)  mix = mix_create();

        swclock_set_freq(sw, +25000.0);
        swclock_adjust(sw, (int64_t)25e6, (int64_t)8e8);

        if (kf)   kf_init((KalmanFilter*)kf, 1e-8, 2e-6);
        if (akf) akf_init((AdaptiveKalmanFilter*)akf, 1e-8, 2e-6);
        if (ekf) ekf_init((ExtendedKalmanFilter*)ekf, 1e-8, 2e-6);
        if (aekf) aekf_init((AdaptiveExtendedKalmanFilter*)aekf, 1e-8, 2e-6);
        if (pi)  { pi_init_default_ptpd((PIServo*)pi); }
        if (mix) { mix_init((MixServo*)mix, 1e-8, 2e-6); } // AKF(q,r), PI defaults
    }
    ~ServoRunner(){
        if (kf)   kf_destroy((KalmanFilter*)kf);
        if (akf) akf_destroy((AdaptiveKalmanFilter*)akf);
        if (ekf) ekf_destroy((ExtendedKalmanFilter*)ekf);
        if (aekf) aekf_destroy((AdaptiveExtendedKalmanFilter*)aekf);
        if (pi)   pi_destroy((PIServo*)pi);
        if (mix)  mix_destroy((MixServo*)mix);
        swclock_destroy(sw);
    }

    double drift_ppb() const {
        switch(kind){
            case KF:   return kf_get_drift_ppb((KalmanFilter*)kf);
            case AKF:  return akf_get_drift_ppb((AdaptiveKalmanFilter*)akf);
            case EKF:  return ekf_get_drift_ppb((ExtendedKalmanFilter*)ekf);
            case AEKF: return aekf_get_drift_ppb((AdaptiveExtendedKalmanFilter*)aekf);
            case PI:   return pi_get_drift_ppb((PIServo*)pi);
            case MIX:  return mix_get_drift_ppb((MixServo*)mix);
        }
        return 0.0;
    }
    double offset_s() const {
        switch(kind){
            case KF:   return kf_get_offset((KalmanFilter*)kf);
            case AKF:  return akf_get_offset((AdaptiveKalmanFilter*)akf);
            case EKF:  return ekf_get_offset((ExtendedKalmanFilter*)ekf);
            case AEKF: return aekf_get_offset((AdaptiveExtendedKalmanFilter*)aekf);
            case PI:   return pi_get_offset((PIServo*)pi);
            case MIX:  return mix_get_offset((MixServo*)mix);
        }
        return 0.0;
    }
    void update(double z, double dt){
        switch(kind){
            case KF:   (void)kf_update((KalmanFilter*)kf, z, dt); break;
            case AKF:  (void)akf_update((AdaptiveKalmanFilter*)akf, z, dt); break;
            case EKF:  (void)ekf_update((ExtendedKalmanFilter*)ekf, z, dt); break;
            case AEKF: (void)aekf_update((AdaptiveExtendedKalmanFilter*)aekf, z, dt); break;
            case PI:   (void)pi_update((PIServo*)pi, z, dt); break;
            case MIX:  (void)mix_update((MixServo*)mix, z, dt); break;
        }
        struct timex tf{}; tf.modes=ADJ_FREQUENCY; tf.freq=ppb_to_freq_fixed(drift_ppb()); sw_adjtimex(sw,&tf);
        struct timex to{}; to.modes=ADJ_OFFSET; to.offset=(long)std::llround(offset_s()*1e6); sw_adjtimex(sw,&to);
    }
};

static void run_condition(const WifiPreset& P, const std::string& csv_prefix){
    int64_t master_start = now_ns();
    steady_clock::time_point wall0 = steady_clock::now();
    auto master_now = [&](){ int64_t el=duration_cast<nanoseconds>(steady_clock::now()-wall0).count(); return master_start+el; };

    Channel chan(P);
    const int iters = 1200; const int tick_ms=10;

    ServoRunner k(ServoRunner::KF);
    ServoRunner a(ServoRunner::AKF);
    ServoRunner e(ServoRunner::EKF);
    ServoRunner ae(ServoRunner::AEKF);
    ServoRunner pi(ServoRunner::PI);
    ServoRunner m(ServoRunner::MIX);

    // Synchronize all SwClocks to master time initially  
    swclock_align_now(k.sw, master_start);
    swclock_align_now(a.sw, master_start);
    swclock_align_now(e.sw, master_start);
    swclock_align_now(ae.sw, master_start);
    swclock_align_now(pi.sw, master_start);
    swclock_align_now(m.sw, master_start);

    std::ofstream csv(csv_prefix + "_" + P.name + ".csv");
    csv << "t_s,servo,offset_s,drift_ppb,z_meas_s,had_meas\n";

    auto t0 = steady_clock::now();
    auto last_tp = steady_clock::now();

    for (int i=0;i<iters;++i){
        int64_t now = master_now();
        double z_true = measure_offset_s(k.sw, now);
        chan.send(now, z_true);

        double z_meas=0.0; bool have = chan.deliver(now, z_meas);
        auto nowtp = steady_clock::now();
        double dt = std::chrono::duration<double>(nowtp - last_tp).count();
        if (dt<=0) dt = tick_ms/1000.0;
        last_tp = nowtp;

        if (have){
            k.update(z_meas, dt);
            a.update(z_meas, dt);
            e.update(z_meas, dt);
            ae.update(z_meas, dt);
            pi.update(z_meas, dt);
            m.update(z_meas, dt);
        }

        double t_s = std::chrono::duration<double>(nowtp - t0).count();
        auto log = [&](const char* name, const ServoRunner& S){
            double off_s = measure_offset_s(S.sw, master_now());
            csv << t_s << "," << name << "," << off_s << "," << S.drift_ppb() << ","
                << (have?z_meas:0.0) << "," << (have?1:0) << "\n";
        };
        log("KF", k); log("AKF", a); log("EKF", e); log("AEKF", ae); log("PI", pi); log("MIX", m);

        sleep_for_ms(tick_ms);
    }
    csv.close();
}

TEST(CompareWifi, PresetsCompareAllWithMIX){
    // Create timestamped output directory
    std::string output_dir = create_timestamped_output_dir();
    printf("[ COMPARE WIFI ] output directory: %s\n", output_dir.c_str());
    
    for (const auto& P : PRESETS){
        printf("[ COMPARE WIFI ] running condition: %s\n", P.name);
        run_condition(P, output_dir + "/compare_wifi");
        std::string csv_path = output_dir + "/compare_wifi_" + P.name + ".csv";
        printf("[ COMPARE WIFI ] wrote CSV: %s\n", csv_path.c_str());
    }
    
    printf("[ COMPARE WIFI ] comparison complete - check %s for results\n", output_dir.c_str());
    SUCCEED();
}
