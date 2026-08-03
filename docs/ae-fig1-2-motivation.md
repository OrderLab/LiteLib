# Figures 1 & 2 - Motivation

Run from `~/LiteLib` on node0:

```bash
# Setup: ~60-75 min
./scripts/ae_fig1_2_setup.sh

# Experiment: ~75-90 min
./scripts/ae_fig1_2_run.sh

# Plot: <1 min
./scripts/ae_fig1_2_plot.sh

# Cleanup: <1 min
./scripts/ae_fig1_2_cleanup.sh
```

Raw results are written under `results/motivation/`. The generated PDFs are
`figures/deathstar_latency.pdf` and `figures/deathstar_isolation.pdf`.

**Interpretation:** after a Memcached failure, the vanilla configuration should
show continuously increasing latency, while LiteLib should return close to its
pre-failure latency. Figure 2 should show substantially less backend latency
inflation with LiteLib.
