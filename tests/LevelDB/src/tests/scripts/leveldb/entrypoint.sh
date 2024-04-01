#!/bin/sh
cp /workspace/scripts/.ssh ~ -r
service ssh start
/opt/redis-leveldb/redis-leveldb -P 6379
tail -f /dev/null