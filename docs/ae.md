# LiteLib Artifact Evaluation Guide

Welcome to the artifact evaluation guide for **LiteLib** (NSDI '27), the
library behind *"LiteLib: Containing Failure Impact for Stateful Applications
with Compact Replicas"*.

This document covers **environment setup**. Choose exactly one access mode:

1. **Authors' provided cluster:** the authors' four nodes are already
   system-initialized; initialize only your evaluator account.
2. **Self-reserved cluster:** reserve four CloudLab nodes and run both the
   system-wise and user-wise initialization.

Once setup is complete, follow the per-experiment guides linked from
[Running the experiments](#-running-the-experiments).

> **Note:** we may push fixes to this repository during the evaluation period.
> Please re-run `./scripts/user_init.sh` to update before you start.

## Overview

This artifact reproduces the paper's eight numbered evaluation results:

- **Figures 1 and 2:** failure amplification in DeathStarBench and LiteLib's
  isolation of the healthy backend.
- **Figure 12:** LevelDB service during failure and stable throughput after
  replay.
- **Figure 13:** Memcached metastability, LiteLib recovery, and checkpoint
  staleness.
- **Figure 14:** memory overhead across Memcached, LevelDB, Redis, and MySQL.
- **Figure 15:** steady-state latency overhead.
- **Figure 16:** steady-state CPU overhead.
- **Table 2:** service gaps for active-passive, active-active, and LiteLib
  configurations.

Each experiment guide gives the command, expected runtime, output location,
and qualitative interpretation. Generated artifacts use the names
`Figure1.pdf`, `Figure2.pdf`, `Figure12.pdf`–`Figure16.pdf`, and `Table2.csv`
under `figures/`.

> ⚠️ **Self-reserved cluster warning:** Figures **1, 2, 12, and 13** are not
> stable because the workload that triggers metastable failure is
> machine-specific, so the reproduced results may differ.

## ✅ Setup Checklist

**Authors' provided cluster**

- [ ] Sent an SSH public key to the authors through HotCRP
- [ ] Received a per-evaluator username and `node0` login address
- [ ] Ran `./scripts/user_init.sh` only

**Self-reserved cluster**

- [ ] Instantiated 4 × CloudLab `c220g5` nodes with Ubuntu 22.04
- [ ] Ran `./scripts/system_init.sh`, rebooted, and ran its `post-reboot` step
- [ ] Ran `./scripts/user_init.sh`

**Both modes**

- [ ] The final setup output reports all four nodes ready
- [ ] Reproduced [Figures 1 & 2](./ae-fig1-2-motivation.md)
- [ ] Reproduced [Figure 13](./ae-fig13-memcached.md)
- [ ] Reproduced LevelDB [Figure 12](./ae-fig12-leveldb.md)
- [ ] Reproduced [Figure 14](./ae-fig14-memory.md)
- [ ] Reproduced Memcached's [Figures 14/15/16](./ae-fig14-16-memcached.md)
- [ ] Reproduced LevelDB's [Figures 15/16](./ae-fig15-16-leveldb.md)
- [ ] Reproduced Redis's [Figures 15/16](./ae-fig15-16-redis.md)
- [ ] Reproduced MySQL's [Figures 15/16](./ae-fig15-16-mysql.md)
- [ ] Reproduced [Table 2](./ae-table2.md)

---

## 🖥 Hardware Requirements

All results in the paper were collected on **CloudLab**, using **four `c220g5`
bare-metal nodes**. LiteLib containment is a distributed, latency-sensitive
mechanism, so the experiments need real machines and a real network; a VM or a
single-node setup cannot reproduce the paper's numbers.

| Resource  | What we used                                              |
| --------- | --------------------------------------------------------- |
| Nodes     | 4 × CloudLab `c220g5` (`node0` … `node3`)                 |
| CPU       | 2 × Intel Xeon Silver 4114 (10 cores @ 2.20 GHz each)     |
| Memory    | 192 GB DDR4 (6 × 32 GB)                                   |
| Storage   | 480 GB SATA SSD (grown to ~430 GiB usable on `/`) |
| Network   | 10 GbE experiment network on `10.10.1.0/24`               |
| OS image  | Ubuntu 22.04 LTS |
| Kernel    | `6.8.0-52-generic` (installed by the setup scripts)       |

### Instantiating the cluster on CloudLab

1. Create an experiment with **4 raw PC nodes** of type `c220g5` and the
   `Ubuntu 22.04` image.
2. Put all four nodes on a shared LAN. The scripts expect the nodes to be
   named `node0` … `node3` and reachable by those names — this is what
   CloudLab's default `/etc/hosts` gives you:

   ```
   10.10.1.1  node0
   10.10.1.2  node1
   10.10.1.3  node2
   10.10.1.4  node3
   ```
3. Reserve the nodes for **at least 3 days**. Setup alone takes roughly an
   hour, and a full pass over all experiments takes 1–2 days.

> **Use the aliases `node0`, `node1`, `node2`, and `node3` for all node-to-node
> SSH and workload traffic. Never substitute public IP addresses.** A public
> CloudLab hostname/IP is used only to enter `node0` from your own computer;
> cluster scripts force the four aliases.

---

## 🔧 Software Setup

All commands below run from **`node0`** and contact peers only as
`node0`–`node3`.

### Mode A — Authors' provided cluster

The authors have already completed kernel, dependency, disk, network, and
runtime initialization. Do **not** run the system-wise initializer.

1. Send an SSH **public** key (for example, `~/.ssh/id_ed25519.pub`) to the
   authors through **HotCRP**. Do not send a private key.
2. The authors create a separate user for each evaluator on all four nodes,
   authorize that public key, configure passwordless SSH from `node0` to
   `node1`–`node3`, enable passwordless `sudo`, and return the username and
   `node0` login address.
3. Log in to `node0` and run the user-wise initializer:

   ```bash
   ssh <evaluator-user>@<provided-node0-address>
   git clone https://github.com/OrderLab/LiteLib.git ~/LiteLib
   cd ~/LiteLib
   ./scripts/user_init.sh
   ```

`user_init.sh` configures only the evaluator account: SSH among the
`node0`–`node3` aliases, host keys, and a checkout/update on every node. It then
**verifies** the existing system prerequisite; it does not run `apt`, change
the kernel, resize disks, or reinstall runtime state.

**Expected duration: 2–10 minutes**, mostly repository/submodule transfer. A
system-check failure means the provided cluster needs author attention; do not
replace it with system initialization.

### Mode B — Self-reserved CloudLab cluster

CloudLab must provide passwordless `sudo` and passwordless SSH from `node0` to
the same account on `node1`–`node3`. Then run:

```bash
git clone https://github.com/OrderLab/LiteLib.git ~/LiteLib
cd ~/LiteLib

./scripts/system_init.sh
./scripts/system_init.sh reboot
sudo systemctl reboot

# Reconnect to node0 after it returns:
cd ~/LiteLib
./scripts/system_init.sh post-reboot
./scripts/user_init.sh
```

**Expected system setup time: 30–45 minutes**, plus reboot time. The system-wise
entry point reuses the verified [`setup_cluster.sh`](../scripts/setup_cluster.sh)
implementation to install dependencies/kernel state, grow the root disk,
apply network controls and CPU/runtime settings, and check all four nodes. The
final user-wise step configures the evaluator account and repository.

While system setup runs you will see live progress:

```
==> init: running on 4 nodes in parallel
==> full output: /users/<you>/LiteLib/logs/20260803-042826-init-<node>.log
  [00:05] [node0] step 1/3: limiting the shared control network
  [00:05] [node1] step 1/3: limiting the shared control network
  [00:41] [node0] step 3/3: installing dependencies
  [02:18] [node0] building Boost 1.87.0 from source (this is the slowest step, ~15 min)
  [04:00] still running (4/4): node0: building Boost 1.87.0 ...; node1: ...
```

Centralized evaluator logs use
`logs/<timestamp>-<stage>-<node>.log`. System initialization writes one log per
node; plotting and single-node setup stages use `node0`. To watch one
initialization log in full detail from a second terminal:

```bash
tail -f ~/LiteLib/logs/*-init-node1.log
```

<details>
<summary>What system-wise initialization changes on each node</summary>

1. **Control-network rate limiting** ([`network_limit.sh`](../scripts/network_limit.sh)) —
   caps the *shared*, routable CloudLab control interface (`eno1`) at 100 Mbit/s
   and 10 000 packets/s, and blocks node-to-node traffic over the public
   network. This forces all experiment traffic onto the dedicated 10 GbE
   `10.10.1.0/24` link, so results are not perturbed by other CloudLab tenants.
   The private experiment network is **not** rate limited.
2. **Root filesystem growth** ([`resize_rootfs.sh`](../scripts/resize_rootfs.sh)) —
   grows the root partition to fill the boot disk (≈63 GiB → ≈430 GiB), which
   the LevelDB and MySQL datasets need. The disk is detected from the live
   mount of `/`, because identically provisioned nodes do not always enumerate
   their disks in the same order (we have seen `/dev/sda` on one node and
   `/dev/sdb` on another in the same experiment).
3. **Dependencies** ([`litesys_dependency.sh`](../scripts/litesys_dependency.sh)) —
   build tools, glog, gperftools, libbpf/clang, `perf`, Boost 1.87.0 and
   libevent 2.1.12 (both from source into `/usr/local`), plus kernel
   `6.8.0-52-generic` and its headers. It also pins every core to 2.2 GHz with
   the `performance` governor so latency measurements are stable.

</details>

### Verify and interpret setup

```bash
cd ~/LiteLib
./scripts/system_init.sh check
```

A fully prepared node reports:

```
==> LiteLib initialization check on node1 (2026-08-03T04:43:25-05:00)
-- system persistent state --
  [ OK ] distro packages (38)
  [ OK ] kernel 6.8.0-52-generic installed
  [ OK ] kernel headers 6.8.0-52-generic
  [ OK ] boost 1.87.0  (/usr/local)
  [ OK ] libevent 2.1.12  (/usr/local)
  [ OK ] root filesystem grown  (432 GiB)
  [ OK ] /mydata removed from /etc/fstab
-- system runtime state (re-apply with post_reboot.sh after every reboot) --
  [ OK ] booted into 6.8.0-52-generic
  [ OK ] cpu governor  (performance)
  [ OK ] cpu frequency pinned to 2.2GHz  (2200000-2200000 kHz)
  [ OK ] tc htb shaping on eno1  (rate 100Mbit)
  [ OK ] iptables rate limits  (10000 pkts/sec)
==> node1: initialized

  [ OK ] all 4 node(s) are initialized
```

The check distinguishes two kinds of state, which matters when something looks
wrong later:

* **persistent** — survives a reboot (packages, kernel, `/usr/local` libraries,
  the grown filesystem).
* **runtime** — lives only in kernel state and is **cleared by every reboot**
  (`tc` shaping, `iptables` rules, CPU frequency pinning).

> ⚠️ **After every reboot, re-apply the network rate limits.** A reboot clears
> the `tc` shaping, the `iptables` packet-rate limits and the CPU frequency
> pinning. Without them the shared control network is no longer isolated from
> the experiment traffic, so results get noticeably noisier. Re-apply them on
> every node with:
>
> ```bash
> cd ~/LiteLib && ./scripts/system_init.sh post-reboot
> ```
>
> or, on a single node:
>
> ```bash
> sudo ~/LiteLib/scripts/post_reboot.sh     # rate limits + CPU pinning
> sudo ~/LiteLib/scripts/network_limit.sh   # only the network rate limits
> ```
>
> This takes seconds — it installs and builds nothing. Re-running the full
> system initializer is unnecessary.
> `check_init.sh` will tell you if you forget.

`check_init.sh` exits non-zero when anything is missing, so it can be used in
your own scripts:

```bash
sudo ~/LiteLib/scripts/check_init.sh --system-only --quiet || echo "system not ready"
```

---

## 🚀 Kick-the-Tires

After completing the appropriate setup mode, run:

```bash
cd ~/LiteLib
./scripts/kick_the_tires.sh
```

**Expected duration: 1–3 minutes.** This does not start a paper experiment. It
checks all four evaluator accounts and system prerequisites, confirms that
`node0`–`node3` resolve to the dedicated experiment network, validates the
main experiment wrappers/collectors, and checks writable output directories.
On the authors' provided cluster it also confirms access to the shared Figure
13 database snapshot.

The final line should be:

```text
==> Kick-the-Tires passed
```

---

## 🧰 Setup Command Reference

Every stage can be run on its own, and all of them are safe to re-run — the
script only redoes work that is actually missing.

```bash
cd ~/LiteLib/scripts

./system_init.sh                         # self-reserved: persistent system setup
./system_init.sh reboot                  # reboot peers (then reboot node0)
./system_init.sh post-reboot             # restore runtime state + system check
./system_init.sh check                   # system prerequisite only
./user_init.sh                           # evaluator SSH/repository + system verify

./setup_cluster.sh                        # ssh + clone + init + check (default)
./setup_cluster.sh user-init              # implementation used by user_init.sh
./setup_cluster.sh system-init            # implementation used by system_init.sh
./setup_cluster.sh system-check           # system state, excluding account files
./setup_cluster.sh ssh                    # only bootstrap SSH keys/known_hosts
./setup_cluster.sh clone                  # only clone/update the repository
./setup_cluster.sh init                   # only run init.sh
./setup_cluster.sh check                  # only verify
./setup_cluster.sh post-reboot            # re-apply rate limits + CPU pinning
./setup_cluster.sh reboot                 # reboot peers + re-apply runtime config

./setup_cluster.sh -n "node1 node2" init  # legacy/debug subset operation
./setup_cluster.sh -f init                # force a re-run of an initialized node
./setup_cluster.sh --serial init          # one node at a time, output inline
./setup_cluster.sh --no-progress init     # no live progress, just the summary
./setup_cluster.sh --sync-local clone     # push your local tree instead of cloning
./setup_cluster.sh --help                 # full option list
```

The two evaluator entry points intentionally fix the peer aliases to
`node0 node1 node2 node3`. `setup_cluster.sh` remains available for backward
compatibility and debugging, but rejects literal IP addresses as node targets.

---

## 🩺 Troubleshooting

| Symptom | Cause and fix |
| ------- | ------------- |
| `cannot SSH to <user>@node1 without a password` | Your key is not authorized on that node. Run `ssh-copy-id <user>@node1` once, then re-run the script. |
| `cannot reach git@github.com:...` | No GitHub SSH key on the node. The script automatically falls back to HTTPS; if that also fails, check the node's outbound connectivity. |
| `passwordless sudo is required on every node` | Run `sudo -n true` on the failing node to confirm; CloudLab normally grants this. |
| `cannot determine the root partition` | Unusual disk layout. Override explicitly: `LITELIB_ROOT_DISK=/dev/nvme0n1 LITELIB_ROOT_PART=3 ./setup_cluster.sh init`. |
| `[update] working tree has local changes, not touching it` | You edited files on that node. Commit or `git checkout .` there, then re-run the `clone` stage. |
| A self-reserved node fails during setup | Read `logs/<timestamp>-init-<node>.log`, fix the cause, and re-run `./scripts/system_init.sh`. |
| Everything passes except `booted into 6.8.0-52-generic` | The self-reserved cluster has not completed the reboot sequence. |
| `tc`/`iptables`/`cpu governor` checks fail | Run `./scripts/system_init.sh post-reboot`; runtime state is cleared by every reboot. |
| `user_init.sh` cannot reach a peer | On a provided cluster, ask the authors to verify the evaluator's node0-to-peer SSH setup; on a self-reserved cluster, verify CloudLab's account-wide SSH access. |

---

## 📊 Running the experiments

When `user_init.sh`'s final system verification (or
`./scripts/system_init.sh check`) reports all four nodes initialized, you are
ready to run the workloads. Each experiment has its own driver under `tests/`,
and different experiments use different nodes as server, client, proxy, and
datastore:

* **LevelDB** — `tests/LevelDB/scripts/{server,client}.sh`
* **Memcached** — `tests/Memcached/src/Metastability/setup_scripts/`
* **Redis** — `tests/Redis/`
* **MySQL** — `tests/MySQL/`
* **Figures 1 & 2 (DeathStar motivation)** —
  [step-by-step guide](./ae-fig1-2-motivation.md). Run from this checkout:

  ```bash
  ./scripts/ae_fig1_2_setup.sh
  ./scripts/ae_fig1_2_run.sh
  ./scripts/ae_fig1_2_plot.sh
  ./scripts/ae_fig1_2_cleanup.sh
  ```
* **Figure 13 (Memcached recovery)** —
  [step-by-step guide](./ae-fig13-memcached.md):

  ```bash
  ./scripts/ae_fig13_setup.sh
  ./scripts/ae_fig13_run.sh
  ./scripts/ae_fig13_plot.sh
  ./scripts/ae_fig13_cleanup.sh
  ```
* **Figure 12 (LevelDB recovery)** —
  [recovery guide](./ae-fig12-leveldb.md):

  ```bash
  ./scripts/ae_leveldb_recovery_setup.sh
  ./scripts/ae_leveldb_recovery_run.sh
  ./scripts/ae_leveldb_recovery_plot.sh
  ./scripts/ae_leveldb_recovery_cleanup.sh
  ```
* **Memcached in Figures 14/15/16** —
  [overhead guide](./ae-fig14-16-memcached.md):

  ```bash
  ./scripts/ae_memcached_overhead_setup.sh
  ./scripts/ae_memcached_overhead_run.sh
  ./scripts/ae_memcached_overhead_plot.sh
  ./scripts/ae_memcached_overhead_cleanup.sh
  ```
* **LevelDB in Figures 15/16** —
  [overhead guide](./ae-fig15-16-leveldb.md):

  ```bash
  ./scripts/ae_leveldb_overhead_setup.sh
  ./scripts/ae_leveldb_overhead_run.sh
  ./scripts/ae_leveldb_overhead_plot.sh
  ./scripts/ae_leveldb_overhead_cleanup.sh
  ```
* **Redis in Figures 15/16** —
  [overhead guide](./ae-fig15-16-redis.md):

  ```bash
  ./scripts/ae_redis_overhead_setup.sh
  ./scripts/ae_redis_overhead_run.sh
  ./scripts/ae_redis_overhead_plot.sh
  ./scripts/ae_redis_overhead_cleanup.sh
  ```
* **MySQL in Figures 15/16** —
  [overhead guide](./ae-fig15-16-mysql.md):

  ```bash
  ./scripts/ae_mysql_overhead_setup.sh
  ./scripts/ae_mysql_overhead_run.sh
  ./scripts/ae_mysql_overhead_plot.sh
  ./scripts/ae_mysql_overhead_cleanup.sh
  ```

After running the overhead collectors, regenerate the combined Figures 15/16
with:

```bash
./scripts/ae_overhead_plot.sh
```

After completing the required memory experiments, regenerate Figure 14 with:

```bash
./scripts/ae_memory_overhead_plot.sh
```

* **Table 2 (service gaps)** —
  [service-gap guide](./ae-table2.md):

  ```bash
  ./scripts/ae_table2_setup.sh
  ./scripts/ae_table2_run.sh
  ./scripts/ae_table2_collect.sh
  ./scripts/ae_table2_cleanup.sh
  ```

---

## 📎 Resources

* [`scripts/config.sh`](../scripts/config.sh) — every configurable knob
* [`scripts/setup_cluster.sh`](../scripts/setup_cluster.sh) — cluster driver
* [`scripts/system_init.sh`](../scripts/system_init.sh) — system-wise evaluator entry point
* [`scripts/user_init.sh`](../scripts/user_init.sh) — user-wise evaluator entry point
* [`scripts/kick_the_tires.sh`](../scripts/kick_the_tires.sh) — fast readiness check
* [`scripts/init.sh`](../scripts/init.sh) — per-node initialization
* [`scripts/post_reboot.sh`](../scripts/post_reboot.sh) — re-apply the runtime state a reboot clears
* [`scripts/check_init.sh`](../scripts/check_init.sh) — per-node verification
* [`README.md`](../README.md) — repository layout
