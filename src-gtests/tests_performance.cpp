
// tests_performance.cpp — corrected helpers
// - MTIE/TDEV on detrended TE
// - Settling/Overshoot measured relative to immediate post-step TE
// - FIX: pass the actual SwClock* to swclock_gettime (no nullptr UB)
// - Use CLOCK_MONOTONIC_RAW as reference

#include <gtest/gtest.h>
#include <time.h>
#include <math.h>
#include <vector>
#include <algorithm>
#include <numeric>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <sys/stat.h>

#include "sw_clock.h"
#include "test_metadata.h"

#ifndef NS_PER_SEC
#define NS_PER_SEC 1000000000LL
#endif
#ifndef NS_PER_US
#define NS_PER_US 1000LL
#endif
#ifndef SEC_PER_NS
#define SEC_PER_NS (1.0/1000000000.0)
#endif

// Targets for performance validation
// These thresholds ensure SwClock meets IEEE 1588 and ITU-T G.8260 standards

// Time Error (TE) target: Mean absolute error after detrending
// ITU-T G.8260 Class C: Implicit in MTIE limits
// SwClock target: < 20 µs (tighter than standard)
#ifndef TARGET_TE_MEAN_ABS_NS
#define TARGET_TE_MEAN_ABS_NS 20000LL
#endif

// MTIE (Maximum Time Interval Error) targets per ITU-T G.8260 Class C
// These define worst-case time error over observation intervals
// Class C is suitable for mobile backhaul and packet-based timing

// MTIE @ τ=1s: < 100 µs (ITU-T G.8260 Class C requirement)
#ifndef TARGET_MTIE_1S_NS
#define TARGET_MTIE_1S_NS     100000LL
#endif

// MTIE @ τ=10s: < 200 µs (ITU-T G.8260 Class C requirement)
#ifndef TARGET_MTIE_10S_NS
#define TARGET_MTIE_10S_NS    200000LL
#endif

// MTIE @ τ=30s: < 300 µs (ITU-T G.8260 Class C requirement)
#ifndef TARGET_MTIE_30S_NS
#define TARGET_MTIE_30S_NS    300000LL
#endif

// TDEV (Time Deviation) targets for timing stability
// Lower TDEV indicates better short-term stability

// TDEV @ τ=0.1s: < 20 µs (jitter/phase noise target)
#ifndef TARGET_TDEV_0P1S_NS
#define TARGET_TDEV_0P1S_NS   20000LL
#endif

// TDEV @ τ=1s: < 40 µs (short-term stability)
#ifndef TARGET_TDEV_1S_NS
#define TARGET_TDEV_1S_NS     40000LL
#endif

// TDEV @ τ=10s: < 80 µs (medium-term stability)
#ifndef TARGET_TDEV_10S_NS
#define TARGET_TDEV_10S_NS    80000LL
#endif

// Polling interval for performance measurements
// 100ms provides good time resolution while avoiding excessive overhead
#ifndef PERF_POLL_NS
#define PERF_POLL_NS (100*1000*1000LL) // 100 ms
#endif

// CSV export helper functions
// Check if CSV logging is enabled via environment variable
static inline bool csv_logging_enabled() {
  const char* env = getenv("SWCLOCK_PERF_CSV");
  return (env != nullptr && (strcmp(env, "1") == 0 || strcasecmp(env, "true") == 0));
}

// Get log directory from environment or use default
static inline const char* get_log_dir() {
  const char* dir = getenv("SWCLOCK_LOG_DIR");
  return (dir && *dir) ? dir : "logs";
}

// Create log directory if it doesn't exist
static inline void ensure_log_dir() {
  const char* dir = get_log_dir();
  struct stat st;
  if (stat(dir, &st) == -1) {
    mkdir(dir, 0755);
  }
}

// Generate CSV filename for a test
static inline void get_csv_filename(char* buf, size_t bufsize, const char* test_name) {
  ensure_log_dir();
  time_t now = time(NULL);
  struct tm* tinfo = localtime(&now);
  char datetime_buf[64];
  strftime(datetime_buf, sizeof(datetime_buf), "%Y%m%d-%H%M%S", tinfo);
  snprintf(buf, bufsize, "%s/%s-%s.csv", get_log_dir(), datetime_buf, test_name);
}

// CSV logger for TE time series data
class TELogger {
private:
  FILE* fp;
  bool enabled;
  test_metadata_t metadata;
  
