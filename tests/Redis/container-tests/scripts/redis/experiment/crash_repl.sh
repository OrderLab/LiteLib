#!/bin/bash

get_master_info() {
    redis-cli -p 26379 sentinel get-master-addr-by-name full_redis
}

initial_master_info=$(get_master_info)
initial_master_ip=$(echo $initial_master_info | awk '{print $1}')
initial_master_port=$(echo $initial_master_info | awk '{print $2}')

echo "Initial master IP: $initial_master_ip"
echo "Initial master port: $initial_master_port"

pid=$(pgrep -f "redis-server.*:$initial_master_port")
if [ -z "$pid" ]; then
    echo "No redis-server process found for port $initial_master_port"
    exit 1
fi

echo "Killing Redis server process $pid"
kill -9 $pid

new_master_info=$(get_master_info)
new_master_ip=$(echo $new_master_info | awk '{print $1}')
new_master_port=$(echo $new_master_info | awk '{print $2}')

while [ "$new_master_ip" == "$initial_master_ip" ] && [ "$new_master_port" == "$initial_master_port" ]; do
    sleep 0.5
    new_master_info=$(get_master_info)
    new_master_ip=$(echo $new_master_info | awk '{print $1}')
    new_master_port=$(echo $new_master_info | awk '{print $2}')
done

if [ "$new_master_port" == "6380" ]; then
    redis-server /workspace/redis_full_running.conf
else
    redis-server /workspace/redis_replica_running.conf
fi
