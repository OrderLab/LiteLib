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
../wrk2/wrk -D zipf -t 40 -c 40 -d 600 -L -s ./wrk2/scripts/social-network/compose-post.lua http://node2:8080/wrk2-api/post/compose -R 2000

# run
../wrk2/wrk -D zipf -t 40 -c 40 -d 300 -L -s ./wrk2/scripts/social-network/read-home-timeline.lua http://node2:8080/wrk2-api/home-timeline/read -R 2000
../wrk2/wrk -D zipf -t 40 -c 40 -d 300 -L -s ./wrk2/scripts/social-network/compose-post.lua http://node2:8080/wrk2-api/post/compose -R 2000

# crash
# go to socialnetwork_post-storage-memcached service
cd /workspace/tests/DeathStar/src/socialNetwork/docker/lite-memcached
./crash.sh
./start-litesys.sh
```

# Change config

Modify the config in `src/socialNetwork/docker/modified-social-network/config.json` in node2 (e.g. post-storage-service.offline_memcached_patch)

```bash
docker service update --force socialnetwork_post-storage-service
docker service logs -f --since 0s socialnetwork_post-storage-service
```