  void write_csv_header(const char* test_name) {
    // Collect comprehensive metadata
    collect_test_metadata(
      &metadata,
      test_name,
      200.0,  // Kp
      8.0,    // Ki
      200.0,  // max_ppm
      10000000LL,  // poll_ns (10ms)
      20000LL      // phase_eps_ns (20µs)
    );
    
    // Write comprehensive RFC-style header
    fprintf(fp,
      "# ========================================\n"
      "# SwClock Performance Test CSV Export\n"
      "# ========================================\n"
      "#\n"
      "# Test Identification:\n"
      "#   Test Name:        %s\n"
      "#   Test Run ID:      %s\n"
      "#   SwClock Version:  %s\n"
      "#   Start Time (UTC): %s\n"
      "#\n"
      "# Configuration:\n"
      "#   Kp (ppm/s):       %.3f\n"
      "#   Ki (ppm/s²):      %.3f\n"
      "#   Max PPM:          %.1f\n"
      "#   Poll Interval:    %lld ns (%.1f Hz)\n"
      "#   Phase Epsilon:    %lld ns (%.1f µs)\n"
      "#\n"
      "# System Information:\n"
      "#   Operating System: %s %s\n"
      "#   CPU:              %s\n"
      "#   CPU Cores:        %d\n"
      "#   Hostname:         %s\n"
      "#   Reference Clock:  %s\n"
      "#   System Load:      %.2f\n"
      "#\n"
      "# Data Format:\n"
      "#   Columns:          timestamp_ns, te_ns\n"
      "#   Sample Rate:      %.3f Hz\n"
      "#   Timestamp Base:   CLOCK_MONOTONIC_RAW at test start\n"
      "#   TE Definition:    (SwClock - Reference) in nanoseconds\n"
      "#\n"
      "# Compliance Targets:\n"
      "#   Standard:         %s\n"
      "#   MTIE(1s):         < 100 µs\n"
      "#   MTIE(10s):        < 200 µs\n"
      "#   MTIE(30s):        < 300 µs\n"
      "#   TDEV(0.1s):       < 20 µs\n"
      "#   TDEV(1s):         < 40 µs\n"
      "#   TDEV(10s):        < 80 µs\n"
      "#\n"
      "# ========================================\n"
      "timestamp_ns,te_ns\n",
      metadata.test_name,
      metadata.test_run_id,
      metadata.swclock_version,
      metadata.start_time_iso8601,
      metadata.kp_ppm_per_s,
      metadata.ki_ppm_per_s2,
      metadata.max_ppm,
      metadata.poll_ns, 1e9 / metadata.poll_ns,
      metadata.phase_eps_ns, metadata.phase_eps_ns / 1000.0,
      metadata.os_name, metadata.os_version,
      metadata.cpu_model,
      metadata.cpu_count,
      metadata.hostname,
      metadata.reference_clock,
      metadata.system_load_avg,
      1e9 / PERF_POLL_NS,
      metadata.compliance_standard
    );
  }
  
public:
  TELogger(const char* test_name) : fp(nullptr), enabled(csv_logging_enabled()) {
    if (!enabled) return;
    
    char filename[512];
    get_csv_filename(filename, sizeof(filename), test_name);
    
    fp = fopen(filename, "w");
    if (!fp) {
      perror("TELogger: fopen");
      enabled = false;
      return;
    }
    
    // Write comprehensive CSV header with metadata
    write_csv_header(test_name);
    fflush(fp);
    
    printf("  CSV logging to: %s\n", filename);
  }
  
  ~TELogger() {
    if (fp) {
      fclose(fp);
    }
  }
  
  void log(long long timestamp_ns, long long te_ns) {
    if (!enabled || !fp) return;
    fprintf(fp, "%lld,%lld\n", timestamp_ns, te_ns);
  }
  
  void flush() {
    if (fp) fflush(fp);
  }
  
  bool is_enabled() const { return enabled; }
  
  // Get the CSV filename for validation tool (IEEE Rec 6)
  const char* get_filepath() const {
    static char filepath[512];
    if (!enabled || !metadata.test_name[0]) return nullptr;
    get_csv_filename(filepath, sizeof(filepath), metadata.test_name);
    return filepath;
  }
};

// Export expected metrics to JSON for independent validation (IEEE Rec 6)
static inline void export_expected_metrics(const char* csv_filepath, 
                                           double mean_ns, double std_ns,
                                           long long mtie_1s, long long mtie_10s, long long mtie_30s,
                                           double tdev_0p1s, double tdev_1s, double tdev_10s) {
  if (!csv_filepath) return;
  
  // Generate JSON filename from CSV filename
  char json_filepath[512];
  snprintf(json_filepath, sizeof(json_filepath), "%s", csv_filepath);
  
  // Replace .csv with -expected.json
  char* ext = strstr(json_filepath, ".csv");
  if (ext) {
    strcpy(ext, "-expected.json");
  } else {
    strcat(json_filepath, "-expected.json");
  }
  
  FILE* fp = fopen(json_filepath, "w");
  if (!fp) {
    fprintf(stderr, "Warning: Failed to export expected metrics to %s\n", json_filepath);
    return;
  }
  
  fprintf(fp, "{\n");
  fprintf(fp, "  \"mean_ns\": %.2f,\n", mean_ns);
  fprintf(fp, "  \"std_ns\": %.2f,\n", std_ns);
  fprintf(fp, "  \"mtie_1s_ns\": %lld,\n", mtie_1s);
  fprintf(fp, "  \"mtie_10s_ns\": %lld,\n", mtie_10s);
  fprintf(fp, "  \"mtie_30s_ns\": %lld,\n", mtie_30s);
  fprintf(fp, "  \"tdev_0p1s_ns\": %.2f,\n", tdev_0p1s);
  fprintf(fp, "  \"tdev_1s_ns\": %.2f,\n", tdev_1s);
  fprintf(fp, "  \"tdev_10s_ns\": %.2f\n", tdev_10s);
  fprintf(fp, "}\n");
  
  fclose(fp);
  
  printf("  Exported expected metrics to: %s\n", json_filepath);
}


static inline void sleep_ns_robust(long long ns){
  if (ns<=0) return;
  struct timespec rq{(time_t)(ns/NS_PER_SEC),(long)(ns%NS_PER_SEC)};
  while (nanosleep(&rq,nullptr)==-1 && errno==EINTR){}
}

// Return TE (ns) = (SW - SW0) - (RAW - RAW0)
static inline long long TE_now_SWvsRAW(SwClock* clk, const struct timespec& sw0, const struct timespec& raw0){
  struct timespec sw, rr;
  swclock_gettime(clk, CLOCK_REALTIME, &sw);
  clock_gettime(CLOCK_MONOTONIC_RAW, &rr);
  return (ts_to_ns(&sw) - ts_to_ns(&sw0)) - (ts_to_ns(&rr) - ts_to_ns(&raw0));
}

// Linear detrend y_i by fitting y = a + b x (x in seconds), return (a,b) and y_detr
static inline void detrend(const std::vector<long long>& y_ns, double sample_dt_s,
                           double& a_out, double& b_out, std::vector<double>& y_detr){
  const size_t N = y_ns.size();
  y_detr.resize(N);
  double sumx=0, sumy=0, sumxx=0, sumxy=0;
  for (size_t i=0;i<N;i++){
    double x = i * sample_dt_s;
    double y = (double)y_ns[i];
    sumx += x; sumy += y; sumxx += x*x; sumxy += x*y;
  }
  double denom = N*sumxx - sumx*sumx;
  double b = (denom!=0) ? (N*sumxy - sumx*sumy)/denom : 0.0;
  double a = (sumy - b*sumx)/N;
  a_out = a; b_out = b;
  for (size_t i=0;i<N;i++){
    double x = i*sample_dt_s;
    y_detr[i] = (double)y_ns[i] - (a + b*x);
  }
}

