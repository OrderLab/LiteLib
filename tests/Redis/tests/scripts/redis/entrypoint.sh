#!/bin/bash

apt-get update && apt-get install -y htop net-tools python3 python3-pip

pip3 install matplotlib numpy pandas networkx scipy SciencePlots psutil redis --break-system-packages

find /workspace -name "*.rdb" -type f -delete

cp /workspace/redis_full.conf /workspace/redis_full_running.conf
redis-server /workspace/redis_full_running.conf

if [ "$IS_REPLICA" = "true" ]; then
    echo "Starting Redis Replica"
    cp /workspace/redis_replica.conf /workspace/redis_replica_running.conf
    redis-server /workspace/redis_replica_running.conf
    cd sentinel
    for port in 26379 26380 26381; do
        cp redis_sentinel.conf redis_sentinel_${port}.conf
        sed -i "1i\port ${port}" redis_sentinel_${port}.conf
        redis-sentinel redis_sentinel_${port}.conf > redis_sentinel_${port}.log 2>&1 &
    done
else
    echo "Starting Redis Lite"
    /workspace/lite-version/entrypoint.sh
    if [ ! -x "/workspace/lite-version/build/redis-lite" ]; then
        mkdir -p /workspace/lite-version/build
        cd /workspace/lite-version/build && cmake.. && make
    fi
    /workspace/lite-version/build/redis-lite -h 172.16.0.2 -p 6479
fi

RedisVersion=$(redis-cli -h 172.16.0.2 info | grep redis_version)
echo "Redis Server: $RedisVersion"

tail -f /dev/null