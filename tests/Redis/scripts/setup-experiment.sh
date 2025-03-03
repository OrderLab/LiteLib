#!/bin/bash

# node 0: benchmark client

SCRIPT_DIR=$(dirname "$0")

# take arg for mode: lite, replica
MODE=$1
SUFFIX=$2

rm -f $SCRIPT_DIR/*.rdb
$SCRIPT_DIR/stop-all.sh

if [ "$MODE" == "lite" ] || [ "$MODE" == "embedded" ]; then
  echo "Starting Redis Lite"
  $SCRIPT_DIR/start-redis.sh $MODE $SUFFIX
  $SCRIPT_DIR/start-lite.sh $MODE $SUFFIX
elif [ "$MODE" == "replica" ]; then
  echo "Starting Redis Replica"
  $SCRIPT_DIR/start-redis.sh $MODE $SUFFIX
  $SCRIPT_DIR/start-repl.sh $MODE $SUFFIX
elif [ "$MODE" == "vanilla" ]; then
  echo "Starting Redis Vanilla"
  $SCRIPT_DIR/start-redis.sh $MODE $SUFFIX
else
  echo "Invalid mode"
  exit 1
fi