# LevelDB in Figure 12

Run from `~/LiteLib` on node0:

```bash
# Setup: ~20-40 min
./scripts/ae_leveldb_recovery_setup.sh

# Experiment: ~35-50 min
./scripts/ae_leveldb_recovery_run.sh

# Plot: <1 min
./scripts/ae_leveldb_recovery_plot.sh

# Cleanup: <1 min
./scripts/ae_leveldb_recovery_cleanup.sh
```

Raw results are written under `results/leveldb-recovery/`. The generated PDF
is `figures/leveldb_throughput.pdf`.

**Interpretation:** baseline throughput should become unstable after restart.
LiteLib should serve successful requests during the failure and return to
stable pre-failure total throughput after replay.
