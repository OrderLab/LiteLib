#!/bin/bash

set -x

SCRIPT_DIR=$(realpath "$(dirname "$0")")
MODE=$1
SUFFIX=$2

if [ ! -d "$SCRIPT_DIR/logs" ]; then
  mkdir "$SCRIPT_DIR/logs"
fi

export GLOG_stderrthreshold=0
export GLOG_logtostderr=1

"$SCRIPT_DIR/../src/lite-version/build/redis-lite" > "$SCRIPT_DIR/logs/$MODE-lite-$SUFFIX.log" 2>&1 &
echo "Redis Lite started"