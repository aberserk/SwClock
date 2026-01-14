# SwClock Development Tools

This directory contains debugging and diagnostic tools used during development. These are not built by default and are intended for developer use only.

## Files

### Debug/Diagnostic Tools

- **test_te_calc.c** - Test Time Error calculation in isolation
  - Verifies TE = System_REALTIME - SwClock_REALTIME
  - Shows epoch times and calculated TE in microseconds
  - Used to debug monitoring TE calculation issue

- **debug_monitor.c** - Simplified monitoring test
  - Tests real-time monitoring without the full demo UI
  - Useful for isolating monitoring infrastructure bugs
  - Minimal output for quick validation

- **test_swclock_te.c** - Test swclock_gettime() behavior
  - Verifies swclock_gettime() returns correct values
  - Compares System vs SwClock timestamps
  - Used to validate time domain consistency

## Building

These tools are not included in the main CMake build. Compile manually:

```bash
# Example: test_te_calc
cc -o build/test_te_calc src-tools/test_te_calc.c \
   -Isrc/sw_clock -Lbuild/ninja-gtests-macos -lswclock -lm

# Example: debug_monitor
cc -o build/debug_monitor src-tools/debug_monitor.c \
   -Isrc/sw_clock -Lbuild/ninja-gtests-macos -lswclock -lm
```

## Usage

```bash
# Test TE calculation
./build/test_te_calc

# Test monitoring (requires SWCLOCK_MONITOR=1)
SWCLOCK_MONITOR=1 ./build/debug_monitor
```

## Notes

- These tools are for development/debugging only
- They link against the SwClock library
- Not suitable for production use
- May have minimal error handling
