#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstdint>
#include <vector>
#include <queue>
#include <random>
#include <algorithm>
#include <fstream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <sys/stat.h>

extern "C" {
#include "kf_servo.h"
#include "swclock.h"
#include "swclock_compat.h"
#include "sw_adjtimex.h"
}

using namespace std::chrono;

/* ---------- helpers ---------- */
static inline void sleep_for_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
static inline int64_t steady_now_ns() { return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count(); }
static inline double measure_offset_s(struct SwClock* sw, int64_t master_ns) {
    int64_t sw_ns = swclock_now_ns(sw);
    return (double)((long double)(master_ns - sw_ns) / 1e9L);
}

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

static inline long ppb_to_freq_fixed(long double ppb) {
    long double ppm = ppb / 1000.0L;
    long double scaled = ppm * 65536.0L;
    if (scaled > (long double)std::numeric_limits<long>::max()) return std::numeric_limits<long>::max();
    if (scaled < (long double)std::numeric_limits<long>::min()) return std::numeric_limits<long>::min();
    return (long)std::llround(scaled);
}
static inline double getenv_double(const char* name, double defv) {
    const char* v = std::getenv(name);
    if (!v) return defv;
    char* end=nullptr;
    double d = std::strtod(v,&end);
    return (end && end!=v) ? d : defv;
}
static inline int getenv_int(const char* name, int defv) {
    const char* v = std::getenv(name);
    if (!v) return defv;
    char* end=nullptr;
    long d = std::strtol(v,&end,10);
    return (end && end!=v) ? (int)d : defv;
}
static inline std::string getenv_str(const char* name, const char* defv) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string(defv);
}

/* ---------- models ---------- */
struct GilbertElliott {
    double p_good_to_bad{0.01};
    double p_bad_to_good{0.20};
    double p_loss_good{0.01};
    double p_loss_bad{0.30};
    bool bad{false};
    std::mt19937 rng{12345};
    std::uniform_real_distribution<double> uni{0.0,1.0};

    bool lost() {
        double u = uni(rng);
        if (!bad) { if (u < p_good_to_bad) bad = true; }
        else      { if (u < p_bad_to_good) bad = false; }
        double pl = bad ? p_loss_bad : p_loss_good;
        return uni(rng) < pl;
    }
};

struct WifiChannel {
    std::mt19937 rng{777};
    std::uniform_real_distribution<double> base_lat_ms{2.0, 10.0};
    std::normal_distribution<double> jitter_ms{0.0, 1.5};
    std::bernoulli_distribution reorder_flag{0.08};
    std::uniform_real_distribution<double> reorder_extra_ms{2.0, 20.0};
    double quant_ms{0.5};

    struct Msg { int64_t arrival_ns; double z_meas_s; };
    std::vector<Msg> queue;

    void configure(double base_min_ms, double base_max_ms, double jitter_sigma_ms,
                   double reorder_prob, double reorder_extra_min_ms, double reorder_extra_max_ms,
                   double quant) {
        base_lat_ms = std::uniform_real_distribution<double>(base_min_ms, base_max_ms);
        jitter_ms   = std::normal_distribution<double>(0.0, jitter_sigma_ms);
        reorder_flag= std::bernoulli_distribution(reorder_prob);
        reorder_extra_ms = std::uniform_real_distribution<double>(reorder_extra_min_ms, reorder_extra_max_ms);
        quant_ms = quant;
    }

    void send(int64_t now_ns, double z_true_s) {
        double lat = base_lat_ms(rng) + jitter_ms(rng);
        if (reorder_flag(rng)) lat += reorder_extra_ms(rng);
        lat = std::round(lat / quant_ms) * quant_ms;
        int64_t arrival = now_ns + (int64_t)std::llround(lat * 1e6);
        queue.push_back({arrival, z_true_s + lat/1000.0});
    }

