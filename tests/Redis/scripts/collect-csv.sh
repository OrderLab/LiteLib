#!/bin/bash

SCRIPT_DIR=$(dirname "$0")
DEST_DIR="litesys/redis"

REPLICA_HOST="10.10.1.3"
SENTINEL_HOST="10.10.1.4"

if [ "$MODE" == "replica" ]; then
    scp $REPLICA_HOST:$DEST_DIR/*.csv $SCRIPT_DIR/data
    scp $SENTINEL_HOST:$DEST_DIR/*.csv $SCRIPT_DIR/data
fi