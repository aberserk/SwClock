# SwClock - VSCode Build Environment

This project is configured for development with VSCode using CMake and Ninja.

## Prerequisites

- macOS with Xcode Command Line Tools
- CMake (3.16+)
- Ninja build system
- GoogleTest (for unit tests)

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

## Build Targets

The project is configured with three build configurations:

### 1. Debug Build (`build/build-debug-macos`)
- Debug symbols enabled
- No optimization (-O0)
- All warnings enabled
- Suitable for development and debugging

### 2. Release Build (`build/build-release-macos`)
- Full optimization (-O3)
- No debug symbols
- NDEBUG defined
- Suitable for production

### 3. GTests Build (`build/build-gtests-macos`)
- Debug configuration with GoogleTest support
- Unit tests located in `gtests/` directory

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

#### Build Presets
- **Debug** → `build/debug-macos/` - Debug symbols, no optimization
- **Release** → `build/release-macos/` - Optimized build
- **GTests** → `build/gtests-macos/` - Debug build with GoogleTest

### Debugging (F5)

- **Debug SwClock Test** - Debug the main test executable
- **Debug SwClock GTests** - Debug GoogleTest suite
- **Debug SwClock GTests (with filter)** - Debug specific tests
- **CMake Debug Current Target** - Debug currently selected CMake target

### Quick Development Workflow

1. **Open Project**: VSCode will automatically detect CMake configuration
2. **Select Preset**: Click configure preset in status bar (debug/release/gtests)
3. **Select Target**: Click build target in status bar (swclock_test/swclock_gtests)
4. **Build**: Click build button in status bar or press `F7`
5. **Debug**: Press `F5` or click debug button in status bar
6. **Run**: Press `Ctrl+F5` for run without debugging

### Build Tasks (Legacy - Ctrl+Shift+P → "Tasks: Run Task")

- **Build Debug** - Configure and build debug version
- **Build Release** - Configure and build release version  
- **Build GTests** - Configure and build unit tests
- **Run Tests** - Build and run GoogleTest suite
- **Clean Debug/Release/GTests** - Clean specific build
- **Clean All** - Remove entire build directory

## Manual Build Commands

### Debug Build
```bash
cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug -S . -B build/build-debug-macos
ninja -C build/build-debug-macos
./build/build-debug-macos/swclock_test
```

### Release Build  
```bash
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -S . -B build/build-release-macos
ninja -C build/build-release-macos
./build/build-release-macos/swclock_test
```

### GTests Build
```bash
cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug -S . -B build/build-gtests-macos
ninja -C build/build-gtests-macos swclock_gtests
./build/build-gtests-macos/swclock_gtests
```

## Project Structure

```
SwClock/
├── src/                    # Source files
│   ├── sw_clock/          # SwClock library source
│   │   ├── swclock.c      # Main clock implementation
│   │   ├── swclock.h      # Header file
│   │   ├── sw_adjtimex.c  # Time adjustment utilities
│   │   ├── sw_adjtimex.h
│   │   ├── swclock_compat.c  # Compatibility layer
│   │   └── swclock_compat.h
│   └── kf_servo/          # Kalman Filter servo library
│       ├── kf_servo.c     # Kalman Filter implementation
│       └── kf_servo.h     # Servo header file
├── gtests/                 # GoogleTest unit tests
│   ├── test_swclock.cpp   # SwClock basic tests
│   ├── kf_servo_swclock_tests.cpp  # Servo integration tests
│   └── kf_servo_wifi_tests.cpp     # Wi-Fi simulation tests
├── tools/                  # Python analysis tools
│   └── plot_kf_wifi_perf.py        # Performance analysis & plotting
├── build/                  # Build directories (auto-generated)
│   ├── build-debug-macos/
│   ├── build-release-macos/
│   └── build-gtests-macos/
├── .vscode/               # VSCode configuration
│   ├── tasks.json         # Build tasks
│   ├── launch.json        # Debug configurations
│   ├── c_cpp_properties.json  # IntelliSense settings
│   └── settings.json      # Workspace settings
├── CMakeLists.txt         # CMake configuration
├── setup.sh              # Environment setup script
└── README.md             # This file
```

## Library Targets

- **swclock** - Static library containing the SwClock implementation (from src/sw_clock/)
- **kf_servo** - Static library containing the Kalman Filter servo (from src/kf_servo/)  
- **swclock_gtests** - Comprehensive GoogleTest suite covering all components

## Testing

The project now includes comprehensive testing:

### GoogleTest Suite (16 tests total)
- **SwClockTest** (9 tests) - Basic SwClock functionality
- **KalmanServo** (6 tests) - Servo integration with SwClock  
- **KalmanServo_Wifi** (1 test) - Wi-Fi simulation with performance analysis

### Python Analysis Tools
- **plot_kf_wifi_perf.py** - Performance analysis and plotting
  - Generates CSV data from tests
  - Creates performance plots and statistics
  - Allan deviation analysis

All testing is now consolidated into the GoogleTest suite:

```bash
# Build and run all tests
ninja -C build/debug-macos swclock_gtests
./build/debug-macos/swclock_gtests

# Output:
# [==========] Running 9 tests from 1 test suite.
# [  PASSED  ] 9 tests.
```

The test suite includes:
- Basic functionality tests
- Time progression and elapsed time validation
- State inspection and validation
- Frequency adjustment testing
- Clock adjustment with slew parameters
- Comprehensive workflow testing (equivalent to former test_main.c)

## Adding Tests

To add new unit tests:

1. Create new `.cpp` files in the `gtests/` directory
2. Follow the GoogleTest framework conventions
3. Include the SwClock headers: `#include "swclock.h"`
4. Rebuild using the "Build GTests" task

## ✅ GoogleTest Integration

GoogleTest is now fully integrated and working without warnings:

```bash
ninja -C build/debug-macos
# ninja: Entering directory `build/debug-macos'
# [6/6] Linking CXX executable swclock_gtests
# (no warnings!)

./build/debug-macos/swclock_gtests
# [==========] Running 6 tests from 1 test suite.
# [  PASSED  ] 6 tests.
```

### Troubleshooting

### ~~GTest Not Found~~ ✅ RESOLVED
~~If you see "GTest not found" warnings~~:
- ✅ GoogleTest is installed and configured
- ✅ All build targets work correctly
- ✅ Unit tests pass successfully

### Build Errors
- Ensure you have Xcode Command Line Tools: `xcode-select --install`
- Verify CMake and Ninja are installed: `brew install cmake ninja`
- Clean and rebuild: Use "Clean All" task, then rebuild

### IntelliSense Issues
- Run "Build Debug" task to generate compile_commands.json
- Restart VSCode if needed
- Check that the C/C++ extension is installed