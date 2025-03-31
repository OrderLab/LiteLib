#!/bin/bash

set -x

Dir=$(dirname $0)
id=$1

$Dir/stop-all.sh

GLOG_stderrthreshold=0 GLOG_logtostderr=1 /workspace/tests/Memcached/src/lite-version-ascii/build/LiteMemcached -t 8 -s 10240 &
cgexec -g cpu:deathstar_cpulimited_$id memcached -m 16384 -t 8 -I 32m -c 4096 -u root -s /tmp/memcached.sock &