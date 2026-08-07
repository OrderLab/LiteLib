# Redis in Figures 15/16

Run from `~/LiteLib` on node0:

```bash
# Setup: ~15-30 min
./scripts/ae_redis_overhead_setup.sh

# Experiment: ~35-55 min
./scripts/ae_redis_overhead_run.sh

# Process Redis results: <1 min
./scripts/ae_redis_overhead_plot.sh

# Cleanup: <1 min
./scripts/ae_redis_overhead_cleanup.sh
```

Raw results are written under `results/redis-overhead/`, processed values under
`results/redis-overhead/processed/`.

This processing step does not have the complete cross-application data for
Figures 15/16. After processing Memcached, LevelDB, Redis, and MySQL, run
`./scripts/ae_overhead_plot.sh` once as described in `ae.md`.

**Interpretation:** embedded LiteLib should remain close to vanilla latency
with modest CPU overhead. The read-only replica configuration should have
higher latency and additional CPU usage.
