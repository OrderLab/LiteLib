#!/bin/bash

set -x

TYPE=$1
DEFCON_CONFIG=${2:-0} # 1 mcrouter readonly, 2 post-storage-service readonly, 3 load shedding
LOAD_SHEDDING_RATE=${3:-1500} # max number of requests per second
CRASH=${3:-20}
# Allow the caller to name the run, so an orchestrating script can correlate the
# client log with the per-component logs this run produces.
LOG_PREFIX=${LOG_PREFIX:-${TYPE}_$(date '+%Y%m%d_%H%M%S')}
# Set NO_CRASH=1 to run the identical workload without injecting the failure.
# This is how the no-crash baseline used by Figure 2 is collected.
NO_CRASH=${NO_CRASH:-0}
# CPU budget for each Memcached instance, in cgroup v2 "quota period" form.
# This sets the *operating point* of the experiment: the effect the figure
# shows is that the surviving instance cannot absorb the ~65% extra load the
# failover sends it.  If the instances are given more CPU than the workload
# needs, the failover is absorbed and no cascade appears, so this value has to
# be matched to the machine (see ae_motivation_calibrate.sh).
MEMCACHED_CPU_MAX=${MEMCACHED_CPU_MAX:-"50000 100000"}
# Offered load, in requests/second, for the warm-up and the measured workload.
# Together with MEMCACHED_CPU_MAX this fixes the operating point.  The load at
# which the surviving instance saturates differs measurably between machines of
# the same type, so it has to be calibrated per cluster; see
# ae_motivation_calibrate.sh.
WARMUP_RATE=${WARMUP_RATE:-2700}
WORKLOAD_RATE=${WORKLOAD_RATE:-2700}
WORKLOAD_CONNS=${WORKLOAD_CONNS:-512}
WORKLOAD_THREADS=${WORKLOAD_THREADS:-80}
LITE_THREADS=${LITE_THREADS:-8}
LITE_CACHE_SIZE=${LITE_CACHE_SIZE:-${LITE_CACHE_ITEMS:-201326592}}
WORKLOAD_SEED=${WORKLOAD_SEED:-20250409}
DeathStarDir=$(cd "$(dirname "$0")/.." && pwd)

function wait_for_memcached_path() {
    local deadline=$((SECONDS + 180))
    local stable=0

    echo "Waiting for both Memcached backends and Mcrouter to become stable..."
    while [ "$SECONDS" -lt "$deadline" ]; do
        local cid
        cid=$(docker ps -q \
          --filter label=com.docker.swarm.service.name=socialnetwork_post-storage-memcached |
          head -1)

        if [ -n "$cid" ] &&
           docker exec post-storage-memcached-1 \
             pgrep -f '(^|/)(memcached|memcached-vanilla)( |$)' >/dev/null 2>&1 &&
           docker exec post-storage-memcached-2 \
             pgrep -f '(^|/)(memcached|memcached-vanilla)( |$)' >/dev/null 2>&1; then
            local failover
            failover=$(
              docker exec "$cid" \
                cat /var/mcrouter/stats/libmcrouter.mcrouter.11211.stats \
                2>/dev/null |
              python3 -c '
import json, sys
try:
    d = json.load(sys.stdin)
    print(float(d.get("libmcrouter.mcrouter.11211.failover_all", -1)))
except Exception:
    print(-1)
'
            )
            if [ "$failover" = "0.0" ]; then
                stable=$((stable + 1))
                if [ "$stable" -ge 5 ]; then
                    echo "Memcached/Mcrouter path stable (failover_all=0 for 10s)."
                    return 0
                fi
            else
                stable=0
            fi
        else
            stable=0
        fi
        sleep 2
    done

    echo "ERROR: Memcached/Mcrouter path did not stabilize within 180s" >&2
    return 1
}

function reset_cache_state() {
    echo "Resetting both post-storage Memcached caches to an empty state..."
    for id in 1 2; do
        docker exec "post-storage-memcached-$id" \
          /workspace/tests/DeathStar/src/socialNetwork/docker/lite-memcached/stop-all.sh ||
          return 1
        docker exec "post-storage-memcached-$id" sh -c \
          'test ! -e /tmp/memcached.sock &&
           test ! -e /tmp/lite_memcached &&
           test ! -e /dev/shm/lite_shared_memory' ||
          return 1
    done
    echo "Both target caches are empty; the 60s warm-up will repopulate them."
}

