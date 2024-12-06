#!/bin/bash

# node 0: benchmark client
# node 1: vanilla + lite
# node 2: replica
# node 3: sentinel

kill_process_by_port() {
  local port=$1
  local pids=$(lsof -t -i:$port)
  if [ -n "$pids" ]; then
    kill $pids
    echo "Killed processes on port $port: $pids"
  else
    echo "No processes found on port $port"
  fi
}

SCRIPT_DIR=$(dirname "$0")
DEST_DIR="litesys/redis"
LITE_DIR="$SCRIPT_DIR/../src/lite-version/build"

MODE=$1

CLIENT_HOST="10.10.1.1"
LITE_HOST="10.10.1.2"
REPLICA_HOST="10.10.1.3"
SENTINEL_HOST="10.10.1.4"

LITE_PORT=6479
SENTINEL_PORT="26479"
MASTER_NAME="vanilla_redis"

if [ "$MODE" == "lite" ]; then
    python -u $SCRIPT_DIR/lite-monitor.py > $SCRIPT_DIR/logs/lite-monitor.log 2>&1 &
elif [ "$MODE" == "replica" ]; then
    python -u $SCRIPT_DIR/vanilla-monitor.py > $SCRIPT_DIR/logs/vanilla-monitor.log 2>&1 &
    ssh $REPLICA_HOST "python -u $DEST_DIR/repl-monitor.py" > $SCRIPT_DIR/logs/repl-monitor.log 2>&1 &
    ssh $SENTINEL_HOST "python -u $DEST_DIR/sentinel-monitor.py" > $SCRIPT_DIR/logs/sentinel-monitor.log 2>&1 &
fi

echo "Monitoring started"

# start benchmark
WRRATIO="1:1"
TEST_TIME=120
RATE_LIMITING=500
DATA_SIZE=1048
THREADS=20
CONNECTIONS=5
KEY_PATTERN="R:R"
KEY_MAX=100000
OUTPUT_FILE="$DEST_DIR/memtier_benchmark.log"

get_master_info() {
    if [ "$MODE" == "lite" ]; then
        MASTER_HOST=$LITE_HOST
        MASTER_PORT=$LITE_PORT
        return
    elif [ "$MODE" == "replica" ]; then
        MASTER_INFO=$(redis-cli -h $SENTINEL_HOST -p $SENTINEL_PORT SENTINEL get-master-addr-by-name $MASTER_NAME)
        MASTER_HOST=$(echo $MASTER_INFO | awk '{print $1}')
        MASTER_PORT=$(echo $MASTER_INFO | awk '{print $2}')
        return
    fi
}

# ./lite_cli -t /tmp/lite_Redis -p /tmp/redis.sock -m 1
# ./lite_cli -t /tmp/lite_Redis -p /tmp/redis.sock -m 0

kill_vanilla_server() {
    sleep 5
    kill_process_by_port 16379
    echo "Vanilla server killed after 40 seconds"
	if [ "$MODE" == "lite" ]; then
		$LITE_DIR/Lite/lite_cli -t /tmp/lite_Redis -p /tmp/redis.sock -m 1
		echo "Enter emergency mode"
	fi
}

rm -f $SCRIPT_DIR/logs/benchmark.log

get_master_info
echo "Master host: $MASTER_HOST, Master port: $MASTER_PORT"

kill_vanilla_server &
echo "Vanilla server will be killed in 40 seconds"

while true; do
    get_master_info
    # append a timestamp (sec) for each start of benchmark
    TIMESTAMP=$(date "+%Y-%m-%d %H:%M:%S")
    echo "[$TIMESTAMP] Starting benchmark" >> $SCRIPT_DIR/logs/benchmark.log

    ssh $CLIENT_HOST "memtier_benchmark --host=$MASTER_HOST --port=$MASTER_PORT --ratio=$WRRATIO --test-time=$TEST_TIME --rate-limiting=$RATE_LIMITING -t $THREADS -c $CONNECTIONS --key-pattern=$KEY_PATTERN --key-maximum=$KEY_MAX --distinct-client-seed --data-size=$DATA_SIZE --out-file=$OUTPUT_FILE" >> $SCRIPT_DIR/logs/benchmark.log 2>&1
    STATUS=$?
    
    if [[ $STATUS -eq 0 ]]; then
        if grep -q "Connection error" $SCRIPT_DIR/logs/benchmark.log; then
            # Benchmark failed due to connection errors
            STATUS=1
        else
            echo "Benchmark completed successfully"
            break
        fi
    fi
    
    if [[ $STATUS -ne 0 && $MODE !=  "lite" ]]; then
        sleep 2
        get_master_info
        if [[ "$MASTER_HOST" == "" || "$MASTER_PORT" == "" ]]; then
            exit 1
        fi
    fi
done

if [ "$MODE" == "replica" ]; then
    scp $REPLICA_HOST:$DEST_DIR/*.csv $SCRIPT_DIR/data
    scp $SENTINEL_HOST:$DEST_DIR/*.csv $SCRIPT_DIR/data
fi
