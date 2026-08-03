# Cascade

## Directories

* `src`: source code for the framework
* `tests`: evaluations
* `scripts`: scripts for setting up the environment in CloudLab
* `docs`: documentation, including the [artifact evaluation guide](docs/ae.md)

## Artifact evaluation

If you are evaluating this artifact for NSDI '27, start with
**[docs/ae.md](docs/ae.md)**.

## Set up the environment

From `node0` of a 4-node CloudLab cluster, this sets up *every* node — SSH keys,
repository checkout, dependencies — and then verifies the result:

```bash
cd ./scripts
./setup_cluster.sh
```

Reboot into the kernel it installed, then re-verify:

```bash
./setup_cluster.sh reboot
./setup_cluster.sh check
```

To prepare only the machine you are on:

```bash
git submodule update --init --recursive
cd ./scripts
sudo ./init.sh          # apply the setup
sudo ./check_init.sh    # verify it
```

Note that `init.sh` also re-applies settings that a reboot clears (CPU
frequency pinning and the network rate limits), so run it again after every
reboot.

# Tests

## LevelDB

```bash
cd ./tests/LevelDB/scripts
./server.sh # or ./client.sh to initialize the server or client
```

## Memcached

```bash
cd ./tests/Memcached/src/Metastability/setup_scripts

# on node0
sudo ./setup_server.sh

# on node1
./setup_memcached.sh

# on node2
sudo ./setup_mysql.sh

# on node3
./setup_client.sh
```