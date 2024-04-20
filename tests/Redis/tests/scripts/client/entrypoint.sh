#!/bin/bash
RedisVersion=$(redis-cli info | grep redis_version)
echo "Redis Client. Redis version: $RedisVersion"

apt-get update && apt-get install -y htop net-tools

tail -f /dev/null