#!/bin/bash

# Check if parameter is provided
if [ $# -ne 1 ]; then
    echo "Usage: $0 [up|down]"
    exit 1
fi

DeathStarDir=$(dirname "$0")/..

# Validate parameter
if [ "$1" != "up" ] && [ "$1" != "down" ]; then
    echo "Invalid parameter. Use 'up' or 'down'"
    exit 1
fi

if [ "$1" == "up" ]; then
    # Deploy the entire stack
    docker stack deploy --compose-file=$DeathStarDir/src/socialNetwork/docker-compose-swarm.yml socialnetwork
    echo "Services started successfully"
elif [ "$1" == "down" ]; then
    # Remove the entire stack
    docker stack rm socialnetwork
    echo "All services shut down successfully"
fi
