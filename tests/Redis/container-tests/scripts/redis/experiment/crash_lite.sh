#!/bin/bash

is_redis_running() {
  pgrep redis-server > /dev/null 2>&1
  return $?
}

if is_redis_running; then
  pkill -9 redis-server
  echo "Existing redis-server process killed."
  while is_redis_running; do
    echo "Waiting for redis-server process to terminate..."
    sleep 1
  done
fi

/workspace/lite-version/build/Lite/lite_cli -t /tmp/lite_Redis -p 6379 -m 1

redis-server /workspace/redis_full_running.conf &

REDIS_PID=$!

check_redis() {
  redis-cli -h 172.16.0.2 -p 6379 ping | grep -q "PONG"
  return $?
}

until check_redis; do
  echo "Waiting for Redis server to start..." > /dev/null
done

/workspace/lite-version/build/Lite/lite_cli -t /tmp/lite_Redis -p 6379 -m 0

echo "Script completed successfully."

