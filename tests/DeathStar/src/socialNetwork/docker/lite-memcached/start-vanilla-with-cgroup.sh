#!/bin/bash

set -x

Dir=$(dirname $0)
id=$1
LOG_PREFIX=$2
VANILLA_MEMCACHED=${VANILLA_MEMCACHED:-/workspace/tests/Memcached/src/memcached-vanilla}

$Dir/stop-all.sh

if [ ! -x "$VANILLA_MEMCACHED" ]; then
  echo "ERROR: vanilla memcached not found at $VANILLA_MEMCACHED" >&2
  echo "       Run ae_motivation_setup.sh build first." >&2
  exit 1
fi

if [ "$id" == "3" ]; then
  cgexec -g cpu:deathstar_cpulimited_1 "$VANILLA_MEMCACHED" -m 16384 -t 8 -I 32m -c 4096 -u root > $Dir/logs/$LOG_PREFIX.memcached.log 2>&1 &
else
  cgexec -g cpu:deathstar_cpulimited_$id "$VANILLA_MEMCACHED" -m 16384 -t 8 -I 32m -c 4096 -u root > $Dir/logs/$LOG_PREFIX.memcached.$id.log 2>&1 &
fi
