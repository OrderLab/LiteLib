# Figure 13 - Memcached recovery

Run from `~/LiteLib` on node0:

```bash
# Initial setup: ~1-2 hours; later runs: ~5-15 min
./scripts/ae_fig13_setup.sh

# Experiment: ~35-45 min
./scripts/ae_fig13_run.sh

# Plot: <1 min
./scripts/ae_fig13_plot.sh

# Cleanup: <1 min
./scripts/ae_fig13_cleanup.sh
```

Raw results are written under `results/fig13/`. The generated PDF is
`figures/Figure13.pdf`.

**Interpretation:** vanilla should remain at very low throughput after the
failure, LiteLib should recover within the plotted window, and checkpoint
should recover faster but return stale responses.
