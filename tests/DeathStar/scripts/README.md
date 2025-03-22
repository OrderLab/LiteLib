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
time python3 scripts/init_social_graph.py --graph=socfb-Reed98 --ip node2 --compose
../wrk2/wrk -D exp -t 12 -c 12 -d 300 -L -s ./wrk2/scripts/social-network/compose-post.lua http://node2:8080/wrk2-api/post/compose -R 5
../wrk2/wrk -D exp -t 12 -c 12 -d 300 -L -s ./wrk2/scripts/social-network/read-home-timeline.lua http://node2:8080/wrk2-api/home-timeline/read -R 5
```
