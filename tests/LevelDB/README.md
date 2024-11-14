## Set up

### Single node

```bash
git submodule update --init --recursive
cd `git rev-parse --show-toplevel`/tests/LevelDB/src/tests
cd ./redis-leveldb && git apply ../scripts/leveldb/redis-leveldb.patch
docker compose build
docker compose up -d
# Use docker logs leveldb-client / leveldb-server to check if there's any compilation error
docker exec -it leveldb-client bash
cd /workspace/client
```

### Cloudlab

```bash
# in server node
`git rev-parse --show-toplevel`/tests/LevelDB/scripts/server.sh
# in client node
`git rev-parse --show-toplevel`/tests/LevelDB/scripts/client.sh
```

## Running

```bash
# in client node
# Modify .env
./target/release/client
```

## Plot

```bash
# in server node
cd `git rev-parse --show-toplevel`/tests/LevelDB/src/tests/tmp-data
python3 ../scripts/client/plot.py -f ./full.jsonl ./lite.jsonl ./checkpoint.jsonl -t 120 -j 2
```
