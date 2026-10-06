# Figure 13 - Memcached recovery

Run from `~/LiteLib` on node0:

```bash
# Authors' provided cluster: import the shared snapshot (~10-20 min)
./scripts/ae_fig13_setup.sh \
  --import-db /srv/litelib-ae/fig13/mysql-snapshot.tar.zst

# Self-reserved cluster: initialize the database (~1-2 hours)
./scripts/ae_fig13_setup.sh

# Experiment: ~25-35 min
./scripts/ae_fig13_run.sh

# Plot: <1 min
./scripts/ae_fig13_plot.sh

# Cleanup: <1 min
./scripts/ae_fig13_cleanup.sh
```

Raw results are written under `results/fig13/`. The generated PDF is
`figures/Figure13.pdf`.

The plot uses a fixed **55-second crash reference** for the standard Figure 13
workload, rather than inferring the marker from the lowest-completion second.
Historical process monitors place the outage around 55-56 seconds; this is a
plotting reference, not an exact recorded injection timestamp for every run.
The original plot style and throughput data are unchanged. This does not change
the workload's failure schedule or the separate qualitative acceptance checks.

**Interpretation:** vanilla should remain at very low throughput after the
failure, LiteLib should recover within the plotted window, and checkpoint
should recover faster but return stale responses.
