## Set up redis-leveldb

```bash
git submodule update --init --recursive
cd redis-leveldb && git apply ../scripts/leveldb/redis-leveldb.patch
```

## Set up lite-version

```bash
# Need to compile the lite version first
mkdir -p ./tests/server
cp ./lite-version/build/LiteLevelDB ./tests/server
cp ./lite-version/build/Lite/lite_cli ./tests/server
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