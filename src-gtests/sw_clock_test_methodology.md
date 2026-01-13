# SwClock Test Methodology (Extended Developer Documentation)

---

## 1. Introduction

This document details the **mathematical foundations**, **engineering rationale**, and **validation goals** behind the SwClock and Perf test suites used in evaluating the SwClock discipline loop implementation. These tests aim to validate the accuracy, stability, and responsiveness of the SwClock in contexts where precise synchronization is critical—such as IEEE 1588 PTP networks, audio streaming, or distributed data acquisition.

The SwClock acts as a software-implemented disciplined clock modeled after the kernel PLL. It provides `adjtime()` and `adjtimex()` equivalents, maintaining a monotonic relationship between system and reference time while allowing phase and frequency adjustments.

---

## 2. Mathematical Background

### 2.1 Clock Model

Every oscillator-driven clock can be modeled as:

$$
t_{local}(t) = (1 + \epsilon)t + \phi
$$

Where:
- $\epsilon$ is the **frequency error** (fractional deviation, e.g., ppm).
- $\phi$ is the **phase offset** (seconds).

The objective of the clock servo is to minimize both $\epsilon$ and $\phi$ relative to a master reference, while maintaining stability and minimizing overshoot.

### 2.2 Phase and Frequency Error

The **phase error** or **time error (TE)** between local and reference clocks is:

$$
TE(t) = t_{local}(t) - t_{ref}(t)
$$

The **frequency error** is the derivative:

$$
\dot{TE}(t) = \frac{d}{dt} TE(t) = \epsilon(t)
$$

The servo loop must correct these by adjusting internal frequency offsets via the PI control law:

$$
\Delta f = K_p \cdot TE(t) + K_i \int_0^t TE(\tau) d\tau
$$

### 2.3 PI Control and Stability

The proportional term provides immediate correction proportional to phase error, while the integral term accumulates long-term bias. Proper tuning of $K_p$ and $K_i$ ensures **fast convergence** and **low steady-state error** without instability.

The **closed-loop transfer function** approximates:

$$
H(s) = \frac{K_p s + K_i}{s^2 + K_p s + K_i}
$$

and yields a natural frequency:

$$
\omega_n = \sqrt{K_i}, \quad \zeta = \frac{K_p}{2\sqrt{K_i}}
$$

Damping ratio $\zeta$ determines overshoot and settling behavior. The tests measure how SwClock behaves under these PI dynamics.

### 2.4 Metrics: TE, MTIE, and TDEV

| Metric | Meaning | Formula / Definition |
|:--------|:--------|:----------------|
| **TE** | Time Error — instantaneous phase offset | $TE(t) = t_{local} - t_{ref}$ |
| **MTIE** | Maximum Time Interval Error — worst-case deviation in any window | $MTIE(\tau) = \max |TE(t+\tau) - TE(t)|$ |
| **TDEV** | Time Deviation — RMS of filtered TE over window $\tau$ | $TDEV(\tau) = \sqrt{\frac{1}{2} E[(TE(t+\tau)-2TE(t)+TE(t-\tau))^2]}$ |

These are telecom-grade stability metrics (ITU-T G.810/G.8260), directly applicable to precision time and frequency systems.

---

## 3. Test Suite: `Perf`

### 3.1 `DisciplineTEStats_MTIE_TDEV`
**Purpose:** Quantifies long-term accuracy and stability under discipline.

**Mathematics:**
- Measures TE over 60s using `CLOCK_MONOTONIC_RAW` as reference.
- Computes mean, RMS, P95, P99, and trend slope (ppm drift).
- Derives MTIE(1s, 10s, 30s) and TDEV(0.1s, 1s, 10s) from detrended TE.

**Rationale:**
Ensures that the disciplined clock maintains sub-µs accuracy with bounded drift (<2 ppm). MTIE and TDEV thresholds validate that the servo loop exhibits low jitter and long-term wander compliance.

