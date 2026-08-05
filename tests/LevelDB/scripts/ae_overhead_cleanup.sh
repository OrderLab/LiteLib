#!/bin/bash
set -euo pipefail
for node in node0 node1; do
  ssh "$node" '
    for name in redis-leveldb redis-leveldb-vanilla LiteLevelDB lite_cli criu; do
      for pid in $(pgrep -x "$name" 2>/dev/null || true); do kill "$pid" 2>/dev/null || true; done
    done
    rm -rf /tmp/lite_LevelDB /tmp/redis-leveldb.sock
  ' || true
done
echo "  [ OK ] LevelDB runtime cleaned; builds/results preserved"