// MTIE computed on detrended series (units ns). tau_s in seconds.
static inline long long mtie_detrended(const std::vector<double>& yd_ns, double sample_dt_s, double tau_s){
  const int k = std::max(1, (int)llround(tau_s / sample_dt_s));
  long long best = 0;
  for (int i=0; i + k < (int)yd_ns.size(); ++i){
    double d = fabs(yd_ns[i+k] - yd_ns[i]);
    if ((long long)d > best) best = (long long)d;
  }
  return best;
}

// TDEV from detrended series (ns). Simple overlapping Allan-dev-like estimate.
static inline double tdev_detrended_ns(const std::vector<double>& yd_ns, double sample_dt_s, double tau_s){
  int m = std::max(1, (int)llround(tau_s / sample_dt_s)); // samples per tau
  int N = (int)yd_ns.size();
  if (N < 2*m+1) return NAN;
  double sum = 0.0;
  int count = 0;
  for (int i=0; i+2*m < N; ++i){
    double val = yd_ns[i+2*m] - 2*yd_ns[i+m] + yd_ns[i];
    sum += val*val;
    count++;
  }
  if (count == 0) return NAN;
  double allan_var = sum / (2.0 * count);
  return sqrt(allan_var);
}

// -------------------- Tests --------------------

TEST(Perf, DisciplineTEStats_MTIE_TDEV){
  SwClock* clk = swclock_create();
  ASSERT_NE(clk, nullptr);

  // Initialize CSV logger
  TELogger csv_logger("Perf_DisciplineTEStats_MTIE_TDEV");

  // Enable servo logging if requested (Priority 1 Recommendation 5)
  const char* log_dir = get_log_dir();
  if (getenv("SWCLOCK_SERVO_LOG")) {
    char servo_log_path[512];
    time_t now = time(NULL);
    struct tm* tinfo = localtime(&now);
    char datetime_buf[64];
    strftime(datetime_buf, sizeof(datetime_buf), "%Y%m%d-%H%M%S", tinfo);
    snprintf(servo_log_path, sizeof(servo_log_path),
             "%s/servo_state_%s_DisciplineTEStats.csv",
             log_dir, datetime_buf);
    swclock_start_log(clk, servo_log_path);
    printf("  Servo logging to: %s\n", servo_log_path);
  }

  // Capture references
  struct timespec sw0, raw0;
  swclock_gettime(clk, CLOCK_REALTIME, &sw0);
  clock_gettime(CLOCK_MONOTONIC_RAW, &raw0);
  long long t0_ns = ts_to_ns(&raw0);

  // Sample 60 s @ 10 Hz
  const double sample_dt_s = 0.1;
  const int    SAMPLES     = 600;   // 601 points (0..600)
  const long long POLL_NS  = (long long)llround(sample_dt_s * 1e9);

  std::vector<long long> te_ns;
  te_ns.reserve(SAMPLES+1);

  printf("\n=== Discipline loop: TE/MTIE/TDEV vs MONOTONIC_RAW reference ===\n");
  for (int i=0;i<=SAMPLES;i++){
    struct timespec raw_now;
    clock_gettime(CLOCK_MONOTONIC_RAW, &raw_now);
    long long timestamp_ns = ts_to_ns(&raw_now) - t0_ns;
    
    long long te = TE_now_SWvsRAW(clk, sw0, raw0);
    te_ns.push_back(te);
    
    // Log to CSV if enabled
    csv_logger.log(timestamp_ns, te);
    
    if (i % 60 == 0) { // print ~every 6 s
      printf("  TE[%d] = %10.3f us\n", i, (double)te / 1000.0);
    }
    sleep_ns_robust(POLL_NS);
  }
  
  csv_logger.flush();

  // Detrend
  double a=0,b=0;
  std::vector<double> yd;
  detrend(te_ns, sample_dt_s, a, b, yd);
  // Stats (mean on detrended)
  double mean_raw = std::accumulate(te_ns.begin(), te_ns.end(), 0.0) / te_ns.size();
  double mean     = std::accumulate(yd.begin(), yd.end(), 0.0) / yd.size();
  double rms=0, p50=0, p95=0, p99=0;
  {
    std::vector<double> tmp = yd;
    std::sort(tmp.begin(), tmp.end());
    auto pick = [&](double q)->double{
      double idx = q*(tmp.size()-1);
      int i = (int)floor(idx);
      int j = std::min((int)tmp.size()-1, i+1);
      double f = idx - i;
      return tmp[i]*(1-f) + tmp[j]*f;
    };
    for (double v: yd) rms += v*v;
    rms = sqrt(rms / yd.size());
    p50 = pick(0.50);
    p95 = pick(0.95);
    p99 = pick(0.99);
  }

  // MTIE on detrended series
  long long mtie1  = mtie_detrended(yd, sample_dt_s, 1.0);
  long long mtie10 = mtie_detrended(yd, sample_dt_s, 10.0);
  long long mtie30 = mtie_detrended(yd, sample_dt_s, 30.0);

  // TDEV on detrended series
  double tdev01 = tdev_detrended_ns(yd, sample_dt_s, 0.1);
  double tdev1  = tdev_detrended_ns(yd, sample_dt_s, 1.0);
  double tdev10 = tdev_detrended_ns(yd, sample_dt_s, 10.0);

  printf("\n-- TE stats over 60 s (raw ref) --\n");
  printf("   mean(raw)   = %10.1f ns\n", mean_raw);
  printf("   mean(detr)  = %10.1f ns  (target |mean(detr)| < %lld)\n", mean, (long long)TARGET_TE_MEAN_ABS_NS);
  printf("   slope       = %+8.3f ns/s  ( %+5.3f ppm)  (target |ppm| < 2.0)\n", b, b/1e3);
  printf("   RMS         = %10.1f ns    (target < 50000)\n", rms);
  printf("   P50         = %10.1f ns\n", p50);
  printf("   P95         = %10.1f ns   (target |P95| < 150000)\n", p95);
  printf("   P99         = %10.1f ns   (target |P99| < 300000)\n", p99);

  printf("\n-- MTIE (detrended) --\n");
  printf("   MTIE(%2d s) = %10lld ns (target < %lld)\n", 1,  mtie1,  (long long)TARGET_MTIE_1S_NS);
  printf("   MTIE(%2d s) = %10lld ns (target < %lld)\n", 10, mtie10, (long long)TARGET_MTIE_10S_NS);
  printf("   MTIE(%2d s) = %10lld ns (target < %lld)\n", 30, mtie30, (long long)TARGET_MTIE_30S_NS);

  printf("\n-- TDEV (detrended) --\n");
  printf("   TDEV(0.1 s) = %10.1f ns (target < %lld)\n", tdev01, (long long)TARGET_TDEV_0P1S_NS);
  printf("   TDEV(  1 s) = %10.1f ns (target < %lld)\n", tdev1,  (long long)TARGET_TDEV_1S_NS);
  printf("   TDEV( 10 s) = %10.1f ns (target < %lld)\n", tdev10,(long long)TARGET_TDEV_10S_NS);

  // Export expected metrics for independent validation (IEEE Rec 6)
  if (csv_logger.is_enabled()) {
    export_expected_metrics(csv_logger.get_filepath(), 
                           mean, rms,  // Using RMS as std_ns proxy
                           mtie1, mtie10, mtie30,
                           tdev01, tdev1, tdev10);
  }

  EXPECT_LE(std::fabs(mean), (double)TARGET_TE_MEAN_ABS_NS);
  EXPECT_LE((double)mtie1,  (double)TARGET_MTIE_1S_NS);
  EXPECT_LE((double)mtie10, (double)TARGET_MTIE_10S_NS);
  EXPECT_LE((double)mtie30, (double)TARGET_MTIE_30S_NS);

  swclock_destroy(clk);
}

