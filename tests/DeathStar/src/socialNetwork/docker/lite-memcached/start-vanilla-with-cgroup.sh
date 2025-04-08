#!/bin/bash

set -x

Dir=$(dirname $0)
id=$1
LOG_PREFIX=$2

$Dir/stop-all.sh

if [ "$id" == "3" ]; then
  cgexec -g cpu:deathstar_cpulimited_1 memcached -m 16384 -t 8 -I 32m -c 4096 -u root > $Dir/logs/$LOG_PREFIX.memcached.log 2>&1 &
else
  cgexec -g cpu:deathstar_cpulimited_$id memcached -m 16384 -t 8 -I 32m -c 4096 -u root > $Dir/logs/$LOG_PREFIX.memcached.$id.log 2>&1 &
fi
