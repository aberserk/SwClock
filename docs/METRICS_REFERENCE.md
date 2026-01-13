# Performance Metrics Reference

## Overview

This document provides detailed explanations of the timing metrics computed by the SwClock performance validation framework. All metrics conform to IEEE and ITU-T standards for timing and synchronization.

---

## Time Error (TE) Metrics

### Definition

Time Error (TE) is the difference between the disciplined clock time and a reference time source.

```
TE(t) = Clock(t) - Reference(t)
```

For SwClock tests, the reference is `CLOCK_MONOTONIC_RAW` on macOS.

### Computed Statistics

#### Mean TE (Detrended)

**Formula:**
```
mean_detrended = mean(TE - linear_trend)
```

**Purpose:** Measures average time error after removing frequency offset.

**Target:** `|mean_detrended| < 20 µs`

**Interpretation:**
- Near zero: Clock is well-synchronized
- Large positive: Clock runs fast
- Large negative: Clock runs slow

#### RMS (Root Mean Square) TE

**Formula:**
```
RMS = sqrt(mean(TE²))
```

**Purpose:** Overall measure of time error magnitude, including both systematic and random components.

**Target:** `RMS < 50 µs`

**Interpretation:**
- Low RMS: Tight synchronization
- High RMS: Large time variations

**SwClock Typical:** 0.3 - 0.5 µs

#### Standard Deviation

**Formula:**
```
σ = sqrt(mean((TE - mean(TE))²))
```

**Purpose:** Measures variability excluding the mean offset.

**Relationship:** `RMS² = mean² + σ²`

#### Percentile Errors

**P50 (Median):** 50% of errors are below this value
**P95:** 95% of errors are below this value  
**P99:** 99% of errors are below this value

**Targets:**
- P95 < 150 µs
- P99 < 300 µs

**Interpretation:**
- P99 indicates worst-case errors under normal operation
- Large P99 - P50 gap indicates occasional spikes

#### Frequency Drift

**Formula:**
```
drift_ppm = (slope_ns_per_s / 1e9) × 1e6
```

Where slope is from linear regression: `TE(t) = slope × t + offset`

**Purpose:** Measures systematic frequency offset of the clock.

**Target:** `|drift| < 2.0 ppm`

**Interpretation:**
- 0 ppm: Perfect frequency match
- +1 ppm: Clock runs 1 µs fast per second
- -1 ppm: Clock runs 1 µs slow per second

**SwClock Typical:** < 0.01 ppm

---

## MTIE (Maximum Time Interval Error)

### Definition (ITU-T G.810)

MTIE measures the maximum peak-to-peak time error variation over all observation intervals of duration τ.

**Formula:**
```
MTIE(τ) = max{|max(x(t)) - min(x(t))|} for all t ∈ [0, T-τ]
```

Where:
- x(t) is the detrended time error
- τ is the observation interval
- T is the total measurement duration

### Purpose

MTIE captures worst-case timing behavior, critical for:
- Network synchronization
- Telecom infrastructure
- Time-sensitive applications

### Computation Method

1. **Detrend** time series: Remove linear drift
2. **Sliding window:** For each interval of length τ
3. **Max - Min:** Find peak-to-peak variation
4. **Take maximum:** Over all window positions

### ITU-T G.8260 Class C Limits

**Packet-Based Timing Requirements:**

| Observation Interval τ | MTIE Limit |
|------------------------|------------|
| 1 second | < 100 µs |
| 10 seconds | < 200 µs |
| 30 seconds | < 300 µs |

**SwClock Performance:**

| τ | Measured | Limit | Margin |
|---|----------|-------|--------|
| 1s | 6.7 µs | 100 µs | 93.3% |
| 10s | 6.8 µs | 200 µs | 96.6% |
| 30s | 6.7 µs | 300 µs | 97.8% |

### Interpretation

- **MTIE < Limit:** Clock meets specification
- **Large MTIE:** Indicates:
  - Drift/wander
  - Transient disturbances
  - Poor stability

**Good MTIE Behavior:**
- Relatively flat across observation intervals
- Large margin below limits
- Consistent across multiple runs

---

## TDEV (Time Deviation)

### Definition (ITU-T G.810)

TDEV measures timing stability by estimating the standard deviation of time error changes, normalized for integration time.

**Formula (approximation):**
```
TDEV(τ) ≈ sqrt(mean((x[i+n] - 2×x[i+n/2] + x[i])²) / 6)
```

Where:
- n = τ / sample_period
- x[i] is detrended time error

### Purpose

TDEV removes the effects of:
- Constant frequency offsets
- Linear frequency drift

Focuses on:
- Random noise
- Time jitter
- Short-term instability

### Typical Targets

| Observation Interval τ | TDEV Target |
|------------------------|-------------|
| 0.1 seconds | < 20 µs |
| 1 second | < 40 µs |
| 10 seconds | < 80 µs |

### Interpretation

- **Low TDEV:** Stable, low-noise clock
- **High TDEV:** 
  - Phase noise
  - Jitter
  - Random walk

**TDEV vs MTIE:**
- MTIE: Worst-case metric
- TDEV: Average-case stability

---

## Allan Deviation

### Definition (IEEE 1139)

Allan deviation (ADEV) measures frequency stability over various averaging times.

**Formula:**
```
σ_y(τ) = sqrt((1/2(M-1)) × Σ(ȳ[i+1] - ȳ[i])²)
```

Where:
- ȳ[i] is the fractional frequency average over interval i
- M is the number of intervals

### Purpose

Originally developed for atomic clocks, now used for:
- Oscillator characterization
- Noise type identification
- Long-term stability assessment

### Noise Types