### 3.2 `SettlingAndOvershoot`
**Purpose:** Measures how quickly the clock stabilizes after a large offset.

**Mathematics:**
After an instantaneous +1 ms phase step:
- Settling time: duration until $|TE| < 10 \mu s$
- Overshoot: $\max(|TE| - step)$ / step × 100%

**Rationale:**
Tests loop damping ratio. A good PI-tuned servo should settle in <20s and overshoot <30%. This reflects real-world PLL transient response.

### 3.3 `SlewRateClamp`
**Purpose:** Confirms that the maximum frequency correction (slew) adheres to configured limits.

**Mathematics:**
Predicted maximum:

$$ ppm_{cmd} = K_p \cdot offset + K_i \cdot t_{win} \cdot offset $$

Measured via:

$$ ppm_{eff} = 10^6 \times \frac{extra}{t_{win}} $$

**Rationale:**
Validates that SwClock clamps adjustments properly and computes effective ppm consistent with servo gains.

### 3.4 `HoldoverDrift`
**Purpose:** Measures free-running drift stability when the clock is left without correction.

**Mathematics:**
Over interval $t$:

$$ drift = 10^6 \times \frac{extra}{t} \quad [ppm] $$

**Rationale:**
Simulates reference loss. Ensures oscillator + PI integrator produce predictable, bounded drift (<100 ppm typical for stable systems).

---

## 4. Test Suite: `SwClockV1`

### 4.1 `CreateDestroy`
**Purpose:** Ensures memory safety and lifecycle correctness of `swclock_create()` / `swclock_destroy()`.

### 4.2 `PrintTime`
**Purpose:** Verifies conversion and formatting of TAI, UTC, and local time, confirming correct leap-second offset handling.

### 4.3 `OffsetImmediateStep`
**Purpose:** Tests `ADJ_OFFSET` with immediate phase step.

**Math:** Verifies:

$$ |actual - desired| < tol_{ns} $$

### 4.4 `FrequencyAdjust`
**Purpose:** Tests `ADJ_FREQUENCY` correctness.

**Math:**

$$ f_{adj} = ppm_{input} \times 2^{16} / 10^6 $$

**Rationale:**
Ensures `ntp_adjtime()`-compatible scaling for portable integration.

### 4.5 `CompareSwClockAndClockGettime`
**Purpose:** Confirms alignment between SwClock and kernel `CLOCK_REALTIME`.

### 4.6 `SetTimeRealtimeOnly`
**Purpose:** Validates that `settimeofday()` only affects real-time clock state, not monotonic reference.

### 4.7 `OffsetSlewedStep`
**Purpose:** Tests gradual slewing instead of immediate phase jump.

**Math:** Slew rate ≈ applied ppm × time window.

### 4.8 `LongTermClockDrift`
**Purpose:** Measures uncorrected drift stability over 10s intervals.

### 4.9 `PIServoPerformance`
**Purpose:** Validates short-term convergence of PI loop.

**Math:**

$$ ppm_{eff}(A,B) = 10^6 \times \frac{extra_{A,B}}{\Delta t} $$

Expected: $|ppm_B| \leq |ppm_A|$.

### 4.10 `LongTermPIServoStability`
**Purpose:** Tests steady-state convergence under longer slews.

**Math:**
- Computes $extra_A$, $extra_B$, $extra_C$ across 3 windows.
- Checks if $|ppm|$ decreases monotonically.

**Rationale:**
Validates long-term loop equilibrium—low residual error, no instability, and consistency across multi-window drift estimates.

---

## 5. References

- IEEE 1588-2019 Annex J — *Clock Servo Specification*
- ITU-T G.810 — *Definitions and terminology for synchronization networks*
- ITU-T G.8260 — *Definitions and metrics for packet-based timing*
- NIST Technical Note 1337 — *Characterization of Clocks and Oscillators*
- D. Mills, *Network Time Protocol v4 Reference Implementation*, ntp.org

