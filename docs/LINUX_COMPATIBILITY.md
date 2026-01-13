# SwClock Linux Compatibility Reference

**Version**: 2.0.0  
**Platform**: macOS (with Linux-compatible API)  
**Updated**: January 13, 2026

---

## Overview

SwClock provides a **Linux-compatible `adjtimex()` implementation** on macOS, enabling PTP daemons and time synchronization applications designed for Linux to work on macOS with minimal or no code changes.

This document details:
- Which Linux `adjtimex()` features are supported
- Known differences from native Linux
- API compatibility matrix
- Testing recommendations for Linux deployment

---

## Linux adjtimex() Compatibility

### Supported Modes (struct timex.modes)

| Mode Flag | Linux | SwClock | Notes |
|-----------|-------|---------|-------|
| `ADJ_OFFSET` | ✅ | ✅ | Slewed phase adjustment via PI servo |
| `ADJ_FREQUENCY` | ✅ | ✅ | Frequency adjustment in scaled ppm (2^-16 units) |
| `ADJ_SETOFFSET` | ✅ | ✅ | Immediate time step (uses timex.time) |
| `ADJ_NANO` | ✅ | ✅ | Interpret offset in nanoseconds (default: microseconds) |
| `ADJ_MICRO` | ✅ | ✅ | Interpret offset in microseconds (default) |
| `ADJ_MAXERROR` | ✅ | 🟡 | Accepted but not actively managed |
| `ADJ_ESTERROR` | ✅ | 🟡 | Accepted but not actively managed |
| `ADJ_STATUS` | ✅ | 🟡 | Partial support (see status flags) |
| `ADJ_TIMECONST` | ✅ | ❌ | Not implemented (uses fixed PI gains) |
| `ADJ_TAI` | ✅ | ✅ | TAI offset support (+37s from UTC) |
| `ADJ_TICK` | ✅ | ❌ | Not implemented (fixed tick rate) |

**Legend:**
- ✅ Full support - Behavior matches Linux
- 🟡 Partial support - Accepted but may differ
- ❌ Not supported - Flag ignored or returns error

### Status Flags (struct timex.status)

| Status Flag | Linux | SwClock | Notes |
|-------------|-------|---------|-------|
| `STA_PLL` | ✅ | 🟡 | Set when PI servo active |
| `STA_UNSYNC` | ✅ | 🟡 | Managed internally |
| `STA_NANO` | ✅ | ✅ | Nanosecond resolution support |
| `STA_INS` | ✅ | ❌ | Leap second insertion (not implemented) |
| `STA_DEL` | ✅ | ❌ | Leap second deletion (not implemented) |
| `STA_CLOCKERR` | ✅ | 🟡 | Error conditions tracked |

### Struct timex Fields

| Field | Type | Linux | SwClock | Notes |
|-------|------|-------|---------|-------|
| `modes` | uint | ✅ | ✅ | Mode flags (see above) |
| `offset` | long | ✅ | ✅ | Phase offset (µs or ns with ADJ_NANO) |
| `freq` | long | ✅ | ✅ | Frequency in scaled ppm (2^-16 units) |
| `maxerror` | long | ✅ | 🟡 | Maximum error estimate (µs) |
| `esterror` | long | ✅ | 🟡 | Estimated error (µs) |
| `status` | int | ✅ | 🟡 | Status flags |
| `constant` | long | ✅ | ❌ | Time constant (not used) |
| `precision` | long | ✅ | ✅ | Clock precision (1 ns) |
| `tolerance` | long | ✅ | ✅ | Frequency tolerance (500 ppm) |
| `time` | timeval | ✅ | ✅ | Time value (for ADJ_SETOFFSET) |
| `tick` | long | ✅ | ❌ | Tick value (not adjustable) |
| `ppsfreq` | long | ✅ | ❌ | PPS frequency (not implemented) |
| `jitter` | long | ✅ | ❌ | PPS jitter (not implemented) |
| `shift` | int | ✅ | ❌ | PPS interval (not implemented) |
| `stabil` | long | ✅ | ❌ | PPS stability (not implemented) |
| `jitcnt` | long | ✅ | ❌ | PPS jitter count (not implemented) |
| `calcnt` | long | ✅ | ❌ | Calibration count (not implemented) |
| `errcnt` | long | ✅ | ❌ | Calibration errors (not implemented) |
| `stbcnt` | long | ✅ | ❌ | Stability count (not implemented) |
| `tai` | int | ✅ | ✅ | TAI offset from UTC (37s) |

---

## POSIX clock_*() Compatibility

### Supported Clock IDs

| Clock ID | Linux | SwClock | Backing Clock (macOS) |
|----------|-------|---------|----------------------|
| `CLOCK_REALTIME` | ✅ | ✅ | Disciplined synthetic clock |
| `CLOCK_MONOTONIC` | ✅ | ✅ | Disciplined synthetic clock |
| `CLOCK_MONOTONIC_RAW` | ✅ | 🟡 | macOS native (reference) |

