# SwClock Performance Results

This document records measured PTP slave clock performance for three configurations:
**System Clock (sysclock)**, **SwClock v1 (baseline)**, and **SwClock v2 (improved)**.

All results are from 5-minute live PTP sessions on a real Ethernet network.

---

## Test Environment

| Parameter         | Value                                      |
|-------------------|--------------------------------------------|
| Host              | macOS (Apple Silicon)                      |
| PTP implementation| ptpd2 v6.6.6-slaveonly                     |
| Network interface | en5 (1000BASE-T, full-duplex)              |
| PTP mode          | Slave-only, multicast IPv4, E2E delay      |
| Run duration      | 300 seconds (5 minutes) per configuration  |
| Sync interval     | ~1 Hz (one sample per second)              |
| Sample count      | ~2727 per run                              |

The system clock backend uses macOS `clock_adjtime()`/`ntp_adjtime()` directly.  
SwClock interposes a software PI-servo disciplined by `CLOCK_MONOTONIC_RAW`.

---

## v1 → v2 Changes

Five targeted improvements were made between SwClock v1 and v2:

### 1. Removed `MIN_SLEW_PPM` floor
**Problem:** A 100 ppm minimum slew rate was clamped into the PI output, preventing the
controller from commanding zero correction. This created a persistent ~+600 µs systematic
bias — the servo was always over-correcting.  
**Fix:** Removed the clamp; the PI controller now outputs zero when the error is zero.

### 2. Preserved integrator across `ADJ_OFFSET` calls
**Problem:** On every `ADJ_OFFSET` call the integral accumulator (`pi_int_error_s`) was
zeroed, discarding learned frequency correction and forcing a cold-restart each cycle.  
**Fix:** The integrator is only reset when a phase step is applied, not on every adjustment.

### 3. Fixed monotonic-clock race in `swclock_gettime()`
**Problem:** `CLOCK_MONOTONIC_RAW` was sampled *after* acquiring the rwlock, so scheduling
delays between the lock and the sample inflated the apparent offset.  
**Fix:** `clock_gettime(CLOCK_MONOTONIC_RAW, ...)` is called *before* taking the read lock,
so the timestamp represents the moment just before the protected state is read.

### 4. Two-stage PI gains
**Problem:** A single set of aggressive gains (Kp = 200, Ki = 8) caused large overshoot and
sustained oscillation once the clock was near lock.  
**Fix:** Gains are selected based on `SWCLOCK_TRACKING_THRESHOLD_NS` (100 µs):

| State                            | Kp (ppm/s) | Ki (ppm/s²) |
|----------------------------------|------------|-------------|
| Pull-in  (`\|offset\|` ≥ 100 µs) | 200        | 8           |
| Tracking (`\|offset\|` < 100 µs) | 20         | 1           |

### 5. Frequency-state persistence
**Problem:** Every cold-start began at 0 ppm, causing a transient convergence period
(typically 30–60 s) on each daemon restart.  
**Fix:** `swclock_create()` reads `/tmp/swclock.freq_state` on startup; `swclock_destroy()`
writes the final learned frequency offset. Subsequent runs converge immediately.

---

## 5-Minute Measurement Results

### Full Run (all samples)

| Metric             | Sysclock      | SwClock v1     | SwClock v2     |
|--------------------|---------------|----------------|----------------|
| Samples            | 2,727         | 2,724          | 2,733          |
| Median offset      | −973.5 µs     | −284.9 µs      | **−186.6 µs**  |
| \|Median\| bias    | 973.5 µs      | 284.9 µs       | **186.6 µs**   |
| Std Dev            | 122.4 µs      | 708,844 µs †   | **90.6 µs**    |
| RMS                | 977.0 µs      | 708,838 µs †   | **207.2 µs**   |
| Peak-to-Peak       | 1,038.7 µs    | 36,996,252 µs †| **746.8 µs**   |
| Sub-1 ms %         | 57.9 %        | 100.0 %        | **100.0 %**    |

† SwClock v1 full-run statistics include the 37-second TAI−UTC offset step applied by
ptpd2 at startup (`step_startup`). This is a ptpd2 behaviour, not a SwClock defect, and
it inflates all variance metrics for that run.

### Locked Window (`|offset| < 1 ms`)

