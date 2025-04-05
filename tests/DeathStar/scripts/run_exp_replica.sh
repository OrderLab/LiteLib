#!/bin/bash

set -x

TYPE=$1
CRASH=${2:-20}

function start_memcached() {
    if [ "$TYPE" == "vanilla" ]; then
        ssh node3 "docker exec post-storage-memcached-1 /workspace/tests/DeathStar/src/socialNetwork/docker/lite-memcached/start-vanilla-with-cgroup.sh 1"
        ssh node3 "docker exec post-storage-memcached-2 /workspace/tests/DeathStar/src/socialNetwork/docker/lite-memcached/start-vanilla-with-cgroup.sh 2"
    elif [ "$TYPE" == "litesys" ]; then
        ssh node3 "docker exec post-storage-memcached-1 /workspace/tests/DeathStar/src/socialNetwork/docker/lite-memcached/start-litesys-with-cgroup.sh 1"
        sleep 3
        ssh node3 "docker exec post-storage-memcached-1 /workspace/tests/DeathStar/src/socialNetwork/docker/lite-memcached/start-litesys-with-cgroup.sh 1"
        ssh node3 "docker exec post-storage-memcached-2 /workspace/tests/DeathStar/src/socialNetwork/docker/lite-memcached/start-vanilla-with-cgroup.sh 2"
    fi
    docker service update --force socialnetwork_post-storage-service
}

function warmup_memcached() {
    ../src/wrk2/wrk -D exp -t 40 -c 40 -d 120 -L -s ../src/socialNetwork/wrk2/scripts/social-network/read-home-timeline.lua http://node2:8080/wrk2-api/home-timeline/read -R 1500
}

function crash_memcached() {
    sleep $CRASH
    ssh node3 "docker exec post-storage-memcached-1 /workspace/tests/DeathStar/src/socialNetwork/docker/lite-memcached/crash.sh"
}

function mcrouter_logging() {
    ssh node3 "docker exec \$(docker ps -q --filter label=com.docker.swarm.service.name=socialnetwork_post-storage-memcached) /workspace/docker/lite-memcached/get_mcrouter_stat.sh $TYPE 90"
}

function run_workload() {
    ../src/wrk2/wrk -D exp -t 40 -c 40 -d 90 -L -s ../src/socialNetwork/wrk2/scripts/social-network/mixed-workload.lua http://node2:8080 -R 1500
}

# Run the experiment
start_memcached
warmup_memcached
crash_memcached &
mcrouter_logging &
run_workload