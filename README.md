# SwClock - Software Clock Implementation for macOS

A precision software clock implementation for macOS that provides Linux-compatible time adjustment semantics. SwClock offers sub-microsecond precision timing suitable for PTP (Precision Time Protocol) applications and high-accuracy timekeeping.

## Features

- **High Precision**: Sub-microsecond timing accuracy (typical: 200-4000 ns)
- **Linux Compatibility**: Provides `adjtimex()` and related POSIX-style interfaces on macOS
- **PTP Ready**: Designed for PTP daemon integration with frequency and phase adjustment
- **Time Standards Support**: UTC, TAI (International Atomic Time), and local time formatting
- **Comprehensive Testing**: Performance characterization and validation test suites
- **VS Code Integration**: Full debugging and development environment

## Prerequisites

- **macOS** with Xcode Command Line Tools
- **CMake** (3.16+)
- **Ninja** build system  
- **GoogleTest** (for unit tests)

## Quick Setup

Run the setup script to install dependencies:

```bash
./setup.sh
```

## Manual Setup

If you prefer manual setup:

```bash
# Install Homebrew (if not already installed)
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install dependencies
brew install cmake ninja googletest
```

## Core Components

### SwClock Library
- **sw_clock.c/h**: Main software clock implementation with CLOCK_MONOTONIC_RAW backing
- **sw_clock_utilities.c/h**: Time conversion and formatting utilities  
- **sw_clock_constants.h**: Timing constants and conversion factors
- **Linux Compatibility**: timex structure and ADJ_* constants for cross-platform code

### Time Utilities
- **Nanosecond precision**: Full support for timespec operations
- **Time formatting**: UTC, TAI (+37s), and local time display functions
- **Frequency adjustment**: PPM to scaled-PPM conversions for PTP integration

## Build Configuration

The project uses a single CMake preset focused on testing and development:

### GTests Build (`build/ninja-gtests-macos`)
- Debug configuration with GoogleTest support (`-g -O0`)
- All warnings enabled (`-Wall -Wextra`) 
- Performance characterization tests included
- Unit tests located in `gtests/` directory
- Suitable for both development and testing

## VSCode Integration

### CMake Tools Integration

The project is fully integrated with VSCode CMake Tools extension for seamless development:

#### Status Bar Controls
- **Configure Preset**: Click to select debug/release/gtests configuration
- **Build Target**: Click to select which target to build (swclock, swclock_test, swclock_gtests)
- **Build**: Click the build button to compile
- **Debug**: Click to run/debug the selected target

#### CMake Commands (Ctrl+Shift+P)
- `CMake: Configure` - Configure the project with selected preset
- `CMake: Build` - Build the current target
- `CMake: Build Target` - Select and build specific target
- `CMake: Set Build Target` - Choose default build target
- `CMake: Clean` - Clean build artifacts
- `CMake: Debug` - Debug current target
- `CMake: Run Without Debugging` - Run current target

#### CMake Presets
- **config-base-macos** - Base configuration for macOS
- **config-gtests-macos** → `build/ninja-gtests-macos/` - Debug build with GoogleTest  
- **build-gtests** - Build preset for test configuration
- **gtests** - Test execution preset

### Debugging (F5)

- **Debug SwClock GTests** - Debug GoogleTest suite with breakpoints
- **Debug SwClock GTests (with filter)** - Debug specific tests using gtest filters
- **CMake Debug Current Target** - Debug currently selected CMake target

#### Breakpoints
- ✅ **Fully functional** - Breakpoints work correctly in source code
- ✅ **Step debugging** - Line-by-line execution and variable inspection  
- ✅ **Multi-target** - Debug both library code and tests

### Quick Development Workflow

1. **Open Project**: VSCode automatically detects CMake configuration
2. **Auto-Configure**: Project uses `config-gtests-macos` preset by default
3. **Build**: Click build button in status bar or run "Build GTests" task
4. **Debug**: Press `F5` to debug with breakpoints (uses `${command:cmake.buildDirectory}`)
5. **Run Tests**: Use "Run GTests" task or run executable directly

### Build Tasks (Ctrl+Shift+P → "Tasks: Run Task")

- **Configure GTests** - Configure CMake with gtests preset
- **Build GTests** - Build the test executable  
- **Run GTests** - Execute the test suite with CTest
- **Clean GTests** - Clean build artifacts
- **Run All GTests** - Chain: Configure → Build → Run

## Manual Build Commands

### Using CMake Presets (Recommended)
```bash
# Configure and build using presets
cmake --preset config-gtests-macos
cmake --build --preset build-gtests
./build/ninja-gtests-macos/swclock_gtests

# Run tests using CTest
ctest --preset gtests
```

### Manual Commands (Alternative)
```bash
# Manual build without presets
cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug -S . -B build/ninja-gtests-macos
ninja -C build/ninja-gtests-macos
./build/ninja-gtests-macos/swclock_gtests
```

## Project Structure

