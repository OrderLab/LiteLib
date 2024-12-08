#!/bin/bash

SCRIPT_DIR=$(dirname "$0")
DEST_DIR="litesys/redis"

REPLICA_HOST="10.10.1.3"
SENTINEL_HOST="10.10.1.4"

scp $REPLICA_HOST:$DEST_DIR/*.csv $SCRIPT_DIR/data
scp $SENTINEL_HOST:$DEST_DIR/*.csv $SCRIPT_DIR/data