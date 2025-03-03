#!/bin/bash

set -x

SCRIPT_DIR=$(dirname "$0")
LITE_HOST="10.10.1.4"
REPLICA_HOST="10.10.1.2"
SENTINEL_HOST="10.10.1.3"

kill_process_by_port() {
  local port=$1
  local pids=$(lsof -t -i:$port)
  if [ -n "$pids" ]; then
    kill -9 $pids
    echo "Killed processes on port $port: $pids"
  else
    echo "No processes found on port $port"
  fi
}

kill_process_by_name() {
  local name=$1
  local pids=$(pgrep -f "$name")
  if [ -n "$pids" ]; then
    kill -9 $pids
    echo "Killed processes named $name: $pids"
  else
    echo "No processes found named $name"
  fi
}

kill_process_by_name "redis-lite"
echo "Redis Lite stopped"

kill_process_by_port 6479
kill_process_by_port 16379
kill_process_by_port 26379

ssh "$REPLICA_HOST" "$(typeset -f kill_process_by_port); kill_process_by_port 16379"
ssh "$REPLICA_HOST" "$(typeset -f kill_process_by_port); kill_process_by_port 26379"
ssh "$SENTINEL_HOST" "$(typeset -f kill_process_by_port); kill_process_by_port 26379"

rm /dev/shm/lite-shared-memory
rm /tmp/lite_Redis
kill_process_by_name "python -u $SCRIPT_DIR/monitor/monitor.py"
# TODO: the following commands are not working
ssh "$REPLICA_HOST" "$(typeset -f kill_process_by_name); kill_process_by_name 'python -u $SCRIPT_DIR/monitor/monitor.py'"
ssh "$SENTINEL_HOST" "$(typeset -f kill_process_by_name); kill_process_by_name 'python -u $SCRIPT_DIR/monitor/monitor.py'"
