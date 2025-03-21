#!/bin/bash

set -x

DeathStarDir=$(cd "$(dirname "$0")/.." && pwd)

function down() {
    echo "Removing stack..."
    docker stack rm socialnetwork
    
    echo "Cleaning up networks..."
    docker network rm socialnetwork_default || true
    ssh node2 "docker network rm socialnetwork_default" || true
    ssh node3 "docker network rm socialnetwork_default" || true
    
    echo "Cleanup completed"
}

function up() {
    # First clean up any existing deployment
    down
    
    # Get full hostnames from node2 and node3
    export NODE2_HOSTNAME=$(ssh node2 hostname)
    export NODE3_HOSTNAME=$(ssh node3 hostname)
    
    echo "Using NODE2_HOSTNAME: $NODE2_HOSTNAME"
    echo "Using NODE3_HOSTNAME: $NODE3_HOSTNAME"
    
    # Verify swarm status on all nodes
    echo "Verifying swarm status..."
    echo "Node1 status:"
    docker node ls
    
    echo "Node2 status:"
    ssh node2 "docker node ls"
    
    echo "Node3 status:"
    ssh node3 "docker node ls"
    
    # Build lite-memcached on node3
    echo "Building lite-memcached image on node3"
    ssh node3 "cd $DeathStarDir/src/socialNetwork/docker/lite-memcached && docker build -t lite-memcached:latest ."

    # Deploy the stack
    echo "Deploying stack..."
    NODE2_HOSTNAME=$NODE2_HOSTNAME NODE3_HOSTNAME=$NODE3_HOSTNAME docker stack deploy --compose-file=$DeathStarDir/src/socialNetwork/docker-compose-swarm.yml socialnetwork
    
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
    *)
        echo "Invalid parameter. Use 'up' or 'down'"
        exit 1
        ;;
esac
