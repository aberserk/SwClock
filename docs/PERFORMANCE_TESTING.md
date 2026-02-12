# SwClock Performance Testing Guide

## Overview

The SwClock performance validation framework provides comprehensive IEEE 1588-2019 and ITU-T G.810/G.8260 standards-compliant testing for the SwClock library. This guide explains how to run tests, interpret results, and understand the metrics.

---

## Quick Start

### Prerequisites

1. **Build environment** (automatically configured by `setup.sh`):
   - CMake 3.16+
   - Ninja build system
   - GoogleTest
   - Python 3 with virtual environment

2. **First-time setup**:
   ```bash
   ./setup.sh
   ```
   This will:
   - Install build dependencies via Homebrew
   - Create Python virtual environment in `tools/venv/`
   - Install analysis packages (numpy, pandas, matplotlib, scipy)

### Running Tests

#### Quick Validation (~70 seconds)
```bash
./scripts/performance.sh --quick
```

Runs 3 essential tests:
- **DisciplineTEStats_MTIE_TDEV** (60s): Clock discipline with MTIE/TDEV metrics
- **SettlingAndOvershoot**: Transient response to 1ms step
- **SlewRateClamp**: Frequency slew rate validation

#### Full Validation (~60+ minutes)
```bash
./scripts/performance.sh --full
```

Includes all quick tests plus:
- **HoldoverDrift** (30min+): Long-term stability without corrections

#### Regression Testing
```bash
# First run establishes baseline
./scripts/performance.sh --quick

# Subsequent runs compare against baseline
./scripts/performance.sh --regression --baseline=performance/performance_YYYYMMDD-HHMMSS
```

---

## Understanding Test Results

### Output Structure

Each test run creates a timestamped directory in `performance/`:
```
performance/performance_20260113-132335/
├── test_output.log          # Raw GTest output
├── test_results.json        # Test execution metadata
├── metrics.json             # Computed IEEE metrics
├── PERFORMANCE_REPORT.md    # Human-readable report
├── plots/                   # Visualizations (if generated)
└── raw_data/                # Raw time series data (if available)
```

### Key Metrics Explained

#### Time Error (TE) Statistics

| Metric | Description | Typical Target | SwClock Performance |
|--------|-------------|----------------|---------------------|
| **Mean TE** | Average time error after detrending | < 20 µs | ~0 ns |
| **RMS TE** | Root-mean-square error | < 50 µs | 0.32 µs |
| **P95 TE** | 95th percentile error | < 150 µs | 0.25 µs |
| **P99 TE** | 99th percentile error | < 300 µs | 0.37 µs |
| **Drift** | Frequency offset drift rate | < 2.0 ppm | 0.000 ppm |

#### MTIE (Maximum Time Interval Error)

Measures worst-case time error over observation intervals. Critical for telecom applications.

**ITU-T G.8260 Class C Limits:**
- τ = 1s: < 100 µs
- τ = 10s: < 200 µs
- τ = 30s: < 300 µs

**SwClock Results:**
- τ = 1s: 6.7 µs (93.3% margin)
- τ = 10s: 6.8 µs (96.6% margin)
- τ = 30s: 6.7 µs (97.8% margin)

#### TDEV (Time Deviation)

Measures timing stability, removing frequency offsets. Lower is better.

**Typical Targets:**
- τ = 0.1s: < 20 µs
- τ = 1s: < 40 µs
- τ = 10s: < 80 µs

#### Slew Rate

Maximum frequency adjustment rate during corrections.

**Limits:**
- NTP-compatible: < 500 ppm
- SwClock measured: ~42 ppm

---

## Interpreting Reports

### Report Sections

#### 1. Executive Summary
Quick overview of test results and overall pass/fail status.

```markdown
**Overall Result**: ✅ PASS
**Tests Executed**: 3
**Time Error (TE) RMS**: 0.32 µs
**Frequency Drift**: 0.000 ppm
```

#### 2. Detailed Test Results
Individual test breakdowns with metrics tables.

**Example:**
```markdown
### DisciplineTEStats_MTIE_TDEV

| Metric | Value | Target | Status |
|--------|-------|--------|--------|
| Mean TE | -0.00 µs | < 20 µs | ✅ |
| RMS TE | 0.32 µs | < 50 µs | ✅ |
| P99 TE | 0.37 µs | < 100 µs | ✅ |
| Drift | 0.000 ppm | < 2 ppm | ✅ |
```

#### 3. Standards Compliance Summary
ITU-T G.8260 and IEEE 1588-2019 compliance status.

**Class C Compliance**: ✅ PASS indicates the clock meets telecom-grade timing requirements.

#### 4. Visualizations
References to generated plots (when available).

---

## Troubleshooting

### Common Issues

#### 1. Python Package Errors
**Symptom:** Import errors for numpy, pandas, or matplotlib

**Solution:**
```bash
cd tools
source venv/bin/activate
pip install -r requirements.txt
```

#### 2. Build Failures
**Symptom:** GoogleTest not found or compilation errors

**Solution:**
```bash
./scripts/setup.sh  # Re-run setup
./scripts/build.sh --clean  # Clean rebuild
```

#### 3. Test Timeouts
**Symptom:** Tests take longer than expected

**Reason:** Performance tests run in real-time to measure actual clock behavior.
- Quick mode: ~70 seconds
- Full mode: ~60+ minutes

This is **normal and expected**.

