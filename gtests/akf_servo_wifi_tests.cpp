#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstdint>
#include <vector>
#include <random>
#include <fstream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include <climits>

extern "C" {
#include "akf_servo.h"
#include "swclock.h"
#include "swclock_compat.h"
#include "sw_adjtimex.h"
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
static inline int64_t steady_now_ns(){ return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count(); }
static inline double measure_offset_s(struct SwClock* sw, int64_t master_ns){
    int64_t sw_ns = swclock_now_ns(sw);
    return (double)((long double)(master_ns - sw_ns) / 1e9L);
}
static inline long ppb_to_freq_fixed(long double ppb){
    long double ppm = ppb / 1000.0L;
    long double scaled = ppm * 65536.0L;
    if (scaled > (long double)std::numeric_limits<long>::max()) return std::numeric_limits<long>::max();
    if (scaled < (long double)std::numeric_limits<long>::min()) return std::numeric_limits<long>::min();
    return (long)std::llround(scaled);
}

struct WifiChannel {
    std::mt19937 rng{777};
    std::uniform_real_distribution<double> base_lat_ms{2.0, 10.0};
    std::normal_distribution<double> jitter_ms{0.0, 1.5};
    std::bernoulli_distribution reorder_flag{0.08};
    std::uniform_real_distribution<double> reorder_extra_ms{2.0, 20.0};
    double quant_ms{0.5};
    struct Msg { int64_t arrival_ns; double z_meas_s; };
    std::vector<Msg> queue;
    void send(int64_t now_ns, double z_true_s){
        double lat = base_lat_ms(rng) + jitter_ms(rng);
        if (reorder_flag(rng)) lat += reorder_extra_ms(rng);
        lat = std::round(lat / quant_ms) * quant_ms;
        int64_t arrival = now_ns + (int64_t)std::llround(lat * 1e6);
        queue.push_back({arrival, z_true_s + lat / 1000.0});
    }
    bool deliver(int64_t now_ns, double& out_z){
        if (queue.empty()) return false;
        auto it = std::min_element(queue.begin(), queue.end(), [](const Msg&a,const Msg&b){return a.arrival_ns<b.arrival_ns;});
        if (it->arrival_ns <= now_ns){ out_z = it->z_meas_s; queue.erase(it); return true; }
        return false;
    }
};

TEST(AKFServo_Wifi, AdaptiveAgainstWifiNoise) {
    SwClock* sw = swclock_create();
    ASSERT_NE(sw,nullptr);
    AdaptiveKalmanFilter* akf = akf_create();
    ASSERT_NE(akf,nullptr);
    akf_init(akf, 1e-8, 2e-6);

    int64_t master_start_ns = steady_now_ns();
    steady_clock::time_point wall0 = steady_clock::now();
    
    // Synchronize SwClock to master time initially
    swclock_align_now(sw, master_start_ns);
    
    auto master_now = [&](){
        int64_t elapsed = duration_cast<nanoseconds>(steady_clock::now() - wall0).count();
        return master_start_ns + elapsed;
    };

    swclock_set_freq(sw, +25000.0);
    swclock_adjust(sw, (int64_t)25e6, (int64_t)8e8);

    WifiChannel chan;

    // Create timestamped output directory and CSV path  
    std::string output_dir = create_timestamped_output_dir();
    std::string out_csv = output_dir + "/akf_wifi_perf.csv";

    const int iters = 900;
    const int tick_ms = 10;
    std::ofstream csv(out_csv);
    csv << "t_s,offset_s,drift_ppb,z_meas_s,had_meas,R_adapt,Q00,Q11\n";

    auto t0 = steady_clock::now();
    auto last_tp = steady_clock::now();
    for (int i=0;i<iters;++i){
        int64_t now = master_now();
        double z_true = measure_offset_s(sw, now);
        chan.send(now, z_true);

        double z_meas=0.0; bool have = chan.deliver(now, z_meas);
        auto nowtp = steady_clock::now();
        double dt = std::chrono::duration<double>(nowtp - last_tp).count();
        if (dt<=0) dt = tick_ms/1000.0;

        if (have){
            (void)akf_update(akf, z_meas, dt);
            last_tp = nowtp;
            struct timex tf{}; tf.modes=ADJ_FREQUENCY;
            tf.freq = ppb_to_freq_fixed(akf_get_drift_ppb(akf));
            sw_adjtimex(sw, &tf);
            struct timex to{}; to.modes=ADJ_OFFSET;
            to.offset = (long)std::llround(akf_get_offset(akf)*1e6);
            sw_adjtimex(sw, &to);
        }
        double t_s = std::chrono::duration<double>(nowtp - t0).count();
        double off_s = measure_offset_s(sw, master_now());
        csv << t_s << "," << off_s << "," << akf_get_drift_ppb(akf) << ","
            << (have?z_meas:0.0) << "," << (have?1:0) << ","
            << akf_get_R_adapt(akf) << "," << akf_get_Q_offset(akf) << "," << akf_get_Q_drift(akf) << "\n";
        sleep_for_ms(tick_ms);
    }
    csv.close();

    double off_ms = std::fabs(measure_offset_s(sw, master_now())) * 1e3;
    double drift_ppb = std::fabs(akf_get_drift_ppb(akf));
    EXPECT_LT(off_ms, 6.0) << "AKF offset under harsh Wi-Fi should be < 6 ms";
    EXPECT_LT(drift_ppb, 1000.0);

    akf_destroy(akf);
    swclock_destroy(sw);
    
    // Informative print for CI logs
    printf("[ WIFI PERF ] wrote CSV: %s\n", out_csv.c_str());
    printf("[ WIFI PERF ] output directory: %s\n", output_dir.c_str());
    
    // Automatically generate plots (run from project root)
    // Since we're already in the project root, use relative path
    std::string csv_path = out_csv;
    std::string plot_cmd = "python3 tools/plot_akf_wifi_perf.py " + csv_path;
    printf("[ WIFI PERF ] generating plots...\n");
    int result = system(plot_cmd.c_str());
    if (result == 0) {
        printf("[ WIFI PERF ] plots generated successfully\n");
    } else {
        printf("[ WIFI PERF ] warning: plot generation failed (code %d)\n", result);
    }
}
