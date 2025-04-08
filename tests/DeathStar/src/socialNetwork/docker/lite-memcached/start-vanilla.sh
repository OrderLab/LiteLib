#!/bin/bash

set -x

Dir=$(dirname $0)
LOG_PREFIX=$1

$Dir/stop-all.sh

memcached -m 16384 -t 8 -I 32m -c 4096 -u root > $Dir/logs/$LOG_PREFIX.memcached.log 2>&1 &
