# LiteLib Artifact Evaluation Guide

Welcome to the artifact evaluation guide for **LiteLib** (NSDI '27), the
framework behind *"LiteLib: Containing Failure Impact for Stateful Applications
with Compact Replicas"*.

This document covers **environment setup**. Once the cluster is up, follow the
per-experiment guides linked from [Running the experiments](#-running-the-experiments).

> **Note:** we may push fixes to this repository during the evaluation period.
> Please re-run `scripts/setup_cluster.sh clone` to pull the latest version
> before you start.

## ✅ Checklist

- [ ] Instantiated a 4-node CloudLab `c220g5` cluster with Ubuntu 22.04
- [ ] Confirmed passwordless `sudo` and node-to-node SSH work
- [ ] Ran `scripts/setup_cluster.sh` from `node0`
- [ ] Rebooted the cluster into kernel `6.8.0-52-generic`
- [ ] Re-applied the network rate limits after the reboot (`setup_cluster.sh post-reboot`)
- [ ] `scripts/setup_cluster.sh check` reports **all 4 nodes initialized**
- [ ] Reproduced [Figures 1 & 2](./ae-fig1-2-motivation.md)
- [ ] Reproduced [Figure 13](./ae-fig13-memcached.md)
- [ ] Reproduced LevelDB [Figure 12](./ae-leveldb-recovery.md)
- [ ] Reproduced [Figure 14](./ae-fig14-memory.md)
- [ ] Reproduced Memcached's [Figures 14/15/16](./ae-fig14-16-memcached.md)
- [ ] Reproduced LevelDB's [Figures 15/16](./ae-leveldb-overhead.md)
- [ ] Reproduced Redis's [Figures 15/16](./ae-redis-overhead.md)
- [ ] Reproduced MySQL's [Figures 15/16](./ae-mysql-overhead.md)
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
| Storage   | 480 GB SATA SSD (grown to ~430 GiB usable on `/`) + 1.2 TB HDD |
| Network   | 10 GbE experiment network on `10.10.1.0/24`               |
| OS image  | `UBUNTU22-64-STD` (Ubuntu 22.04 LTS)                      |
| Kernel    | `6.8.0-52-generic` (installed by the setup scripts)       |

### Instantiating the cluster on CloudLab

1. Create an experiment with **4 raw PC nodes** of type `c220g5` and the
   `UBUNTU22-64-STD` image.
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

> **Different node names or a different node count?**
> Nothing is hardcoded. Pass `--nodes "n0 n1 n2 n3"` to the setup script, or
> export `LITELIB_NODES`. See [`scripts/config.sh`](../scripts/config.sh) for
> every knob.

---

## 🔧 Software Setup

Everything below is driven from **`node0`**; the script takes care of the other
three nodes over SSH. The whole process is a single command, but read the
prerequisites first.

### Prerequisites

The setup script needs three things to be true on every node. On a stock
CloudLab experiment they already are:

1. **Passwordless `sudo`** — verify with `sudo -n true`.
2. **Passwordless SSH from `node0` to the other nodes.** CloudLab installs your
   account cluster-wide, so `ssh node1` should just work. If it asks for a
   password, run `ssh-copy-id node1` (and likewise for `node2`/`node3`) once.
3. **Access to GitHub.** Either add an SSH key to your GitHub account, or let
   the script fall back to HTTPS for this public repository.

The script handles the interactive prompts that would otherwise stall an
unattended run — SSH host-key fingerprint confirmations are pre-seeded with
`ssh-keyscan`, and `apt` runs under `DEBIAN_FRONTEND=noninteractive` so it never
stops on a configuration-file or service-restart question.

### Step 1 — Get the repository onto `node0`

```bash
git clone https://github.com/OrderLab/LiteLib.git ~/LiteLib
cd ~/LiteLib
```

### Step 2 — Set up the whole cluster

```bash
cd ~/LiteLib/scripts
./setup_cluster.sh
```

That single command performs, on **all four nodes in parallel**:

| Stage   | What it does                                                                       |
| ------- | ---------------------------------------------------------------------------------- |
| `ssh`   | Generates/derives the SSH keypair, authorizes it cluster-wide, and pre-seeds `known_hosts` for GitHub and every node (for both your account and `root`). |
| `clone` | Clones or fast-forwards this repository into `~/LiteLib` on every node and checks out submodules. |
| `init`  | Runs [`scripts/init.sh`](../scripts/init.sh): rate-limits the shared control network, grows the root filesystem, and installs all dependencies. |
| `check` | Runs [`scripts/check_init.sh`](../scripts/check_init.sh) on every node and prints a per-node report. |

**Expected duration: 30–45 minutes**, dominated by building Boost from source.
While it runs you will see live progress:

```
==> init: running on 4 nodes in parallel
==> full output: /users/<you>/LiteLib/logs/20260803-042826-init-<node>.log
  [00:05] [node0] step 1/3: limiting the shared control network
  [00:05] [node1] step 1/3: limiting the shared control network
  [00:41] [node0] step 3/3: installing dependencies
  [02:18] [node0] building Boost 1.87.0 from source (this is the slowest step, ~15 min)
  [04:00] still running (4/4): node0: building Boost 1.87.0 ...; node1: ...
```

The complete, unabridged output of each node is written to
`logs/<timestamp>-<stage>-<node>.log`. To watch one node in full detail from a
second terminal:

```bash
tail -f ~/LiteLib/logs/*-init-node1.log
```

<details>
<summary>What <code>init.sh</code> actually changes on each node</summary>

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

### Step 3 — Reboot into the LiteLib kernel

`init.sh` installs kernel `6.8.0-52-generic` but cannot activate it. Reboot the
cluster:

```bash
./setup_cluster.sh reboot
```

This reboots the peer nodes, waits for them to come back, and re-applies the
settings that a reboot clears (the `tc`/`iptables` rate limits and the CPU
frequency pinning). It deliberately does **not** reboot the node you are
logged into. Reboot `node0` yourself afterwards and re-apply its runtime
configuration:

```bash
sudo systemctl reboot
# once it is back up:
cd ~/LiteLib/scripts && ./setup_cluster.sh post-reboot
```

### Step 4 — Verify the cluster

```bash
./setup_cluster.sh check
```

A fully prepared node reports:

```
==> LiteLib initialization check on node1 (2026-08-03T04:43:25-05:00)
-- persistent state --
  [ OK ] distro packages (38)
  [ OK ] kernel 6.8.0-52-generic installed
  [ OK ] kernel headers 6.8.0-52-generic
  [ OK ] boost 1.87.0  (/usr/local)
  [ OK ] libevent 2.1.12  (/usr/local)
  [ OK ] root filesystem grown  (432 GiB)
  [ OK ] /mydata removed from /etc/fstab
  [ OK ] ssh authorized_keys present
-- runtime state (re-apply with post_reboot.sh after every reboot) --
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
> cd ~/LiteLib/scripts && ./setup_cluster.sh post-reboot
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
> `./setup_cluster.sh init` is equally correct, just much slower.
> `check_init.sh` will tell you if you forget.

`check_init.sh` exits non-zero when anything is missing, so it can be used in
your own scripts:

```bash
sudo ~/LiteLib/scripts/check_init.sh --persistent-only --quiet || echo "needs init"
```

---

## 🧰 Setup Command Reference

Every stage can be run on its own, and all of them are safe to re-run — the
script only redoes work that is actually missing.

```bash
./setup_cluster.sh                        # ssh + clone + init + check (default)
./setup_cluster.sh ssh                    # only bootstrap SSH keys/known_hosts
./setup_cluster.sh clone                  # only clone/update the repository
./setup_cluster.sh init                   # only run init.sh
./setup_cluster.sh check                  # only verify
./setup_cluster.sh post-reboot            # re-apply rate limits + CPU pinning
./setup_cluster.sh reboot                 # reboot peers + re-apply runtime config

./setup_cluster.sh -n "node1 node2" init  # operate on a subset of nodes
./setup_cluster.sh -f init                # force a re-run of an initialized node
./setup_cluster.sh --serial init          # one node at a time, output inline
./setup_cluster.sh --no-progress init     # no live progress, just the summary
./setup_cluster.sh --sync-local clone     # push your local tree instead of cloning
./setup_cluster.sh --help                 # full option list
```

Cluster-wide settings live in [`scripts/config.sh`](../scripts/config.sh) and can
all be overridden from the environment, e.g.:

```bash
LITELIB_NODES="n0 n1 n2 n3" LITELIB_CTRL_IFACE=eth0 ./setup_cluster.sh
```

---

## 🩺 Troubleshooting

| Symptom | Cause and fix |
| ------- | ------------- |
| `cannot SSH to <user>@node1 without a password` | Your key is not authorized on that node. Run `ssh-copy-id <user>@node1` once, then re-run the script. |
| `cannot reach git@github.com:...` | No GitHub SSH key on the node. The script automatically falls back to HTTPS; if that also fails, check the node's outbound connectivity. |
| `passwordless sudo is required on every node` | Run `sudo -n true` on the failing node to confirm; CloudLab normally grants this. |
| `cannot determine the root partition` | Unusual disk layout. Override explicitly: `LITELIB_ROOT_DISK=/dev/nvme0n1 LITELIB_ROOT_PART=3 ./setup_cluster.sh init`. |
| `[update] working tree has local changes, not touching it` | You edited files on that node. Commit or `git checkout .` there, then re-run the `clone` stage. |
| A node fails mid-`init` | Read `logs/<timestamp>-init-<node>.log`, fix the cause, and re-run `./setup_cluster.sh -n "<node>" init`. Nothing needs to be undone first. |
| Everything passes except `booted into 6.8.0-52-generic` | You have not rebooted yet — run `./setup_cluster.sh reboot`. |
| `tc`/`iptables`/`cpu governor` checks fail | The node was rebooted without re-applying runtime state. Run `./setup_cluster.sh post-reboot`. |

---

## 📊 Running the experiments

With `./setup_cluster.sh check` reporting all four nodes initialized, you are
ready to run the workloads. Each experiment has its own driver under
`tests/`, and different experiments use different nodes as server, client,
proxy, and datastore:

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
  [recovery guide](./ae-leveldb-recovery.md):

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
  [overhead guide](./ae-leveldb-overhead.md):

  ```bash
  ./scripts/ae_leveldb_overhead_setup.sh
  ./scripts/ae_leveldb_overhead_run.sh
  ./scripts/ae_leveldb_overhead_plot.sh
  ./scripts/ae_leveldb_overhead_cleanup.sh
  ```
* **Redis in Figures 15/16** —
  [overhead guide](./ae-redis-overhead.md):

  ```bash
  ./scripts/ae_redis_overhead_setup.sh
  ./scripts/ae_redis_overhead_run.sh
  ./scripts/ae_redis_overhead_plot.sh
  ./scripts/ae_redis_overhead_cleanup.sh
  ```
* **MySQL in Figures 15/16** —
  [overhead guide](./ae-mysql-overhead.md):

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
* [`scripts/init.sh`](../scripts/init.sh) — per-node initialization
* [`scripts/post_reboot.sh`](../scripts/post_reboot.sh) — re-apply the runtime state a reboot clears
* [`scripts/check_init.sh`](../scripts/check_init.sh) — per-node verification
* [`README.md`](../README.md) — repository layout
