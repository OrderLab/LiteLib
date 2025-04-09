#!/bin/bash

apt install -y netcat-traditional

# Start mcrouter in the background
/build/mcrouter/bin/mcrouter -p 11211 -f /social-network-microservices/config/mcrouter.json --num-proxies=8 --pool-stats-config-file=/social-network-microservices/config/poolstats.json --stats-logging-interval=500 &

exec tail -f /dev/null