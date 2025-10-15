
#include <chrono>
#include <cmath>
#include <cstdint>
#include <vector>
#include <string>
#include <random>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <thread>
#include <limits>

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

static inline double measure_offset_s(struct SwClock* sw, int64_t master_ns){
    return (double)((long double)(master_ns - swclock_now_ns(sw))/1e9L);
}

static inline long ppb_to_freq_fixed(long double ppb){
    long double ppm=ppb/1000.0L; long double scaled=ppm*65536.0L;
    if (scaled > (long double)std::numeric_limits<long>::max()) return std::numeric_limits<long>::max();
    if (scaled < (long double)std::numeric_limits<long>::min()) return std::numeric_limits<long>::min();
    return (long)std::llround(scaled);
}

struct SweepConfig {
    std::string id;
    std::string servo;
    // Runtime params we can set without API changes
    double akf_q0_over_R = NAN;
    double akf_q1_over_q0 = NAN;
    double akf_adapt_alpha = 0.95;
    double akf_adapt_beta  = 0.98;
    double akf_baseQ_mult  = 1.0;

    double pi_kp = NAN;
    double pi_ki = NAN;

    double mix_pi_kp = NAN;
    double mix_pi_ki = NAN;
};

static void run_config(const WifiPreset& P, const SweepConfig& C, const std::string& out_dir){
    // Setup clocks and chosen servo
    SwClock* sw = swclock_create();
    swclock_set_freq(sw, +25000.0);
    swclock_adjust(sw, (int64_t)25e6, (int64_t)8e8);

    // Create all servos but drive only the one named by config
    KalmanFilter* kf = nullptr;
    AdaptiveKalmanFilter* akf = nullptr;
    ExtendedKalmanFilter* ekf = nullptr;
    AdaptiveExtendedKalmanFilter* aekf = nullptr;
    PIServo* pi = nullptr;
    MixServo* mix = nullptr;

    if (C.servo=="AKF") akf = akf_create();
    else if (C.servo=="KF") kf = kf_create();
    else if (C.servo=="EKF") ekf = ekf_create();
    else if (C.servo=="AEKF") aekf = aekf_create();
    else if (C.servo=="PI") pi = pi_create();
    else if (C.servo=="MIX") mix = mix_create();

    // Init
    if (kf)  kf_init(kf, 1e-8, 2e-6);
    if (akf) akf_init(akf, 1e-8, 2e-6);
    if (ekf) ekf_init(ekf, 1e-8, 2e-6);
    if (aekf) aekf_init(aekf, 1e-8, 2e-6);
    if (pi)  pi_init_default_ptpd(pi);
    if (mix) mix_init(mix, 1e-8, 2e-6);

    // Runtime tuning knobs
    if (akf && !std::isnan(C.akf_q0_over_R) && !std::isnan(C.akf_q1_over_q0)){
        double R = 2e-6;
        double Q0 = C.akf_q0_over_R * R;
        double Q1 = C.akf_q1_over_q0 * Q0;
        akf_set_noise(akf, Q0, Q1, R);
        akf_set_adaptation(akf, Q1, C.akf_adapt_alpha, C.akf_adapt_beta);
    }
    if (pi && !std::isnan(C.pi_kp) && !std::isnan(C.pi_ki)){
        pi_set_gains(pi, C.pi_kp, C.pi_ki);
    }
    if (mix && !std::isnan(C.mix_pi_kp) && !std::isnan(C.mix_pi_ki)){
        mix_set_pi_gains(mix, C.mix_pi_kp, C.mix_pi_ki);
    }

    // Output CSV
    std::string fname = out_dir + "/" + C.servo + "__" + C.id + "__" + P.name + ".csv";
    std::ofstream csv(fname);
    csv << "#config_id=" << C.id << ",servo=" << C.servo
        << ",akf_q0_over_R=" << C.akf_q0_over_R
        << ",akf_q1_over_q0=" << C.akf_q1_over_q0
        << ",akf_alpha=" << C.akf_adapt_alpha
        << ",akf_beta=" << C.akf_adapt_beta
        << ",akf_baseQ_mult=" << C.akf_baseQ_mult
        << ",pi_kp=" << C.pi_kp
        << ",pi_ki=" << C.pi_ki
        << ",mix_pi_kp=" << C.mix_pi_kp
        << ",mix_pi_ki=" << C.mix_pi_ki
        << "\n";
    csv << "t_s,servo,offset_s,drift_ppb,z_meas_s,had_meas\n";

    // Sim loop
    Channel chan(P);
    const int iters = 1200; const int tick_ms=10;
    auto wall0 = steady_clock::now();
    auto master_start = duration_cast<nanoseconds>(wall0.time_since_epoch()).count();
    auto master_now = [&](){ int64_t el=duration_cast<nanoseconds>(steady_clock::now()-wall0).count(); return master_start+el; };

    // Synchronize SwClock to master time initially
    swclock_align_now(sw, master_start);
    
    auto last_tp = steady_clock::now();

    auto drift_ppb = [&](void)->double{
        if (kf) return kf_get_drift_ppb(kf);
        if (akf) return akf_get_drift_ppb(akf);
        if (ekf) return ekf_get_drift_ppb(ekf);
        if (aekf) return aekf_get_drift_ppb(aekf);
        if (pi) return pi_get_drift_ppb(pi);
        if (mix) return mix_get_drift_ppb(mix);
        return 0.0;
    };
    auto offset_s = [&](void)->double{
        if (kf) return kf_get_offset(kf);
        if (akf) return akf_get_offset(akf);
        if (ekf) return ekf_get_offset(ekf);
        if (aekf) return aekf_get_offset(aekf);
        if (pi) return pi_get_offset(pi);
        if (mix) return mix_get_offset(mix);
        return 0.0;
    };
    auto update = [&](double z, double dt){
        if (kf) (void)kf_update(kf, z, dt);
        if (akf) (void)akf_update(akf, z, dt);
        if (ekf) (void)ekf_update(ekf, z, dt);
        if (aekf) (void)aekf_update(aekf, z, dt);
        if (pi) (void)pi_update(pi, z, dt);
        if (mix) (void)mix_update(mix, z, dt);
        // Apply offset first, then frequency
        struct timex to{}; to.modes=ADJ_OFFSET; to.offset=(long)std::llround(offset_s()*1e6); sw_adjtimex(sw,&to);
        struct timex tf{}; tf.modes=ADJ_FREQUENCY; tf.freq=ppb_to_freq_fixed(drift_ppb()); sw_adjtimex(sw,&tf);
    };

    for (int i=0;i<iters;++i){
        int64_t now = master_now();
        double z_true = measure_offset_s(sw, now);
        chan.send(now, z_true);

        double z_meas=0.0; bool have = chan.deliver(now, z_meas);
        auto nowtp = steady_clock::now();
        double dt = std::chrono::duration<double>(nowtp - last_tp).count();
        if (dt<=0) dt = tick_ms/1000.0;
        last_tp = nowtp;

        if (have) update(z_meas, dt);

        double t_s = std::chrono::duration<double>(nowtp - wall0).count();
        double off_s = measure_offset_s(sw, master_now());
        csv << t_s << "," << C.servo << "," << off_s << "," << drift_ppb() << ","
            << (have?z_meas:0.0) << "," << (have?1:0) << "\n";

        std::this_thread::sleep_for(std::chrono::milliseconds(tick_ms));
    }

    csv.close();
    if (kf) kf_destroy(kf);
    if (akf) akf_destroy(akf);
    if (ekf) ekf_destroy(ekf);
    if (aekf) aekf_destroy(aekf);
    if (pi) pi_destroy(pi);
    if (mix) mix_destroy(mix);
    swclock_destroy(sw);
}

