#!/bin/bash

SCRIPT_DIR=$(dirname "$0")

rm "$SCRIPT_DIR/dump_full.rdb"	

if [ ! -d "$SCRIPT_DIR/logs" ]; then
  mkdir "$SCRIPT_DIR/logs"
fi

# Start a vanilla redis server instance dump rdb to dbfilename dump_full.rdb
cp "$SCRIPT_DIR/config/vanilla.conf" "$SCRIPT_DIR/config/vanilla-running.conf"
sed -i "s|logfile .*|logfile \"$SCRIPT_DIR/logs/redis-vanilla.log\"|" "$SCRIPT_DIR/config/vanilla-running.conf"
redis-server "$SCRIPT_DIR/config/vanilla-running.conf"
echo "Redis Vanilla started"