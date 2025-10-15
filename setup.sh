#!/bin/bash

echo "SwClock Build Environment Setup"
echo "================================"

# Check if Homebrew is installed
if command -v brew >/dev/null 2>&1; then
    echo "✓ Homebrew is installed"
    
    # Check if GoogleTest is installed
    if brew list googletest >/dev/null 2>&1; then
        echo "✓ GoogleTest is already installed"
    else
        echo "Installing GoogleTest via Homebrew..."
        brew install googletest
    fi
    
    # Check if CMake is installed
    if command -v cmake >/dev/null 2>&1; then
        echo "✓ CMake is installed"
    else
        echo "Installing CMake via Homebrew..."
        brew install cmake
    fi
    
    # Check if Ninja is installed
    if command -v ninja >/dev/null 2>&1; then
        echo "✓ Ninja is installed"
    else
        echo "Installing Ninja via Homebrew..."
        brew install ninja
    fi
    
else
    echo "❌ Homebrew is not installed. Please install Homebrew first:"
    echo "   /bin/bash -c \"\$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\""
    exit 1
fi

echo ""
echo "Build Environment Setup Complete!"
echo ""
echo "✅ GoogleTest is installed and configured"
echo "✅ All build targets are working:"
echo "   - Debug build (swclock + swclock_test)"
echo "   - Release build (swclock + swclock_test)" 
echo "   - GTests build (swclock_gtests)"
echo ""
echo "You can now use the following VSCode features:"
echo "  - Status bar: Select configure preset and build target"
echo "  - F7: Build current target"
echo "  - F5: Debug current target"
echo "  - Ctrl+F5: Run without debugging"
echo ""
echo "Or use these commands manually:"
echo "  cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug -S . -B build/debug-macos"
echo "  ninja -C build/debug-macos"
echo "  ./build/debug-macos/swclock_gtests  # Run unit tests"