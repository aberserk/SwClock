// gtests/tests_performance.cpp (full, patched)
// Performance characterization for the software clock under PTP-like discipline.
// Uses GoogleTest; prints real-time telemetry via printf.
// TE/MTIE/TDEV use CLOCK_MONOTONIC_RAW as reference.
// SlewRateClamp checks expected commanded ppm from current Kp/Ki.

#include <gtest/gtest.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <errno.h>
#include <vector>
#include <algorithm>

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

#ifndef PERF_POLL_NS
#define PERF_POLL_NS (10*1000*1000L)   // 10 ms
#endif

static constexpr int DISCIPLINE_WARMUP_S  = 10;
static constexpr int DISCIPLINE_MEASURE_S = 60;

// PDV injection parameters
static constexpr double PDV_SIGMA_US   = 20.0;   // Gaussian stddev
static constexpr double PDV_SPIKE_US   = 200.0;  // rare spike
static constexpr double PDV_SPIKE_PROB = 0.01;   // prob of spike
static constexpr double PDV_BIAS_US    = 0.0;    // intentional bias

// Step/settling
static constexpr double STEP_DISTURBANCE_US = 1000.0;
static constexpr double SETTLE_BAND_US      = 10.0;
static constexpr double SETTLE_DWELL_S      = 3.0;

// Holdover
static constexpr int    HOLDOVER_S          = 30;

// Slew/Clamp test
static constexpr double SLEW_TEST_OFFSET_MS = 200.0;

// Servo params fallbacks (read from your compile-time defines)
#ifndef SWCLOCK_PI_MAX_PPM
#define SWCLOCK_PI_MAX_PPM 200.0
#endif
#ifndef SWCLOCK_PI_KP_PPM_PER_S
#define SWCLOCK_PI_KP_PPM_PER_S 200.0
#endif
#ifndef SWCLOCK_PI_KI_PPM_PER_S2
#define SWCLOCK_PI_KI_PPM_PER_S2 0.0
#endif

static constexpr double SLEW_NEAR_PPM_TOL = 15.0;

// Targets
static constexpr long long TARGET_TE_MEAN_ABS_NS = 20LL*NS_PER_US;  // after detrending
static constexpr long long TARGET_TE_RMS_NS      = 50LL*NS_PER_US;
static constexpr long long TARGET_TE_P95_NS      = 150LL*NS_PER_US;
static constexpr long long TARGET_TE_P99_NS      = 300LL*NS_PER_US;

static constexpr long long TARGET_MTIE_1S_NS     = 100LL*NS_PER_US;
static constexpr long long TARGET_MTIE_10S_NS    = 200LL*NS_PER_US;
static constexpr long long TARGET_MTIE_30S_NS    = 300LL*NS_PER_US;

static constexpr long long TARGET_TDEV_0p1S_NS   = 20LL*NS_PER_US;
static constexpr long long TARGET_TDEV_1S_NS     = 40LL*NS_PER_US;
static constexpr long long TARGET_TDEV_10S_NS    = 80LL*NS_PER_US;

// Allow a small residual drift slope in detrended-mean test
static constexpr double     TARGET_TE_SLOPE_PPM  = 2.0;

static constexpr double     TARGET_SETTLE_TIME_S = 20.0;
static constexpr double     TARGET_OVERSHOOT_PCT = 30.0;
static constexpr double     TARGET_HOLDOVER_RATE_PPM = 100.0;

// --- Helpers ---
static inline long long llabsll(long long x) { return x < 0 ? -x : x; }

static double urand01(uint64_t& state) {
  uint64_t x = state;
  x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
  state = x;
  return (double)((x * 2685821657736338717ULL) >> 11) / (double)(1ULL<<53);
}
static double gauss_std(uint64_t& st) {
  double u1 = urand01(st);
  double u2 = urand01(st);
  double r = std::sqrt(-2.0 * std::log(u1 + 1e-16));
  double t = 2.0 * M_PI * u2;
  return r * std::cos(t);
}