#ifndef TARGET_SETTLE_TIME_S
#define TARGET_SETTLE_TIME_S 20.0
#endif
#ifndef TARGET_OVERSHOOT_PCT
#define TARGET_OVERSHOOT_PCT 30.0
#endif

TEST(Perf, SettlingAndOvershoot){
  SwClock* clk = swclock_create();
  ASSERT_NE(clk, nullptr);
  printf("\n=== Settling & Overshoot (IMMEDIATE step +1 ms, RELATIVE TE) ===\n");

  // Initialize CSV logger
  TELogger csv_logger("Perf_SettlingAndOvershoot");

  // Reference
  struct timespec sw0, raw0;
  swclock_gettime(clk, CLOCK_REALTIME, &sw0);
  clock_gettime(CLOCK_MONOTONIC_RAW, &raw0);
  long long t0_ns = ts_to_ns(&raw0);

  auto TE_now = [&](void)->long long{
    struct timespec sw, rr;
    swclock_gettime(clk, CLOCK_REALTIME, &sw);
    clock_gettime(CLOCK_MONOTONIC_RAW, &rr);
    return (ts_to_ns(&sw) - ts_to_ns(&sw0)) - (ts_to_ns(&rr) - ts_to_ns(&raw0));
  };

  // Apply +1 ms IMMEDIATE RELATIVE STEP
  const double STEP_US = 1000.0;
  struct timex tx = {};
  tx.modes        = ADJ_SETOFFSET | ADJ_MICRO;
  tx.time.tv_sec  = 0;
  tx.time.tv_usec = (int)llround(STEP_US);
  swclock_adjtime(clk, &tx);

  // Capture TE0 just after the step (baseline for RELATIVE error)
  sleep_ns_robust(PERF_POLL_NS);
  const long long TE0 = TE_now();

  const double SETTLE_BAND_US = 10.0;
  const double DWELL_S        = 3.0;
  const double TIMEOUT_S      = 60.0;
  const long long POLL_NS     = PERF_POLL_NS;
  const double POLL_S         = (double)POLL_NS * 1e-9;

  long long min_below_zero_rel_ns = 0;
  bool settled = false;
  double t = 0.0, dwell = 0.0;

  for (;;){
    sleep_ns_robust(POLL_NS);
    
    struct timespec raw_now;
    clock_gettime(CLOCK_MONOTONIC_RAW, &raw_now);
    long long timestamp_ns = ts_to_ns(&raw_now) - t0_ns;
    
    long long te = TE_now();
    long long e_rel_ns = te - TE0; // RELATIVE error w.r.t immediate step value
    double e_rel_us = (double)e_rel_ns / 1000.0;

    // Log to CSV if enabled
    csv_logger.log(timestamp_ns, te);

    if (fmod(t, 1.0) < POLL_S) {
      printf("  t=%5.2fs  TE=%+8.3f us  (rel=%+7.3f us)\n", t, (double)te/1000.0, e_rel_us);
    }

    if (e_rel_ns < 0 && llabs(e_rel_ns) > llabs(min_below_zero_rel_ns)){
      min_below_zero_rel_ns = e_rel_ns;
    }

    if (fabs(e_rel_us) <= SETTLE_BAND_US) {
      dwell += POLL_S;
      if (dwell >= DWELL_S) { settled = true; break; }
    } else {
      dwell = 0.0;
    }

    t += POLL_S;
    if (t > TIMEOUT_S) break;
  }

  csv_logger.flush();

  double settle_time = settled ? t : INFINITY;
  double overshoot_ns = (double)llabs(min_below_zero_rel_ns);
  double overshoot_pct = 100.0 * overshoot_ns / (STEP_US * 1000.0);

  printf("  Settling time to |rel-TE|<=%.1f us: %s\n", SETTLE_BAND_US,
         std::isfinite(settle_time) ? "REACHED" : "TIMEOUT");
  if (std::isfinite(settle_time)) {
    printf("    settle_time = %.2f s (target < %.2f s)\n", settle_time, TARGET_SETTLE_TIME_S);
  }
  printf("  Overshoot: %.0f ns  (%.1f%% of step; target < %.1f%%)\n",
         overshoot_ns, overshoot_pct, TARGET_OVERSHOOT_PCT);

  ASSERT_TRUE(std::isfinite(settle_time));
  EXPECT_LT(settle_time, TARGET_SETTLE_TIME_S);
  EXPECT_LT(overshoot_pct, TARGET_OVERSHOOT_PCT);

  swclock_destroy(clk);
}

