#!/bin/bash

set -x

Dir=$(dirname $0)
LOG_PREFIX=$1

$Dir/stop-all.sh

if [ "$LOG_PREFIX" != "none" ]; then
  GLOG_stderrthreshold=0 GLOG_logtostderr=1 $Dir/../../src/memcached/memcached -m 16384 -t 8 -I 32m -c 4096 > $Dir/logs/$LOG_PREFIX.memcached.log 2>&1 &
else
  GLOG_stderrthreshold=0 GLOG_logtostderr=1 $Dir/../../src/memcached/memcached -m 16384 -t 8 -I 32m -c 4096 &
fi
