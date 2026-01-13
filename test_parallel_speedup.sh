#!/bin/bash
# Quick benchmark script to compare sequential vs parallel execution

echo "========================================"
echo "SwClock Parallel Execution Benchmark"
echo "========================================"
echo ""

TEST_BINARY="build/ninja-gtests-macos/swclock_gtests"
TEST_FILTER="Perf.DisciplineTEStats_MTIE_TDEV:Perf.SettlingAndOvershoot:Perf.SlewRateClamp"

if [ ! -f "$TEST_BINARY" ]; then
    echo "Test binary not found. Run ./build.sh first."
    exit 1
fi

# Sequential test
echo "Running SEQUENTIAL tests..."
START=$(date +%s)
$TEST_BINARY --gtest_filter="$TEST_FILTER" > /tmp/seq_test.log 2>&1
SEQ_CODE=$?
END=$(date +%s)
SEQ_TIME=$((END - START))

if [ $SEQ_CODE -eq 0 ]; then
    echo "✓ Sequential: ${SEQ_TIME}s"
else
    echo "✗ Sequential failed"
    exit 1
fi
echo ""

# Parallel test
echo "Running PARALLEL tests..."
declare -a TESTS=("Perf.DisciplineTEStats_MTIE_TDEV" "Perf.SettlingAndOvershoot" "Perf.SlewRateClamp")
declare -a PIDS=()

START=$(date +%s)
for i in "${!TESTS[@]}"; do
    ($TEST_BINARY --gtest_filter="${TESTS[$i]}" > /tmp/par_test_${i}.log 2>&1) &
    PIDS+=($!)
    
    # Limit to 2 parallel
    if [ ${#PIDS[@]} -ge 2 ]; then
        wait ${PIDS[0]}
        PIDS=("${PIDS[@]:1}")
    fi
done

# Wait for remaining
for pid in "${PIDS[@]}"; do
    wait $pid
done
END=$(date +%s)
PAR_TIME=$((END - START))

echo "✓ Parallel: ${PAR_TIME}s"
echo ""

# Calculate speedup
SPEEDUP=$(echo "scale=2; $SEQ_TIME / $PAR_TIME" | bc)
REDUCTION=$(echo "scale=1; ($SEQ_TIME - $PAR_TIME) * 100 / $SEQ_TIME" | bc)

echo "========================================"
echo "Results:"
echo "  Sequential: ${SEQ_TIME}s"
echo "  Parallel:   ${PAR_TIME}s"
echo "  Speedup:    ${SPEEDUP}x"
echo "  Reduction:  ${REDUCTION}%"
echo "========================================"
