#!/bin/bash
set -euo pipefail
for node in node0 node1; do
  ssh "$node" sudo -n bash -s <<'REMOTE_SCRIPT' || true
    for pattern in redis-leveldb LiteLevelDB /lite_cli criu; do
      for pid in $(pgrep -f "$pattern" 2>/dev/null || true); do
        kill "$pid" 2>/dev/null || true
      done
    done
    rm -rf /tmp/lite_LevelDB /tmp/redis-leveldb.sock
REMOTE_SCRIPT
done
echo "  [ OK ] LevelDB runtime cleaned; builds/results preserved"