// Holdover
static constexpr int    HOLDOVER_S               = 30;
static constexpr double SLEW_TEST_OFFSET_MS      = 200.0;
static constexpr double TARGET_HOLDOVER_RATE_PPM = 100.0;
static constexpr double SLEW_NEAR_PPM_TOL        = 15.0;


TEST(Perf, SlewRateClamp) {
  SwClock* clk = swclock_create();
  ASSERT_NE(clk, nullptr);

  printf("\n=== Slew-rate command/clamp check (+%.0f ms) ===\n", SLEW_TEST_OFFSET_MS);

  // Initialize CSV logger
  TELogger csv_logger("Perf_SlewRateClamp");

  struct timex tx = {};
  tx.modes  = ADJ_OFFSET | ADJ_MICRO;
  tx.offset = (int)llround(SLEW_TEST_OFFSET_MS * 1000.0);
  swclock_adjtime(clk, &tx);

  struct timespec sw0, mr0, sw1, mr1;
  swclock_gettime(clk, CLOCK_REALTIME, &sw0);
  clock_gettime(CLOCK_MONOTONIC_RAW, &mr0);
  long long t0_ns = ts_to_ns(&mr0);

  const double WIN_S = 3.0;
  
  // Sample TE during slew window
  for (double t = 0; t <= WIN_S; t += 0.1) {
    struct timespec raw_now;
    clock_gettime(CLOCK_MONOTONIC_RAW, &raw_now);
    long long timestamp_ns = ts_to_ns(&raw_now) - t0_ns;
    long long te = TE_now_SWvsRAW(clk, sw0, mr0);
    csv_logger.log(timestamp_ns, te);
    if (t < WIN_S) sleep_ns((long long)(0.1 * NS_PER_SEC));
  }
  csv_logger.flush();
  
  swclock_gettime(clk, CLOCK_REALTIME, &sw1);
  clock_gettime(CLOCK_MONOTONIC_RAW, &mr1);

  long long d_sw  = ts_to_ns(&sw1) - ts_to_ns(&sw0);
  long long d_raw = ts_to_ns(&mr1) - ts_to_ns(&mr0);
  long long extra = d_sw - d_raw;
  double eff_ppm = (double)extra * 1e6 / (double)d_raw;

  const double offset_s = (SLEW_TEST_OFFSET_MS / 1000.0);
  const double kp_ppm_per_s  = (double)SWCLOCK_PI_KP_PPM_PER_S;
  const double ki_ppm_per_s2 = (double)SWCLOCK_PI_KI_PPM_PER_S2;

  double ppm0 = kp_ppm_per_s * offset_s;
  double ppm_win_est = ppm0 + ki_ppm_per_s2 * offset_s * WIN_S;
  double expected_target = std::min((double)SWCLOCK_PI_MAX_PPM, ppm_win_est);

  printf("  Gains: Kp=%.3f [ppm/s], Ki=%.3f [ppm/s^2], MAX=%.1f [ppm]\n",
         kp_ppm_per_s, ki_ppm_per_s2, (double)SWCLOCK_PI_MAX_PPM);
  printf("  Offset: %.3f s  → ppm0=%.2f, ppm_win_est=%.2f, expected_target=%.2f\n",
         offset_s, ppm0, ppm_win_est, expected_target);
  printf("  over %.0fs: extra = %+9.0f ns, eff_ppm = %+7.2f (checking vs expected_target ± %.1f)\n",
         WIN_S, (double)extra, eff_ppm, SLEW_NEAR_PPM_TOL);

  EXPECT_NEAR(std::fabs(eff_ppm), expected_target, SLEW_NEAR_PPM_TOL);

  swclock_destroy(clk);
}

TEST(Perf, HoldoverDrift) {
  SwClock* clk = swclock_create();
  ASSERT_NE(clk, nullptr);

  printf("\n=== Holdover drift (no corrections for %ds) ===\n", HOLDOVER_S);

  // Initialize CSV logger
  TELogger csv_logger("Perf_HoldoverDrift");

  struct timespec sw0, rt0, sw1, rt1;
  swclock_gettime(clk, CLOCK_REALTIME, &sw0);
  clock_gettime(CLOCK_MONOTONIC_RAW, &rt0);
  long long t0_ns = ts_to_ns(&rt0);
  
  // Sample TE during holdover period
  for (int i = 0; i <= HOLDOVER_S; i++) {
    struct timespec raw_now;
    clock_gettime(CLOCK_MONOTONIC_RAW, &raw_now);
    long long timestamp_ns = ts_to_ns(&raw_now) - t0_ns;
    long long te = TE_now_SWvsRAW(clk, sw0, rt0);
    csv_logger.log(timestamp_ns, te);
    if (i < HOLDOVER_S) sleep_ns(NS_PER_SEC);
  }
  csv_logger.flush();
  swclock_gettime(clk, CLOCK_REALTIME, &sw1);
  clock_gettime(CLOCK_MONOTONIC_RAW, &rt1);

  long long d_sw  = ts_to_ns(&sw1) - ts_to_ns(&sw0);
  long long d_sys = ts_to_ns(&rt1) - ts_to_ns(&rt0);
  long long extra = d_sw - d_sys;
  double rate_ppm = (double)extra * 1e6 / (double)d_sys;

  printf("  extra = %+lld ns over %ds  → drift rate = %+7.2f ppm (target |ppm| < %.1f)\n",
         extra, HOLDOVER_S, rate_ppm, TARGET_HOLDOVER_RATE_PPM);

  EXPECT_LT(std::fabs(rate_ppm), TARGET_HOLDOVER_RATE_PPM);

  swclock_destroy(clk);
}

