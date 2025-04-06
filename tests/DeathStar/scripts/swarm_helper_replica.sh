#!/bin/bash

set -x

DeathStarDir=$(cd "$(dirname "$0")/.." && pwd)

function build_docker_images() {
    # Build lite-memcached on node3
    echo "Building lite-memcached image on node3"
    ssh node3 "cd $DeathStarDir/src/socialNetwork/docker/lite-memcached && docker build -t lite-memcached:latest ."

    # Build mcrouter on node3
    echo "Building mcrouter image on node3"
    ssh node3 "cd $DeathStarDir/src/socialNetwork/docker/mcrouter && docker build -t modified-mcrouter:latest ."

    # Build modified-social-network on node2
    echo "Building modified-social-network image on node2"
    ssh node2 "cd $DeathStarDir/src/socialNetwork && docker build -t modified-social-network:latest ."

    # Build mongo-with-cgroup on node0
    echo "Building mongo-with-cgroup image on node0"
    ssh node0 "cd $DeathStarDir/src/socialNetwork/docker/mongo-with-cgroup && docker build -t mongo-with-cgroup:latest ."
}

function down() {
    echo "Removing MongoDB container on node0..."
    ssh node0 "docker stop post-storage-mongodb || true; docker rm -f post-storage-mongodb || true"

    echo "Removing Memcached instances on node3..."
    for i in {1..2}; do
        ssh node3 "docker stop post-storage-memcached-$i || true; docker rm -f post-storage-memcached-$i || true"
    done

    echo "Removing stack..."
    docker stack rm socialnetwork

    echo "Cleaning up networks..."
    docker network rm socialnetwork_default || true
    
    echo "Node0 leaving swarm and rejoining..."
    JOIN_TOKEN=$(docker swarm join-token worker -q)
    MANAGER_IP=$(hostname -i)
    ssh node0 "docker swarm leave --force"
    ssh node0 "docker swarm join --token $JOIN_TOKEN $MANAGER_IP:2377"

    echo "Node3 leaving swarm and rejoining..."
    JOIN_TOKEN=$(docker swarm join-token worker -q)
    MANAGER_IP=$(hostname -i)
    ssh node3 "docker swarm leave --force"
    ssh node3 "docker swarm join --token $JOIN_TOKEN $MANAGER_IP:2377"

    sleep 5
    
    # Remove any down nodes from the swarm
    echo "Removing down nodes from swarm..."
    for node in $(docker node ls -q --filter "role=worker" --format "{{if ne .Status \"Ready\"}}{{.ID}}{{end}}"); do
        echo "Removing node $node from swarm..."
        docker node rm --force $node || true
    done
    
    echo "Cleanup completed"
}

function up() {
    # First clean up any existing deployment
    down
    
    # Get full hostnames from node2 and node3
    export NODE0_HOSTNAME=$(ssh node0 hostname)
    export NODE1_HOSTNAME=$(ssh node1 hostname)
    export NODE2_HOSTNAME=$(ssh node2 hostname)
    export NODE3_HOSTNAME=$(ssh node3 hostname)
    
    echo "Using NODE0_HOSTNAME: $NODE0_HOSTNAME"
    echo "Using NODE1_HOSTNAME: $NODE1_HOSTNAME"
    echo "Using NODE2_HOSTNAME: $NODE2_HOSTNAME"
    echo "Using NODE3_HOSTNAME: $NODE3_HOSTNAME"
    
    # Verify swarm status on all nodes
    echo "Verifying swarm status..."
    docker node ls

    # Deploy the stack
    echo "Deploying stack..."
    NODE0_HOSTNAME=$NODE0_HOSTNAME NODE1_HOSTNAME=$NODE1_HOSTNAME NODE2_HOSTNAME=$NODE2_HOSTNAME NODE3_HOSTNAME=$NODE3_HOSTNAME docker stack deploy --compose-file=$DeathStarDir/src/socialNetwork/docker-compose-swarm-replica.yml socialnetwork
    
    # Wait for network to be created
    sleep 5

    # Remove MongoDB service from swarm and run it manually with required privileges
    echo "Setting up MongoDB container with special privileges..."
    # docker service rm socialnetwork_post-storage-mongodb || true
    ssh node0 "cd $DeathStarDir/src/socialNetwork && \
        docker run -d \
        --name post-storage-mongodb \
        --network socialnetwork_default \
        --network-alias post-storage-mongodb \
        --network-alias post-storage-mongodb.socialnetwork_default \
        --hostname post-storage-mongodb \
        --privileged \
        --cgroupns host \
        -v \$(pwd)/config:/social-network-microservices/config \
        -v \$(pwd)/keys:/keys \
        -v /sys/fs/cgroup:/sys/fs/cgroup:rw \
        mongo-with-cgroup:latest \
        sh -c 'cgcreate -g cpu:/deathstar_cpulimited && cgset -r cpu.max=\"400000 100000\" deathstar_cpulimited && cgexec -g cpu:deathstar_cpulimited mongod --bind_ip_all --nojournal --quiet --config /social-network-microservices/config/mongod.conf'"

    # Memcached instances on node3
    for i in {1..2}; do
        echo "Setting up Memcached instance $i on node3"
        ssh node3 "cd $DeathStarDir/../../ && \
        docker run -d \
        --name post-storage-memcached-$i \
        --network socialnetwork_default \
        --network-alias post-storage-memcached-$i \
        --network-alias post-storage-memcached-$i.socialnetwork_default \
        --hostname post-storage-memcached-$i \
        --privileged \
        --cgroupns host \
        --shm-size 6g \
        -v \$(pwd):/workspace \
        -v /sys/fs/cgroup:/sys/fs/cgroup:rw \
        lite-memcached:latest \
        /workspace/tests/DeathStar/src/socialNetwork/docker/lite-memcached/start-memcached-with-cgroup.sh $i"
    done

    # Check service status
    echo "Service status:"
    docker stack services socialnetwork
}

# Check if parameter is provided
if [ $# -ne 1 ]; then
    echo "Usage: $0 [up|down]"
    exit 1
fi

# Validate parameter and call appropriate function
case "$1" in
    up)
        up
        ;;
    down)
        down
        ;;
    build)
        build_docker_images
        ;;
    *)
        echo "Invalid parameter. Use 'up' or 'down' or 'build'"
        exit 1
        ;;
esac
