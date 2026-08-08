# Figures 1 & 2 - Motivation

Run from `~/LiteLib` on node0:

```bash
# Setup: ~45-65 min
./scripts/ae_fig1_2_setup.sh

# Experiment: ~90-110 min
./scripts/ae_fig1_2_run.sh

# Plot: <1 min
./scripts/ae_fig1_2_plot.sh

# Cleanup: <1 min
./scripts/ae_fig1_2_cleanup.sh
```

Raw results are written under `results/motivation/`. The generated PDFs are
`figures/Figure1.pdf` and `figures/Figure2.pdf`.

Each arm has three repetitions. The scripts rank them by the relevant latency
trend and use the middle run consistently for Figures 1, 2, and the DeathStar
memory values in Figure 14.

**Interpretation:** after a Memcached failure, the vanilla configuration should
show continuously increasing latency, while LiteLib should return close to its
pre-failure latency. Figure 2 should show substantially less backend latency
inflation with LiteLib.
