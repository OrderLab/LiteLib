#!/bin/sh
cp /workspace/scripts/.ssh ~ -r
chmod 600 ~/.ssh/id_ed25519
service ssh start

cd /workspace/redis-leveldb
make -j
make test

pip3 install psutil redis --break-system-packages

# /opt/redis-leveldb/redis-leveldb -P 6379
tail -f /dev/null