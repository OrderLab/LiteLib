#!/bin/bash

Dir=$(dirname $0)

$Dir/stop-all.sh

GLOG_stderrthreshold=0 GLOG_logtostderr=1 /workspace/tests/Memcached/src/lite-version/build/LiteMemcached -t 2 -s 1024 &
memcached -m 16384 -t 8 -I 32m -c 4096 -u root -s /tmp/memcached.sock &