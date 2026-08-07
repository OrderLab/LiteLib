# Figure 13 - Memcached recovery

Run from `~/LiteLib` on node0:

```bash
# Authors' provided cluster: import the shared snapshot (~10-20 min)
./scripts/ae_fig13_setup.sh \
  --import-db /srv/litelib-ae/fig13/mysql-snapshot.tar.zst

# Self-reserved cluster: initialize the database (~1-2 hours)
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
