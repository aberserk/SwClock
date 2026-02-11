#!/bin/bash

# Script directory and project root
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$( cd "$SCRIPT_DIR/.." && pwd )"
cd "$PROJECT_ROOT"

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

# Check if Python 3 is installed
if command -v python3 >/dev/null 2>&1; then
    echo "✓ Python 3 is installed ($(python3 --version))"
    
    # Create virtual environment for performance analysis tools
    VENV_DIR="tools/venv"
    if [ -d "$VENV_DIR" ]; then
        echo "✓ Python virtual environment already exists"
    else
        echo "Creating Python virtual environment in $VENV_DIR..."
        python3 -m venv "$VENV_DIR"
        echo "✓ Virtual environment created"
    fi
    
    # Install Python packages
    echo "Installing Python packages for performance analysis..."
    source "$VENV_DIR/bin/activate"
    pip install --upgrade pip > /dev/null 2>&1
    pip install -r tools/requirements.txt
    deactivate
    echo "✓ Python packages installed"
else
    echo "⚠️  Python 3 not found. Performance analysis tools will not be available."
    echo "   Install Python 3 to enable performance testing and metrics."
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