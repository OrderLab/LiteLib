#!/bin/bash
RedisVersion=$(redis-cli info | grep redis_version)
echo "Redis Server. Full Redis version: $RedisVersion"

apt-get update && apt-get install -y htop net-tools

apt-get install -y python3 python3-pip
pip3 install matplotlib numpy pandas networkx scipy SciencePlots

redis-server /workspace/redis_full.conf

if [ "$IS_REPLICA" = "true" ]; then
    echo "Starting Redis Replica"
    redis-server /workspace/redis_replica.conf
    cd sentinel
    for port in 26379 26380 26381; do
        cp redis_sentinel.conf redis_sentinel_${port}.conf
        sed -i "1i\port ${port}" redis_sentinel_${port}.conf
        redis-sentinel redis_sentinel_${port}.conf > redis_sentinel_${port}.log 2>&1 &
    done
else
    echo "Starting Redis Lite"
    # TODO: Add Redis Lite
fi

tail -f /dev/null