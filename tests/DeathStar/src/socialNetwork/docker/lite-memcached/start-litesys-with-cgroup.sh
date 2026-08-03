#!/bin/bash

set -x

Dir=$(dirname $0)
id=$1
LOG_PREFIX=$2

# Size of LiteMemcached's shared-memory segment, in BYTES.
#
# Despite the "-s, --size  Max number of items in cache" help text, this value
# is used directly as the size of the boost::interprocess managed segment.  The
# old hard-coded 20480 therefore asked for a 20 KB segment, which aborts at
# start-up with "exception: boost::interprocess::bad_alloc" -- and because the
# failure is silent from the experiment's point of view, the LiteLib arm then
# degrades exactly like the vanilla one.
#
# Must fit inside the container's --shm-size (see swarm_helper_replica.sh).
LITE_SHM_BYTES=${LITE_SHM_BYTES:-2147483648}

$Dir/stop-all.sh

if [ "$id" == "3" ]; then
  if [ "$LOG_PREFIX" != "none" ]; then
    GLOG_stderrthreshold=0 GLOG_logtostderr=1 /workspace/tests/Memcached/src/lite-version-ascii-embedded/build/LiteMemcached -t 8 -s ${LITE_SHM_BYTES} > $Dir/logs/$LOG_PREFIX.lite_memcached.log 2>&1 &

    sleep 2

    cgexec -g cpu:deathstar_cpulimited_1 memcached -m 16384 -t 8 -I 32m -c 4096 -u root -s /tmp/memcached.sock > $Dir/logs/$LOG_PREFIX.memcached.log 2>&1 &
  else
    GLOG_stderrthreshold=0 GLOG_logtostderr=1 /workspace/tests/Memcached/src/lite-version-ascii-embedded/build/LiteMemcached -t 8 -s ${LITE_SHM_BYTES} &

    sleep 2

    cgexec -g cpu:deathstar_cpulimited_1 memcached -m 16384 -t 8 -I 32m -c 4096 -u root -s /tmp/memcached.sock &
  fi
else
  if [ "$LOG_PREFIX" != "none" ]; then
    GLOG_stderrthreshold=0 GLOG_logtostderr=1 LD_LIBRARY_PATH=/workspace/tests/Memcached/src/memcached/vendor/LiteSys/build:$LD_LIBRARY_PATH cgexec -g cpu:deathstar_cpulimited_$id /workspace/tests/Memcached/src/memcached/memcached -m 16384 -t 1 -I 32m -c 4096 -u root > $Dir/logs/$LOG_PREFIX.memcached.$id.log 2>&1 &

    sleep 2

    GLOG_stderrthreshold=0 GLOG_logtostderr=1 /workspace/tests/Memcached/src/lite-version-ascii-embedded/build/LiteMemcached -t 8 -s ${LITE_SHM_BYTES} > $Dir/logs/$LOG_PREFIX.lite_memcached.$id.log 2>&1 &
  else
    GLOG_stderrthreshold=0 GLOG_logtostderr=1 LD_LIBRARY_PATH=/workspace/tests/Memcached/src/memcached/vendor/LiteSys/build:$LD_LIBRARY_PATH cgexec -g cpu:deathstar_cpulimited_$id /workspace/tests/Memcached/src/memcached/memcached -m 16384 -t 1 -I 32m -c 4096 -u root &

    sleep 2

    GLOG_stderrthreshold=0 GLOG_logtostderr=1 /workspace/tests/Memcached/src/lite-version-ascii-embedded/build/LiteMemcached -t 8 -s ${LITE_SHM_BYTES} &
  fi
fi
