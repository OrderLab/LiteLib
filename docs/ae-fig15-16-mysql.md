# MySQL in Figures 15/16

Run from `~/LiteLib` on node0:

```bash
# Setup: ~45-75 min
./scripts/ae_mysql_overhead_setup.sh

# Experiment: ~30-45 min
./scripts/ae_mysql_overhead_run.sh

# Process MySQL results: <1 min
./scripts/ae_mysql_overhead_plot.sh

# Cleanup: <1 min
./scripts/ae_mysql_overhead_cleanup.sh
```

Raw results are written under `results/mysql-overhead/`, processed values under
`results/mysql-overhead/processed/`.

This processing step does not have the complete cross-application data for
Figures 15/16. After processing Memcached, LevelDB, Redis, and MySQL, run
`./scripts/ae_overhead_plot.sh` once as described in `ae.md`.

**Interpretation:** LiteMySQL should remain close to the standalone baseline.
Classic replication and both NDB configurations should show higher latency and
additional CPU usage from their extra components.
