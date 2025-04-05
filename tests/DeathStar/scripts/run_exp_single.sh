#!/bin/bash

set -x

TYPE=$1
CRASH=${2:-20}

function start_memcached() {
    if [ "$TYPE" == "litesys" ]; then
        ssh node3 "docker exec post-storage-memcached /workspace/tests/DeathStar/src/socialNetwork/docker/lite-memcached/start-litesys.sh"
        sleep 3
        ssh node3 "docker exec post-storage-memcached /workspace/tests/DeathStar/src/socialNetwork/docker/lite-memcached/start-litesys.sh"
    else
        ssh node3 "docker exec post-storage-memcached /workspace/tests/DeathStar/src/socialNetwork/docker/lite-memcached/start-vanilla.sh"
    fi

    if [ "$TYPE" == "vanilla" ]; then
        ssh node2 "sed -i 's/\"offline_memcached_patch\": true/\"offline_memcached_patch\": false/' tests/DeathStar/src/socialNetwork/config/service-config.json"
    else
        ssh node2 "sed -i 's/\"offline_memcached_patch\": false/\"offline_memcached_patch\": true/' tests/DeathStar/src/socialNetwork/config/service-config.json"
    fi

    docker service update --force socialnetwork_post-storage-service
}

# 200MB for memcached
function warmup_memcached() {
    ../src/wrk2/wrk -D exp -t 80 -c 512 -d 60 -L -s ../src/socialNetwork/wrk2/scripts/social-network/read-home-timeline.lua http://node2:8080/wrk2-api/home-timeline/read -R 3000
}

function crash_memcached() {
    sleep $CRASH
    ssh node3 "docker exec post-storage-memcached /workspace/tests/DeathStar/src/socialNetwork/docker/lite-memcached/crash.sh"
}

function run_workload() {
    ../src/wrk2/wrk -D exp -t 80 -c 256 -d 90 -L -s ../src/socialNetwork/wrk2/scripts/social-network/mixed-workload.lua http://node2:8080 -R 2500
}

# Run the experiment
start_memcached
warmup_memcached
crash_memcached &
run_workload