# Redis in Figures 15/16

Run from `~/LiteLib` on node0:

```bash
# Setup: ~15-30 min
./scripts/ae_redis_overhead_setup.sh

# Experiment: ~35-55 min
./scripts/ae_redis_overhead_run.sh

# Process results and regenerate Figures 15/16: <1 min
./scripts/ae_redis_overhead_plot.sh
./scripts/ae_overhead_plot.sh

# Cleanup: <1 min
./scripts/ae_redis_overhead_cleanup.sh
```

Raw results are written under `results/redis-overhead/`, processed values under
`results/redis-overhead/processed/`, and the generated PDFs under `figures/`.

**Interpretation:** embedded LiteLib should remain close to vanilla latency
with modest CPU overhead. The read-only replica configuration should have
higher latency and additional CPU usage.
