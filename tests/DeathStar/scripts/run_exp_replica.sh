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
MEMCACHED_CPU_MAX=${MEMCACHED_CPU_MAX:-"100000 100000"}
# Offered load, in requests/second, for the warm-up and the measured workload.
# Together with MEMCACHED_CPU_MAX this fixes the operating point.  The load at
# which the surviving instance saturates differs measurably between machines of
# the same type, so it has to be calibrated per cluster; see
# ae_motivation_calibrate.sh.
WARMUP_RATE=${WARMUP_RATE:-3000}
WORKLOAD_RATE=${WORKLOAD_RATE:-2500}
WORKLOAD_CONNS=${WORKLOAD_CONNS:-512}
WORKLOAD_THREADS=${WORKLOAD_THREADS:-80}
DeathStarDir=$(cd "$(dirname "$0")/.." && pwd)

function start_memcached() {
    ssh node0 "docker exec post-storage-mongodb cgcreate -g cpu:/deathstar_cpulimited"
    docker exec post-storage-memcached-1 cgcreate -g cpu:/deathstar_cpulimited_1
    docker exec post-storage-memcached-1 cgset -r cpu.max="$MEMCACHED_CPU_MAX" deathstar_cpulimited_1
    docker exec post-storage-memcached-2 cgcreate -g cpu:/deathstar_cpulimited_2
    docker exec post-storage-memcached-2 cgset -r cpu.max="$MEMCACHED_CPU_MAX" deathstar_cpulimited_2
    if [ "$TYPE" == "vanilla" ]; then
        docker exec post-storage-memcached-1 /workspace/tests/DeathStar/src/socialNetwork/docker/lite-memcached/start-vanilla-with-cgroup.sh 1 $LOG_PREFIX
        docker exec post-storage-memcached-2 /workspace/tests/DeathStar/src/socialNetwork/docker/lite-memcached/start-vanilla-with-cgroup.sh 2 $LOG_PREFIX
    elif [ "$TYPE" == "litesys" ]; then
        docker exec post-storage-memcached-1 /workspace/tests/DeathStar/src/socialNetwork/docker/lite-memcached/start-litesys-with-cgroup.sh 1 none
        sleep 3 # restart LiteSys again to prevent some port/shm reuse issues
        docker exec post-storage-memcached-1 /workspace/tests/DeathStar/src/socialNetwork/docker/lite-memcached/start-litesys-with-cgroup.sh 1 $LOG_PREFIX
        docker exec post-storage-memcached-2 /workspace/tests/DeathStar/src/socialNetwork/docker/lite-memcached/start-vanilla-with-cgroup.sh 2 $LOG_PREFIX
    fi
    ssh node1 "docker service update --force socialnetwork_post-storage-memcached"
}

# 200MB for memcached
function warmup_memcached() {
    ../src/wrk2/wrk -D exp -t ${WORKLOAD_THREADS} -c ${WORKLOAD_CONNS} -d 60 -L -s ../src/socialNetwork/wrk2/scripts/social-network/read-home-timeline.lua http://node1:8080/wrk2-api/home-timeline/read -R ${WARMUP_RATE}
}

function crash_memcached() {
    if [ "$NO_CRASH" == "1" ]; then
        echo "NO_CRASH=1: skipping failure injection (no-crash baseline)"
        return 0
    fi
    sleep $CRASH
    docker exec post-storage-memcached-1 /workspace/tests/DeathStar/src/socialNetwork/docker/lite-memcached/crash.sh
}

function logging() {
    docker exec $(docker ps -q --filter label=com.docker.swarm.service.name=socialnetwork_post-storage-memcached) /workspace/docker/lite-memcached/get_mcrouter_stat.sh $LOG_PREFIX.mcrouter.log 90 &
    docker exec post-storage-memcached-1 python3 /workspace/tests/DeathStar/src/socialNetwork/docker/lite-memcached/monitor.py 90 $LOG_PREFIX.memcached.monitor.1.log &
    docker exec post-storage-memcached-2 python3 /workspace/tests/DeathStar/src/socialNetwork/docker/lite-memcached/monitor.py 90 $LOG_PREFIX.memcached.monitor.2.log
}

function run_workload() {
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
start_memcached
warmup_memcached
sleep 5
crash_memcached &
mcrouter_readonly &
post_storage_service_readonly &
load_shedding &
logging &
run_workload
restore_mcrouter
restore_post_storage_service
restore_load_shedding