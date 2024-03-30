#!/bin/sh
cd /workspace/redis-leveldb
apt-get update -qq -y
apt-get install libsnappy-dev libev-dev libgmp-dev cpanminus perl build-essential gdb vim procps git -y
cpanm --quiet --notest --skip-satisfied --force Redis
make -j
make test
tail -f /dev/null