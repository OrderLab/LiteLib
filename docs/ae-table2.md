# Table 2 - Service gaps

Run from `~/LiteLib` on node0:

```bash
# Setup: ~45-75 min
./scripts/ae_table2_setup.sh

# Experiment: ~60-90 min
./scripts/ae_table2_run.sh

# Rebuild the CSV from an existing result directory: <1 min
./scripts/ae_table2_collect.sh

# Cleanup: <1 min
./scripts/ae_table2_cleanup.sh
```

Raw results are written under `results/table2/`. The generated table is
`figures/table2_service_gap.csv`.

**Interpretation:** active-passive configurations should have second-scale
gaps, active-active configurations should have millisecond-scale gaps, and
LiteLib should have the smallest gaps.
