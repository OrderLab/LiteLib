# Setup

```bash
./init.sh
./swarm_init.sh
```

# Single node

## Update docker images

```bash
./swarm_helper_single.sh build
```

## Run

```bash
./swarm_helper_single.sh up
```

## Stop

```bash
./swarm_helper_single.sh down
```

## Exp

```bash
# init
# in node1
time python3 scripts/init_social_graph.py --graph=socfb-Reed98 --ip node1 --compose
../wrk2/wrk -D exp -t 40 -c 40 -d 600 -L -s ./wrk2/scripts/social-network/compose-post.lua http://node1:8080/wrk2-api/post/compose -R 2000

# run
# in node3
./run_exp_single.sh vanilla
./run_exp_single.sh vanilla+
./run_exp_single.sh litesys
```

## Change cgroup manually

```bash
# go to post-storage-mongodb in node2
cd /workspace/tests/DeathStar/src/socialNetwork/docker/mongo-with-cgroup

# change cgroup
cgset -r cpu.max="100000 100000" deathstar_cpulimited
cgget -g cpu:/deathstar_cpulimited
```

# Replica

## Update docker images

```bash
./swarm_helper_replica.sh build
```

## Run

```bash
./swarm_helper_replica.sh up
```

## Stop

```bash
./swarm_helper_replica.sh down
```

## Exp

```bash
# init database
# in node1
time python3 scripts/init_social_graph.py --graph=socfb-Reed98 --ip node1 --compose
../wrk2/wrk -D exp -t 40 -c 40 -d 600 -L -s ./wrk2/scripts/social-network/compose-post.lua http://node1:8080/wrk2-api/post/compose -R 2000

# in node3
./run_exp_replica.sh vanilla
./run_exp_replica.sh litesys
```

## Change cgroup manually

### Memcached

```bash
# go to post-storage-memcached-1 in node4
cgset -r cpu.max="100000 100000" deathstar_cpulimited_1
cgget -g cpu:/deathstar_cpulimited_1
```
