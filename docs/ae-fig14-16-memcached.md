# Memcached in Figures 14, 15 and 16

Run from `~/LiteLib` on node0:

```bash
# Setup: ~5-15 min
./scripts/ae_memcached_overhead_setup.sh

# Experiment: ~25-40 min
./scripts/ae_memcached_overhead_run.sh

# Process Memcached results: <1 min
./scripts/ae_memcached_overhead_plot.sh

# Cleanup: <1 min
./scripts/ae_memcached_overhead_cleanup.sh
```

Raw and processed results are written under `results/memcached-overhead/`.
After processing Memcached, LevelDB, Redis, and MySQL, run
`./scripts/ae_overhead_plot.sh` to generate the complete `Figure15.pdf` and
`Figure16.pdf`. Generate `Figure14.pdf` after all memory inputs are ready with
`./scripts/ae_memory_overhead_plot.sh`.

**Interpretation:** Figure 14 reports memory immediately before failure.
Figures 15 and 16 compare steady-state latency and CPU overhead; the LiteLib
variants should remain close to the baseline while using additional resources
for the compact replica.
