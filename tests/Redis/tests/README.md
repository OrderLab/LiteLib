## Docker Containers Setup

This consists of setting up 4 Docker containers:

- Redis server (172.16.0.2)
  - A custom proxy listening to port `6279`
  - Full Redis server listening to port `6379`
  - Lite Redis server listening to port `6479`
- Redis benchmarking client (172.16.0.3)
- Monitoring tools
  - cAdvisor(v0.49.1)
  - Prometheus

### Replica model

Redis provides replication model for high usability. A full server, a replica server and 3 sentinels are involved to simulate the real scenario in production.

- [Redis-replication](https://redis.io/docs/latest/operate/oss_and_stack/management/replication/)
- [Redis-sentinel](https://redis.io/docs/latest/operate/oss_and_stack/management/sentinel/)

```sh
# Make sure that you are in the cascade/tests/Redis/tests directory.

# Any IS_REPLICA except "true" falls back to lite mode.
# IS_REPLICA is by default a blank string, therefore the container boots in lite mode without setting IS_REPLICA.
export IS_REPLICA=true
docker compose up -d
# Entering the container of Redis server
docker exec -it redis-server bash

# Setting up the server takes a while. Use ping to check if the Redis server is on
# Inside the server container, the server is up if it replies PONG
redis-cli ping
```

### Lite model

Source code at `Redis/src`. In this setting, a full server and a lite server are involved. The function and performance overhead of switching is integrated in Lite-Redis.

```sh
export IS_REPLICA=false
docker compose up -d
# Entering the container of Redis server
docker exec -it redis-server bash

# If Redis-lite has not been built in advance, building process would be triggered.
# Export the log to check if the setup is done. (optional)
docker logs redis-server >& redis-server.log
# Inside the server container, the server is up if it replies PONG
redis-cli ping -p 6479
```

## Benchmarking

> Official benchmarking terminates if failure detected, the benchmark client is modified.

To benchmark the Redis servers, Redis-benchmark utility from Redis is used. About how to use Redis-benchmark, please check the [official manual](https://redis.io/docs/latest/operate/oss_and_stack/management/optimization/benchmarks/).

```sh
docker exec -it redis-client bash
# Inside the client container
# 1. To test with lite Redis server
redis-benchmark -h 172.16.0.2 -p 6479 --csv -n 100000 -t set,get,incr,lpush,rpush,lpop,rpop,sadd,spop,hset,hget
# 2. To directly test with only the full Redis server
redis-benchmark -h 172.16.0.2 -p 6379 --csv -n 100000 -t set,get,incr,lpush,rpush,lpop,rpop,sadd,spop,hset,hget
```

The lite Redis and one-step experiment scripts are still under construction.

## Container Performance Monitoring

- cAdvisor: [localhost:8080](http://localhost:8080)
- Prometheus: [localhost:9090](http://localhost:9090)
  - [CPU Usage of Redis Container](http://localhost:9090/graph?g0.expr=rate(container_cpu_usage_seconds_total%7Bname%3D%22redis-server%22%7D%5B1m%5D)&g0.tab=0&g0.display_mode=lines&g0.show_exemplars=0&g0.range_input=1h)
  - [Memory Usage of Redis Container](http://localhost:9090/graph?g0.expr=container_memory_usage_bytes%7Bname%3D%22redis-server%22%7D&g0.tab=0&g0.display_mode=lines&g0.show_exemplars=0&g0.range_input=1h)

