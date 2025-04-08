#!/bin/bash

apt install -y netcat-traditional

# Start mcrouter in the background
/build/mcrouter/bin/mcrouter -p 11211 -f /social-network-microservices/config/mcrouter.json --num-proxies=8 &

exec tail -f /dev/null