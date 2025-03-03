#!/bin/bash

SCRIPT_DIR=$(dirname "$0")
MODE=$1
SUFFIX=$2

if [ ! -d "$SCRIPT_DIR/logs" ]; then
  mkdir "$SCRIPT_DIR/logs"
fi

"$SCRIPT_DIR/../src/lite-version/build/redis-lite" > "$SCRIPT_DIR/logs/redis-lite-$MODE-$SUFFIX.log" 2>&1 &
echo "Redis Lite started"