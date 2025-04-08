#!/bin/bash

set -x

Dir=$(dirname $0)
LOG_PREFIX=$1

$Dir/stop-all.sh

if [ "$LOG_PREFIX" != "none" ]; then
  GLOG_stderrthreshold=0 GLOG_logtostderr=1 LD_LIBRARY_PATH=/workspace/tests/Memcached/src/memcached/vendor/LiteSys/build:$LD_LIBRARY_PATH /workspace/tests/Memcached/src/memcached/memcached -m 16384 -t 1 -I 32m -c 4096 -u root > $Dir/logs/$LOG_PREFIX.memcached.log 2>&1 &

  sleep 2

  GLOG_stderrthreshold=0 GLOG_logtostderr=1 /workspace/tests/Memcached/src/lite-version-ascii-embedded/build/LiteMemcached -t 8 -s 20480 > $Dir/logs/$LOG_PREFIX.lite_memcached.log 2>&1 &
else
  GLOG_stderrthreshold=0 GLOG_logtostderr=1 LD_LIBRARY_PATH=/workspace/tests/Memcached/src/memcached/vendor/LiteSys/build:$LD_LIBRARY_PATH /workspace/tests/Memcached/src/memcached/memcached -m 16384 -t 1 -I 32m -c 4096 -u root &

  sleep 2

  GLOG_stderrthreshold=0 GLOG_logtostderr=1 /workspace/tests/Memcached/src/lite-version-ascii-embedded/build/LiteMemcached -t 8 -s 20480 &
fi
