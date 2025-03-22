#!/bin/bash

# Hardcoded memcached configuration parameters
MEMCACHED_PARAMS="-m 16384 -t 8 -I 32m -c 4096 -u root"

# Start memcached with the hardcoded parameters
echo "Starting memcached with parameters: ${MEMCACHED_PARAMS}"
memcached ${MEMCACHED_PARAMS} &
MEMCACHED_PID=$!

# Report that we've started
echo "Memcached started with PID: $MEMCACHED_PID"

# Log when memcached exits but don't restart it
monitor_memcached() {
  while kill -0 $MEMCACHED_PID 2>/dev/null; do
    sleep 5
  done

  echo "Memcached process with PID $MEMCACHED_PID has crashed or exited."
  echo "Container will continue running without restarting memcached."
}

# Start the monitoring function in the background
monitor_memcached &

# Keep the container running indefinitely without consuming CPU cycles
echo "Container will continue running even if memcached stops"
exec tail -f /dev/null