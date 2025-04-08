#!/bin/bash

set -x

TYPE=$1
CRASH=${2:-20}
LOG_PREFIX=${TYPE}_$(date '+%Y%m%d_%H%M%S')
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)

function start_memcached() {
    ssh node0 "docker exec post-storage-mongodb cgcreate -g cpu:/deathstar_cpulimited"
    ssh node0 "docker exec post-storage-mongodb cgset -r cpu.max=\"100000 100000\" deathstar_cpulimited"
    docker exec post-storage-memcached cgcreate -g cpu:/deathstar_cpulimited_1
    docker exec post-storage-memcached cgset -r cpu.max="100000 100000" deathstar_cpulimited_1
    if [ "$TYPE" == "litesys" ]; then
        docker exec post-storage-memcached /workspace/tests/DeathStar/src/socialNetwork/docker/lite-memcached/start-litesys-with-cgroup.sh 3 none
        sleep 3 # restart LiteSys again to prevent some port/shm reuse issues
        docker exec post-storage-memcached /workspace/tests/DeathStar/src/socialNetwork/docker/lite-memcached/start-litesys-with-cgroup.sh 3 $LOG_PREFIX
    else
        docker exec post-storage-memcached /workspace/tests/DeathStar/src/socialNetwork/docker/lite-memcached/start-vanilla-with-cgroup.sh 3 $LOG_PREFIX
    fi

    if [ "$TYPE" == "vanilla" ]; then
        ssh node2 "sed -i 's/\"offline_memcached_patch\": true/\"offline_memcached_patch\": false/' $SCRIPT_DIR/../src/socialNetwork/config/service-config.json"
    else
        ssh node2 "sed -i 's/\"offline_memcached_patch\": false/\"offline_memcached_patch\": true/' $SCRIPT_DIR/../src/socialNetwork/config/service-config.json"
    fi

    ssh node1 "docker service update --force socialnetwork_post-storage-service"
}

# 200MB for memcached
function warmup_memcached() {
    ../src/wrk2/wrk -D exp -t 80 -c 512 -d 60 -L -s ../src/socialNetwork/wrk2/scripts/social-network/read-home-timeline.lua http://node1:8080/wrk2-api/home-timeline/read -R 3000
}

function crash_memcached() {
    sleep $CRASH
    docker exec post-storage-memcached /workspace/tests/DeathStar/src/socialNetwork/docker/lite-memcached/crash.sh
}

function logging() {
    docker exec post-storage-memcached python3 /workspace/tests/DeathStar/src/socialNetwork/docker/lite-memcached/monitor.py 90 $LOG_PREFIX.memcached.monitor.log
}

function run_workload() {
    ../src/wrk2/wrk -D exp -t 80 -c 512 -d 90 -L -s ../src/socialNetwork/wrk2/scripts/social-network/mixed-workload.lua http://node1:8080 -R 1500
}

# Run the experiment
start_memcached
warmup_memcached
sleep 5
crash_memcached &
logging &
run_workload