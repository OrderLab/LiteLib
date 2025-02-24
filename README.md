# Cascade

## Directories

* `src`: source code for the framework
* `tests`: evaluations
* `scripts`: scripts for setting up the environment in CloudLab

## Set up the environment

```bash
git submodule update --init --recursive
cd ./scripts
sudo ./init.sh
```

Distribute ssh public key across all nodes.

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