Allan deviation slope identifies noise processes:

| Slope | Noise Type | Typical Source |
|-------|------------|----------------|
| -1 | White Phase Noise | Electronic noise |
| -1/2 | Flicker Phase Noise | Components |
| 0 | White Frequency Noise | Random walk |
| +1/2 | Flicker Frequency Noise | Temperature |
| +1 | Random Walk Frequency | Aging |

### Interpretation

- **Flat region:** Optimal averaging time
- **Decreasing:** Averaging reduces noise
- **Increasing:** Drift/wander dominates

**SwClock:** Allan deviation code implemented but requires frequency data for validation.

---

## Servo Performance Metrics

### Settling Time

**Definition:** Time required for time error to reach and stay within ±10 µs after a step correction.

**Measurement:** 
1. Apply step offset (e.g., +1 ms)
2. Monitor |TE| over time
3. Find first time when |TE| < 10 µs
4. Verify stays below threshold

**Target:** < 20 seconds

**SwClock Typical:** 2.9 seconds

### Overshoot

**Definition:** Maximum excursion beyond target after step correction.

**Formula:**
```
overshoot_pct = (max_relative_error / step_size) × 100%
```

**Target:** < 30%

**SwClock Typical:** < 0.5%

**Interpretation:**
- 0%: Critically damped (ideal)
- < 10%: Well-damped
- > 30%: Under-damped, oscillatory

### Slew Rate

**Definition:** Maximum rate of frequency adjustment during corrections.

**Formula:**
```
slew_rate_ppm = (frequency_change / time_interval) × 1e6
```

**Limits:**
- NTP: ≤ 500 ppm
- SwClock target: ≤ 100 ppm

**SwClock Measured:** ~42 ppm

**Interpretation:**
- Low slew rate: Smooth, gradual corrections
- High slew rate: Aggressive but potentially disruptive

---

## Compliance Thresholds

### ITU-T G.8260 Class C

**Application:** Packet-based timing for mobile backhaul

**Requirements:**
- ✅ MTIE(1s) < 100 µs
- ✅ MTIE(10s) < 200 µs
- ✅ MTIE(30s) < 300 µs

**SwClock Status:** ✅ **PASS** (93-98% margin)

### IEEE 1588-2019 Annex J

**Application:** PTP servo specification

**Requirements (typical):**
- TE RMS < 50 µs
- Frequency drift < 2 ppm
- Settling time < 30 s
- Overshoot < 50%

**SwClock Status:** ✅ **EXCEEDS** requirements

### Custom SwClock Targets

More stringent than standards for high-precision applications:

| Metric | Standard | SwClock Target | Measured |
|--------|----------|----------------|----------|
| RMS TE | 50 µs | 1 µs | 0.32 µs ✅ |
| P99 TE | 300 µs | 2 µs | 0.37 µs ✅ |
| Drift | 2 ppm | 0.1 ppm | 0.000 ppm ✅ |
| MTIE(1s) | 100 µs | 50 µs | 6.7 µs ✅ |
| Settling | 30 s | 10 s | 2.9 s ✅ |
| Overshoot | 50% | 5% | 0.1% ✅ |

---

## Statistical Significance

### Sample Requirements

**Minimum samples for reliable metrics:**
- TE statistics: 100+ samples
- MTIE: 100+ samples per observation interval
- TDEV: 1000+ samples recommended
- Allan deviation: 10,000+ samples ideal

### Confidence Intervals

Not currently computed but recommended additions:
- 95% confidence intervals on RMS, MTIE, TDEV
- Bootstrap resampling for uncertainty estimation
- Multiple test runs for reproducibility

---

## Units and Conventions

### Time Units

- **Seconds (s):** Base SI unit
- **Milliseconds (ms):** 1 ms = 10⁻³ s
- **Microseconds (µs):** 1 µs = 10⁻⁶ s
- **Nanoseconds (ns):** 1 ns = 10⁻⁹ s

**SwClock internal representation:** nanoseconds (int64_t)

### Frequency Units

- **Hertz (Hz):** Cycles per second
- **Parts per million (ppm):** 1 ppm = 10⁻⁶ fractional frequency
- **Parts per billion (ppb):** 1 ppb = 10⁻⁹ fractional frequency

**Conversion:**
```
drift_ppm = (frequency_error_hz / nominal_frequency_hz) × 1e6
```

**Example:**
- 1 ppm at 1 Hz = 1 µHz error
- 1 ppm = 1 µs error per second
- 1 ppm = 86.4 ms error per day

---

## References

### Standards Documents

1. **ITU-T G.810** (08/1996)  
   "Definitions and terminology for synchronization networks"

2. **ITU-T G.8260** (2020)  
   "Definitions and terminology for synchronization in packet networks"

3. **IEEE 1588-2019**  
   "Precision Clock Synchronization Protocol for Networked Measurement and Control Systems"  
   Annex J: "Default PTP Dataset Specificatio and Clock Servo Specification"

4. **IEEE 1139-2008**  
   "Standard Definitions of Physical Quantities for Fundamental Frequency and Time Metrology"

### Additional Resources

- [NIST Time and Frequency Publications](https://www.nist.gov/pml/time-and-frequency-division/publications)
- [ITU-T SG15 - Transport networks](https://www.itu.int/en/ITU-T/studygroups/2017-2020/15/Pages/default.aspx)
- [IEEE 1588 Standards Association](https://standards.ieee.org/standard/1588-2019.html)

---

## Metric Computation Code

See implementation in:
- `tools/ieee_metrics.py` - All metric calculations
- `src-gtests/tests_performance.cpp` - Test data collection
- `tools/analyze_performance_logs.py` - Metric extraction from logs
