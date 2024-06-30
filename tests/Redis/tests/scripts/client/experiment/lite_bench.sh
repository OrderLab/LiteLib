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
        redis-benchmark -h 172.16.0.2 -p 6479 -t set -d 100 -l
        # set,get,hset,hget,sadd,spop,zadd,zpopmin,lpush,rpush,lpop,rpop

        # Check exit status
        if [[ $? -ne 0 ]]; then
            echo "spin" > /dev/null
        fi

        # Sleep for a short duration before restarting
    done
}

# Run the benchmark function
run_benchmark
