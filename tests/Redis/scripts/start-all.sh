#!/bin/bash

SCRIPT_DIR=$(dirname "$0")

# take arg for mode: lite, replica
MODE=$1

$SCRIPT_DIR/stop-all.sh

if [ "$MODE" == "lite" ]; then
  $SCRIPT_DIR/start-redis.sh
  $SCRIPT_DIR/start-lite.sh
elif [ "$MODE" == "replica" ]; then
  $SCRIPT_DIR/start-redis.sh
  $SCRIPT_DIR/start-repl.sh
elif [ "$MODE" == "vanilla" ]; then
  $SCRIPT_DIR/start-redis.sh
fi