# Figure 13 — Memcached metastability and recovery

Figure 13 evaluates a lookaside-cache stack (nginx/PHP → Memcached → MySQL)
under a Memcached crash:

1. **Vanilla** — empty Memcached restart;
2. **LiteLib** — LiteMemcached serves while replay warms the restarted full
   Memcached;
3. **Checkpoint** — CRIU checkpoint every 30 seconds.

Evaluators invoke this experiment from the main `nsdi27-ae` checkout. Its
wrappers automatically manage the `nsdi27-ae-memcached` worktree; no branch
switch is required.

## Commands

```bash
cd ~/LiteLib

# One-time setup: ~1–2 hours. Most of this is the 1.4M-row MySQL load and
# linearization; its named Docker volume and archive are preserved afterwards.
# Subsequent setup runs take ~5–15 min.
./scripts/ae_fig13_setup.sh

# Run full, LiteLib and checkpoint arms (~35–45 min total).
./scripts/ae_fig13_run.sh

# Generate figures/memcached.pdf (<1 min).
./scripts/ae_fig13_plot.sh
```

## Reset rule and database exception

Before **every arm**, the runner restarts all four containers and clears every
transient cache, trace, result, checkpoint and process. The sole exception is
the MySQL database file in the named `mysql_data` Docker volume: initializing
1.4M rows is the expensive one-time step. `run_experiment.py` resets the mutable database column
before each arm, while the immutable initialized table is reused.

Do not run `docker compose down -v`; `-v` deletes the initialized database.
Immediately after first-time initialization, setup stops MySQL and creates a
checksummed archive under `~/LiteLib/results/fig13/database/`. The run command
refuses to start until that archive exists and verifies. If the Docker volume
is later lost, rerunning setup restores it from the archive instead of
reinitializing the dataset.

## Fixed paper parameters

The evaluator scripts use the paper settings directly:

| Parameter | Value |
| --- | --- |
| Arrival rate | 400 req/s |
| Duration | 300 s |
| Zipf α | 1.00001 |
| Threads | 256 |
| Crash offset | 60 s (observed crash at 57 s) |
| Checkpoint interval | 30 s |
| Lite cache entries | 256K |

## Expected result

The pass/fail check is qualitative; exact recovery times vary:

1. **Vanilla recovers very slowly** (or never reaches 90% within 300 seconds).
2. **LiteLib recovers within the experiment window.**
3. **Checkpoint recovers much faster than LiteLib, but returns substantial
   stale data.**

The published run's 10.8%, 18-second replay, 7-second checkpoint recovery and
2K+ stale responses are examples, not required exact values.

## Analysis-only check

The archived raw data is an overlay: `v2` overrides the LiteLib run from `v1`;
`full` and `checkpoint` remain from `v1` (`v1` itself overrides `v0`).

```bash
# <1 min
./scripts/ae_fig13_plot.sh --check ~/OriginalRawData/Memcached
```

The resolved three inputs match `~/litesys-nsdi27/data/memcached/`
byte-for-byte and regenerate `figures/memcached.pdf`.
