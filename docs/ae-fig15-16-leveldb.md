# LevelDB in Figures 15/16

Run from `~/LiteLib` on node0:

```bash
# Setup: ~20-40 min
./scripts/ae_leveldb_overhead_setup.sh

# Experiment: ~90-120 min
./scripts/ae_leveldb_overhead_run.sh

# Process LevelDB results: <1 min
./scripts/ae_leveldb_overhead_plot.sh

# Cleanup: <1 min
./scripts/ae_leveldb_overhead_cleanup.sh
```

Raw results are written under `results/leveldb-overhead/`, processed values
under `results/leveldb-overhead/processed/`.

This processing step does not have the complete cross-application data for
Figures 15/16. After processing Memcached, LevelDB, Redis, and MySQL, run
`./scripts/ae_overhead_plot.sh` once as described in `ae.md`.

**Interpretation:** eBPF LiteLib should have latency close to vanilla with
modest additional CPU usage. Checkpoint should have substantially higher
latency.