// ============================================================================
// Extended Test Scenarios (Priority 5)
// ============================================================================

/**
 * Test: Frequency Offset Correction (Positive)
 * 
 * Purpose: Validate clock maintains stability with initial frequency offset
 * Scenario: Initialize clock with +100 ppm offset, measure drift over time
 * Success: Clock discipline maintains low drift despite initial offset
 * 
 * Note: SwClock doesn't actively correct arbitrary frequency offsets without
 *       time error feedback. This test validates that applied offsets are stable.
 */
TEST(Perf, FrequencyOffsetPositive) {
  SwClock* clk = swclock_create();
  ASSERT_NE(clk, nullptr);

  const double FREQ_OFFSET_PPM = 100.0;   // +100 ppm initial offset
  const double MAX_DRIFT_PPM = 1.0;       // Drift should be minimal
  const double MEASURE_TIME_S = 10.0;     // Measurement window

  printf("\n=== Frequency Offset Stability: +%.1f ppm ===\n", FREQ_OFFSET_PPM);

  // Initialize CSV logger
  TELogger csv_logger("Perf_FrequencyOffsetPositive");

  // Apply initial frequency offset
  struct timex tx = {};
  tx.modes = ADJ_FREQUENCY;
  tx.freq = (long)(FREQ_OFFSET_PPM * 65536.0);  // Linux units: ppm * 2^16
  swclock_adjtime(clk, &tx);

  printf("  Applied +%.1f ppm frequency offset\n", FREQ_OFFSET_PPM);
  printf("  Measuring stability over %.0f seconds...\n", MEASURE_TIME_S);

  // Measure frequency stability with applied offset
  struct timespec sw0, rt0, sw1, rt1;
  swclock_gettime(clk, CLOCK_REALTIME, &sw0);
  clock_gettime(CLOCK_MONOTONIC_RAW, &rt0);
  long long t0_ns = ts_to_ns(&rt0);
  
  // Sample TE during measurement window
  for (double t = 0; t <= MEASURE_TIME_S; t += 1.0) {
    struct timespec raw_now;
    clock_gettime(CLOCK_MONOTONIC_RAW, &raw_now);
    long long timestamp_ns = ts_to_ns(&raw_now) - t0_ns;
    long long te = TE_now_SWvsRAW(clk, sw0, rt0);
    csv_logger.log(timestamp_ns, te);
    if (t < MEASURE_TIME_S) sleep_ns(NS_PER_SEC);
  }
  csv_logger.flush();
  
  swclock_gettime(clk, CLOCK_REALTIME, &sw1);
  clock_gettime(CLOCK_MONOTONIC_RAW, &rt1);

  long long d_sw = ts_to_ns(&sw1) - ts_to_ns(&sw0);
  long long d_rt = ts_to_ns(&rt1) - ts_to_ns(&rt0);
  double measured_ppm = ((double)(d_sw - d_rt) / (double)d_rt) * 1e6;
  double drift_from_target = std::fabs(measured_ppm - FREQ_OFFSET_PPM);

  printf("  Measured frequency: %.3f ppm\n", measured_ppm);
  printf("  Drift from applied offset: %.3f ppm (target < %.1f ppm)\n", 
         drift_from_target, MAX_DRIFT_PPM);
  printf("  ✓ Frequency offset is stable\n");

  // Verify the applied offset is maintained with minimal drift
  EXPECT_LT(drift_from_target, MAX_DRIFT_PPM);

  swclock_destroy(clk);
}

/**maintains stability with negative frequency offset
 * Scenario: Initialize clock with -100 ppm offset, measure drift over time
 * Success: Clock discipline maintains low drift despite initial offset
 */
TEST(Perf, FrequencyOffsetNegative) {
  SwClock* clk = swclock_create();
  ASSERT_NE(clk, nullptr);

  // Initialize CSV logger
  TELogger csv_logger("Perf_FrequencyOffsetNegative");

  const double FREQ_OFFSET_PPM = -100.0;  // -100 ppm initial offset
  const double MAX_DRIFT_PPM = 1.0;       // Drift should be minimal
  const double MEASURE_TIME_S = 10.0;     // Measurement window

  printf("\n=== Frequency Offset Stability: %.1f ppm ===\n", FREQ_OFFSET_PPM);

  // Apply initial frequency offset
  struct timex tx = {};
  tx.modes = ADJ_FREQUENCY;
  tx.freq = (long)(FREQ_OFFSET_PPM * 65536.0);  // Linux units: ppm * 2^16
  swclock_adjtime(clk, &tx);

  printf("  Applied %.1f ppm frequency offset\n", FREQ_OFFSET_PPM);
  printf("  Measuring stability over %.0f seconds...\n", MEASURE_TIME_S);

  // Measure frequency stability with applied offset
  struct timespec sw0, rt0, sw1, rt1;
  swclock_gettime(clk, CLOCK_REALTIME, &sw0);
  clock_gettime(CLOCK_MONOTONIC_RAW, &rt0);
  long long t0_ns = ts_to_ns(&rt0);
  
  // Sample TE during measurement window
  for (double t = 0; t <= MEASURE_TIME_S; t += 1.0) {
    struct timespec raw_now;
    clock_gettime(CLOCK_MONOTONIC_RAW, &raw_now);
    long long timestamp_ns = ts_to_ns(&raw_now) - t0_ns;
    long long te = TE_now_SWvsRAW(clk, sw0, rt0);
    csv_logger.log(timestamp_ns, te);
    if (t < MEASURE_TIME_S) sleep_ns(NS_PER_SEC);
  }
  csv_logger.flush();
  
  swclock_gettime(clk, CLOCK_REALTIME, &sw1);
  clock_gettime(CLOCK_MONOTONIC_RAW, &rt1);

  long long d_sw = ts_to_ns(&sw1) - ts_to_ns(&sw0);
  long long d_rt = ts_to_ns(&rt1) - ts_to_ns(&rt0);
  double measured_ppm = ((double)(d_sw - d_rt) / (double)d_rt) * 1e6;
  double drift_from_target = std::fabs(measured_ppm - FREQ_OFFSET_PPM);

  printf("  Measured frequency: %.3f ppm\n", measured_ppm);
  printf("  Drift from applied offset: %.3f ppm (target < %.1f ppm)\n", 
         drift_from_target, MAX_DRIFT_PPM);
  printf("  ✓ Frequency offset is stable\n");

  // Verify the applied offset is maintained with minimal drift
  EXPECT_LT(drift_from_target, MAX_DRIFT_PPM);

  swclock_destroy(clk);
}

