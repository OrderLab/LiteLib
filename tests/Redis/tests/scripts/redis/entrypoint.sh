#!/bin/bash
RedisVersion=$(redis-cli info | grep redis_version)
echo "Redis Server. Full Redis version: $RedisVersion"

apt-get update && apt-get install -y htop net-tools

# Install Python and some usual packages
apt-get install -y python3 python3-pip
pip3 install matplotlib numpy pandas networkx scipy SciencePlots

redis-server /workspace/redis.conf

# Execute the Python script
python3 /workspace/myproxy.py

tail -f /dev/null