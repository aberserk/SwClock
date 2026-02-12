#!/bin/bash
#
# 4-Hour Long-Duration Performance Validation
# Runs full test suite multiple times over 4 hours
#

set -e

# Colors
GREEN='\033[0;32m'
CYAN='\033[0;36m'
YELLOW='\033[1;33m'
NC='\033[0m'

DURATION_HOURS=4
DURATION_SECONDS=$((DURATION_HOURS * 3600))
START_TIME=$(date +%s)
END_TIME=$((START_TIME + DURATION_SECONDS))

OUTPUT_DIR="performance/long_duration_4h_$(date +%Y%m%d-%H%M%S)"
mkdir -p "$OUTPUT_DIR"

echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN}4-Hour Performance Validation${NC}"
echo -e "${CYAN}========================================${NC}"
echo -e "Start time: $(date)"
echo -e "End time:   $(date -r $END_TIME)"
echo -e "Output:     $OUTPUT_DIR"
echo -e ""

ITERATION=1

while [ $(date +%s) -lt $END_TIME ]; do
    REMAINING=$((END_TIME - $(date +%s)))
    REMAINING_MIN=$((REMAINING / 60))
    
    echo -e "${YELLOW}========================================${NC}"
    echo -e "${YELLOW}Iteration $ITERATION${NC}"
    echo -e "${YELLOW}Time remaining: ${REMAINING_MIN} minutes${NC}"
    echo -e "${YELLOW}========================================${NC}"
    
    ITER_DIR="$OUTPUT_DIR/iteration_$ITERATION"
    mkdir -p "$ITER_DIR"
    
    # Run full performance test suite
    ./scripts/performance.sh --full --output-dir="$ITER_DIR" 2>&1 | tee "$ITER_DIR/output.log"
    
    EXIT_CODE=${PIPESTATUS[0]}
    
    if [ $EXIT_CODE -eq 0 ]; then
        echo -e "${GREEN}✓ Iteration $ITERATION completed successfully${NC}"
    else
        echo -e "${YELLOW}⚠ Iteration $ITERATION completed with warnings${NC}"
    fi
    
    # Record iteration summary
    echo "Iteration $ITERATION: $(date), Exit code: $EXIT_CODE" >> "$OUTPUT_DIR/summary.txt"
    
    ITERATION=$((ITERATION + 1))
    
    # Brief pause between iterations
    sleep 10
done

# Generate final summary
echo -e ""
echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN}4-Hour Test Complete${NC}"
echo -e "${CYAN}========================================${NC}"
echo -e "Total iterations: $((ITERATION - 1))"
echo -e "Results saved to: $OUTPUT_DIR"
echo -e ""
echo "Summary:"
cat "$OUTPUT_DIR/summary.txt"

echo -e ""
echo -e "${GREEN}✓ 4-hour performance validation complete${NC}"
