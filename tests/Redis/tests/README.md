## Docker Containers Setup

This consists of setting up 4 Docker containers:

- Redis server (172.16.0.2)
  - A custom proxy listening to port `6279`
  - Full Redis server listening to port `6379`
  - Lite Redis server listening to port `6479`
- Redis benchmarking client (172.16.0.3)
- Monitoring tools
  - cAdvisor
  - Prometheus

```sh
# Make sure that you are in the cascade/tests/Redis/tests directory.
docker compose up -d
# Entering the container of Redis server
docker exec -it redis-server bash

# Setting up the server takes a while. Use ping to check if the Redis server is on
# Inside the server container, the server is up if it replies PONG
redis-cli ping
```

## Benchmarking

To benchmark the Redis servers, Redis-benchmark utility from Redis is used. About how to use Redis-benchmark, please check the [official manual](https://redis.io/docs/latest/operate/oss_and_stack/management/optimization/benchmarks/).

```sh
docker exec -it redis-client bash
# Inside the client container
# 1. To test with lite Redis server (currently proxy has bugs)
redis-benchmark -h 172.16.0.2 -p 6279 -q -n 100000
# 2. To directly test with only the full Redis server
redis-benchmark -h 172.16.0.2 -p 6379 -q -n 100000
```

The lite Redis and one-step experiment scripts are still under construction.

## Performance Monitoring

- cAdvisor: [localhost:8080](http://localhost:8080)
- Prometheus: [localhost:9090](http://localhost:9090)
  - [CPU Usage of Redis Container](http://localhost:9090/graph?g0.expr=rate(container_cpu_usage_seconds_total%7Bname%3D%22redis-server%22%7D%5B1m%5D)&g0.tab=0&g0.display_mode=lines&g0.show_exemplars=0&g0.range_input=1h)
  - [Memory Usage of Redis Container](http://localhost:9090/graph?g0.expr=container_memory_usage_bytes%7Bname%3D%22redis-server%22%7D&g0.tab=0&g0.display_mode=lines&g0.show_exemplars=0&g0.range_input=1h)