#### 4. Performance Regressions
**Symptom:** Regression report shows degraded metrics

**Action:**
1. Review recent code changes
2. Check system load during test execution
3. Verify consistent test conditions (temperature, system load)
4. Run multiple times to confirm consistency

---

## Advanced Usage

### Custom Test Filters

Run specific tests:
```bash
./build/ninja-gtests-macos/swclock_gtests --gtest_filter="Perf.DisciplineTEStats_MTIE_TDEV"
```

List all available tests:
```bash
./build/ninja-gtests-macos/swclock_gtests --gtest_list_tests
```

### Analyzing Existing Results

Re-analyze test output without re-running tests:
```bash
source tools/venv/bin/activate
python3 tools/analyze_performance_logs.py \
    --test-output=performance/performance_YYYYMMDD-HHMMSS/test_output.log \
    --test-results=performance/performance_YYYYMMDD-HHMMSS/test_results.json \
    --output-dir=performance/performance_YYYYMMDD-HHMMSS \
    --mode=quick
```

### Comparing Performance

```bash
source tools/venv/bin/activate
python3 tools/compare_performance.py \
    --baseline=performance/baseline/metrics.json \
    --current=performance/performance_YYYYMMDD-HHMMSS/metrics.json \
    --output=comparison_report.md
```

Exit code indicates results:
- `0`: No regressions (< 10% change)
- `1`: Regressions detected (≥ 10% degradation)

---

## Standards Reference

### ITU-T G.8260

"Definitions and terminology for synchronization in packet networks"

**Class C Limits** (telecom-grade timing):
- MTIE(τ=1s) < 100 µs
- MTIE(τ=10s) < 200 µs
- MTIE(τ=30s) < 300 µs

**Use Cases:** Packet-based timing for mobile networks, synchronous Ethernet

### IEEE 1588-2019 Annex J

Clock servo specification for PTP (Precision Time Protocol).

**Key Requirements:**
- Low steady-state time error
- Bounded transient response
- Minimal overshoot on step corrections
- Appropriate damping characteristics

---

## Performance Expectations

### Typical SwClock Results

Based on macOS with 1ms tick resolution:

| Metric | Expected Range | Production Goal |
|--------|----------------|-----------------|
| RMS TE | 0.3 - 0.5 µs | < 1 µs |
| P99 TE | 0.3 - 0.6 µs | < 2 µs |
| Drift | < 0.01 ppm | < 0.1 ppm |
| MTIE(1s) | 5 - 10 µs | < 50 µs |
| Settling time | 2 - 5 s | < 10 s |
| Overshoot | < 1% | < 5% |

### When to Be Concerned

**Red Flags:**
- RMS TE > 5 µs
- Drift > 1 ppm
- MTIE fails ITU-T G.8260 limits
- Settling time > 20 seconds
- Overshoot > 30%

**Possible Causes:**
- System under heavy load
- Thermal instability
- Clock interrupt issues
- PI servo tuning needed

---

## CI/CD Integration

### Exit Codes

Scripts return meaningful exit codes for automation:

```bash
./performance.sh --quick
# Exit 0: All tests pass
# Exit 1: Test failures or errors

./scripts/test.sh
# Exit 0: All tests pass
# Exit 1: Test failures

./build.sh
# Exit 0: Build successful
# Exit 1: Build failed
```

### GitHub Actions Example

```yaml
name: Performance Validation
on: [push, pull_request]

jobs:
  performance:
    runs-on: macos-latest
    steps:
      - uses: actions/checkout@v3
      - name: Setup environment
        run: ./setup.sh
      - name: Run performance tests
        run: ./performance.sh --quick
      - name: Upload results
        uses: actions/upload-artifact@v3
        with:
          name: performance-results
          path: performance/
```

---

## FAQ

### Q: Why do tests take so long?

**A:** Performance tests measure real-time clock behavior. The DisciplineTEStats test runs for 60 seconds to gather enough samples for accurate MTIE/TDEV calculations. This is necessary for standards compliance validation.

### Q: Can I speed up tests?

**A:** Not recommended. Shorter test durations won't provide statistically significant results for MTIE/TDEV metrics. Use `--quick` mode (70s) for rapid validation; reserve `--full` mode (60+ min) for comprehensive analysis.

### Q: Why no plots generated?

**A:** Tests output summary statistics rather than full time series. Plotting code exists but requires raw data export from tests. See TODO.txt item #7 for CSV export feature.

### Q: What's the difference between quick and full mode?

**A:** Quick mode runs 3 fast tests (~70s total). Full mode includes all tests plus extended holdover measurements (60+ minutes). Quick mode is sufficient for most development validation.

### Q: How do I know if my changes broke something?

**A:** Use regression mode:
1. Establish baseline: `./performance.sh --quick`
2. Make changes
3. Compare: `./performance.sh --regression --baseline=performance/performance_YYYYMMDD-HHMMSS`

The report will flag any metrics that degraded by >10%.

---

## Next Steps

- **Development:** Use `--quick` mode after changes
- **Release validation:** Run `--full` mode before tagging
- **CI/CD:** Integrate `--quick` mode in PR checks
- **Production monitoring:** Set up periodic validation

For additional help, see:
- [README.md](../README.md) - Project overview
- [TODO.txt](../TODO.txt) - Known limitations and future work
- [IMPLEMENTATION_GAP_ANALYSIS.md](../IMPLEMENTATION_GAP_ANALYSIS.md) - Technical details
