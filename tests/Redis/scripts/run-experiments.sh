#!/bin/bash

# Nodes Configuration:
# Node 0: Benchmark client
# Node 1: Vanilla + Lite
# Node 2: Replica
# Node 3: Sentinel

SCRIPT_DIR=$(dirname "$0")
DEST_DIR="litesys/redis"
LITE_DIR="$SCRIPT_DIR/../src/lite-version/build"
YCSB_DIR="$DEST_DIR/YCSB"

MODE=$1

CLIENT_HOST="10.10.1.1"
LITE_HOST="10.10.1.2"
REPLICA_HOST="10.10.1.3"
SENTINEL_HOST="10.10.1.4"

LITE_PORT=6479
VANILLA_PORT=16379
SENTINEL_PORT="26479"
MASTER_NAME="vanilla_redis"

CRASH_TIME=20

# Function to kill processes by port
kill_process_by_port() {
  local port=$1
  local pids=$(lsof -t -i:$port)
  if [ -n "$pids" ]; then
    kill -9 $pids
    echo "Killed processes on port $port: $pids"
  else
    echo "No processes found on port $port"
  fi
}

# Function to get master node information based on the mode
get_master_info() {
    if [ "$MODE" == "lite" ]; then
        MASTER_HOST=$LITE_HOST
        MASTER_PORT=$LITE_PORT
    elif [ "$MODE" == "replica" ]; then
        MASTER_INFO=$(redis-cli -h $SENTINEL_HOST -p $SENTINEL_PORT SENTINEL get-master-addr-by-name $MASTER_NAME)
        MASTER_HOST=$(echo $MASTER_INFO | awk '{print $1}')
        MASTER_PORT=$(echo $MASTER_INFO | awk '{print $2}')
    elif [ "$MODE" == "vanilla" ]; then
        MASTER_HOST=$LITE_HOST
        MASTER_PORT=$VANILLA_PORT
    fi
}

alive_on_port() {
	local port=$1
	local if=$(lsof -t -i:$port)
	if [ -n "$if" ]; then
		return 0
	else
		return 1
	fi
}

# Function to handle the killing of the vanilla server and recovery
kill_vanilla_server() {
    sleep $CRASH_TIME
	
	redis-cli -h $MASTER_HOST -p $VANILLA_PORT shutdown save &
	echo "Attempting to kill vanilla server on port $VANILLA_PORT"
    if [ "$MODE" == "lite" ]; then
        $LITE_DIR/Lite/lite_cli -t /tmp/lite_Redis -p /tmp/redis.sock -m 1
        echo "Entered emergency mode"
        while alive_on_port $VANILLA_PORT; do
            sleep 0.1
        done
        echo "Vanilla server killed after $CRASH_TIME seconds"
        
        # Reboot the vanilla server
		redis-server $SCRIPT_DIR/config/vanilla-running.conf
        while ! redis-cli -h $MASTER_HOST -p $VANILLA_PORT ping | grep -q "PONG"; do
            sleep 0.1
        done
        $LITE_DIR/Lite/lite_cli -t /tmp/lite_Redis -p /tmp/redis.sock -m 0
        echo "Vanilla server is back up and running"
        
    elif [ "$MODE" == "replica" ]; then
        while alive_on_port $VANILLA_PORT; do
            sleep 0.1
        done
        echo "Vanilla server killed after $CRASH_TIME seconds"
        while true; do
            NEW_MASTER_INFO=$(redis-cli -h $SENTINEL_HOST -p $SENTINEL_PORT SENTINEL get-master-addr-by-name $MASTER_NAME)
            NEW_MASTER_HOST=$(echo $NEW_MASTER_INFO | awk '{print $1}')
            NEW_MASTER_PORT=$(echo $NEW_MASTER_INFO | awk '{print $2}')
            if [[ "$NEW_MASTER_HOST" != "$MASTER_HOST" || "$NEW_MASTER_PORT" != "$MASTER_PORT" ]]; then
                echo "Master switched to $NEW_MASTER_HOST:$NEW_MASTER_PORT"
                redis-server $SCRIPT_DIR/config/vanilla-running.conf
                break
            fi
            sleep 0.1
        done
		echo "" >> /dev/null
    elif [ "$MODE" == "vanilla" ]; then
        while alive_on_port $VANILLA_PORT; do
            sleep 0.1
        done
        echo "Vanilla server killed after $CRASH_TIME seconds"
        $SCRIPT_DIR/start-redis.sh
        echo "Vanilla server is back up and running"
    fi
}