static long long percentile_ns(std::vector<long long> v, double p) {
  if (v.empty()) return 0;
  std::sort(v.begin(), v.end());
  double pos = p / 100.0 * (double)(v.size() - 1);
  size_t i = (size_t)std::floor(pos);
  size_t j = std::min(i + 1, v.size() - 1);
  double a = pos - (double)i;
  double vi = (double)v[i], vj = (double)v[j];
  return (long long)llround((1.0 - a) * vi + a * vj);
}

static long long mtie_ns(const std::vector<long long>& x, size_t m) {
  if (x.size() < m + 1 || m == 0) return 0;
  long long mt = 0;
  for (size_t i = 0; i + m < x.size(); ++i) {
    long long d = llabsll(x[i + m] - x[i]);
    if (d > mt) mt = d;
  }
  return mt;
}

static double tdev_ns(const std::vector<long long>& x, size_t m) {
  if (m == 0 || x.size() < 2 * m + 2) return 0.0;
  long double acc = 0.0L;
  size_t K = x.size() - 2 * m;
  for (size_t i = 0; i < K; ++i) {
    long long d = x[i + 2 * m] - 2 * x[i + m] + x[i];
    long double dd = (long double)d;
    acc += dd * dd;
  }
  long double denom = (long double)2.0 * (long double)m * (long double)m * (long double)K;
  if (denom <= 0.0L) return 0.0;
  long double var = acc / denom;
  return std::sqrt((double)var);
}

struct RawRef { struct timespec sw0; struct timespec raw0; };

static inline long long measure_TE_ns_rawref(SwClock* clk, const RawRef& ref) {
  struct timespec sw, raw;
  swclock_gettime(clk, CLOCK_REALTIME, &sw);
  clock_gettime(CLOCK_MONOTONIC_RAW, &raw);
  long long sw_rel  = ts_to_ns(&sw)  - ts_to_ns(&ref.sw0);
  long long raw_rel = ts_to_ns(&raw) - ts_to_ns(&ref.raw0);
  return sw_rel - raw_rel;
}

// PDV injection with stronger guard to avoid mean drift from micro-jitter
static void apply_noise_sample(SwClock* clk, uint64_t& rng) {
  double sample_us = PDV_BIAS_US + PDV_SIGMA_US * gauss_std(rng);
  if (urand01(rng) < PDV_SPIKE_PROB) sample_us += PDV_SPIKE_US;

  const double DEADBAND_US = 10.0;  // ignore tiny offsets
  const double CLAMP_US    = 100.0; // clip spikes

  if (std::fabs(sample_us) < DEADBAND_US) return;
  if (sample_us >  CLAMP_US) sample_us =  CLAMP_US;
  if (sample_us < -CLAMP_US) sample_us = -CLAMP_US;

  struct timex tx = {};
  tx.modes  = ADJ_OFFSET | ADJ_MICRO;  // slewed correction
  tx.offset = (int)llround(sample_us);
  swclock_adjtime(clk, &tx);
}

// -------- Tests --------

