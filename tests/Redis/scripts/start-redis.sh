#!/bin/bash

set -x

SCRIPT_DIR=$(realpath "$(dirname "$0")")
MODE=$1
SUFFIX=$2
SECOND_TIME=${3:-0}

if [ "$MODE" == "embedded" ]; then
	cp $SCRIPT_DIR/config/embedded.conf $SCRIPT_DIR/config/redis-tmp.conf
else
	cp $SCRIPT_DIR/config/vanilla.conf $SCRIPT_DIR/config/redis-tmp.conf
fi

export GLOG_stderrthreshold=0
export GLOG_logtostderr=1
export LD_LIBRARY_PATH=$SCRIPT_DIR/../src/lite-version/build:$LD_LIBRARY_PATH

REDIS="$SCRIPT_DIR/../src/redis/src/redis-server-vanilla"
if [ "$MODE" == "embedded" ]; then
	REDIS="$SCRIPT_DIR/../src/redis/src/redis-server"
fi

if [ ! -d "$SCRIPT_DIR/logs" ]; then
  mkdir "$SCRIPT_DIR/logs"
fi

# Function to check if port is available
alive_on_port() {
    local port=$1
    local addr=0.0.0.0
    if [ "$MODE" == "embedded" ]; then
        addr="127.0.0.1"
    fi
    local if=$(lsof -t -i @$addr:$port)
    if [ -n "$if" ]; then
        return 0
    fi
    return 1
}

# Wait until port 16379 is free
while alive_on_port 16379; do
    echo "Waiting for port 16379 to be available..."
    sleep 0.1
done

# Start a vanilla redis server instance dump rdb to dbfilename dump_full.rdb
if [ "$SECOND_TIME" == "0" ]; then
    rm $SCRIPT_DIR/logs/$MODE-$SUFFIX.log
fi
taskset -c 36,37,38,39 $REDIS "$SCRIPT_DIR/config/redis-tmp.conf" >> "$SCRIPT_DIR/logs/$MODE-$SUFFIX.log" 2>&1 &
echo "Redis $MODE started"
