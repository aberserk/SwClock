#!/bin/bash
#
# Build script for SwClock library and test cases
#
# Usage:
#   ./build.sh [options]
#
# Options:
#   --clean       Clean build directory before building
#   --tests       Build and run tests after building
#   --release     Build in Release mode (default: Debug)
#   --help        Show this help message
#

set -e  # Exit on error

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Script directory and project root
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$( cd "$SCRIPT_DIR/.." && pwd )"
cd "$PROJECT_ROOT"

# Default options
CLEAN=false
RUN_TESTS=false
BUILD_TYPE="Debug"

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --clean)
            CLEAN=true
            shift
            ;;
        --tests)
            RUN_TESTS=true
            shift
            ;;
        --release)
            BUILD_TYPE="Release"
            shift
            ;;
        --help)
            echo "Usage: $0 [options]"
            echo ""
            echo "Options:"
            echo "  --clean       Clean build directory before building"
            echo "  --tests       Build and run tests after building"
            echo "  --release     Build in Release mode (default: Debug)"
            echo "  --help        Show this help message"
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

# Print configuration
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}SwClock Build Script${NC}"
echo -e "${GREEN}========================================${NC}"
echo -e "Build Type: ${YELLOW}${BUILD_TYPE}${NC}"
echo -e "Clean Build: ${YELLOW}${CLEAN}${NC}"
echo -e "Run Tests: ${YELLOW}${RUN_TESTS}${NC}"
echo ""

# Clean if requested
if [ "$CLEAN" = true ]; then
    echo -e "${YELLOW}Cleaning build directory...${NC}"
    if [ -d "build/ninja-gtests-macos" ]; then
        rm -rf build/ninja-gtests-macos
        echo -e "${GREEN}✓ Build directory cleaned${NC}"
    else
        echo -e "${YELLOW}⚠ Build directory does not exist${NC}"
    fi
    echo ""
fi

# Configure the project
echo -e "${YELLOW}Configuring project...${NC}"
cmake --preset config-gtests-macos
if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓ Configuration successful${NC}"
else
    echo -e "${RED}✗ Configuration failed${NC}"
    exit 1
fi
echo ""

# Build the project
echo -e "${YELLOW}Building project...${NC}"
cmake --build --preset build-gtests
if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓ Build successful${NC}"
else
    echo -e "${RED}✗ Build failed${NC}"
    exit 1
fi
echo ""

# Run tests if requested
if [ "$RUN_TESTS" = true ]; then
    echo -e "${YELLOW}Running tests...${NC}"
    ctest --preset gtests
    if [ $? -eq 0 ]; then
        echo -e "${GREEN}✓ All tests passed${NC}"
    else
        echo -e "${RED}✗ Some tests failed${NC}"
        exit 1
    fi
    echo ""
fi

# Summary
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}Build completed successfully!${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo "Binaries are located in: build/ninja-gtests-macos/"
echo ""
echo "To run tests manually:"
echo "  ctest --preset gtests"
echo ""
echo "Or run the test executable directly:"
echo "  ./build/ninja-gtests-macos/swclock_gtests"
echo ""
