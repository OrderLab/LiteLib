#!/bin/bash

# Initialize the swarm on local machine
echo "Initializing Docker Swarm on local machine..."
docker swarm init --advertise-addr $(hostname -i)

# Get the join token for workers
JOIN_TOKEN=$(docker swarm join-token worker -q)
MANAGER_IP=$(hostname -i)

# Join worker nodes to the swarm
for node in node0 node2 node3; do
    echo "Joining $node to the swarm..."
    ssh $node "docker swarm join --token $JOIN_TOKEN $MANAGER_IP:2377"
done

# Verify the swarm status
echo "Verifying swarm status..."
docker node ls
