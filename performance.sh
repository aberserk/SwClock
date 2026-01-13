#!/bin/bash
#
# performance.sh - SwClock IEEE Standards Performance Validation
#
# Comprehensive performance testing suite that validates SwClock against:
# - ITU-T G.810/G.8260 timing stability standards
# - IEEE 1588-2019 Annex J servo specifications
# - Linux adjtimex() compatibility
#
# Usage:
#   ./performance.sh [options]
#
# Options:
#   --quick       Quick validation (~5-10 min)
#   --full        Full validation with long-term tests (~60 min)
#   --regression  Compare against baseline performance
#   --baseline=   Specify baseline directory for regression testing
#   --output-dir= Custom output directory (default: performance/)
#   --help        Show this help message
#

set -e  # Exit on error

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Script directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$SCRIPT_DIR"

# Default options
TEST_MODE="quick"
RUN_REGRESSION=false
BASELINE_DIR=""
OUTPUT_BASE_DIR="performance"
TIMESTAMP=$(date +"%Y%m%d-%H%M%S")
OUTPUT_DIR="${OUTPUT_BASE_DIR}/performance_${TIMESTAMP}"

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --quick)
            TEST_MODE="quick"
            shift
            ;;
        --full)
            TEST_MODE="full"
            shift
            ;;
        --regression)
            RUN_REGRESSION=true
            shift
            ;;
        --baseline=*)
            BASELINE_DIR="${1#*=}"
            RUN_REGRESSION=true
            shift
            ;;
        --output-dir=*)
            OUTPUT_BASE_DIR="${1#*=}"
            OUTPUT_DIR="${OUTPUT_BASE_DIR}/performance_${TIMESTAMP}"
            shift
            ;;
        --help)
            echo "SwClock Performance Validation Suite"
            echo ""
            echo "Usage: $0 [options]"
            echo ""
            echo "Options:"
            echo "  --quick           Quick validation (~5-10 min)"
            echo "  --full            Full validation with long-term tests (~60 min)"
            echo "  --regression      Compare against baseline performance"
            echo "  --baseline=DIR    Specify baseline directory for comparison"
            echo "  --output-dir=DIR  Custom output directory (default: performance/)"
            echo "  --help            Show this help message"
            echo ""
            echo "Test Modes:"
            echo "  quick:  Discipline loop (60s), step response, slew rate"
            echo "  full:   All quick tests + holdover (30min), extended MTIE/TDEV"
            echo ""
            echo "Output:"
            echo "  ${OUTPUT_BASE_DIR}/performance_YYYYMMDD-HHMMSS/"
            echo "    ├── summary_report.md"
            echo "    ├── metrics.json"
            echo "    ├── plots (PNG files)"
            echo "    └── raw_data (CSV logs)"
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
echo -e "${BLUE}SwClock Performance Validation${NC}"
echo -e "${BLUE}IEEE 1588 / ITU-T G.810/G.8260${NC}"
echo -e "${BLUE}========================================${NC}"
echo -e "Mode: ${CYAN}${TEST_MODE}${NC}"
echo -e "Output: ${CYAN}${OUTPUT_DIR}${NC}"
if [ "$RUN_REGRESSION" = true ]; then
    echo -e "Regression: ${CYAN}enabled${NC}"
    if [ -n "$BASELINE_DIR" ]; then
        echo -e "Baseline: ${CYAN}${BASELINE_DIR}${NC}"
    fi
fi
echo ""

# Create output directory structure
echo -e "${YELLOW}Creating output directory structure...${NC}"
mkdir -p "${OUTPUT_DIR}/raw_data"
mkdir -p "${OUTPUT_DIR}/plots"
echo -e "${GREEN}✓ Directory structure created${NC}"
echo ""

# Build the project if needed
echo -e "${YELLOW}Building SwClock test suite...${NC}"
./build.sh --clean
if [ $? -ne 0 ]; then
    echo -e "${RED}✗ Build failed${NC}"
    exit 1