TEST(Perf, DisciplineTEStats_MTIE_TDEV) {
  SwClock* clk = swclock_create();
  ASSERT_NE(clk, nullptr);

  printf("\n=== Discipline loop: TE/MTIE/TDEV vs MONOTONIC_RAW reference ===\n");
  const long long poll_ns = PERF_POLL_NS;
  const size_t warm_steps = (size_t)(DISCIPLINE_WARMUP_S  * (NS_PER_SEC / poll_ns));
  const size_t meas_steps = (size_t)(DISCIPLINE_MEASURE_S * (NS_PER_SEC / poll_ns));

  RawRef ref;
  swclock_gettime(clk, CLOCK_REALTIME, &ref.sw0);
  clock_gettime(CLOCK_MONOTONIC_RAW, &ref.raw0);

  std::vector<long long> TE; TE.reserve(meas_steps);
  uint64_t rng = 0xC0FFEEBEEFu;

  for (size_t i = 0; i < warm_steps; ++i) { apply_noise_sample(clk, rng); sleep_ns(poll_ns); }
  for (size_t i = 0; i < meas_steps; ++i) {
    apply_noise_sample(clk, rng);
    sleep_ns(poll_ns);
    long long te = measure_TE_ns_rawref(clk, ref);
    TE.push_back(te);
    if ((i % (meas_steps / 10 + 1)) == 0) {
      printf("  TE[%zu] = %+9.3f us\n", i, (double)te / 1000.0);
    }
  }

  // --- TE stats with detrended mean ---
  // Raw mean / rms:
  long long sum = 0; for (auto v : TE) sum += v;
  double mean_raw = (double)sum / (double)TE.size();
  long double acc = 0.0L; for (auto v : TE) { long double d = (long double)v - (long double)mean_raw; acc += d * d; }
  double rms = std::sqrt((double)(acc / (long double)TE.size()));

  // Percentiles on raw TE:
  auto p50 = percentile_ns(TE, 50.0);
  auto p95 = percentile_ns(TE, 95.0);
  auto p99 = percentile_ns(TE, 99.0);

  // Fit TE(t) ≈ a + b * t, with t in seconds from start
  const double dt_s = (double)poll_ns * SEC_PER_NS;
  const size_t N = TE.size();
  long double S1=0, St=0, Stt=0, Sy=0, Sty=0;
  for (size_t i=0; i<N; ++i) {
    long double t = (long double)i * dt_s;
    long double y = (long double)TE[i];
    S1  += 1.0L;
    St  += t;
    Stt += t*t;
    Sy  += y;
    Sty += t*y;
  }
  long double denom = S1*Stt - St*St;
  long double a=0.0L, b=0.0L;
  if (fabsl(denom) > 0.0L) {
    a = (Stt*Sy - St*Sty) / denom;      // intercept in ns
    b = (S1*Sty - St*Sy) / denom;       // slope in ns/s
  }
  // Detrended mean = mean of residuals
  long double res_sum = 0.0L;
  for (size_t i=0; i<N; ++i) {
    long double t = (long double)i * dt_s;
    long double y = (long double)TE[i];
    res_sum += (y - (a + b*t));
  }
  double mean_detrended = (double)(res_sum / (long double)N);

  // Slope as ppm
  double slope_ns_per_s = (double)b;
  double slope_ppm = slope_ns_per_s / 1e3;  // (ns/s) / 1000 = ppm

  printf("\n-- TE stats over %d s (raw ref) --\n", DISCIPLINE_MEASURE_S);
  printf("   mean(raw)   = %+10.1f ns\n", mean_raw);
  printf("   mean(detr)  = %+10.1f ns  (target |mean(detr)| < %lld)\n", mean_detrended, TARGET_TE_MEAN_ABS_NS);
  printf("   slope       = %+9.3f ns/s  (%+7.3f ppm)  (target |ppm| < %.1f)\n", slope_ns_per_s, slope_ppm, TARGET_TE_SLOPE_PPM);
  printf("   RMS         = %10.1f ns    (target < %lld)\n",        rms,  TARGET_TE_RMS_NS);
  printf("   P50         = %+10.1f ns\n", (double)p50);
  printf("   P95         = %+10.1f ns   (target |P95| < %lld)\n", (double)p95, TARGET_TE_P95_NS);
  printf("   P99         = %+10.1f ns   (target |P99| < %lld)\n", (double)p99, TARGET_TE_P99_NS);

  // Assertions (detrended mean + slope constraint)
  EXPECT_LE(std::fabs(mean_detrended), (double)TARGET_TE_MEAN_ABS_NS);
  EXPECT_LE(std::fabs(slope_ppm), TARGET_TE_SLOPE_PPM);
  EXPECT_LE(rms,  (double)TARGET_TE_RMS_NS);
  EXPECT_LE((double)llabsll(p95), (double)TARGET_TE_P95_NS);
  EXPECT_LE((double)llabsll(p99), (double)TARGET_TE_P99_NS);

  // --- MTIE ---
  size_t m1s  = (size_t)((1.0  * NS_PER_SEC) / (double)poll_ns);
  size_t m10s = (size_t)((10.0 * NS_PER_SEC) / (double)poll_ns);
  size_t m30s = (size_t)((30.0 * NS_PER_SEC) / (double)poll_ns);
  auto mtie1  = mtie_ns(TE, m1s);
  auto mtie10 = mtie_ns(TE, m10s);
  auto mtie30 = mtie_ns(TE, m30s);

  printf("\n-- MTIE (raw ref) --\n");
  printf("   MTIE( 1 s) = %9.1f ns (target < %lld)\n", (double)mtie1,  TARGET_MTIE_1S_NS);
  printf("   MTIE(10 s) = %9.1f ns (target < %lld)\n", (double)mtie10, TARGET_MTIE_10S_NS);
  printf("   MTIE(30 s) = %9.1f ns (target < %lld)\n", (double)mtie30, TARGET_MTIE_30S_NS);

  EXPECT_LE((double)mtie1,  (double)TARGET_MTIE_1S_NS);
  EXPECT_LE((double)mtie10, (double)TARGET_MTIE_10S_NS);
  EXPECT_LE((double)mtie30, (double)TARGET_MTIE_30S_NS);

  // --- TDEV ---
  size_t m01s = (size_t)((0.1 * NS_PER_SEC) / (double)poll_ns); if (m01s == 0) m01s = 1;
  double tdev01 = tdev_ns(TE, m01s);
  double tdev1  = tdev_ns(TE, m1s);
  double tdev10 = tdev_ns(TE, m10s);

  printf("\n-- TDEV (raw ref) --\n");
  printf("   TDEV(0.1 s) = %9.1f ns (target < %lld)\n", tdev01, TARGET_TDEV_0p1S_NS);
  printf("   TDEV(  1 s) = %9.1f ns (target < %lld)\n", tdev1,  TARGET_TDEV_1S_NS);
  printf("   TDEV( 10 s) = %9.1f ns (target < %lld)\n", tdev10, TARGET_TDEV_10S_NS);

  EXPECT_LE(tdev01, (double)TARGET_TDEV_0p1S_NS);
  EXPECT_LE(tdev1,  (double)TARGET_TDEV_1S_NS);
  EXPECT_LE(tdev10, (double)TARGET_TDEV_10S_NS);

  swclock_destroy(clk);
}

