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
    
    # Swarm membership is initialized once by swarm_init.sh. Rejoining workers
    # for every experiment reset leaves duplicate Ready records and can stall
    # the scheduler, so only prune records that do not match each host's
    # currently active NodeID.
    echo "Removing stale and duplicate nodes from swarm..."
    for alias in node0 node1 node2 node3; do
        host=$(ssh "$alias" hostname)
        current=$(ssh "$alias" "docker info --format '{{.Swarm.NodeID}}'")
        for _ in $(seq 1 30); do
            docker node ls -q | grep -qx "$current" && break
            sleep 1
        done
        for node in $(docker node ls --format '{{.ID}} {{.Hostname}}' |
            awk -v h="$host" '$2==h{print $1}'); do
            if [ "$node" != "$current" ]; then
                echo "Removing stale node $node for $host..."
                docker node rm --force "$node" || true
            fi
        done
    done
    for node in $(docker node ls --format '{{.ID}} {{.Status}}' |
        awk '$2!="Ready"{print $1}'); do
        echo "Removing non-Ready node $node from swarm..."
        docker node rm --force "$node" || true
    done
    node_count=$(docker node ls -q | wc -l)
    [ "$node_count" -eq 4 ] || {
        echo "ERROR: expected 4 unique swarm nodes, found $node_count" >&2
        return 1
    }
    
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

    # Label by node ID rather than hostname: a hostname can still match more
    # than one entry if a stale record survived, and `docker node update` then
    # fails instead of applying the label.
    label_nginx() {
        local host=$1 id
        id=$(docker node ls --format '{{.ID}} {{.Hostname}} {{.Status}}' |
             awk -v h="$host" '$2==h && $3=="Ready"{print $1; exit}')
        if [ -z "$id" ]; then
            echo "WARNING: no Ready swarm node for ${host}; nginx will be under-replicated" >&2
            return 1
        fi
        docker node update --label-add nginx=true "$id"
    }
    label_nginx "${NODE0_HOSTNAME}"
    label_nginx "${NODE1_HOSTNAME}"

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
    # the 400% limit here is way larger than the peak usage

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
        --shm-size ${LITE_SHM_SIZE:-6g} \
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
