# SwClock User Guide

## Table of Contents

1. [Getting Started](#getting-started)
2. [Interpreting Test Results](#interpreting-test-results)
3. [Common Performance Issues](#common-performance-issues)
4. [Troubleshooting](#troubleshooting)
5. [Best Practices](#best-practices)
6. [Working with CSV Data](#working-with-csv-data)
7. [Understanding Compliance Levels](#understanding-compliance-levels)

---

## Getting Started

### Your First Test Run

After running `./scripts/setup.sh`, start with a quick validation:

```bash
./scripts/performance.sh --quick
```

**Expected duration:** ~70 seconds

**What happens:**
1. Builds GoogleTest suite
2. Runs 7 core performance tests
3. Computes IEEE/ITU-T metrics
4. Generates performance report
5. Creates visualization plots (if CSV enabled)

**Success indicators:**
- All tests show `[  PASSED  ]`
- MTIE/TDEV within ITU-T G.8260 Class C limits
- RMS TE < 1 µs
- No test timeouts or crashes

### Reading Your First Report

Open `performance/performance_YYYYMMDD-HHMMSS/PERFORMANCE_REPORT.md`

**Key sections to check:**

1. **Executive Summary** - Pass/fail status and compliance
2. **MTIE/TDEV Results** - Compliance margins (should be > 90%)
3. **Test Summary Table** - Individual test outcomes
4. **Raw Test Output** - Detailed metrics from each test

---

## Interpreting Test Results

### Understanding Time Error (TE)

**Time Error** is the difference between your clock and the reference:

```
If TE = +500 ns: Your clock is 500 ns ahead
If TE = -200 ns: Your clock is 200 ns behind
```

**Key TE metrics:**

| Metric | What it tells you | Good value | Concern threshold |
|--------|-------------------|------------|-------------------|
| **Mean TE** | Average error (after removing drift) | < 100 ns | > 10 µs |
| **RMS TE** | Overall error magnitude | < 500 ns | > 10 µs |
| **P95 TE** | 95% of errors below this | < 1 µs | > 50 µs |
| **P99 TE** | Worst-case under normal operation | < 2 µs | > 100 µs |
| **Drift** | Frequency offset rate | < 0.01 ppm | > 1.0 ppm |

**Example - Good result:**
```
Mean TE (detrended): 0.01 µs
RMS TE: 0.32 µs
P95 TE: 0.25 µs
P99 TE: 0.37 µs
Drift: 0.000 ppm
```
✅ Excellent synchronization with sub-microsecond errors

**Example - Problem:**
```
Mean TE (detrended): 2.34 µs
RMS TE: 15.67 µs
P95 TE: 45.23 µs
P99 TE: 78.92 µs
Drift: 0.850 ppm
```
⚠️ High errors suggest servo tuning issue or system load

### Understanding MTIE and TDEV

**MTIE (Maximum Time Interval Error):** The worst-case time error over any observation window of length τ.

**Why it matters:** Telecom and timing applications require bounded worst-case error.

**Reading MTIE results:**

```
MTIE Results (ITU-T G.8260 Class C):
  τ = 1s:  6.7 µs  (limit: 100 µs, margin: 93.3%)  ✓
  τ = 10s: 6.8 µs  (limit: 200 µs, margin: 96.6%)  ✓
  τ = 30s: 6.7 µs  (limit: 300 µs, margin: 97.8%)  ✓
```

**What this means:**
- Over ANY 1-second window, max error was 6.7 µs (93.3% better than limit)
- Consistently good across all timescales
- Healthy compliance margins (> 90%)

**Warning signs:**
- Margin < 50%: Limited safety margin, investigate
- Margin < 20%: Close to failure, requires tuning
- Failed (margin < 0%): Non-compliant, critical issue

**TDEV (Time Deviation):** RMS time variation over observation windows.

**Reading TDEV results:**

```
TDEV Results (IEEE 1588-2019):
  τ = 1s:  2.3 µs  (limit: 50 µs, margin: 95.4%)   ✓
  τ = 10s: 2.4 µs  (limit: 100 µs, margin: 97.6%)  ✓
  τ = 30s: 2.3 µs  (limit: 150 µs, margin: 98.5%)  ✓
```

**What this means:**
- RMS time jitter over 1-second windows is 2.3 µs
- Excellent stability across timescales
- Meets IEEE 1588-2019 requirements

### Understanding Allan Deviation (ADEV)

**Allan Deviation:** Characterizes frequency stability as a function of averaging time.

**Reading ADEV plots:**

```
Allan Deviation Results:
  τ = 1s:    0.012 ppm
  τ = 10s:   0.008 ppm
  τ = 100s:  0.005 ppm
```

**Interpretation:**
- **Decreasing ADEV** with τ: Good - averaging reduces noise
- **Flat ADEV**: White phase/frequency noise (acceptable)
- **Increasing ADEV**: Frequency drift or flicker noise (investigate)

**Typical SwClock:** 0.01-0.02 ppm at τ = 1s, improving with longer τ

---

## Common Performance Issues

### Issue 1: High RMS TE (> 10 µs)

**Symptoms:**
```
RMS TE: 15.67 µs
P95 TE: 45.23 µs
P99 TE: 78.92 µs
```

**Common causes:**
1. **System load interference**
   - Check: Run `top` during test - CPU usage should be low
   - Fix: Close unnecessary applications, disable background tasks

2. **Aggressive servo tuning**
   - Check: Look for oscillations in TE time series plot
   - Fix: Increase PLL time constant (reduce aggressiveness)

3. **Clock source noise**
   - Check: Compare CLOCK_REALTIME vs CLOCK_MONOTONIC_RAW performance
   - Fix: May be hardware-limited, consider better oscillator

**Diagnosis steps:**
```bash
# Enable CSV logging to see time series
export SWCLOCK_PERF_CSV=1
./scripts/performance.sh --quick

# Plot the data
python tools/read_performance_csv.py performance/*/raw_data/*.csv
```

Look for patterns: spikes, oscillations, drift

### Issue 2: MTIE/TDEV Compliance Failure

**Symptoms:**
```
MTIE Results (ITU-T G.8260 Class C):
  τ = 1s:  150.2 µs  (limit: 100 µs, margin: -50.2%)  ✗ FAIL
```

**Common causes:**
1. **Insufficient settling time**
   - Issue: Clock not given enough time to stabilize
   - Fix: Increase test duration or warmup period

2. **Incorrect limits applied**
   - Check: Verify you're using appropriate Class (A/B/C) for your application
   - Fix: Adjust limits in test code or use different class

3. **Servo oscillation**
   - Symptom: MTIE increases with τ (should stay flat or decrease)
   - Fix: Reduce servo bandwidth, increase damping

**Diagnosis:**
```bash
# Check if it's a transient issue
./scripts/performance.sh --quick
./scripts/performance.sh --quick  # Run again - should be similar

# Try longer test
./performance.sh --full
```

### Issue 3: Large Frequency Drift

**Symptoms:**
```
Drift: 15.234 ppm
```

**Common causes:**
1. **Uncalibrated oscillator**
   - Expected: Most crystals have 10-100 ppm initial offset
   - This is NORMAL before frequency discipline

2. **Test too short**
   - Issue: Not enough data points for accurate drift estimation
   - Fix: Run longer test (60s minimum)

3. **Temperature drift**
   - Issue: Crystal frequency changing with temperature
   - Fix: Allow system to reach thermal equilibrium before testing

**Expected behavior:**
- **Before discipline:** Drift can be 10-100 ppm
- **After discipline:** Drift should be < 1 ppm
- **Well-tuned:** Drift < 0.01 ppm

### Issue 4: Test Timeouts

**Symptoms:**
```
[  TIMEOUT ] DisciplineTEStats_MTIE_TDEV (120000 ms)
```

**Common causes:**
1. **System overload**
   - Check: CPU usage, memory pressure, disk I/O
   - Fix: Close applications, free up resources

2. **Deadlock in code**
   - Rare: Usually indicates a bug
   - Fix: Report issue with test logs

3. **CSV logging overhead**
   - Issue: Writing CSV slows down test
   - Fix: Temporarily disable: `unset SWCLOCK_PERF_CSV`

### Issue 5: Inconsistent Results

**Symptoms:**
- Pass on one run, fail on another
- Metrics vary significantly between runs

**Common causes:**
1. **Background activity**
   - Issue: Inconsistent system load
   - Fix: Run tests in quiet environment, disable background services

2. **Short test duration**
   - Issue: Not enough samples for stable statistics
   - Fix: Use `--full` mode for longer tests

3. **Thermal effects**
   - Issue: CPU/oscillator temperature changing
   - Fix: Run tests after warmup, in temperature-controlled environment

**Diagnosis:**
```bash
# Run multiple times to establish baseline variation
for i in {1..5}; do
  ./performance.sh --quick
  sleep 60  # Let system settle between runs
done

# Check variation in metrics
grep "RMS TE" performance/*/PERFORMANCE_REPORT.md
```

---

## Troubleshooting

### Build Issues

#### Error: CMake not found
```bash
# macOS
brew install cmake

# Linux
sudo apt-get install cmake
```

#### Error: GoogleTest not found
```bash
# macOS
brew install googletest

# Linux
sudo apt-get install libgtest-dev
```

#### Error: Ninja not found
```bash
# macOS
brew install ninja

# Linux
sudo apt-get install ninja-build
```

#### Build fails with C++ errors
```bash
# Clean and rebuild
rm -rf build/
./setup.sh
./performance.sh --quick
```

### Python Environment Issues

#### Error: numpy/pandas not found
```bash
# Recreate virtual environment
rm -rf tools/venv
./setup.sh

# Manually activate and install
source tools/venv/bin/activate
pip install -r tools/requirements.txt
```

#### Error: matplotlib backend issues (macOS)
```bash
# Set backend for non-interactive use
export MPLBACKEND=Agg
./performance.sh --quick
```

### Test Execution Issues

#### All tests fail immediately
```bash
# Check test binary exists
ls -la build/swclock_gtests

# Run tests directly to see errors
./build/swclock_gtests --gtest_filter=*DisciplineTEStats*
```

#### CSV files not generated
```bash
# Verify environment variable is set
echo $SWCLOCK_PERF_CSV

# Explicitly set it
export SWCLOCK_PERF_CSV=1
./performance.sh --quick

# Check log directory permissions
ls -la performance/
```

#### No plots generated
```bash
# Check Python environment
source tools/venv/bin/activate
python -c "import matplotlib; print(matplotlib.__version__)"

# Run plotting script directly
python tools/read_performance_csv.py performance/*/raw_data/*.csv
```

### Performance Issues During Testing

#### Tests run very slowly
**Possible causes:**
1. System under heavy load - close other applications
2. CSV logging enabled - disable with `unset SWCLOCK_PERF_CSV`
3. Disk I/O bottleneck - check disk space and activity
4. Thermal throttling - check CPU temperature

#### System becomes unresponsive
**Prevention:**
1. Run tests at nice priority: `nice -n 10 ./performance.sh --quick`
2. Limit CPU cores: `taskset -c 0-3 ./performance.sh --quick` (Linux)
3. Monitor resources: Open Activity Monitor/htop in another terminal

---

## Best Practices

### When to Use Quick vs Full Mode

**Use `--quick` mode when:**
- ✅ Rapid development/debugging cycle
- ✅ CI/CD automated testing
- ✅ Sanity checking after code changes
- ✅ Verifying build success
- ✅ Routine regression testing

**Use `--full` mode when:**
- ✅ Final validation before release
- ✅ Establishing performance baselines
- ✅ Long-term stability characterization
- ✅ Investigating intermittent issues
- ✅ Standards compliance certification

**Comparison:**

| Aspect | Quick Mode | Full Mode |
|--------|-----------|-----------|
| Duration | ~70 seconds | 60+ minutes |
| Tests | 7 core tests | All tests including extended holdover |
| MTIE/TDEV | Up to τ=30s | Up to τ=1800s |
| Use case | Development | Validation |

### Setting Appropriate Thresholds

**For development/debugging:**
```cpp
// Relaxed thresholds for catching regressions
EXPECT_LT(rms_te_us, 2.0);     // 2 µs (vs 0.5 µs strict)
EXPECT_LT(drift_ppm, 0.1);     // 0.1 ppm (vs 0.01 ppm strict)
```

**For production validation:**
```cpp
// Strict thresholds for compliance
EXPECT_LT(rms_te_us, 0.5);     // 500 ns
EXPECT_LT(drift_ppm, 0.01);    // 0.01 ppm
```

**Standards-based thresholds:**
- **ITU-T G.8260 Class C:** Use provided limits (100/200/300 µs for MTIE)
- **IEEE 1588-2019:** Use boundary clock specs (typically 50-150 µs TDEV)
- **Custom application:** Define based on requirements

### Working with Baselines

**Establishing a baseline:**
```bash
# Run comprehensive test
./performance.sh --full

# Tag the results
BASELINE=$(ls -td performance/performance_* | head -1)
echo $BASELINE > performance/baseline.txt
```

**Running regression tests:**
```bash
# Compare against baseline
BASELINE=$(cat performance/baseline.txt)
./performance.sh --regression --baseline=$BASELINE
```

**Interpreting regression results:**
- ✅ **Green:** Metrics within 10% of baseline (expected variation)
- ⚠️ **Yellow:** Metrics 10-25% different (investigate)
- ❌ **Red:** Metrics > 25% different (regression or improvement to verify)

### Interpreting CSV Data

**CSV format:**
```csv
timestamp_ns,te_ns
1000000000,123
1000100000,145
1000200000,132
...
```

**Analyzing with Python:**
```python
import pandas as pd
import matplotlib.pyplot as plt

# Load data
df = pd.read_csv('test_data.csv')

# Convert to microseconds
df['te_us'] = df['te_ns'] / 1000.0
df['time_s'] = (df['timestamp_ns'] - df['timestamp_ns'].iloc[0]) / 1e9

# Plot
plt.figure(figsize=(12, 6))
plt.plot(df['time_s'], df['te_us'])
plt.xlabel('Time (s)')
plt.ylabel('Time Error (µs)')
plt.title('Time Error vs Time')
plt.grid(True)
plt.savefig('te_timeseries.png')
```

**Key analyses:**
1. **Time series plot:** Visualize TE behavior over time
2. **Histogram:** Understand error distribution
3. **Allan deviation:** Frequency stability characterization
4. **Spectral analysis:** Identify periodic noise components

### Test Environment Best Practices

**For consistent results:**

1. **Minimize system load**
   ```bash
   # Close unnecessary applications
   # Disable background services
   # Stop indexing (Spotlight on macOS)
   ```

2. **Thermal stability**
   ```bash
   # Let system reach thermal equilibrium
   # Run in temperature-controlled environment
   # Avoid testing during thermal transients
   ```

3. **Isolated test runs**
   ```bash
   # Allow settling time between tests
   ./performance.sh --quick
   sleep 300  # 5 minutes
   ./performance.sh --quick
   ```

4. **Dedicated testing time**
   ```bash
   # Schedule long tests during off-hours
   at 2am <<< './performance.sh --full'
   ```

---

## Working with CSV Data

### Enabling CSV Export

```bash
# Enable CSV logging
export SWCLOCK_PERF_CSV=1

# Optionally specify output directory
export SWCLOCK_LOG_DIR=./my_test_data

# Run tests
./performance.sh --quick
```

**Output location:**
```
performance/performance_YYYYMMDD-HHMMSS/raw_data/
├── DisciplineTEStats_MTIE_TDEV.csv
├── SettlingAndOvershoot.csv
├── SlewRateClamp.csv
├── HoldoverDrift.csv
├── FrequencyOffsetPositive.csv
├── FrequencyOffsetNegative.csv
└── MultipleStepSizes.csv
```

### Analyzing CSV Data

**Using provided tools:**
```bash
# Generate plots for all CSV files
python tools/read_performance_csv.py performance/*/raw_data/*.csv

# Output in plots/csv_analysis/
# - *_timeseries.png: TE over time
# - *_histogram.png: TE distribution
```

**Custom analysis:**
```python
from tools.read_performance_csv import read_te_csv
import numpy as np

# Load data
df = read_te_csv('test_data.csv')

# Custom metrics
te_us = df['te_ns'] / 1000.0
max_excursion = np.max(np.abs(te_us))
zero_crossings = np.sum(np.diff(np.sign(te_us)) != 0)

print(f"Max excursion: {max_excursion:.2f} µs")
print(f"Zero crossings: {zero_crossings}")
```

### Allan Deviation Analysis

**Computing ADEV:**
```python
from tools.test_allan_deviation import compute_adev

# Convert TE to fractional frequency
# For TE in nanoseconds at 10 Hz sampling:
freq_fractional = np.gradient(df['te_ns']) * 10 / 1e9

# Compute ADEV
tau, adev = compute_adev(freq_fractional, rate=10)

# Plot
plt.loglog(tau, adev)
plt.xlabel('Averaging Time τ (s)')
plt.ylabel('Allan Deviation (fractional frequency)')
plt.grid(True)
plt.savefig('allan_deviation.png')
```

---

## Advanced Logging and Audit Features

### Enhanced CSV Metadata Headers

**New in v2.1**: CSV log files now include comprehensive metadata headers with test identification, system information, and compliance targets.

**Header contents (36 lines):**
- **Test Identification**: Test name, UUID, SwClock version, timestamp
- **Configuration**: Kp, Ki, max_ppm, poll interval, phase epsilon
- **System Information**: OS, CPU model, hostname, reference clock, load average
- **Data Format**: Column descriptions, sample rate, timestamp base
- **Compliance Targets**: ITU-T G.8260 Class C limits for MTIE/TDEV

**Example header:**
```csv
# ========================================
# SwClock Performance Test CSV Export
# ========================================
#
# Test Identification:
#   Test Name:        Perf_DisciplineTEStats_MTIE_TDEV
#   Test Run ID:      550e8400-e29b-41d4-a716-446655440000
#   SwClock Version:  v2.0.0
#   Start Time (UTC): 2026-01-13T18:32:49Z
#
# Configuration:
#   Kp (ppm/s):       200.000
#   Ki (ppm/s²):      8.000
#   Max PPM:          200.0
#   Poll Interval:    10000000 ns (100.0 Hz)
#   Phase Epsilon:    20000 ns (20.0 µs)
#
# System Information:
#   Operating System: Darwin 24.2.0
#   CPU:              Apple M1 Max
#   CPU Cores:        10
#   Hostname:         macbook.local
#   Reference Clock:  CLOCK_MONOTONIC_RAW
#   System Load:      2.34
#
# Compliance Targets:
#   Standard:         ITU-T G.8260 Class C
#   MTIE(1s):         < 100 µs
#   MTIE(10s):        < 200 µs
#   MTIE(30s):        < 300 µs
#
timestamp_ns,te_ns
```

**Benefits:**
- **Audit Trail**: Complete test provenance for regulatory compliance
- **Reproducibility**: All configuration parameters documented
- **Traceability**: UUID allows tracking across analysis tools
- **Context**: System state captured for result interpretation

### Servo State Logging

**Enable detailed servo state logging** with the `SWCLOCK_SERVO_LOG` environment variable:

```bash
# Enable servo state logging for performance tests
SWCLOCK_SERVO_LOG=1 ./performance.sh --quick

# Or for individual test runs
SWCLOCK_PERF_CSV=1 SWCLOCK_SERVO_LOG=1 ./build/swclock_gtests
```

**Servo log contents (13 columns):**
1. `timestamp_ns` - CLOCK_MONOTONIC_RAW timestamp
2. `base_rt_ns` - REALTIME base (adjusted)
3. `base_mono_ns` - MONOTONIC base (disciplined)
4. `freq_scaled_ppm` - Base frequency bias (Linux scaled units)
5. `pi_freq_ppm` - PI controller output (ppm)
6. `pi_int_error_s` - Integral error accumulator (seconds)
7. `remaining_phase_ns` - Outstanding phase to slew (ns)
8. `pi_servo_enabled` - Boolean (1=enabled, 0=disabled)
9. `maxerror` - Maximum error estimate (µs)
10. `esterror` - Estimated error (µs)
11. `constant` - Time constant
12. `tick` - Tick value
13. `tai` - TAI offset (seconds)

**Use cases:**
- **Debugging**: Analyze servo behavior during transients
- **Tuning**: Optimize Kp/Ki parameters for specific conditions
- **Audit**: Verify servo operates within design parameters
- **Research**: Study PI controller dynamics

**Performance impact:** Minimal (<1% CPU) when disabled, ~15 KB/s write rate when enabled.

**Log location:** `logs/servo_state_YYYYMMDD-HHMMSS_TestName.csv`

### Structured Event Logging

**Binary event logging** provides high-performance, tamper-evident audit trails for servo operations.

```bash
# Enable event logging
SWCLOCK_EVENT_LOG=1 ./build/swclock_gtests

# Creates: logs/events_YYYYMMDD-HHMMSS.bin
```

**Event types captured:**

| Event | Description | Payload |
|-------|-------------|---------|
| `ADJTIME_CALL` | adjtime() invoked | modes, offset, frequency |
| `ADJTIME_RETURN` | adjtime() returned | return code, final state |
| `PI_ENABLE` | PI servo activated | None |
| `PI_DISABLE` | PI servo deactivated | None |
| `PI_STEP` | PI computation completed | freq_ppm, int_error, phase |
| `PHASE_SLEW_START` | Phase slew initiated | target_ns, duration, rate |
| `PHASE_SLEW_DONE` | Phase slew completed | final_phase, actual_time |
| `FREQUENCY_CLAMP` | PI output clamped | requested, clamped, limit |
| `THRESHOLD_CROSS` | Phase threshold crossed | threshold, direction |

**Reading event logs:**

```bash
# Convert binary log to human-readable format
./build/swclock_event_dump logs/events_20260119-143022.bin

# Or save to file
./build/swclock_event_dump logs/events_20260119-143022.bin events.txt
```

**Example output:**
```
=== SwClock Event Log ===
Format Version: 1.0
SwClock Version: 2.0.0
Start Time: 2026-01-19 14:30:22.418293847
Host: macbook-pro.local

Sequence   Timestamp                      Event Type           Payload
---------- ------------------------------ -------------------- --------------
[0000000000] 2026-01-19 14:30:22.418306123 | PI_ENABLE             | 
[0000000001] 2026-01-19 14:30:22.428441056 | ADJTIME_CALL          | modes=0x0002 offset=500000 ns freq=0 scaled_ppm return=-1
[0000000002] 2026-01-19 14:30:22.428445312 | ADJTIME_RETURN        | modes=0x0002 offset=0 ns freq=0 scaled_ppm return=0
[0000000003] 2026-01-19 14:30:22.438571891 | PI_STEP               | freq=100.000 ppm int_error=0.000000050 s phase=0 ns enabled=1
[0000000004] 2026-01-19 14:30:22.448698234 | FREQUENCY_CLAMP       | requested=5000.000 ppm clamped=500.000 ppm max=500.000 ppm
```

**Benefits:**
- **Zero-copy design**: Lock-free ring buffer, no servo thread blocking
- **High performance**: <1 µs overhead per event, ~80 KB/s write rate
- **Tamper-evident**: Binary format with magic numbers, sequence numbers
- **Compact**: ~16 bytes header + variable payload (24-40 bytes typical)
- **Self-describing**: File header contains version, host, timestamp
- **Production-ready**: Automatic buffer overrun detection

**Use cases:**
- **Regulatory compliance**: Timestamped audit trail of all servo actions
- **Field debugging**: Capture events leading to clock errors
- **Performance analysis**: Identify servo behavior patterns
- **Security**: Detect tampering or unexpected servo operations

**Performance impact:** <0.1% CPU when disabled, <1% when enabled at 100 Hz servo rate.

**Log location:** `logs/events_YYYYMMDD-HHMMSS.bin`

### Log Integrity Protection

**Automatic log sealing** with SHA-256 hashing ensures tamper detection for audit compliance.

**Enabled automatically** in `performance.sh` after test completion:

```bash
# Integrity protection happens automatically
./performance.sh --quick

# Creates: performance/performance_YYYYMMDD-HHMMSS/manifest.json
```

**Manifest format:**
```json
{
  "test_run_id": "550e8400-e29b-41d4-a716-446655440000",
  "timestamp": "2026-01-13T18:32:49Z",
  "files": [
    {
      "path": "raw_data/test1.csv",
      "sha256": "a3f8b2e9c1d4...",
      "size": 12345
    },
    {
      "path": "test_output.log",
      "sha256": "b2e9c1d4f5a6...",
      "size": 67890
    }
  ]
}
```

**Manual integrity verification:**

```bash
# Seal logs manually
python3 tools/log_integrity.py seal performance/test_run/

# Verify integrity
python3 tools/log_integrity.py verify performance/test_run/manifest.json

# Output:
# ✓ test_output.log: OK
# ✓ raw_data/test1.csv: OK
# ✓ metrics.json: OK
# All files verified successfully
```

**Features:**
- **Tamper Detection**: Any modification invalidates SHA-256 hash
- **Completeness**: Verifies all logged files present
- **Timestamps**: Manifest includes creation time
- **Optional Signatures**: Supports RSA signatures for non-repudiation

**Use in validation workflow:**
```bash
# Before analysis, verify integrity
if python3 tools/log_integrity.py verify results/manifest.json; then
    python3 tools/analyze_performance_logs.py results/
else
    echo "Error: Log integrity compromised"
    exit 1
fi
```

### Independent Metrics Validation

**Break circular validation** by recomputing metrics from raw data:

```bash
# Validate single CSV file
python3 tools/validate_metrics.py logs/test.csv

# Validate all CSV files in directory
python3 tools/validate_metrics.py --dir logs/

# With expected values (assertions)
python3 tools/validate_metrics.py logs/test.csv --assertions expected.json

# Generate JSON report
python3 tools/validate_metrics.py --dir logs/ --output validation_report.json
```

**What it does:**
1. Loads raw TE time series from CSV
2. Recomputes MTIE/TDEV using `ieee_metrics.py`
3. Compares against test assertions (if provided)
4. Reports validation errors with tolerance checking

**Example output:**
```
Validating: 20260113-183249-Perf_DisciplineTEStats.csv... ✓ PASS

Validation Report
==================

Computed Metrics:
  te_stats:
    mean_te_ns: -274.1
    std_te_ns: 406.9
    max_te_ns: 9543.0
    min_te_ns: -9360.0
  mtie:
    mtie_1s: 9543.0 ns
    mtie_10s: 9360.0 ns
    mtie_30s: 9415.0 ns
  tdev:
    tdev_0.1s: 711.0 ns
    tdev_1s: 653.5 ns
    tdev_10s: 382.4 ns

All metrics within tolerance (1.0%)
```

**Assertions file format:**
```json
{
  "mtie_1s": 9543.0,
  "mtie_10s": 9360.0,
  "tdev_1s": 653.5,
  "mean_te_ns": -274.1
}
```

**Use cases:**
- **Regression Testing**: Verify metrics haven't degraded
- **Cross-Validation**: Independent check of test results
- **Audit Compliance**: Demonstrate independent verification
- **CI/CD**: Automated validation in build pipelines

**Tolerance control:**
```bash
# Default 1% tolerance
python3 tools/validate_metrics.py logs/test.csv

# Stricter 0.1% tolerance
python3 tools/validate_metrics.py logs/test.csv --tolerance=0.1

# Relaxed 5% tolerance
python3 tools/validate_metrics.py logs/test.csv --tolerance=5.0
```

### Logging Best Practices

**For development and debugging:**
```bash
# Enable all logging
SWCLOCK_PERF_CSV=1 SWCLOCK_SERVO_LOG=1 ./performance.sh --quick
```

**For regression testing:**
```bash
# Enable CSV only, verify integrity
SWCLOCK_PERF_CSV=1 ./performance.sh --quick
python3 tools/validate_metrics.py --dir performance/latest/raw_data/
```

**For audit compliance:**
```bash
# Full logging with integrity sealing (automatic)
./performance.sh --full

# Verify and archive
python3 tools/log_integrity.py verify performance/latest/manifest.json
tar czf audit_$(date +%Y%m%d).tar.gz performance/latest/
```

**Storage considerations:**
- CSV logs: ~15 KB per 60s test
- Servo logs: ~90 KB per 60s test (when enabled)
- Manifest: <5 KB
- Total: ~110 KB per test with full logging

---

## Understanding Compliance Levels

### ITU-T G.8260 Classes

**Class A (Stringent):**
- Primary reference clocks
- Core network synchronization
- MTIE limits: 25/50/75 µs at 1/10/30s

**Class B (Standard):**
- Boundary clocks in metro networks
- Secondary synchronization
- MTIE limits: 50/100/150 µs at 1/10/30s

**Class C (Relaxed):**
- Edge devices and access networks
- End applications
- MTIE limits: 100/200/300 µs at 1/10/30s

**SwClock targets:** Class C with >90% margins, capable of Class B

### IEEE 1588-2019 Requirements

**Boundary Clock (BC):**
- TDEV < 100 µs at τ=1-100s
- Asymmetry compensation < 100 ns
- Slave-only operation supported

**Ordinary Clock (OC):**
- TDEV < 50 µs at τ=1-100s
- Full PTP state machine
- Multi-profile support

**SwClock implementation:** Servo layer compatible with BC requirements

### Application-Specific Requirements

**5G RAN Fronthaul:**
- Phase error < 1.3 µs (< 1.5 µs limit)
- Frequency accuracy < 10 ppb
- MTIE per ITU-T G.8271.1

**Financial Trading:**
- Sub-microsecond timestamping
- Traceable to UTC
- Nanosecond-level accuracy

**Industrial Automation (TSN):**
- Bounded latency < 1 ms
- Jitter < 100 µs
- Time-aware scheduling support

**SwClock suitability:**
- ✅ Excellent for 5G with margins
- ✅ Suitable for financial (sub-µs capable)
- ✅ TSN-compatible timing performance

---

## Additional Resources

- [PERFORMANCE_TESTING.md](PERFORMANCE_TESTING.md) - Detailed testing procedures
- [METRICS_REFERENCE.md](METRICS_REFERENCE.md) - Mathematical definitions
- [LINUX_COMPATIBILITY.md](LINUX_COMPATIBILITY.md) - Platform compatibility
- [../README.md](../README.md) - Project overview

## Getting Help

**If you encounter issues:**

1. Check this guide's [Troubleshooting](#troubleshooting) section
2. Review test logs in `performance/*/test_output.log`
3. Enable verbose logging and CSV export for detailed diagnostics
4. Compare results against baseline runs

**For bugs or feature requests:**
- Review [TODO.txt](../TODO.txt) for known limitations
- Check if issue is already documented
- Collect diagnostic info: logs, system specs, test results
