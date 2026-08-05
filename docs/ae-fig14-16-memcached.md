# Memcached in Figures 14, 15 and 16

Run from `~/LiteLib` on node0:

```bash
# Setup: ~20-40 min
./scripts/ae_memcached_overhead_setup.sh

# Experiment: ~25-40 min
./scripts/ae_memcached_overhead_run.sh

# Plot: <1 min
./scripts/ae_memcached_overhead_plot.sh

# Cleanup: <1 min
./scripts/ae_memcached_overhead_cleanup.sh
```

Raw results are written under `results/memcached-overhead/`. The generated PDFs
are `figures/memory_overhead.pdf`, `figures/latency_overhead.pdf`, and
`figures/cpu_overhead.pdf`.

**Interpretation:** Figure 14 reports memory immediately before failure.
Figures 15 and 16 compare steady-state latency and CPU overhead; the LiteLib
variants should remain close to the baseline while using additional resources
for the compact replica.
