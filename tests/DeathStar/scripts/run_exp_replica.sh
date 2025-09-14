#!/bin/bash

set -x

TYPE=$1
DEFCON_CONFIG=${2:-0} # 1 mcrouter readonly, 2 post-storage-service readonly
CRASH=${3:-20}
LOG_PREFIX=${TYPE}_$(date '+%Y%m%d_%H%M%S')

function start_memcached() {
    ssh node0 "docker exec post-storage-mongodb cgcreate -g cpu:/deathstar_cpulimited"
    docker exec post-storage-memcached-1 cgcreate -g cpu:/deathstar_cpulimited_1
    docker exec post-storage-memcached-1 cgset -r cpu.max="100000 100000" deathstar_cpulimited_1
    docker exec post-storage-memcached-2 cgcreate -g cpu:/deathstar_cpulimited_2
    docker exec post-storage-memcached-2 cgset -r cpu.max="100000 100000" deathstar_cpulimited_2
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
    ../src/wrk2/wrk -D exp -t 80 -c 512 -d 60 -L -s ../src/socialNetwork/wrk2/scripts/social-network/read-home-timeline.lua http://node1:8080/wrk2-api/home-timeline/read -R 3000
}

function crash_memcached() {
    sleep $CRASH
    docker exec post-storage-memcached-1 /workspace/tests/DeathStar/src/socialNetwork/docker/lite-memcached/crash.sh
}

function logging() {
    docker exec $(docker ps -q --filter label=com.docker.swarm.service.name=socialnetwork_post-storage-memcached) /workspace/docker/lite-memcached/get_mcrouter_stat.sh $LOG_PREFIX.mcrouter.log 90 &
    docker exec post-storage-memcached-1 python3 /workspace/tests/DeathStar/src/socialNetwork/docker/lite-memcached/monitor.py 90 $LOG_PREFIX.memcached.monitor.1.log &
    docker exec post-storage-memcached-2 python3 /workspace/tests/DeathStar/src/socialNetwork/docker/lite-memcached/monitor.py 90 $LOG_PREFIX.memcached.monitor.2.log
}

function run_workload() {
    ../src/wrk2/wrk -D exp -t 80 -c 512 -d 90 -L -s ../src/socialNetwork/wrk2/scripts/social-network/mixed-workload.lua http://node1:8080 -R 2500
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

# Run the experiment
start_memcached
warmup_memcached
sleep 5
crash_memcached &
mcrouter_readonly &
post_storage_service_readonly &
logging &
run_workload
restore_mcrouter
restore_post_storage_service