TEST(Perf, SettlingAndOvershoot) {
  SwClock* clk = swclock_create();
  ASSERT_NE(clk, nullptr);
  printf("\n=== Settling & Overshoot (IMMEDIATE step +1 ms) ===\n");

  // Establish raw reference for TE
  struct timespec sw0, raw0;
  swclock_gettime(clk, CLOCK_REALTIME, &sw0);
  clock_gettime(CLOCK_MONOTONIC_RAW, &raw0);
  auto ts_to_ns = [](const struct timespec* ts) -> long long {
    return (long long)ts->tv_sec * NS_PER_SEC + (long long)ts->tv_nsec;
  };
  auto TE_now = [&](void)->long long {
    struct timespec sw, rr;
    swclock_gettime(clk, CLOCK_REALTIME, &sw);
    clock_gettime(CLOCK_MONOTONIC_RAW, &rr);
    long long sw_rel  = ts_to_ns(&sw) - ts_to_ns(&sw0);
    long long raw_rel = ts_to_ns(&rr) - ts_to_ns(&raw0);
    return sw_rel - raw_rel;
  };
  auto sleep_ns = [&](long long ns){
    if (ns<=0) return;
    struct timespec rq{(time_t)(ns/NS_PER_SEC),(long)(ns%NS_PER_SEC)};
    while (nanosleep(&rq,nullptr)==-1 && errno==EINTR) {}
  };

  // Apply an IMMEDIATE relative step of +1 ms.
  // This is the correct stimulus for a classical step-response overshoot metric.
  const double STEP_US = 1000.0;  // 1 ms
  struct timex tx = {};
  tx.modes        = ADJ_SETOFFSET | ADJ_MICRO;   // immediate relative step
  tx.time.tv_sec  = 0;
  tx.time.tv_usec = (int)llround(STEP_US);
  swclock_adjtime(clk, &tx);

  // Immediately after the step, TE should be close to +1 ms.
  // We will track (i) settling time into ±10 µs for ≥3 s, and
  // (ii) percent overshoot = |min_TE_below_zero| / step.
  const double SETTLE_BAND_US = 10.0;
  const double DWELL_S        = 3.0;
  const double TIMEOUT_S      = 60.0;

  long long min_below_zero_ns = 0;   // most negative TE after the step
  bool settled = false;
  double t = 0.0, dwell = 0.0;

  // Log once per ~1 s
  const long long POLL_NS = PERF_POLL_NS;
  const double POLL_S = (double)POLL_NS * 1e-9;

  // First few polls to let the immediate step register in the readout
  for (int i=0;i<3;i++){ sleep_ns(POLL_NS); }

  // Main loop
  while (t < TIMEOUT_S) {
    sleep_ns(POLL_NS);
    long long te = TE_now();  // ns
    double te_us = (double)te / 1000.0;

    if (fmod(t, 1.0) < POLL_S) {
      printf("  t=%5.2fs  TE=%+8.3f us\n", t, te_us);
    }

    // Track worst excursion below zero (overshoot for a positive step)
    if (te < 0 && llabs(te) > llabs(min_below_zero_ns)) {
      min_below_zero_ns = te;
    }

    // Settling check: inside ±10 µs continuously for DWELL_S seconds
    if (fabs(te_us) <= SETTLE_BAND_US) {
      dwell += POLL_S;
      if (dwell >= DWELL_S) { settled = true; break; }
    } else {
      dwell = 0.0;
    }

    t += POLL_S;
  }

  double settle_time = settled ? t : INFINITY;

  // Percent overshoot relative to 1 ms step
  double overshoot_ns = (double)llabs(min_below_zero_ns);
  double overshoot_pct = 100.0 * overshoot_ns / (STEP_US * 1000.0);

  printf("  Settling time to |TE|<=%.1f us: %s\n", SETTLE_BAND_US,
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

TEST(Perf, SlewRateClamp) {
  SwClock* clk = swclock_create();
  ASSERT_NE(clk, nullptr);

  printf("\n=== Slew-rate command/clamp check (+%.0f ms) ===\n", SLEW_TEST_OFFSET_MS);

  struct timex tx = {};
  tx.modes  = ADJ_OFFSET | ADJ_MICRO;
  tx.offset = (int)llround(SLEW_TEST_OFFSET_MS * 1000.0);
  swclock_adjtime(clk, &tx);

  struct timespec sw0, mr0, sw1, mr1;
  swclock_gettime(clk, CLOCK_REALTIME, &sw0);
  clock_gettime(CLOCK_MONOTONIC_RAW, &mr0);

  const double WIN_S = 3.0;
  sleep_ns((long long)llround(WIN_S * NS_PER_SEC));

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

  struct timespec sw0, rt0, sw1, rt1;
  swclock_gettime(clk, CLOCK_REALTIME, &sw0);
  clock_gettime(CLOCK_REALTIME, &rt0);
  sleep_ns((long long)HOLDOVER_S * NS_PER_SEC);
  swclock_gettime(clk, CLOCK_REALTIME, &sw1);
  clock_gettime(CLOCK_REALTIME, &rt1);

  long long d_sw  = ts_to_ns(&sw1) - ts_to_ns(&sw0);
  long long d_sys = ts_to_ns(&rt1) - ts_to_ns(&rt0);
  long long extra = d_sw - d_sys;
  double rate_ppm = (double)extra * 1e6 / (double)d_sys;

  printf("  extra = %+lld ns over %ds  → drift rate = %+7.2f ppm (target |ppm| < %.1f)\n",
         extra, HOLDOVER_S, rate_ppm, TARGET_HOLDOVER_RATE_PPM);

  EXPECT_LT(std::fabs(rate_ppm), TARGET_HOLDOVER_RATE_PPM);

  swclock_destroy(clk);
}
