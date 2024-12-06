# LiteSys for Redis

```sh

# first setup the experiment 
./setup-experiment.sh replica
# or, for litesys experiment
./setup-experiment.sh lite

# then run scripts to boot the benchmark on node 0 and monitor scipts on corresponding nodes
./lite_cli -t /tmp/lite_Redis -p /tmp/redis.sock -m 1
./lite_cli -t /tmp/lite_Redis -p /tmp/redis.sock -m 0
```