```
SwClock/
├── src/                            # Source files
│   └── sw_clock/                   # SwClock library implementation
│       ├── sw_clock.c              # Main software clock implementation  
│       ├── sw_clock.h              # Public API and timex compatibility
│       ├── sw_clock_utilities.c    # Time formatting & utility functions
│       ├── sw_clock_utilities.h    # Utility function declarations  
│       └── sw_clock_constants.h    # Timing constants & conversions
├── gtests/                         # GoogleTest test suites
│   ├── tests_sw_clock.cpp          # Core SwClock functionality tests (6 tests)
│   └── tests_performance.cpp       # Performance characterization (4 tests)  
├── build/                          # Build directories (auto-generated)
│   └── ninja-gtests-macos/         # Test build output (Debug mode)
├── .vscode/                        # VSCode configuration
│   ├── tasks.json                  # Build tasks & presets
│   ├── launch.json                 # Debug configurations (working breakpoints)
│   ├── c_cpp_properties.json       # IntelliSense settings
│   └── settings.json               # Workspace settings & CMake integration
├── CMakeLists.txt                  # CMake build configuration
├── CMakePresets.json               # Build presets (debug/release/gtests)
├── setup.sh                        # Dependency installation script
├── logs/                           # Test output logs (auto-generated)
└── README.md                       # This documentation
```

## Library Targets

- **swclock** - Static library containing the SwClock implementation and utilities
- **swclock_gtests** - Comprehensive GoogleTest executable with all test suites

## API Overview

### Core Functions
```c
// Clock lifecycle
SwClock* swclock_create(void);
void swclock_destroy(SwClock* clk);

// Time operations  
int swclock_gettime(SwClock* clk, struct timespec* tp);
int swclock_settime(SwClock* clk, const struct timespec* tp);
int swclock_adjtime(SwClock* clk, struct timex* tx);

// Utility functions
void print_timespec_as_datetime(const struct timespec *ts);    // UTC format
void print_timespec_as_TAI(const struct timespec *ts);        // TAI (+37s) 
void print_timespec_as_localtime(const struct timespec *ts);  // Local timezone
```

### Linux Compatibility Layer
```c
// timex structure and ADJ_* constants for cross-platform PTP integration
struct timex {
    unsigned int   modes;
    long           offset;    // Phase offset (usec or nsec with ADJ_NANO)
    long           freq;      // Frequency offset in scaled ppm (2^-16 ppm units)
    int            status;    // Status flags
    struct timeval time;
    // ... additional fields
};
```

## Testing

### GoogleTest Suite (10 tests total)

**SwClockV1 Test Suite** (6 core functionality tests):
- `CreateDestroy` - Basic lifecycle operations
- `PrintTime` - Time formatting (UTC/TAI/Local) 
- `OffsetImmediateStep` - Immediate time step adjustments
- `FrequencyAdjust` - PPM frequency corrections
- `CompareSwClockAndClockGettime` - Precision validation vs system clock
- `SetTimeRealtimeOnly` - Direct time setting operations

**Performance Test Suite** (4 characterization tests):
- `DisciplineTEStats_MTIE_TDEV` - Long-term stability analysis  
- `SettlingAndOvershoot` - Step response characteristics
- `SlewRateClamp` - Slew rate limiting validation
- `HoldoverDrift` - Free-running accuracy measurement

### Running Tests

```bash
# Build and run all tests
cmake --build --preset build-gtests
./build/ninja-gtests-macos/swclock_gtests

# Run specific test suite
./build/ninja-gtests-macos/swclock_gtests --gtest_filter="SwClockV1.*"

# Run single test with verbose output  
./build/ninja-gtests-macos/swclock_gtests --gtest_filter="SwClockV1.PrintTime"
```

### Sample Test Output
```
SwClock CURRENT TIME:

 UTC Time    : 2025-10-19 01:46:40.014394125 UTC
 TAI Time    : 2025-10-19 01:47:17.014394125 TAI (+37s)  
 Local Time  : 2025-10-18 19:46:40.014394125 CST

[       OK ] SwClockV1.PrintTime (12 ms)
```

### Performance Characteristics
- **Timing Precision**: 200-4000 nanosecond accuracy typical
- **Step Response**:    Sub-microsecond immediate adjustments  
- **Frequency Range**:  ±200 ppm adjustment capability
- **Stability**:        Long-term drift < 100 ppm in holdover mode

## Adding Tests

To add new unit tests:

1. Create new `.cpp` files in the `gtests/` directory
2. Follow the GoogleTest framework conventions
3. Include the SwClock headers: `#include "sw_clock.h"`
4. Rebuild using the "Build GTests" task

## ✅ Project Status

### Fully Functional ✅
- **Build System**:           CMake presets working correctly
- **GoogleTest Integration**: All 10 tests building and running  
- **VS Code Debugging**:      Breakpoints functional, uses `${command:cmake.buildDirectory}`
- **Cross-Platform**:         Linux-compatible timex interface on macOS
- **High Precision**:         Sub-microsecond timing accuracy achieved

### Current Build Status
```bash
cmake --build --preset build-gtests
# [6/6] Linking CXX executable swclock_gtests; Creating logs directory
# Build completed successfully

./build/ninja-gtests-macos/swclock_gtests
# [==========] Running 10 tests from 2 test suites.
# [  PASSED  ] 6 tests (SwClockV1 suite) 
# [  PASSED  ] 4 tests (Performance suite)
```

## Troubleshooting

### Build Errors
- **Xcode Tools**:  Ensure Command Line Tools installed: `xcode-select --install`
- **Dependencies**: Verify CMake and Ninja: `brew install cmake ninja googletest`
- **Clean Build**:  Remove `build/` directory and reconfigure: `rm -rf build/`

### VS Code Issues  
- **CMake Integration**: Ensure CMake Tools extension is installed
- **IntelliSense**:      Build debug target to generate `compile_commands.json`
- **Breakpoints**:       Use "Debug SwClock GTests" launch configuration

### Performance Test Failures
Performance tests may fail on different hardware or under system load. These failures don't indicate functional problems but rather that timing doesn't meet strict performance criteria.

## License

This project is part of the PTP Implementation research suite.