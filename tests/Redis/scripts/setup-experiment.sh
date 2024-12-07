#!/bin/bash

# node 0: benchmark client

SCRIPT_DIR=$(dirname "$0")

# take arg for mode: lite, replica
MODE=$1

rm -f $SCRIPT_DIR/*.rdb

if [ "$MODE" == "lite" ]; then
  echo "Starting Redis Lite"
  $SCRIPT_DIR/start-all.sh lite
elif [ "$MODE" == "replica" ]; then
  echo "Starting Redis Replica"
  $SCRIPT_DIR/start-all.sh replica
elif [ "$MODE" == "vanilla" ]; then
  echo "Starting Redis Vanilla"
  $SCRIPT_DIR/start-all.sh vanilla
else
  echo "Invalid mode"
  exit 1
fi