    bool deliver(int64_t now_ns, double& out_z) {
        if (queue.empty()) return false;
        auto it = std::min_element(queue.begin(), queue.end(),
                                   [](const Msg& a, const Msg& b){ return a.arrival_ns < b.arrival_ns; });
        if (it->arrival_ns <= now_ns) {
            out_z = it->z_meas_s;
            queue.erase(it);
            return true;
        }
        return false;
    }
};

struct WifiHarness {
    SwClock* sw{};
    KalmanFilter* kf{};
    int64_t master_start_ns{};
    steady_clock::time_point wall0;

    void init(double init_step_ms, double init_freq_ppb, double Q00, double R) {
        wall0 = steady_clock::now();
        master_start_ns = steady_now_ns();
        sw = swclock_create();
        ASSERT_NE(sw, nullptr);
        kf = kf_create();
        ASSERT_NE(kf, nullptr);
        kf_init(kf, Q00, R);
        
        // Synchronize SwClock to master time initially
        swclock_align_now(sw, master_start_ns);
        
        if (init_freq_ppb != 0.0) swclock_set_freq(sw, init_freq_ppb);
        if (init_step_ms != 0.0) {
            int64_t offset_ns = (int64_t)std::llround(init_step_ms * 1e6);
            swclock_adjust(sw, offset_ns, (int64_t)8e8); // 0.8 s
        }
    }
    void fini() { kf_destroy(kf); swclock_destroy(sw); kf=nullptr; sw=nullptr; }
    int64_t master_now_ns() const {
        int64_t elapsed = duration_cast<nanoseconds>(steady_clock::now() - wall0).count();
        return master_start_ns + elapsed;
    }
};

