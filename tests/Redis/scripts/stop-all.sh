#!/bin/bash

SCRIPT_DIR=$(dirname "$0")
REPLICA_HOST="10.10.1.3"
SENTINEL_HOST="10.10.1.4"

kill_process_by_port() {
  local port=$1
  local pids=$(lsof -t -i:$port)
  if [ -n "$pids" ]; then
    kill $pids
    echo "Killed processes on port $port: $pids"
  else
    echo "No processes found on port $port"
  fi
}

pkill -f "redis-lite"
echo "Redis Lite stopped"

kill_process_by_port 16379

ssh "$REPLICA_HOST" "$(typeset -f kill_process_by_port); kill_process_by_port 16379"

for port in 26479 26480 26481; do
  ssh "$SENTINEL_HOST" "$(typeset -f kill_process_by_port); kill_process_by_port $port"
done