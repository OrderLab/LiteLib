#!/bin/bash

# Check if duration argument is provided
if [ $# -ne 2 ]; then
    echo "Usage: $0 <log_prefix> <duration_in_seconds>"
    exit 1
fi

LOG_FILE=$1
DURATION=$2

# Validate that duration is a positive number
if ! [[ "$DURATION" =~ ^[0-9]+$ ]]; then
    echo "Error: Duration must be a positive number"
    exit 1
fi

# Create a log directory if it doesn't exist
SCRIPT_DIR="$(dirname "$0")"
LOG_DIR="$SCRIPT_DIR/logs"
mkdir -p "$LOG_DIR"
chmod 777 "$LOG_DIR"

# Get the initial timestamp with nanosecond precision
START_TIME=$(date +%s.%N)

# Create a single combined log file with timestamp
COMBINED_LOG="$LOG_DIR/${LOG_FILE}"

echo "========================================= Starting to collect stats for $DURATION seconds =========================================" >> "$COMBINED_LOG"
echo "========================================= Getting mcrouter stats =========================================" >> "$COMBINED_LOG"
echo -e "get __mcrouter__.options\r\nquit\r\n" | nc 0 11211 >> "$COMBINED_LOG"
echo -e "get __mcrouter__.preprocessed_config\r\nquit\r\n" | nc 0 11211 >> "$COMBINED_LOG"

# Counter for elapsed seconds
ELAPSED=0

while [ $ELAPSED -lt $DURATION ]; do
    # Get current timestamp with nanosecond precision
    TIMESTAMP=$(date '+%Y-%m-%d %H:%M:%S.%N')
    UNIX_TIMESTAMP=$(date +%s.%N)
    RELATIVE_TIME=$(awk -v start="$START_TIME" -v current="$UNIX_TIMESTAMP" 'BEGIN {printf "%.9f", current - start}')

    # Write timestamp and stats to the combined log file
    echo "========================================= Seconds Elapsed: $ELAPSED (at $TIMESTAMP, Unix: $UNIX_TIMESTAMP, Relative: $RELATIVE_TIME) =========================================" >> "$COMBINED_LOG"
    (echo -e "stats all\r\nquit\r\n" | nc 0 11211) >> "$COMBINED_LOG"
    echo "" >> "$COMBINED_LOG"  # Add a blank line between entries

    # Busy wait until next second
    NEXT_TIME=$(awk -v start="$START_TIME" -v elapsed="$ELAPSED" 'BEGIN {printf "%.9f", start + elapsed + 1}')
    while true; do
        CURRENT_TIME=$(date +%s.%N)
        if awk -v current="$CURRENT_TIME" -v next_time="$NEXT_TIME" 'BEGIN {exit !(current >= next_time)}'; then
            break
        fi
    done

    # Increment elapsed time counter
    ((ELAPSED++))
done

chmod 666 "$COMBINED_LOG"

echo "Completed collecting stats for $DURATION seconds"
echo "All stats have been saved to $COMBINED_LOG"