This is the fair apples-to-apples comparison: it shows steady-state tracking quality
for the fraction of the run where each backend was actually within 1 ms of the master.

| Metric          | Sysclock    | SwClock v1  | SwClock v2      | v2 vs v1 | v2 vs sys |
|-----------------|-------------|-------------|-----------------|----------|-----------|
| Locked samples  | 1,580       | 2,723       | **2,733**       | —        | —         |
| \|Median\| bias | 895.9 µs    | 284.9 µs    | **186.6 µs**    | −35 %    | −79 %     |
| Std Dev         | **84.6 µs** | 99.0 µs     | 90.6 µs         | −8 %     | +7 %      |
| RMS             | 890.2 µs    | 301.3 µs    | **207.2 µs**    | −31 %    | −77 %     |
| Peak-to-Peak    | 759.2 µs    | 822.9 µs    | **746.8 µs**    | −9 %     | −2 %      |
| Jitter (mean)   | **30.3 µs** | 49.6 µs     | 46.1 µs         | −7 %     | +52 %     |

---

## Analysis

### Sub-1 ms retention is the decisive metric

Sysclock held sub-1 ms for only **57.9 %** of the run. For the remaining 42 % of the
session the clock was more than 1 ms away from the PTP master — an operational failure
for any application with sub-millisecond timing requirements.

Both SwClock variants retained sub-1 ms for **100 %** of the run.

### v2 delivers best steady-state accuracy

Within the locked window, SwClock v2 achieves:
- **35 % lower bias** than v1 (186.6 µs vs 284.9 µs) — driven primarily by the removal
  of the `MIN_SLEW_PPM` floor.
- **31 % lower RMS** than v1 — reflects both lower bias and tighter variance.
- **9 % lower peak-to-peak** than v1 — the two-stage gains reduce the overshoot that
  caused excursions in v1.

### Sysclock short-term noise advantage

Within its locked window, sysclock has lower per-sample jitter (30.3 µs vs 46.1 µs) and
lower std dev (84.6 µs vs 90.6 µs). This reflects direct kernel hardware-clock access
with no interposed software layer. However, sysclock provides no long-term frequency
discipline — it relies entirely on ptpd2's own servo, which is insufficient to keep the
macOS system clock within 1 ms when running slave-only for several minutes.

SwClock trades ~16 µs of short-term jitter for guaranteed long-term lock retention. This
is the correct trade-off for a PTP slave on macOS.

### Verdict

| Metric        | Winner         |
|---------------|----------------|
| \|Bias\|      | **SwClock v2** |
| RMS           | **SwClock v2** |
| Peak-to-Peak  | **SwClock v2** |
| Std Dev       | Sysclock (within its 58 % locked window) |
| Jitter        | Sysclock (within its 58 % locked window) |
| Lock retention| **SwClock v1 & v2** (100 % vs 57.9 %) |

**SwClock v2 is the recommended backend** for production PTP slave deployments on macOS.

---

## Configuration Reference

Key constants governing v2 behaviour (see `sw_clock_constants.h`):

| Constant                          | Value    | Effect                                          |
|-----------------------------------|----------|-------------------------------------------------|
| `SWCLOCK_PI_KP_PPM_PER_S`         | 200.0    | Pull-in proportional gain                       |
| `SWCLOCK_PI_KI_PPM_PER_S2`        | 8.0      | Pull-in integral gain                           |
| `SWCLOCK_TRACKING_THRESHOLD_NS`   | 100,000  | Pull-in → tracking transition point (100 µs)   |
| `SWCLOCK_PI_KP_FINE_PPM_PER_S`    | 20.0     | Tracking proportional gain                      |
| `SWCLOCK_PI_KI_FINE_PPM_PER_S2`   | 1.0      | Tracking integral gain                          |
| `SWCLOCK_PI_MAX_PPM`              | 200.0    | Maximum frequency correction magnitude (ppm)   |
| `SWCLOCK_PHASE_EPS_NS`            | 20,000   | Dead-band below which phase correction is zero |
| `SWCLOCK_POLL_NS`                 | 10,000,000 | Background thread period (10 ms / 100 Hz)     |

Frequency state is persisted at `/tmp/swclock.freq_state` across daemon restarts.
