#!/bin/bash

set -x

Dir=$(dirname $0)
id=$1
LOG_PREFIX=$2

# Figure 1/2 use the original, non-embedded architecture:
#
#   client -> LiteMemcached -> vanilla memcached (Unix socket)
#
# LiteMemcached is deliberately outside the CPU cgroup.  Only the full
# memcached process is constrained; after it fails, LiteMemcached must retain
# the CPU needed to serve its compact cache.
LITE_MEMCACHED=${LITE_MEMCACHED:-/workspace/tests/Memcached/src/lite-version-ascii/build/LiteMemcached}
VANILLA_MEMCACHED=${VANILLA_MEMCACHED:-/workspace/tests/Memcached/src/memcached-vanilla}
LITE_CACHE_SIZE=${LITE_CACHE_SIZE:-${LITE_CACHE_ITEMS:-201326592}}
LITE_THREADS=${LITE_THREADS:-8}
MEMCACHED_THREADS=${MEMCACHED_THREADS:-8}

$Dir/stop-all.sh

if [ ! -x "$LITE_MEMCACHED" ]; then
  echo "ERROR: non-embedded LiteMemcached not found at $LITE_MEMCACHED" >&2
  echo "       Run ae_motivation_setup.sh build first." >&2
  exit 1
fi
if [ ! -x "$VANILLA_MEMCACHED" ]; then
  echo "ERROR: vanilla memcached not found at $VANILLA_MEMCACHED" >&2
  echo "       Run ae_motivation_setup.sh build first." >&2
  exit 1
fi

if [ "$id" == "3" ]; then
  LITE_LOG_SUFFIX=""
  MEMCACHED_LOG_SUFFIX=""
  CGROUP_ID=1
else
  LITE_LOG_SUFFIX=".$id"
  MEMCACHED_LOG_SUFFIX=".$id"
  CGROUP_ID=$id
fi

if [ "$LOG_PREFIX" != "none" ]; then
  GLOG_stderrthreshold=0 GLOG_logtostderr=1 \
    "$LITE_MEMCACHED" -t "$LITE_THREADS" -s "$LITE_CACHE_SIZE" \
    > "$Dir/logs/$LOG_PREFIX.lite_memcached$LITE_LOG_SUFFIX.log" 2>&1 &
else
  GLOG_stderrthreshold=0 GLOG_logtostderr=1 \
    "$LITE_MEMCACHED" -t "$LITE_THREADS" -s "$LITE_CACHE_SIZE" &
fi

sleep 2

if ! pgrep -x LiteMemcached >/dev/null; then
  echo "ERROR: LiteMemcached exited during startup" >&2
  [ "$LOG_PREFIX" = "none" ] ||
    tail -20 "$Dir/logs/$LOG_PREFIX.lite_memcached$LITE_LOG_SUFFIX.log" >&2
  exit 1
fi

if [ "$LOG_PREFIX" != "none" ]; then
  cgexec -g cpu:deathstar_cpulimited_$CGROUP_ID \
    "$VANILLA_MEMCACHED" -m 16384 -t "$MEMCACHED_THREADS" -I 32m -c 4096 \
    -u root -s /tmp/memcached.sock \
    > "$Dir/logs/$LOG_PREFIX.memcached$MEMCACHED_LOG_SUFFIX.log" 2>&1 &
else
  cgexec -g cpu:deathstar_cpulimited_$CGROUP_ID \
    "$VANILLA_MEMCACHED" -m 16384 -t "$MEMCACHED_THREADS" -I 32m -c 4096 \
    -u root -s /tmp/memcached.sock &
fi
