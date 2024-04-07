#!/bin/sh
cp /workspace/scripts/.ssh ~ -r
service ssh start

cd /workspace/redis-leveldb
make -j
make test

# /opt/redis-leveldb/redis-leveldb -P 6379
tail -f /dev/null