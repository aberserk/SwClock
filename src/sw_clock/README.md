# SwClock C API Reference Documentation

---

## 1. Overview

The **SwClock** library provides a software-based disciplined clock implementation designed for macOS and Linux compatibility. It exposes a subset of Linux's `adjtimex()` and `clock_*()` functions, allowing a daemon such as **PTPd** or **chronyd** to control and discipline a synthetic software clock.

The API consists of three headers:
- `sw_clock.h` — Core API (creation, time queries, adjustments)
- `sw_clock_utilities.h` — Helper utilities (time conversions, frequency conversions, diagnostics)
- `sw_clock_constants.h` — Portable definitions for `ADJ_*`, `STA_*`, and `TIME_*` constants.

The goal is full functional equivalence with Linux-style timing interfaces while remaining portable to platforms like macOS that lack `ntp_adjtime()`.

---

## 2. Core Types

### 2.1 `SwClock`
A structure representing the software clock instance. The internal fields are opaque and managed entirely by the library. Use `swclock_create()` and `swclock_destroy()` to allocate or free an instance.

---

### 2.2 `struct timex`
This structure mirrors Linux’s `struct timex` used with `ntp_adjtime()`.

| Field | Type | Description |
|:-------|:------|:------------|
| `modes` | `unsigned int` | Bitmask of `ADJ_*` flags specifying which fields to apply. |
| `offset` | `long` | Phase offset to apply. Units: µs (or ns if `ADJ_NANO` set). Used with `ADJ_OFFSET`. |
| `freq` | `long` | Frequency correction (scaled ppm, i.e. `ppm × 2¹⁶`). Used with `ADJ_FREQUENCY`. |
| `status` | `int` | `STA_*` status flags (e.g., `STA_UNSYNC`). Stored but not enforced. |
| `time` | `struct timeval` | Used with `ADJ_SETOFFSET` to apply a relative step. |
| `tai` | `int` | TAI offset (stored). |

**Supported Mode Bits:**
- `ADJ_OFFSET` — Slew clock gradually by offset.
- `ADJ_FREQUENCY` — Set frequency bias (scaled ppm).
- `ADJ_SETOFFSET` — Instantaneously step time by given `time` delta.
- `ADJ_STATUS` — Set `status` word only.
- `ADJ_MICRO` / `ADJ_NANO` — Select offset units.

**Supported Status Bits:**
- `STA_PLL`, `STA_UNSYNC`, `STA_FREQHOLD`, `STA_NANO`, etc. (portable subset)

---

## 3. API Functions

### 3.1 Lifecycle

```c
SwClock* swclock_create(void);
void     swclock_destroy(SwClock* clk);
```

Creates or destroys a SwClock instance. Multiple clocks may coexist independently. The instance encapsulates time bases, current frequency bias, and a PI discipline state if compiled with servo support.

---

### 3.2 Time Queries

```c
int swclock_gettime(SwClock* clk, clockid_t clk_id, struct timespec* tp);
```

Retrieves the current time from the requested clock:

| Clock ID | Meaning |
|:----------|:--------|
| `CLOCK_REALTIME` | The disciplined wall-clock time maintained by SwClock. |
| `CLOCK_MONOTONIC` | Monotonic timebase (software version). |
| `CLOCK_MONOTONIC_RAW` | Hardware raw monotonic clock passthrough. |

**Returns:** `0` on success, `-1` on error (`errno = EINVAL`).

---

### 3.3 Setting Time

```c
int swclock_settime(SwClock* clk, clockid_t clk_id, const struct timespec* tp);
```

Sets the specified clock’s time. Supported only for `CLOCK_REALTIME`. The adjustment is **immediate** (step). Returns `0` on success or `-1` on invalid parameters.

---

### 3.4 Adjusting Time — `swclock_adjtime()`

```c
int swclock_adjtime(SwClock* clk, struct timex* t);
```

Applies frequency or phase adjustments according to `t->modes`. Supported actions:

| Mode | Behavior | Units |
|:------|:----------|:-------|
| `ADJ_FREQUENCY` | Apply frequency bias (scaled ppm). | ppm × 2¹⁶ |
| `ADJ_OFFSET` | Slew by offset (gradual). | µs or ns |
| `ADJ_SETOFFSET` | Step by `t->time` immediately. | s + µs |
| `ADJ_STATUS` | Update `t->status` only. | — |

