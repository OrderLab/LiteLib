#!/bin/bash

set -x

Dir="/home/rishika/cascade/tests/Memcached/tests/ycsb"
LOG_PREFIX=$1

$Dir/stop-all.sh

if [ "$LOG_PREFIX" != "none" ]; then
  GLOG_stderrthreshold=0 GLOG_logtostderr=1 $Dir/../../src/memcached/memcached -u rishika -m 16384 -t 8 -I 32m -c 4096 > $Dir/logs/$LOG_PREFIX.memcached.log 2>&1 &

  sleep 2

  GLOG_stderrthreshold=0 GLOG_logtostderr=1 $Dir/../../src/lite-version-ascii-ebpf/build/LiteMemcached -t 1 -s 20480 > $Dir/logs/$LOG_PREFIX.lite_memcached.log 2>&1 &
else
  GLOG_stderrthreshold=0 GLOG_logtostderr=1 $Dir/../../src/memcached/memcached -u rishika -m 16384 -t 8 -I 32m -c 4096&

  sleep 2

  GLOG_stderrthreshold=0 GLOG_logtostderr=1 $Dir/../../src/lite-version-ascii-ebpf/build/LiteMemcached -t 1 -s 20480 &
fi