TEST(KalmanServo_Wifi, WifiStatsAndPlotsReady) {
    /* Tunables via env (defaults in parentheses):
       WIFI_BASE_MIN_MS(2) WIFI_BASE_MAX_MS(10) WIFI_JITTER_MS(1.5)
       WIFI_REORDER_P(0.08) WIFI_REORDER_MIN_MS(2) WIFI_REORDER_MAX_MS(20)
       WIFI_QUANT_MS(0.5)
       LOSS_G2B(0.01) LOSS_B2G(0.2) LOSS_PGOOD(0.01) LOSS_PBAD(0.3)
       INIT_STEP_MS(25) INIT_FREQ_PPB(25000)
       KF_Q00(1e-8) KF_R(2e-6)
       TICK_MS(10) ITERS(1200)
       PERF_OUT(csv path), default: logs/YYYY-MM-DD_HH-MM-SS/kf_wifi_perf.csv
       WARMUP_S(2.0) — stats computed after warmup time
    */
    double base_min = getenv_double("WIFI_BASE_MIN_MS", 2.0);
    double base_max = getenv_double("WIFI_BASE_MAX_MS", 10.0);
    double jitter   = getenv_double("WIFI_JITTER_MS", 1.5);
    double reorderp = getenv_double("WIFI_REORDER_P", 0.08);
    double reorder_min = getenv_double("WIFI_REORDER_MIN_MS", 2.0);
    double reorder_max = getenv_double("WIFI_REORDER_MAX_MS", 20.0);
    double quant_ms = getenv_double("WIFI_QUANT_MS", 0.5);

    double p_g2b = getenv_double("LOSS_G2B", 0.01);
    double p_b2g = getenv_double("LOSS_B2G", 0.2);
    double p_pgood = getenv_double("LOSS_PGOOD", 0.01);
    double p_pbad  = getenv_double("LOSS_PBAD", 0.3);

    double step_ms = getenv_double("INIT_STEP_MS", 25.0);
    double freq_ppb= getenv_double("INIT_FREQ_PPB", 25000.0);
    double Q00     = getenv_double("KF_Q00", 1e-8);
    double R       = getenv_double("KF_R", 2e-6);
    int tick_ms    = getenv_int("TICK_MS", 10);
    int iters      = getenv_int("ITERS", 1200);
    double warmup_s= getenv_double("WARMUP_S", 2.0);
    
    // Create timestamped output directory and CSV path
    std::string output_dir;
    std::string out_csv;
    const char* env_perf_out = getenv("PERF_OUT");
    if (env_perf_out) {
        // If PERF_OUT is specified, use it as-is (for backwards compatibility)
        out_csv = env_perf_out;
        output_dir = ""; // Will be derived from path if needed by Python tool
    } else {
        // Create timestamped directory and use it
        output_dir = create_timestamped_output_dir();
        out_csv = output_dir + "/kf_wifi_perf.csv";
    }

    WifiHarness H;
    H.init(step_ms, freq_ppb, Q00, R);

    WifiChannel chan;
    chan.configure(base_min, base_max, jitter, reorderp, reorder_min, reorder_max, quant_ms);

    GilbertElliott loss;
    loss.p_good_to_bad = p_g2b;
    loss.p_bad_to_good = p_b2g;
    loss.p_loss_good   = p_pgood;
    loss.p_loss_bad    = p_pbad;

    std::ofstream os(out_csv);
    ASSERT_TRUE(os.good()) << "Failed to open PERF_OUT path";
    os << "t_s,offset_s,drift_ppb,had_meas,z_meas_s\n";

    auto t0 = steady_clock::now();
    bool have_last = false;
    steady_clock::time_point last_tp = t0;

    for (int i=0; i<iters; ++i) {
        int64_t now_ns = H.master_now_ns();

        // True offset
        double z_true = measure_offset_s(H.sw, now_ns);

        // Produce a packet (maybe lost)
        if (!loss.lost()) chan.send(now_ns, z_true);

        // Deliver if any arrival
        double z_meas = 0.0;
        bool have = chan.deliver(now_ns, z_meas);

        // dt is elapsed since last delivered sample
        auto nowtp = steady_clock::now();
        double dt = std::chrono::duration<double>(nowtp - last_tp).count();
        if (dt <= 0.0) dt = tick_ms/1000.0;

        if (have) {
            (void)kf_update(H.kf, z_meas, dt);
            last_tp = nowtp;

            struct timex tf{}; tf.modes = ADJ_FREQUENCY;
            tf.freq = ppb_to_freq_fixed(kf_get_drift_ppb(H.kf));
            sw_adjtimex(H.sw, &tf);

            struct timex to{}; to.modes = ADJ_OFFSET;
            to.offset = (long)std::llround(kf_get_offset(H.kf)*1e6);
            sw_adjtimex(H.sw, &to);
        }

        // Record
        double t_s = std::chrono::duration<double>(nowtp - t0).count();
        double off_s = measure_offset_s(H.sw, H.master_now_ns());
        os << t_s << "," << off_s << "," << kf_get_drift_ppb(H.kf) << ","
           << (have?1:0) << "," << (have?z_meas:0.0) << "\n";

        sleep_for_ms(tick_ms);
    }
    os.close();

    // Basic expectations (loose under harsh Wi-Fi)
    double off_ms = std::fabs(measure_offset_s(H.sw, H.master_now_ns())) * 1e3;
    double drift_ppb = std::fabs(kf_get_drift_ppb(H.kf));

    EXPECT_LT(off_ms, 6.0) << "Offset under harsh Wi-Fi should be < 6 ms";
    EXPECT_LT(drift_ppb, 1000.0) << "Drift estimate should be < 1000 ppb";

    H.fini();
    // Informative print for CI logs
    printf("[ WIFI PERF ] wrote CSV: %s\n", out_csv.c_str());
    if (!output_dir.empty()) {
        printf("[ WIFI PERF ] output directory: %s\n", output_dir.c_str());
        
        // Automatically generate plots (run from project root)
        // Since we're already in the project root, use relative path directly
        std::string csv_path = out_csv;
        std::string plot_cmd = "python3 tools/plot_kf_wifi_perf.py " + csv_path;
        printf("[ WIFI PERF ] generating plots...\n");
        int result = system(plot_cmd.c_str());
        if (result == 0) {
            printf("[ WIFI PERF ] plots generated successfully\n");
        } else {
            printf("[ WIFI PERF ] warning: plot generation failed (code %d)\n", result);
        }
    }
}