**Return:** `TIME_OK` on success; `-1` on failure.

**Notes:**
- Slewing (`ADJ_OFFSET`) causes the internal PI servo to adjust frequency over time until the offset is absorbed.
- `ADJ_SETOFFSET` directly shifts the clock epoch.

---

### 3.5 Polling

```c
void swclock_poll(SwClock* clk);
```

Explicitly runs a discipline update iteration. Normally unnecessary when using the background thread but useful in test harnesses for deterministic updates.

---

## 4. Utility Functions

Defined in `sw_clock_utilities.h`.

### 4.1 Time Conversion

```c
int64_t ts_to_ns(const struct timespec* t);
struct timespec ns_to_ts(int64_t ns);
int64_t diff_ns(const struct timespec* a, const struct timespec* b);
```

Converts `timespec` values to and from nanoseconds and computes time differences (`b - a`).

---

### 4.2 Frequency Conversion

```c
long   ppm_to_ntp_freq(double ppm);
double ntp_freq_to_ppm(long ntp_freq);
double scaledppm_to_factor(long scaled_ppm);
```

Helpers for converting between human-readable ppm, scaled NTP representation, and multiplicative clock factors.

---

### 4.3 Sleeping

```c
void sleep_ns(long long ns);
```

Portable sleep function that handles signal interruptions (EINTR). Used to create consistent polling intervals in tests.

---

### 4.4 Diagnostics

```c
void print_timespec_as_datetime(const struct timespec* ts);
void print_timespec_as_localtime(const struct timespec* ts);
void print_timespec_as_TAI(const struct timespec* ts);
```

Utility print functions to display `timespec` values as UTC, local time, or TAI. Useful for debugging and logging during test runs.

---

## 5. Example Usage

### 5.1 Initialization and Query

```c
SwClock* clk = swclock_create();
struct timespec now;
swclock_gettime(clk, CLOCK_REALTIME, &now);
```

### 5.2 Frequency Adjustment (+100 ppm)

```c
struct timex tx = {0};
tx.modes = ADJ_FREQUENCY;
tx.freq  = ppm_to_ntp_freq(100.0);
swclock_adjtime(clk, &tx);
```

### 5.3 Slew Correction (+200 ms)

```c
struct timex tx = {0};
tx.modes  = ADJ_OFFSET | ADJ_MICRO;
tx.offset = 200000; // µs
swclock_adjtime(clk, &tx);
```

### 5.4 Relative Step (+200 ms)

```c
struct timex tx = {0};
tx.modes        = ADJ_SETOFFSET | ADJ_MICRO;
tx.time.tv_sec  = 0;
tx.time.tv_usec = 200000;
swclock_adjtime(clk, &tx);
```

---

## 6. Error Handling and Compatibility

- Functions return `0` on success or `-1` with `errno` set to `EINVAL` or `EPERM`.
- `swclock_adjtime()` returns `TIME_OK` (0) on success.
- Unsupported modes are ignored.
- All functions are thread-safe for concurrent `gettime()` calls.

**macOS portability:**
- Defines `CLOCK_MONOTONIC_RAW` as `CLOCK_UPTIME_RAW` if unavailable.
- Re-implements Linux constants for compatibility with code using `ntp_adjtime()`.

---

## 7. Design Notes

- The SwClock advances from `CLOCK_MONOTONIC_RAW` and applies frequency corrections multiplicatively:  
  $$ factor = 1 + (freq\_ppm + servo\_ppm) / 10^6 $$
- Slew operations integrate phase error over time; steps modify the epoch immediately.
- Intended to integrate seamlessly with Linux-derived synchronization stacks such as **PTPd** or **chronyd**.

---

## 8. References

- Linux man pages: `adjtimex(2)`, `ntp_adjtime(2)`
- IEEE 1588-2019 Annex J — Clock Servo Specification
- D. Mills, *Network Time Protocol Reference Implementation*
- ITU-T G.810 / G.8260 — Stability and wander metrics for timing systems