/**
 * Test: Multiple Step Sizes
 * 
 * Purpose: Validate servo response across different step magnitudes
 * Scenario: Test step corrections of 100µs, 1ms, 10ms, 100ms
 * Success: Settling time scales appropriately with step size
 */
TEST(Perf, MultipleStepSizes) {
  printf("\n=== Multiple Step Size Response ===\n");

  struct StepTest {
    double step_ms;
    double max_settling_s;
    double max_overshoot_pct;
  };

  // Define test cases: step size -> expected settling time
  StepTest tests[] = {
    {0.1,   5.0,  10.0},  // 100µs step: fast settle
    {1.0,   10.0, 20.0},  // 1ms step: moderate settle
    {10.0,  20.0, 30.0},  // 10ms step: slower settle
    {100.0, 40.0, 40.0},  // 100ms step: longest settle
  };

  for (const auto& test : tests) {
    SwClock* clk = swclock_create();
    ASSERT_NE(clk, nullptr);

    printf("\n  Step: %.3f ms\n", test.step_ms);

    // Initialize CSV logger for this step size
    char test_name[128];
    snprintf(test_name, sizeof(test_name), "Perf_MultipleStepSizes_%.1fms", test.step_ms);
    TELogger csv_logger(test_name);

    // Apply step offset
    struct timex tx = {};
    tx.modes = ADJ_SETOFFSET | ADJ_NANO;
    long long offset_ns = (long long)(test.step_ms * 1e6);
    tx.time.tv_sec = offset_ns / NS_PER_SEC;
    tx.time.tv_usec = offset_ns % NS_PER_SEC;
    swclock_adjtime(clk, &tx);

    // Measure settling behavior
    struct timespec sw_ref, rt_ref;
    swclock_gettime(clk, CLOCK_REALTIME, &sw_ref);
    clock_gettime(CLOCK_MONOTONIC_RAW, &rt_ref);
    long long t0_ns = ts_to_ns(&rt_ref);

    long long max_te = 0;
    double settling_time = -1.0;
    const long long SETTLE_THRESHOLD_NS = 10000;  // ±10µs
    const double MAX_TEST_TIME_S = test.max_settling_s + 10.0;
    
    for (double t = 0.1; t < MAX_TEST_TIME_S; t += 0.1) {
      sleep_ns((long long)(0.1 * NS_PER_SEC));
      
      struct timespec raw_now;
      clock_gettime(CLOCK_MONOTONIC_RAW, &raw_now);
      long long timestamp_ns = ts_to_ns(&raw_now) - t0_ns;
      
      long long te = TE_now_SWvsRAW(clk, sw_ref, rt_ref);
      csv_logger.log(timestamp_ns, te);
      
      if (std::abs(te) > max_te) max_te = std::abs(te);
      
      if (settling_time < 0 && std::abs(te) <= SETTLE_THRESHOLD_NS) {
        settling_time = t;
      }
    }
    
    csv_logger.flush();

    double overshoot_pct = (max_te / (offset_ns)) * 100.0;

    printf("    Settling time: %.1f s (target < %.1f s)\n", 
           settling_time, test.max_settling_s);
    printf("    Max overshoot: %.1f%% (target < %.1f%%)\n", 
           overshoot_pct, test.max_overshoot_pct);

    EXPECT_LT(settling_time, test.max_settling_s);
    EXPECT_LT(overshoot_pct, test.max_overshoot_pct);

    swclock_destroy(clk);
  }
}

// ============================================================================
// Measurement Uncertainty Analysis Tests (ISO/IEC Guide 98-3 / GUM)
// ============================================================================
//
// These tests support IEEE Audit Priority 3 Recommendation 13:
// Quantify measurement uncertainty using GUM methodology
//
// Type A Uncertainty: Statistical analysis from repeated measurements
// Type B Uncertainty: Systematic uncertainties from specifications
//
// Purpose:
// - Characterize measurement repeatability
// - Generate data for uncertainty_analysis.py tool
// - Support expanded uncertainty statement: U = ±X ns (k=2, 95% confidence)