# Clean up previous logs and dump files
rm -f $SCRIPT_DIR/*.rdb
rm -f $SCRIPT_DIR/logs/*-monitor.log

# Start relevant monitoring processes
if [ "$MODE" == "lite" ]; then
    python -u $SCRIPT_DIR/lite-monitor.py > $SCRIPT_DIR/logs/lite-monitor.log 2>&1 &
elif [ "$MODE" == "replica" ]; then
    python -u $SCRIPT_DIR/vanilla-monitor.py > $SCRIPT_DIR/logs/vanilla-monitor.log 2>&1 &
    ssh $REPLICA_HOST "python -u $DEST_DIR/repl-monitor.py" > $SCRIPT_DIR/logs/repl-monitor.log 2>&1 &
    ssh $SENTINEL_HOST "python -u $DEST_DIR/sentinel-monitor.py" > $SCRIPT_DIR/logs/sentinel-monitor.log 2>&1 &
elif [ "$MODE" == "vanilla" ]; then
	python -u $SCRIPT_DIR/vanilla-monitor.py > $SCRIPT_DIR/logs/vanilla-monitor.log 2>&1 &
fi
echo "Monitoring started"

# Copy workload file to client host
scp $SCRIPT_DIR/ycsb_workload $CLIENT_HOST:$YCSB_DIR/workloads/ycsb_workload

# Get master node info and print it
get_master_info
echo "Master host: $MASTER_HOST, Master port: $MASTER_PORT"

if [ "$MODE" == "lite" ]; then
    ssh $CLIENT_HOST "cd $YCSB_DIR; ./bin/ycsb load redis -s -P workloads/ycsb_workload -p redis.host=$MASTER_HOST -p redis.port=$MASTER_PORT" > $SCRIPT_DIR/logs/benchmark.log 2>&1
elif [ "$MODE" == "replica" ]; then
    ssh $CLIENT_HOST "cd $YCSB_DIR; ./bin/ycsb load redis -s -P workloads/ycsb_workload -p redis.sentinel=$SENTINEL_HOST:$SENTINEL_PORT -p redis.sentinel.master=$MASTER_NAME" > $SCRIPT_DIR/logs/benchmark.log 2>&1
elif [ "$MODE" == "vanilla" ]; then
    ssh $CLIENT_HOST "cd $YCSB_DIR; ./bin/ycsb load redis -s -P workloads/ycsb_workload -p redis.host=$MASTER_HOST -p redis.port=$MASTER_PORT" > $SCRIPT_DIR/logs/benchmark.log 2>&1
fi

# Kill vanilla server after the crash time
# kill_vanilla_server &
# echo "Vanilla server will be killed in $CRASH_TIME seconds"

# Run benchmarks in a loop
while true; do
    get_master_info
    TIMESTAMP=$(date "+%Y-%m-%d %H:%M:%S")
    echo "[$TIMESTAMP] Starting benchmark" >> $SCRIPT_DIR/logs/benchmark.log

    if [ "$MODE" == "replica" ]; then
        ssh $CLIENT_HOST "cd $YCSB_DIR; ./bin/ycsb run redis -s -P workloads/ycsb_workload -p redis.sentinel=$SENTINEL_HOST:$SENTINEL_PORT -p redis.sentinel.master=$MASTER_NAME" >> $SCRIPT_DIR/logs/benchmark.log 2>&1
    elif [ "$MODE" == "lite" ]; then
        ssh $CLIENT_HOST "cd $YCSB_DIR; ./bin/ycsb run redis -s -P workloads/ycsb_workload -p redis.host=$MASTER_HOST -p redis.port=$MASTER_PORT" >> $SCRIPT_DIR/logs/benchmark.log 2>&1
    elif [ "$MODE" == "vanilla" ]; then
        ssh $CLIENT_HOST "cd $YCSB_DIR; ./bin/ycsb run redis -s -P workloads/ycsb_workload -p redis.host=$MASTER_HOST -p redis.port=$MASTER_PORT" >> $SCRIPT_DIR/logs/benchmark.log 2>&1
    fi
    STATUS=$?
    
    # Check for errors in the benchmark
    if [[ $STATUS -eq 0 ]]; then
        if grep -q "Connection error" $SCRIPT_DIR/logs/benchmark.log; then
            STATUS=1
        elif grep -q "Exception" $SCRIPT_DIR/logs/benchmark.log; then
            STATUS=2
        else
            echo "Benchmark completed successfully"
            break
        fi
    fi
    
    # Retry if there are issues
    if [[ $STATUS -ne 0 && $MODE != "lite" ]]; then
        sleep 1
        get_master_info
        if [[ -z "$MASTER_HOST" || -z "$MASTER_PORT" ]]; then
            exit 1
        fi
    fi
done

# Copy the resulting CSV files from replica and sentinel hosts
if [ "$MODE" == "replica" ]; then
    scp $REPLICA_HOST:$DEST_DIR/*.csv $SCRIPT_DIR/data
    scp $SENTINEL_HOST:$DEST_DIR/*.csv $SCRIPT_DIR/data
fi