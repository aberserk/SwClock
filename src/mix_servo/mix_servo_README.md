# mix_servo (AKF → PI cascade)

The **mix_servo** combines the **Adaptive Kalman Filter** as an *estimator* with the **PTPd‑style PI** as an *actuator*.  
API mirrors the other servos (`*_create/init/update/destroy` + getters), so it drops into your harness.

## Build
Include these files alongside your existing servos:
- `src/servo/mix_servo.h`
- `src/servo/mix_servo.c`
- plus dependencies already in your tree: `akf_servo.*`, `pi_servo.*`

## Defaults
- `mix_init(s, q, r)` configures AKF with `(q,r)` and PI with PTPd‑like defaults.
- Optional compile‑time drift hint (disabled by default):
  - `-DMIX_USE_DRIFT_HINT -DMIX_DRIFT_HINT_GAIN=0.5` (example)

## Harness addition (pseudo)
```cpp
#include "mix_servo.h"
...
ServoRunner m(ServoRunner::MIX); // add a new kind
...
case MIX:  (void)mix_update((MixServo*)m.mix, z_meas, dt); break;
...
log("MIX", m); // report like others
```

## Getters
- Offset from AKF (`mix_get_offset`), drift from PI (`mix_get_drift[_ppb]`).
- Innovation and gains forwarded from AKF for debugging/plotting.
