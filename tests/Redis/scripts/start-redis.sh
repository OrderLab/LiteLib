#!/bin/bash

SCRIPT_DIR=$(dirname "$0")
MODE=$1
SUFFIX=$2

CONFIG_FILE=vanilla.conf
if [ "$MODE" == "embedded" ]; then
	CONFIG_FILE=vanilla-embedded.conf
fi

REDIS="$SCRIPT_DIR/../src/redis/src/redis-server-vanilla"
if [ "$MODE" == "embedded" ]; then
	REDIS="$SCRIPT_DIR/../src/redis/src/redis-server"
fi

if [ ! -d "$SCRIPT_DIR/logs" ]; then
  mkdir "$SCRIPT_DIR/logs"
fi

# Start a vanilla redis server instance dump rdb to dbfilename dump_full.rdb
$REDIS "$SCRIPT_DIR/config/$CONFIG_FILE.conf" > "$SCRIPT_DIR/logs/redis-$MODE-$SUFFIX.log" 2>&1 &
echo "Redis $MODE started"
