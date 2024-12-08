#!/bin/bash

SCRIPT_DIR=$(dirname "$0")
REPLICA_HOST="10.10.1.3"
DEST_DIR="litesys/redis"

# Start the Redis replica on REPLICA_HOST, making directory if it doesn't exist
ssh "$REPLICA_HOST" "
  if [ ! -d \"$DEST_DIR/logs\" ]; then
    mkdir -p \"$DEST_DIR/logs\"
  fi

  if [ ! -d \"$DEST_DIR/config\" ]; then
    mkdir -p \"$DEST_DIR/config\"
  fi
"

scp "$SCRIPT_DIR/repl-monitor.py" "$REPLICA_HOST:$DEST_DIR/repl-monitor.py"
scp "$SCRIPT_DIR/config/replica.conf" "$REPLICA_HOST:$DEST_DIR/config/replica.conf"

ssh "$REPLICA_HOST" "
  rm -f *.rdb
  rm -f $DEST_DIR/logs/*.log
  sed -i \"s|logfile .*|logfile \\\"$DEST_DIR/logs/redis-replica.log\\\"|\" \"$DEST_DIR/config/replica.conf\"
  redis-server \"$DEST_DIR/config/replica.conf\"
"

SENTINEL_HOST="10.10.1.4"

# start sentinels on 26479, 26480, 26481

ssh "$SENTINEL_HOST" "
	if [ ! -d \"$DEST_DIR/config\" ]; then
	  mkdir -p \"$DEST_DIR/config\"
	fi
	if [ ! -d \"$DEST_DIR/logs\" ]; then
	  mkdir -p \"$DEST_DIR/logs\"
	fi
"

scp "$SCRIPT_DIR/sentinel-monitor.py" "$SENTINEL_HOST:$DEST_DIR/sentinel-monitor.py"
scp "$SCRIPT_DIR/config/sentinel.conf" "$SENTINEL_HOST:$DEST_DIR/config/sentinel.conf"

for port in 26479 26480 26481; do
  ssh "$SENTINEL_HOST" "
	cp \"$DEST_DIR/config/sentinel.conf\" \"$DEST_DIR/config/sentinel-$port.conf\"
	sed -i \"1i\port $port\" \"$DEST_DIR/config/sentinel-$port.conf\"
	redis-sentinel \"$DEST_DIR/config/sentinel-$port.conf\" > \"$DEST_DIR/logs/redis-sentinel-$port.log\" 2>&1 &
  "
done