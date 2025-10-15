#!/bin/bash
# Script to find and analyze the latest WiFi performance test output

BUILD_DIR="$1"
TOOLS_DIR="$2"

if [ -z "$BUILD_DIR" ] || [ -z "$TOOLS_DIR" ]; then
    echo "Usage: $0 <build_directory> <tools_directory>"
    exit 1
fi

LOGS_DIR="$BUILD_DIR/logs"

if [ ! -d "$LOGS_DIR" ]; then
    echo "No logs directory found at $LOGS_DIR"
    echo "Run the WiFi test first to generate data."
    exit 1
fi

# Find the most recent timestamped directory
LATEST_DIR=$(find "$LOGS_DIR" -maxdepth 1 -type d -name "20*" | sort | tail -1)

if [ -z "$LATEST_DIR" ]; then
    echo "No timestamped directories found in $LOGS_DIR"
    echo "Run the WiFi test first to generate data."
    exit 1
fi

CSV_FILE="$LATEST_DIR/kf_wifi_perf.csv"

if [ ! -f "$CSV_FILE" ]; then
    echo "No CSV file found at $CSV_FILE"
    exit 1
fi

echo "Processing latest test output from: $LATEST_DIR"
python3 "$TOOLS_DIR/plot_kf_wifi_perf.py" "$CSV_FILE"