### clock_gettime()
```c
int swclock_gettime(SwClock* clk, clockid_t clk_id, struct timespec* tp);
```
- ✅ `CLOCK_REALTIME`: Returns disciplined wall-clock time
- ✅ `CLOCK_MONOTONIC`: Returns disciplined monotonic time
- 🟡 `CLOCK_MONOTONIC_RAW`: Passes through to macOS (reference clock)

### clock_settime()
```c
int swclock_settime(SwClock* clk, clockid_t clk_id, const struct timespec* tp);
```
- ✅ `CLOCK_REALTIME`: Sets absolute time (immediate step)
- ❌ `CLOCK_MONOTONIC`: Not settable (returns error, Linux-compatible)

---

## Key Differences from Native Linux

### 1. PI Servo Implementation
**Linux**: Uses NTP-style PLL/FLL  
**SwClock**: Fixed PI controller with tuned gains

```c
// SwClock PI gains (sw_clock_constants.h)
#define SWCLOCK_PI_KP_PPM_PER_S   200.0  // Proportional gain
#define SWCLOCK_PI_KI_PPM_PER_S2  8.0    // Integral gain
#define SWCLOCK_PI_MAX_PPM        200.0  // Frequency clamp
```

**Impact**: SwClock has faster step response but less flexibility than Linux PLL

### 2. ADJ_TIMECONST Not Supported
**Linux**: Time constant adjustable via `ADJ_TIMECONST`  
**SwClock**: Fixed PI gains (not adjustable at runtime)

**Workaround**: Tune gains at compile time if needed

### 3. No PPS Support
**Linux**: Hardware PPS input for high-accuracy timekeeping  
**SwClock**: Software-only (no hardware PPS interface)

**Impact**: Cannot use external PPS signals for disciplining

### 4. Backing Clock
**Linux**: Native kernel timekeeping  
**SwClock**: Built on macOS `CLOCK_MONOTONIC_RAW`

**Impact**: SwClock accuracy limited by macOS clock stability (~10-50 ns jitter)

### 5. Leap Seconds
**Linux**: Full leap second support (STA_INS, STA_DEL)  
**SwClock**: Leap seconds not implemented

**Impact**: Applications requiring leap second handling may need modification

---

## Performance Comparison

### Typical Performance (macOS vs Linux Expectations)

| Metric | SwClock (macOS) | Native Linux | Notes |
|--------|-----------------|--------------|-------|
| Time Error (RMS) | 200-800 ns | 50-500 ns | macOS clock jitter |
| MTIE @ 1s | 6-20 µs | 5-15 µs | ITU-T G.8260 limit: 100 µs |
| MTIE @ 10s | 6-20 µs | 5-15 µs | ITU-T G.8260 limit: 200 µs |
| MTIE @ 30s | 6-20 µs | 10-20 µs | ITU-T G.8260 limit: 300 µs |
| Settling Time | 2-5 s | 1-3 s | After 1 ms step |
| Frequency Accuracy | <0.1 ppm | <0.01 ppm | Free-running drift |

**Conclusion**: SwClock on macOS achieves excellent performance, slightly higher noise than native Linux but well within IEEE/ITU-T standards.

---

## API Usage Examples

### Example 1: Frequency Adjustment (PTP-style)
```c
#include "sw_clock.h"

SwClock* clk = swclock_create();

// Apply +10 ppm frequency correction
struct timex tx = {0};
tx.modes = ADJ_FREQUENCY;
tx.freq = (long)(10.0 * 65536.0);  // Scaled ppm (2^16)

swclock_adjtime(clk, &tx);
```

**Linux Compatibility**: ✅ Identical API

### Example 2: Phase Adjustment (Slewed)
```c
// Apply +1 ms phase correction (slewed via PI servo)
struct timex tx = {0};
tx.modes = ADJ_OFFSET | ADJ_MICRO;
tx.offset = 1000;  // 1 ms in microseconds

swclock_adjtime(clk, &tx);
```

**Linux Compatibility**: ✅ Identical API and behavior

### Example 3: Immediate Step
```c
// Step clock forward by 100 ms
struct timex tx = {0};
tx.modes = ADJ_SETOFFSET | ADJ_MICRO;
tx.time.tv_sec = 0;
tx.time.tv_usec = 100000;  // 100 ms

swclock_adjtime(clk, &tx);
```

**Linux Compatibility**: ✅ Identical API

### Example 4: Reading Time
```c
struct timespec ts;
swclock_gettime(clk, CLOCK_REALTIME, &ts);

printf("Time: %ld.%09ld\n", ts.tv_sec, ts.tv_nsec);
```

**Linux Compatibility**: ✅ Same as Linux `clock_gettime()`

---

## Testing on Linux

### Recommended Test Procedure

If deploying on actual Linux, validate compatibility with these tests:

#### 1. Build on Linux
```bash
# Ubuntu/Debian
sudo apt-get install build-essential cmake ninja-build libgtest-dev

# Fedora/RHEL
sudo dnf install gcc gcc-c++ cmake ninja-build gtest-devel

# Build
./build.sh --tests
```

#### 2. Run Unit Tests
```bash
./build/ninja-gtests-macos/swclock_gtests --gtest_filter="SwClockV1.*"
```

