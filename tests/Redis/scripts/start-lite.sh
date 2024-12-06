#!/bin/bash

SCRIPT_DIR=$(dirname "$0")

# Start the lite Redis at: ../src/lite-version/build/redis-lite
# Check if the redis-lite has been built, if not, build it
if [ ! -f "$SCRIPT_DIR/../src/lite-version/build/redis-lite" ]; then
  echo "redis-lite has not been built. Building redis-lite..."
  if [ -d "$SCRIPT_DIR/../src/lite-version/build" ]; then
    rm -rf "$SCRIPT_DIR/../src/lite-version/build"
  fi
  if [ ! -L "$SCRIPT_DIR/../src/lite-version/Lite" ]; then
    ln -s "$SCRIPT_DIR/../../../../src" "$SCRIPT_DIR/../src/lite-version/Lite"
    echo "Created soft link to src directory: $SCRIPT_DIR/../../../src"
    echo "Created soft link to src directory: $SCRIPT_DIR/../src/lite-version/Lite"
  fi
  mkdir "$SCRIPT_DIR/../src/lite-version/build"
  cd "$SCRIPT_DIR/../src/lite-version/build" && cmake .. && make
fi

if [ ! -d "$SCRIPT_DIR/logs" ]; then
  mkdir "$SCRIPT_DIR/logs"
fi

"$SCRIPT_DIR/../src/lite-version/build/redis-lite" > "$SCRIPT_DIR/logs/redis-lite.log" 2>&1 &
echo "Redis Lite started"