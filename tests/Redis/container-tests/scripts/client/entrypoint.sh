#!/bin/bash

apt-get update && apt-get install -y htop net-tools wget gcc make git pkg-config

git clone https://github.com/redis/redis.git /workspace/redis-code

cd /workspace/redis-code && make && make install

RedisVersion=$(redis-cli -h 172.16.0.2 info | grep redis_version)
echo "Redis Client. Connected to Redis server: $RedisVersion"

tail -f /dev/null