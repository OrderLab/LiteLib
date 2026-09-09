#!/bin/bash

set -x

Dir=$(dirname $0)
id=$1
LOG_PREFIX=$2
VANILLA_MEMCACHED=${VANILLA_MEMCACHED:-/workspace/tests/Memcached/src/memcached-vanilla}
MEMCACHED_HASHPOWER=${MEMCACHED_HASHPOWER:-22}

$Dir/stop-all.sh

if [ ! -x "$VANILLA_MEMCACHED" ]; then
  echo "ERROR: vanilla memcached not found at $VANILLA_MEMCACHED" >&2
  echo "       Run ae_motivation_setup.sh build first." >&2
  exit 1
fi

CGROUP_ID=$id
LOG_SUFFIX=".$id"
if [ "$id" == "3" ]; then
  CGROUP_ID=1
  LOG_SUFFIX=""
fi
COMMAND=(
  cgexec -g "cpu:deathstar_cpulimited_$CGROUP_ID"
  "$VANILLA_MEMCACHED" -m 16384 -t 8 -I 32m -c 4096 -u root
  -o "hashpower=$MEMCACHED_HASHPOWER"
)
"${COMMAND[@]}" >"$Dir/logs/$LOG_PREFIX.memcached$LOG_SUFFIX.log" 2>&1 &
