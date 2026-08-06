# Figure 14 - Memory overhead

After completing the Memcached overhead, LevelDB recovery, Redis overhead, and
MySQL overhead experiments, run:

```bash
# Collect memory data and plot: <1 min
./scripts/ae_memory_overhead_plot.sh
```

Processed values are written to `results/overhead/memory.json`. The generated
PDF is `figures/Figure14.pdf`.

**Interpretation:** each bar reports memory immediately before the configured
failure point. LiteLib adds compact-replica memory while avoiding a full
additional application replica.