Expected: All 6 core tests PASS

#### 3. Run Performance Tests
```bash
./performance.sh --quick
```

Expected: MTIE/TDEV within ITU-T G.8260 Class C limits

#### 4. Compare with Native Linux
```bash
# On Linux, compare SwClock vs native adjtimex()
./tools/compare_linux_native.sh  # (to be created)
```

### Known Platform Differences to Test

1. **Threading behavior**: macOS vs Linux pthread differences
2. **Clock precision**: macOS CLOCK_MONOTONIC_RAW vs Linux
3. **Nanosleep accuracy**: Linux typically higher resolution
4. **Memory barriers**: Architecture-specific behavior

---

## Cross-Platform Code Guidelines

### Writing Portable PTP Code

If your application targets both Linux and macOS with SwClock:

```c
#ifdef __APPLE__
  #include "sw_clock.h"
  SwClock* clk = swclock_create();
  #define MY_ADJTIME(tx) swclock_adjtime(clk, tx)
  #define MY_GETTIME(id, ts) swclock_gettime(clk, id, ts)
#else
  #include <sys/timex.h>
  #define MY_ADJTIME(tx) adjtimex(tx)
  #define MY_GETTIME(id, ts) clock_gettime(id, ts)
#endif
```

### Feature Detection
```c
// Check for SwClock-specific features
#ifdef SWCLOCK_VERSION
  // SwClock environment
  #define HAS_PI_SERVO 1
  #define HAS_PPS_SUPPORT 0
#else
  // Native Linux
  #define HAS_PI_SERVO 0
  #define HAS_PPS_SUPPORT 1
#endif
```

---

## Limitations and Known Issues

### 1. No Kernel-Level Timekeeping
**Linux**: adjtimex() affects system-wide kernel time  
**SwClock**: User-space only (does not affect macOS system clock)

**Impact**: Cannot discipline system time; suitable for PTP applications that manage their own timebase

### 2. No Hardware Timestamping
**Linux**: PHC (PTP Hardware Clock) support via ethtool  
**macOS**: No hardware timestamping API

**Impact**: Software timestamps only (microsecond accuracy vs nanosecond with hardware)

### 3. No STA_FREQHOLD
**Linux**: Can hold frequency during PLL updates  
**SwClock**: Always applies frequency adjustments immediately

**Impact**: Minor behavioral difference in transient response

### 4. Different Error Estimates
**Linux**: maxerror and esterror actively updated by NTP algorithm  
**SwClock**: Simplified error tracking (may differ from Linux values)

**Impact**: Applications relying on precise error estimates may need adjustment

---

## Future Linux Compatibility Work

### Short-term (Can be added)
- [ ] `ADJ_TIMECONST` support (make PI gains runtime-configurable)
- [ ] Improved error estimate calculations (match Linux NTP behavior)
- [ ] Full status flag implementation (STA_*)
- [ ] ADJ_TICK support (if needed for compatibility)

### Long-term (Requires significant effort)
- [ ] PPS input support (software-emulated)
- [ ] Leap second handling (STA_INS, STA_DEL)
- [ ] Linux kernel module version (true kernel timekeeping)
- [ ] Hardware timestamping via custom driver

### Deferred (Out of scope)
- [ ] PHC (PTP Hardware Clock) support - requires hardware
- [ ] Kernel-level system clock discipline - architectural limitation

---

## Validation Matrix

| Test Scenario | macOS SwClock | Linux Native | Status |
|---------------|---------------|--------------|--------|
| Basic adjtimex() calls | ✅ Tested | 🟡 Not tested | Needs Linux validation |
| Frequency adjustment | ✅ Tested | 🟡 Not tested | Needs Linux validation |
| Phase slewing | ✅ Tested | 🟡 Not tested | Needs Linux validation |
| Immediate steps | ✅ Tested | 🟡 Not tested | Needs Linux validation |
| clock_gettime() | ✅ Tested | 🟡 Not tested | Needs Linux validation |
| PI servo response | ✅ Tested | 🟡 Not tested | Needs Linux validation |
| MTIE/TDEV compliance | ✅ Tested | 🟡 Not tested | Needs Linux validation |
| TAI offset | ✅ Tested | 🟡 Not tested | Needs Linux validation |
| Multi-threaded access | ✅ Tested | 🟡 Not tested | Needs Linux validation |
| Error handling | ✅ Tested | 🟡 Not tested | Needs Linux validation |

**Next Step**: Establish Linux test environment (VM, container, or native) for validation

---

## Conclusion

SwClock provides **high-fidelity Linux compatibility** for the subset of `adjtimex()` features relevant to PTP and time synchronization applications. While not 100% feature-complete compared to native Linux, it covers the essential use cases and maintains API compatibility.

**Recommendation for Linux Deployment:**
1. Test thoroughly in Linux environment before production use
2. Validate performance meets your application requirements
3. Check for any application-specific features that may differ
4. Consider native Linux implementation if hardware PPS or kernel-level timekeeping required

**For macOS Development:**
SwClock is production-ready and provides excellent timing performance suitable for most PTP applications.
