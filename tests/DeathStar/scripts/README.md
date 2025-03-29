# Setup

```bash
./init.sh
./swarm_init.sh
```

# Run

```bash
./swarm_helper.sh up
```

# Stop

```bash
./swarm_helper.sh down
```

# Exp

```bash
# init
time python3 scripts/init_social_graph.py --graph=socfb-Reed98 --ip node2 --compose
# set cpu limit to inf
../wrk2/wrk -D exp -t 40 -c 40 -d 600 -L -s ./wrk2/scripts/social-network/compose-post.lua http://node2:8080/wrk2-api/post/compose -R 2000

# warm up (200MB for memcached)
../wrk2/wrk -D exp -t 40 -c 40 -d 120 -L -s ./wrk2/scripts/social-network/read-home-timeline.lua http://node2:8080/wrk2-api/home-timeline/read -R 1500
# run
# set cpu limit to 100%
../wrk2/wrk -D exp -t 40 -c 40 -d 300 -L -s ./wrk2/scripts/social-network/mixed-workload.lua http://node2:8080 -R 1500
# crash
# go to socialnetwork_post-storage-memcached service in node4
/workspace/tests/DeathStar/src/socialNetwork/docker/lite-memcached/crash.sh
```

```bash
# go to socialnetwork_post-storage-memcached service in node4
# start litesys
/workspace/tests/DeathStar/src/socialNetwork/docker/lite-memcached/start-litesys.sh
docker service update --force socialnetwork_post-storage-service # and check if memcached is connected to post-storage-service
# start vanilla
/workspace/tests/DeathStar/src/socialNetwork/docker/lite-memcached/start-vanilla.sh
docker service update --force socialnetwork_post-storage-service # and check if memcached is connected to post-storage-service
```

# Change config

Modify the config in `src/socialNetwork/docker/modified-social-network/config.json` in node2 (e.g. post-storage-service.offline_memcached_patch)

```bash
docker service update --force socialnetwork_post-storage-service
docker service logs -f --since 0s socialnetwork_post-storage-service
```

# Change cgroup

```bash
# go to post-storage-mongodb in node2
cd /workspace/tests/DeathStar/src/socialNetwork/docker/mongo-with-cgroup

# change cgroup
cgset -r cpu.max="100000 100000" deathstar_cpulimited
cgget -g cpu:/deathstar_cpulimited
```
