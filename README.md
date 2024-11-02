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

# Tests

## LevelDB

```bash
cd ./tests/LevelDB/scripts
./server.sh # or ./client.sh to initialize the server or client
```