int main(int argc, char** argv){
    std::string out_dir = "out";
    if (argc > 1) out_dir = argv[1];
    (void)argv;

    // Build a small default grid (safe, fast)
    std::vector<SweepConfig> grid;

    // AKF grid (runtime knobs only)
    double akf_q0_over_R_vals[] = {0.5, 0.8, 1.2};
    double akf_q1_over_q0_vals[] = {0.1, 0.2};
    for (double q0r : akf_q0_over_R_vals){
        for (double q1q0 : akf_q1_over_q0_vals){
            SweepConfig c; c.servo="AKF";
            c.akf_q0_over_R = q0r; c.akf_q1_over_q0 = q1q0;
            c.akf_adapt_alpha = 0.95; c.akf_adapt_beta = 0.98; c.akf_baseQ_mult=1.0;
            c.id = "AKF_Q0R" + std::to_string(q0r) + "_Q1Q0" + std::to_string(q1q0);
            grid.push_back(c);
        }
    }

    // MIX grid (PI gains only at runtime; step/abs/deadzone may be compile-time defines)
    double mix_kp_vals[] = {0.05, 0.065, 0.08};
    double mix_ki_vals[] = {0.001, 0.0015, 0.002};
    for (double kp : mix_kp_vals){
        for (double ki : mix_ki_vals){
            SweepConfig c; c.servo="MIX"; c.mix_pi_kp=kp; c.mix_pi_ki=ki;
            c.id = "MIX_KP" + std::to_string(kp) + "_KI" + std::to_string(ki);
            grid.push_back(c);
        }
    }

    // PI grid (pure PI)
    double pi_kp_vals[] = {0.08, 0.1, 0.12};
    double pi_ki_vals[] = {0.0008, 0.001, 0.0015};
    for (double kp : pi_kp_vals){
        for (double ki : pi_ki_vals){
            SweepConfig c; c.servo="PI"; c.pi_kp=kp; c.pi_ki=ki;
            c.id = "PI_KP" + std::to_string(kp) + "_KI" + std::to_string(ki);
            grid.push_back(c);
        }
    }

    // Ensure output structure
    std::error_code ec; std::filesystem::create_directories(out_dir, ec);

    // Run
    for (const auto& P : PRESETS){
        for (const auto& C : grid){
            run_config(P, C, out_dir);
        }
    }
    std::cout << "Done. CSVs in " << out_dir << std::endl;
    return 0;
}
