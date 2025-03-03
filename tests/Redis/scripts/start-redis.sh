#!/bin/bash

set -x

SCRIPT_DIR=$(realpath "$(dirname "$0")")
MODE=$1
SUFFIX=$2

CONFIG_FILE=vanilla.conf
if [ "$MODE" == "embedded" ]; then
	CONFIG_FILE=embedded.conf
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

# Start a vanilla redis server instance dump rdb to dbfilename dump_full.rdb
taskset -c 36,37,38,39 $REDIS "$SCRIPT_DIR/config/$CONFIG_FILE" > "$SCRIPT_DIR/logs/$MODE-$SUFFIX.log" 2>&1 &
echo "Redis $MODE started"