fi
echo -e "${GREEN}✓ Build successful${NC}"
echo ""

# Test binary path
TEST_BINARY="build/ninja-gtests-macos/swclock_gtests"

if [ ! -f "$TEST_BINARY" ]; then
    echo -e "${RED}✗ Test binary not found: $TEST_BINARY${NC}"
    exit 1
fi

# ========================================
# Test Execution
# ========================================

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Running Performance Tests${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# Test filter based on mode
if [ "$TEST_MODE" = "quick" ]; then
    TEST_FILTER="Perf.DisciplineTEStats_MTIE_TDEV:Perf.SettlingAndOvershoot:Perf.SlewRateClamp"
    echo -e "${CYAN}Quick Mode Tests:${NC}"
    echo "  - Discipline Loop (60s)"
    echo "  - Settling & Overshoot"
    echo "  - Slew Rate Validation"
else
    TEST_FILTER="Perf.*"
    echo -e "${CYAN}Full Mode Tests:${NC}"
    echo "  - All quick mode tests"
    echo "  - Holdover Drift (30 min)"
    echo "  - Extended stability measurements"
fi
echo ""

# Set log directory for tests to use
export SWCLOCK_LOG_DIR="${OUTPUT_DIR}/raw_data"

# Enable CSV export for detailed time series data
export SWCLOCK_PERF_CSV=1

echo -e "${CYAN}Data collection:${NC}"
echo "  Output directory: ${OUTPUT_DIR}"
echo "  CSV export: ENABLED"
echo "  Raw data: ${OUTPUT_DIR}/raw_data"
echo ""

# Run the performance tests
echo -e "${YELLOW}Executing test suite...${NC}"
START_TIME=$(date +%s)

$TEST_BINARY --gtest_filter="$TEST_FILTER" --gtest_output=json:${OUTPUT_DIR}/test_results.json 2>&1 | tee ${OUTPUT_DIR}/test_output.log

TEST_EXIT_CODE=$?
END_TIME=$(date +%s)
DURATION=$((END_TIME - START_TIME))

echo ""
if [ $TEST_EXIT_CODE -eq 0 ]; then
    echo -e "${GREEN}✓ All performance tests passed (${DURATION}s)${NC}"
else
    echo -e "${RED}✗ Some performance tests failed${NC}"
    echo -e "${YELLOW}Continuing with analysis of completed tests...${NC}"
fi
echo ""

# ========================================
# Data Analysis & Plot Generation
# ========================================

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Analyzing Results${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# Check for Python and required packages
echo -e "${YELLOW}Checking Python environment...${NC}"

# Activate virtual environment if it exists
VENV_PATH="${SCRIPT_DIR}/tools/venv"
if [ -d "$VENV_PATH" ]; then
    echo -e "${CYAN}Activating Python virtual environment...${NC}"
    source "$VENV_PATH/bin/activate"
    echo -e "${GREEN}✓ Virtual environment activated${NC}"
elif ! command -v python3 &> /dev/null; then
    echo -e "${RED}✗ Python 3 not found${NC}"
    echo -e "${YELLOW}Create virtual environment with: python3 -m venv tools/venv${NC}"
    exit 1
else
    echo -e "${YELLOW}⚠ Virtual environment not found, using system Python${NC}"
    echo -e "${YELLOW}  Consider creating one: cd tools && python3 -m venv venv${NC}"
fi

# Run the performance analysis tools
echo -e "${YELLOW}Computing IEEE metrics and generating plots...${NC}"

# Extract metrics from test output and generate report directly
python3 "${SCRIPT_DIR}/tools/analyze_performance_logs.py" \
    --test-output="${OUTPUT_DIR}/test_output.log" \
    --test-results="${OUTPUT_DIR}/test_results.json" \
    --output-dir="${OUTPUT_DIR}" \
    --mode="${TEST_MODE}"

ANALYSIS_EXIT_CODE=$?

if [ $ANALYSIS_EXIT_CODE -eq 0 ]; then
    echo -e "${GREEN}✓ Analysis completed${NC}"
else
    echo -e "${RED}✗ Analysis failed${NC}"
    exit 1
fi

# Generate time series plots from CSV data
echo -e "${YELLOW}Generating time series plots from CSV data...${NC}"
python3 "${SCRIPT_DIR}/tools/read_performance_csv.py" \
    --logs-dir="${OUTPUT_DIR}/raw_data" \
    --output-dir="${OUTPUT_DIR}/plots"

if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓ CSV plots generated${NC}"
else
    echo -e "${YELLOW}⚠ CSV plotting skipped (no CSV data or error)${NC}"
fi

echo ""

# ========================================
# Report Generation
# ========================================

echo -e "${YELLOW}Generating performance report...${NC}"

python3 "${SCRIPT_DIR}/tools/generate_performance_report.py" \
    --metrics="${OUTPUT_DIR}/metrics.json" \
    --output="${OUTPUT_DIR}/summary_report.md" \
    --test-mode="${TEST_MODE}" \
    --plots-dir="${OUTPUT_DIR}/plots"

if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓ Report generated${NC}"
else
    echo -e "${RED}✗ Report generation failed${NC}"
fi
echo ""

# ========================================
# Regression Testing (if enabled)
# ========================================

if [ "$RUN_REGRESSION" = true ]; then
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}Regression Analysis${NC}"
    echo -e "${BLUE}========================================${NC}"
    echo ""
    
    if [ -z "$BASELINE_DIR" ]; then
        # Find most recent baseline
        BASELINE_DIR=$(find "$OUTPUT_BASE_DIR" -maxdepth 1 -type d -name "performance_*" | sort -r | head -2 | tail -1)
    fi
    
    if [ -n "$BASELINE_DIR" ] && [ -d "$BASELINE_DIR" ]; then
        echo -e "${YELLOW}Comparing against baseline: ${BASELINE_DIR}${NC}"
        
        python3 "${SCRIPT_DIR}/tools/compare_performance.py" \
            --baseline="${BASELINE_DIR}/metrics.json" \
            --current="${OUTPUT_DIR}/metrics.json" \
            --output="${OUTPUT_DIR}/regression_report.md"
        
        if [ $? -eq 0 ]; then
            echo -e "${GREEN}✓ Regression analysis completed${NC}"
        else
            echo -e "${YELLOW}⚠ Regression analysis had issues${NC}"
        fi
    else
        echo -e "${YELLOW}⚠ No baseline found for comparison${NC}"
    fi
    echo ""
fi

# ========================================
# Summary
# ========================================

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Performance Validation Complete${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# Display summary from metrics.json if it exists
if [ -f "${OUTPUT_DIR}/metrics.json" ]; then
    OVERALL_PASS=$(python3 -c "import json; print(json.load(open('${OUTPUT_DIR}/metrics.json'))['overall_pass'])" 2>/dev/null || echo "unknown")
    
    if [ "$OVERALL_PASS" = "True" ]; then
        echo -e "${GREEN}✓ OVERALL RESULT: PASS${NC}"
    elif [ "$OVERALL_PASS" = "False" ]; then
        echo -e "${RED}✗ OVERALL RESULT: FAIL${NC}"
    else
        echo -e "${YELLOW}⚠ OVERALL RESULT: INCOMPLETE${NC}"
    fi
    echo ""
fi

echo "Results saved to: ${OUTPUT_DIR}"
echo ""
echo "Key files:"
echo "  - summary_report.md    : Human-readable summary"
echo "  - metrics.json         : Machine-readable metrics"
echo "  - plots/              : PNG visualizations"
echo "  - raw_data/           : CSV data files"
echo ""

if [ -f "${OUTPUT_DIR}/summary_report.md" ]; then
    echo "View summary:"
    echo "  cat ${OUTPUT_DIR}/summary_report.md"
    echo ""
fi

# Exit with test result code
exit $TEST_EXIT_CODE
