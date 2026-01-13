#!/bin/bash
#
# Test script for SwClock - builds if necessary and runs all gtests
#
# Usage:
#   ./test.sh [options]
#
# Options:
#   --rebuild     Force rebuild before running tests
#   --verbose     Run tests with verbose output
#   --filter=     Run only tests matching the filter pattern (e.g., --filter=SwClockInit*)
#   --help        Show this help message
#

set -e  # Exit on error

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Script directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$SCRIPT_DIR"

# Default options
REBUILD=false
VERBOSE=false
TEST_FILTER=""

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --rebuild)
            REBUILD=true
            shift
            ;;
        --verbose)
            VERBOSE=true
            shift
            ;;
        --filter=*)
            TEST_FILTER="${1#*=}"
            shift
            ;;
        --help)
            echo "Usage: $0 [options]"
            echo ""
            echo "Options:"
            echo "  --rebuild     Force rebuild before running tests"
            echo "  --verbose     Run tests with verbose output"
            echo "  --filter=     Run only tests matching the filter pattern"
            echo "  --help        Show this help message"
            echo ""
            echo "Examples:"
            echo "  ./test.sh                                  # Run all tests"
            echo "  ./test.sh --rebuild                        # Rebuild and run all tests"
            echo "  ./test.sh --verbose                        # Run tests with verbose output"
            echo "  ./test.sh --filter='SwClockInit*'          # Run only SwClockInit tests"
            echo "  ./test.sh --rebuild --filter='*Accuracy*'  # Rebuild and run Accuracy tests"
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

# Print header
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}SwClock Test Runner${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# Check if test binary exists
TEST_BINARY="build/ninja-gtests-macos/swclock_gtests"
BUILD_NEEDED=false

if [ "$REBUILD" = true ]; then
    echo -e "${YELLOW}Forced rebuild requested${NC}"
    BUILD_NEEDED=true
elif [ ! -f "$TEST_BINARY" ]; then
    echo -e "${YELLOW}Test binary not found, build required${NC}"
    BUILD_NEEDED=true
else
    # Check if source files are newer than the binary
    NEWEST_SOURCE=$(find src src-gtests -type f \( -name "*.c" -o -name "*.cpp" -o -name "*.h" \) -newer "$TEST_BINARY" 2>/dev/null | head -n 1)
    if [ -n "$NEWEST_SOURCE" ]; then
        echo -e "${YELLOW}Source files have changed, rebuild required${NC}"
        BUILD_NEEDED=true
    else
        echo -e "${GREEN}Test binary is up to date${NC}"
    fi
fi
echo ""

# Build if necessary
if [ "$BUILD_NEEDED" = true ]; then
    echo -e "${YELLOW}Building project...${NC}"
    ./build.sh --clean
    if [ $? -ne 0 ]; then
        echo -e "${RED}✗ Build failed${NC}"
        exit 1
    fi
    echo ""
fi

# Run tests
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Running Tests${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# Prepare test command
if [ -n "$TEST_FILTER" ]; then
    echo -e "${YELLOW}Running tests matching filter: ${TEST_FILTER}${NC}"
    echo ""
    TEST_CMD="$TEST_BINARY --gtest_filter=$TEST_FILTER"
else
    echo -e "${YELLOW}Running all tests${NC}"
    echo ""
    TEST_CMD="ctest --preset gtests"
fi

# Add verbose flag if requested
if [ "$VERBOSE" = true ]; then
    if [ -n "$TEST_FILTER" ]; then
        TEST_CMD="$TEST_CMD --gtest_verbose"
    else
        TEST_CMD="$TEST_CMD --verbose"
    fi
fi

# Execute tests
eval $TEST_CMD
TEST_RESULT=$?

echo ""
# Print summary
echo -e "${BLUE}========================================${NC}"
if [ $TEST_RESULT -eq 0 ]; then
    echo -e "${GREEN}✓ All tests passed!${NC}"
    echo -e "${BLUE}========================================${NC}"
    exit 0
else
    echo -e "${RED}✗ Some tests failed${NC}"
    echo -e "${BLUE}========================================${NC}"
    echo ""
    echo -e "${YELLOW}Tip: Run with --verbose for more details${NC}"
    if [ -z "$TEST_FILTER" ]; then
        echo -e "${YELLOW}Tip: Use --filter= to run specific tests${NC}"
    fi
    exit 1
fi
