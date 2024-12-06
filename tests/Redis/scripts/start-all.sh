#!/bin/bash

SCRIPT_DIR=$(dirname "$0")

# take arg for mode: lite, replica
MODE=$1

$SCRIPT_DIR/stop-all.sh

if [ "$MODE" == "lite" ]; then
  echo "Starting Redis Lite"
  $SCRIPT_DIR/start-redis.sh
  $SCRIPT_DIR/start-lite.sh
elif [ "$MODE" == "replica" ]; then
  echo "Starting Redis Replica"
  $SCRIPT_DIR/start-redis.sh
  $SCRIPT_DIR/start-repl.sh
fi