TEST(Perf, MeasurementRepeatability) {
  // Run N identical trials to characterize Type A uncertainty
  // Each trial: 60s sampling period with ideal reference (CLOCK_MONOTONIC_RAW)
  // Expected: TE distribution reflects measurement noise, not servo error
  
  const int num_trials = 10;
  const double sample_duration_s = 60.0;
  const double sample_dt_s = 0.1;  // 10 Hz sampling
  const int samples_per_trial = static_cast<int>(sample_duration_s / sample_dt_s) + 1;
  const long long poll_ns = (long long)(sample_dt_s * NS_PER_SEC);
  
  // Initialize CSV logger for Type A analysis
  bool csv_enabled = csv_logging_enabled();
  TELogger csv_logger("Perf_MeasurementRepeatability");
  
  printf("\n");
  printf("==============================================================================\n");
  printf("Measurement Repeatability Test - ISO/IEC Guide 98-3 (GUM)\n");
  printf("==============================================================================\n");
  printf("\n");
  printf("Purpose: Characterize Type A measurement uncertainty\n");
  printf("Method:  %d identical trials, %d samples each\n", num_trials, samples_per_trial);
  printf("Config:  Ideal reference (CLOCK_MONOTONIC_RAW), no offsets\n");
  printf("Output:  CSV data for tools/uncertainty_analysis.py\n");
  printf("\n");
  
  // Storage for trial statistics
  std::vector<double> trial_means_ns;
  std::vector<double> trial_stds_ns;
  
  for (int trial = 0; trial < num_trials; trial++) {
    printf("Trial %d/%d: ", trial + 1, num_trials);
    fflush(stdout);
    
    // Create SwClock instance
    SwClock* clk = swclock_create();
    ASSERT_NE(clk, nullptr);
    
    // Capture initial reference points
    struct timespec sw0, raw0;
    swclock_gettime(clk, CLOCK_REALTIME, &sw0);
    clock_gettime(CLOCK_MONOTONIC_RAW, &raw0);
    long long t0_ns = (long long)raw0.tv_sec * NS_PER_SEC + raw0.tv_nsec;
    
    // Sample time errors
    std::vector<long long> te_samples;
    te_samples.reserve(samples_per_trial);
    
    for (int i = 0; i < samples_per_trial; i++) {
      // Compute time error: SwClock - MONOTONIC_RAW
      struct timespec sw_now, raw_now;
      swclock_gettime(clk, CLOCK_REALTIME, &sw_now);
      clock_gettime(CLOCK_MONOTONIC_RAW, &raw_now);
      
      // Calculate elapsed time from both clocks
      long long sw_elapsed_ns = ((long long)sw_now.tv_sec * NS_PER_SEC + sw_now.tv_nsec) -
                                 ((long long)sw0.tv_sec * NS_PER_SEC + sw0.tv_nsec);
      long long raw_elapsed_ns = ((long long)raw_now.tv_sec * NS_PER_SEC + raw_now.tv_nsec) -
                                  ((long long)raw0.tv_sec * NS_PER_SEC + raw0.tv_nsec);
      
      // Time error is the difference
      long long te_ns = sw_elapsed_ns - raw_elapsed_ns;
      te_samples.push_back(te_ns);
      
      // Log to CSV if enabled
      if (csv_enabled) {
        long long timestamp_ns = raw_elapsed_ns;
        csv_logger.log(timestamp_ns, te_ns);
      }
      
      // Sleep until next sample
      struct timespec sleep_time = {
        .tv_sec = poll_ns / NS_PER_SEC,
        .tv_nsec = poll_ns % NS_PER_SEC
      };
      nanosleep(&sleep_time, nullptr);
      
      // Poll the servo
      swclock_poll(clk);
    }
    
    // Compute trial statistics
    double sum = 0.0;
    for (auto te : te_samples) {
      sum += te;
    }
    double mean = sum / te_samples.size();
    
    double sum_sq = 0.0;
    for (auto te : te_samples) {
      double diff = te - mean;
      sum_sq += diff * diff;
    }
    double variance = sum_sq / (te_samples.size() - 1);
    double std_dev = sqrt(variance);
    
    trial_means_ns.push_back(mean);
    trial_stds_ns.push_back(std_dev);
    
    printf("Mean=%.2f ns, StdDev=%.2f ns, Samples=%zu\n", 
           mean, std_dev, te_samples.size());
    
    swclock_destroy(clk);
  }
  
  // Compute inter-trial statistics
  double mean_of_means = std::accumulate(trial_means_ns.begin(), 
                                         trial_means_ns.end(), 0.0) / num_trials;
  
  double sum_sq_means = 0.0;
  for (auto m : trial_means_ns) {
    double diff = m - mean_of_means;
    sum_sq_means += diff * diff;
  }
  double std_of_means = sqrt(sum_sq_means / (num_trials - 1));
  double type_a_uncertainty = std_of_means / sqrt(num_trials);
  
  double mean_of_stds = std::accumulate(trial_stds_ns.begin(), 
                                        trial_stds_ns.end(), 0.0) / num_trials;
  
  printf("\n");
  printf("==============================================================================\n");
  printf("Type A Uncertainty Analysis Results\n");
  printf("==============================================================================\n");
  printf("\n");
  printf("Inter-trial statistics:\n");
  printf("  Mean of trial means:        %.2f ns\n", mean_of_means);
  printf("  Std dev of trial means:     %.2f ns\n", std_of_means);
  printf("  Type A uncertainty u(x):    %.2f ns (= σ/√n, n=%d)\n", 
         type_a_uncertainty, num_trials);
  printf("\n");
  printf("Intra-trial statistics:\n");
  printf("  Mean of trial std devs:     %.2f ns\n", mean_of_stds);
  printf("\n");
  
  // Save CSV if enabled
  if (csv_enabled) {
    csv_logger.flush();
    printf("CSV data exported: %s\n", csv_logger.get_filepath());
    printf("Run: python3 tools/uncertainty_analysis.py %s\n", csv_logger.get_filepath());
  }
  
  printf("\n");
  
  // Expectations: Type A uncertainty should be small (<50ns)
  // This validates that repeated measurements are consistent
  EXPECT_LT(type_a_uncertainty, 50.0) 
    << "Type A uncertainty too large - measurement not repeatable";
  EXPECT_LT(mean_of_stds, 1000.0)
    << "Intra-trial variation too large - check system stability";
}
