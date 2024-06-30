#!/bin/bash

# Define the stop file
STOP_FILE="/tmp/redis_benchmark_stop"

# Create the stop file
touch $STOP_FILE

# Function to get master info from sentinel
get_master_info() {
    MASTER_INFO=$(redis-cli -h 172.16.0.2 -p 26379 sentinel get-master-addr-by-name full_redis)
    MASTER_IP=$(echo $MASTER_INFO | awk '{print $1}')
    MASTER_PORT=$(echo $MASTER_INFO | awk '{print $2}')
}

# Function to run redis-benchmark
run_benchmark() {
    while true; do
        # Check if the stop file exists
        if [[ ! -f $STOP_FILE ]]; then
            echo "Stop file removed, stopping the script."
            exit 0
        fi

        # Get master info
        get_master_info

        # Run redis-benchmark
        redis-benchmark -h $MASTER_IP -p $MASTER_PORT -t set -d 100 -l
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
