## Set up redis-leveldb

```bash
git submodule update --init --recursive
cd redis-leveldb && git apply ../scripts/leveldb/redis-leveldb.patch
```

## Running

```bash
docker compose up -d
docker exec -it leveldb-client bash
cd /workspace/client
# Modify .env
./target/release/client
```

## Plot

```bash
python ./scripts/client/plot.py -f ./client/log1.jsonl ./client/log2.jsonl
```