function start_memcached() {
    reset_cache_state || return 1
    ssh node0 "docker exec post-storage-mongodb cgcreate -g cpu:/deathstar_cpulimited"
    docker exec post-storage-memcached-1 cgcreate -g cpu:/deathstar_cpulimited_1
    docker exec post-storage-memcached-1 cgset -r cpu.max="$MEMCACHED_CPU_MAX" deathstar_cpulimited_1
    docker exec post-storage-memcached-2 cgcreate -g cpu:/deathstar_cpulimited_2
    docker exec post-storage-memcached-2 cgset -r cpu.max="$MEMCACHED_CPU_MAX" deathstar_cpulimited_2
    if [ "$TYPE" == "vanilla" ]; then
        docker exec post-storage-memcached-1 /workspace/tests/DeathStar/src/socialNetwork/docker/lite-memcached/start-vanilla-with-cgroup.sh 1 $LOG_PREFIX
        docker exec post-storage-memcached-2 /workspace/tests/DeathStar/src/socialNetwork/docker/lite-memcached/start-vanilla-with-cgroup.sh 2 $LOG_PREFIX
    elif [ "$TYPE" == "litesys" ]; then
        docker exec \
          -e LITE_THREADS="$LITE_THREADS" \
          -e LITE_CACHE_SIZE="$LITE_CACHE_SIZE" \
          post-storage-memcached-1 \
          /workspace/tests/DeathStar/src/socialNetwork/docker/lite-memcached/start-litesys-with-cgroup.sh 1 none
        sleep 3 # restart LiteSys again to prevent some port/shm reuse issues
        docker exec \
          -e LITE_THREADS="$LITE_THREADS" \
          -e LITE_CACHE_SIZE="$LITE_CACHE_SIZE" \
          post-storage-memcached-1 \
          /workspace/tests/DeathStar/src/socialNetwork/docker/lite-memcached/start-litesys-with-cgroup.sh 1 $LOG_PREFIX
        docker exec post-storage-memcached-2 /workspace/tests/DeathStar/src/socialNetwork/docker/lite-memcached/start-vanilla-with-cgroup.sh 2 $LOG_PREFIX
    fi
    ssh node1 "docker service update --force socialnetwork_post-storage-memcached"
    wait_for_memcached_path
}

# 200MB for memcached
function warmup_memcached() {
    # Populate the same cache working set used by the measured workload.
    WORKLOAD_SEED=${WORKLOAD_SEED} rps=${WARMUP_RATE} \
      ../src/wrk2/wrk -D exp -t ${WORKLOAD_THREADS} -c ${WORKLOAD_CONNS} -d 60 -L -s ../src/socialNetwork/wrk2/scripts/social-network/mixed-workload.lua http://node1:8080 -R ${WARMUP_RATE}
}

function crash_memcached() {
    if [ "$NO_CRASH" == "1" ]; then
        echo "NO_CRASH=1: skipping failure injection (no-crash baseline)"
        return 0
    fi
    sleep $CRASH
    if [ "$TYPE" == "litesys" ]; then
        # Switch LiteMemcached to emergency mode before killing its full
        # memcached backend.
        docker exec post-storage-memcached-1 \
          /workspace/tests/DeathStar/src/socialNetwork/docker/lite-memcached/crash.sh
    else
        # Vanilla has no LiteMemcached handover; just kill the replica.
        docker exec post-storage-memcached-1 sh -c \
          "pgrep -f '(^|/)(memcached|memcached-vanilla)( |$)' | xargs -r kill -15"
    fi
}

function logging() {
    docker exec $(docker ps -q --filter label=com.docker.swarm.service.name=socialnetwork_post-storage-memcached) /workspace/docker/lite-memcached/get_mcrouter_stat.sh $LOG_PREFIX.mcrouter.log 90 &
    docker exec post-storage-memcached-1 python3 /workspace/tests/DeathStar/src/socialNetwork/docker/lite-memcached/monitor.py 90 $LOG_PREFIX.memcached.monitor.1.log &
    docker exec post-storage-memcached-2 python3 /workspace/tests/DeathStar/src/socialNetwork/docker/lite-memcached/monitor.py 90 $LOG_PREFIX.memcached.monitor.2.log
}

function run_workload() {
    WORKLOAD_SEED=${WORKLOAD_SEED} rps=${WORKLOAD_RATE} \
      ../src/wrk2/wrk -D exp -t ${WORKLOAD_THREADS} -c ${WORKLOAD_CONNS} -d 90 -L -s ../src/socialNetwork/wrk2/scripts/social-network/mixed-workload.lua http://node1:8080 -R ${WORKLOAD_RATE}
}

