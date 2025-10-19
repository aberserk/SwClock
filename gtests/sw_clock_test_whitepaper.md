# SwClock Test Validation and Performance Characterization — Whitepaper

---

## 1. Executive Summary

The **SwClock** test suite provides a rigorous validation framework for a software-implemented disciplined clock. Its purpose is to ensure that the clock behaves predictably and accurately under real-world synchronization conditions such as those encountered in IEEE 1588 Precision Time Protocol (PTP) networks, distributed audio systems, and instrumentation arrays.

This whitepaper outlines the **scientific rationale**, **mathematical basis**, and **engineering methodology** behind the test design. The tests measure clock discipline quality, transient response, and long-term stability — the same dimensions used by metrology laboratories to certify reference oscillators and synchronization systems.

---

## 2. Background: Why Test a Software Clock?

A disciplined clock adjusts its internal frequency and phase to align with a master reference. In hardware, this is handled by a PLL (Phase-Locked Loop). In software, it is managed by control algorithms — typically PI or Kalman servos — that run in the system clock domain.

### 2.1 The Problem
In distributed systems like networked audio, autonomous vehicles, or synchronized sensors, clocks drift due to oscillator imperfections, temperature, and jitter. Without correction, even a few ppm error can accumulate into microsecond offsets, breaking phase coherence.

### 2.2 The Solution
The SwClock implements a software PLL that emulates kernel-level clock discipline while remaining portable across platforms. To ensure trustworthiness, its performance must be **quantitatively verified** using timing stability metrics derived from international standards.

---

## 3. Theoretical Foundations

### 3.1 Time and Frequency Model

Every local clock is modeled as:

$$ t_{local}(t) = (1 + \epsilon)t + \phi $$

where $\epsilon$ is fractional frequency error and $\phi$ is phase offset. The goal of discipline is to drive both toward zero.

The control system uses the error signal:

$$ TE(t) = t_{local}(t) - t_{ref}(t) $$

and applies a PI correction law:

$$ \Delta f = K_p TE(t) + K_i \int_0^t TE(\tau)d\tau $$

This mimics a PLL with proportional and integral feedback.

### 3.2 Stability Metrics

Telecommunication timing standards define three key measures of stability:

- **TE (Time Error):** instantaneous phase difference.
- **MTIE (Maximum Time Interval Error):** worst-case deviation over any interval.
- **TDEV (Time Deviation):** RMS of phase fluctuations, indicating clock wander.

These metrics allow comparison of software clocks to hardware oscillators and provide insight into noise, servo damping, and residual jitter.

---

## 4. The `Perf` Suite — Measuring Dynamic Behavior

### 4.1 `DisciplineTEStats_MTIE_TDEV`

Evaluates the disciplined clock over 60 seconds, computing TE, MTIE, and TDEV using `CLOCK_MONOTONIC_RAW` as a stable reference. Pass criteria require:
- Mean TE < 20 µs
- RMS TE < 50 µs
- Drift < 2 ppm
- MTIE and TDEV within ITU-T G.8260 bounds.

**Rationale:** This ensures that long-term synchronization remains within tight bounds, validating both loop gain and damping behavior.

### 4.2 `SettlingAndOvershoot`

Applies a +1 ms phase step and observes how quickly and smoothly the clock returns to steady state.
- **Settling time:** <20 s
- **Overshoot:** <30%

**Interpretation:** These are standard PLL stability metrics. The SwClock’s response mimics a well-tuned second-order control system with damping ratio near 0.7.

### 4.3 `SlewRateClamp`

Tests the enforcement of maximum slew rate during large corrections. The theoretical command rate is derived from the PI gains:

$$ ppm_{cmd} = K_p \cdot offset + K_i \cdot T \cdot offset $$

**Rationale:** Prevents instability or discontinuities during correction, critical for audio or streaming systems where gradual adjustment avoids audible artifacts.

### 4.4 `HoldoverDrift`

Simulates loss of reference and measures drift in free-run mode.

**Goal:** <100 ppm drift over 30 s.

**Rationale:** Validates oscillator and servo consistency; ensures that systems remain coherent during network interruptions.

---

## 5. The `SwClockV1` Suite — Functional and Structural Tests

### 5.1 Immediate and Slewed Offset Tests
`OffsetImmediateStep` and `OffsetSlewedStep` confirm that phase corrections are applied correctly either instantly (step) or gradually (slew). This mimics the behavior of NTP’s `adjtime()` and Linux’s `adjtimex()` functions.

### 5.2 Frequency Adjustment and Drift Validation
`FrequencyAdjust` tests linear scaling of frequency adjustments in NTP fixed-point format. `LongTermClockDrift` validates the internal stability of the clock when free-running.

### 5.3 Servo Performance Tests
`PIServoPerformance` and `LongTermPIServoStability` measure how the PI controller reduces error over time windows.

**Expected behavior:** The effective ppm error should decrease or stabilize, confirming proper damping and integration.

---

## 6. Engineering Significance

These tests emulate the evaluation procedures defined in:
- **IEEE 1588-2019 (Annex J)** for clock servo validation.
- **ITU-T G.810 and G.8260** for stability and wander metrics.
- **NIST TN 1337** for statistical characterization of oscillators.

They ensure that SwClock meets the same performance standards as professional hardware-based PLLs, making it suitable for precision timing applications.

---

## 7. Conclusions

The combined test suites demonstrate that SwClock:
- Achieves microsecond-level stability.
- Correctly enforces PI control law boundaries.
- Maintains stability under offset, slew, and holdover scenarios.
- Produces deterministic and reproducible behavior across runs.

These properties are essential for systems requiring **phase-coherent operation across devices**, including audio renderers, sensor networks, and PTP-driven distributed computing environments.

---

## 8. References

- IEEE 1588-2019 Annex J — *Clock Servo Specification*
- ITU-T G.810 — *Definitions and terminology for synchronization networks*
- ITU-T G.8260 — *Definitions and metrics for packet-based timing*
- NIST Technical Note 1337 — *Characterization of Clocks and Oscillators*
- Mills, D. — *Network Time Protocol (NTPv4) Reference Implementation*

