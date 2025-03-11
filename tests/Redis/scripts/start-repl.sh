#!/bin/bash

set -x

SCRIPT_DIR=$(realpath "$(dirname "$0")")
REPLICA_HOST="10.10.1.2"
DEST_DIR="$SCRIPT_DIR"
MODE=$1
SUFFIX=$2

REDIS="$SCRIPT_DIR/../src/redis/src/redis-server-vanilla"
if [ "$MODE" == "embedded" ]; then
	REDIS="$SCRIPT_DIR/../src/redis/src/redis-server"
fi

REDIS_SENTINEL="$SCRIPT_DIR/../src/redis/src/redis-sentinel-vanilla"
if [ "$MODE" == "embedded" ]; then
	REDIS_SENTINEL="$SCRIPT_DIR/../src/redis/src/redis-sentinel"
fi

# Start the Redis replica on REPLICA_HOST, making directory if it doesn't exist
ssh "$REPLICA_HOST" "
  if [ ! -d \"$DEST_DIR/logs\" ]; then
    mkdir -p \"$DEST_DIR/logs\"
  fi

  rm -f *.rdb
  rm -f $DEST_DIR/logs/*.log
  taskset -c 36,37,38,39 $REDIS \"$DEST_DIR/config/replica.conf\" > \"$DEST_DIR/logs/$MODE-replica-$SUFFIX.log\" 2>&1 &
"

ssh "$SENTINEL_HOST" "
  if [ ! -d \"$DEST_DIR/logs\" ]; then
    mkdir -p \"$DEST_DIR/logs\"
  fi

  rm -f *.rdb
  rm -f $DEST_DIR/logs/*.log
"

LITE_HOST="10.10.1.4"
REPLICA_HOST="10.10.1.2"
SENTINEL_HOST="10.10.1.3"

for HOST in "$LITE_HOST" "$REPLICA_HOST" "$SENTINEL_HOST"; do
  ssh "$HOST" "
    if [ ! -d \"$DEST_DIR/logs\" ]; then
      mkdir -p \"$DEST_DIR/logs\"
    fi
  "

  ssh "$HOST" "
	taskset -c 28,29 $REDIS_SENTINEL \"$DEST_DIR/config/sentinel.conf\" > \"$DEST_DIR/logs/$MODE-sentinel-$HOST-$SUFFIX.log\" 2>&1 &
  "
done