function mcrouter_readonly() {
    sleep $CRASH
    if [ "$DEFCON_CONFIG" == "1" ]; then
        mv ../src/socialNetwork/config/mcrouter.json ../src/socialNetwork/config/mcrouter.json.bak
        cp ../src/socialNetwork/config/mcrouter.readonly.json ../src/socialNetwork/config/mcrouter.json
    fi
}

function restore_mcrouter() {
    if [ "$DEFCON_CONFIG" == "1" ]; then
        mv ../src/socialNetwork/config/mcrouter.json.bak ../src/socialNetwork/config/mcrouter.json
    fi
}

function post_storage_service_readonly() {
    sleep $CRASH
    if [ "$DEFCON_CONFIG" == "2" ]; then
        ssh node2 "docker exec \$(docker ps -q -f name=socialnetwork_post-storage-service) kill -USR1 1"
    fi
}

function restore_post_storage_service() {
    if [ "$DEFCON_CONFIG" == "2" ]; then
        ssh node2 "docker exec \$(docker ps -q -f name=socialnetwork_post-storage-service) kill -USR1 1"
    fi
}

function load_shedding() {
    sleep $CRASH
    if [ "$DEFCON_CONFIG" == "3" ]; then
        ssh node1 "
            cp $DeathStarDir/src/socialNetwork/nginx-web-server/conf/nginx.conf $DeathStarDir/src/socialNetwork/nginx-web-server/conf/nginx.conf.bak &&
            cp $DeathStarDir/src/socialNetwork/nginx-web-server/conf/nginx.load_shedding.conf $DeathStarDir/src/socialNetwork/nginx-web-server/conf/nginx.new_load_shedding.conf &&
            sed -i 's/\(limit_req_zone.*rate=\)[0-9]\+r\/s/\1'"${LOAD_SHEDDING_RATE}"'r\/s/' $DeathStarDir/src/socialNetwork/nginx-web-server/conf/nginx.new_load_shedding.conf &&
            cat $DeathStarDir/src/socialNetwork/nginx-web-server/conf/nginx.new_load_shedding.conf > $DeathStarDir/src/socialNetwork/nginx-web-server/conf/nginx.conf &&
            docker exec \$(docker ps -q -f name=socialnetwork_nginx-web-server) sh -c \"cat /usr/local/openresty/nginx/conf/nginx.conf && nginx -s reload\"
            rm -f $DeathStarDir/src/socialNetwork/nginx-web-server/conf/nginx.new_load_shedding.conf
        "
    fi
}

function restore_load_shedding() {
    if [ "$DEFCON_CONFIG" == "3" ]; then
        ssh node1 "
            cat $DeathStarDir/src/socialNetwork/nginx-web-server/conf/nginx.conf.bak > $DeathStarDir/src/socialNetwork/nginx-web-server/conf/nginx.conf &&
            docker exec \$(docker ps -q -f name=socialnetwork_nginx-web-server) sh -c \"nginx -s reload\"
        "
    fi
}

# Run the experiment
start_memcached || exit 1
warmup_memcached || exit 1
sleep 5
wait_for_memcached_path || exit 1
crash_memcached & crash_pid=$!
mcrouter_readonly & mcrouter_pid=$!
post_storage_service_readonly & service_pid=$!
load_shedding & shedding_pid=$!
logging &
logging_pid=$!
run_workload
workload_rc=$?

# A failed LiteMemcached handover used to be invisible because crash.sh ran in
# the background and its status was never checked.  Do not accept such a run.
wait "$crash_pid"; crash_rc=$?
wait "$mcrouter_pid"; mcrouter_rc=$?
wait "$service_pid"; service_rc=$?
wait "$shedding_pid"; shedding_rc=$?
wait "$logging_pid"; logging_rc=$?

restore_mcrouter
restore_post_storage_service
restore_load_shedding

if [ "$workload_rc" -ne 0 ] || [ "$crash_rc" -ne 0 ] ||
   [ "$mcrouter_rc" -ne 0 ] || [ "$service_rc" -ne 0 ] ||
   [ "$shedding_rc" -ne 0 ] || [ "$logging_rc" -ne 0 ]; then
    echo "ERROR: experiment component failed:" >&2
    echo "  workload=$workload_rc crash=$crash_rc mcrouter=$mcrouter_rc" >&2
    echo "  service=$service_rc shedding=$shedding_rc logging=$logging_rc" >&2
    exit 1
fi