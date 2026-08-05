# LevelDB in Figures 15/16

Run from `~/LiteLib` on node0:

```bash
# Setup: ~20-40 min
./scripts/ae_leveldb_overhead_setup.sh

# Experiment: ~90-120 min
./scripts/ae_leveldb_overhead_run.sh

# Process results and regenerate Figures 15/16: <1 min
./scripts/ae_leveldb_overhead_plot.sh
./scripts/ae_overhead_plot.sh

# Cleanup: <1 min
./scripts/ae_leveldb_overhead_cleanup.sh
```

Raw results are written under `results/leveldb-overhead/`, processed values
under `results/leveldb-overhead/processed/`, and the generated PDFs under
`figures/`.

**Interpretation:** eBPF LiteLib should have latency close to vanilla with
modest additional CPU usage. Checkpoint should have substantially higher
latency.
