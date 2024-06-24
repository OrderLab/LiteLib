#!/bin/bash

# Define the stop file
STOP_FILE="/tmp/redis_benchmark_stop"

# Create the stop file
touch $STOP_FILE

# Function to run redis-benchmark
run_benchmark() {
    while true; do
        # Check if the stop file exists
        if [[ ! -f $STOP_FILE ]]; then
            echo "Stop file removed, stopping the script."
            exit 0
        fi

        # Run redis-benchmark
        redis-benchmark -p 6479 -t set,hset,zadd,lpush -l

        # Check exit status
        if [[ $? -ne 0 ]]; then
            echo "redis-benchmark terminated, restarting..."
        fi

        # Sleep for a short duration before restarting
        sleep 2
    done
}

# Run the benchmark function
run_benchmark
