#!/bin/bash
set -euo pipefail
for pid in $(ssh node3 "pgrep -x redis-lite 2>/dev/null || true"); do
  ssh node3 "kill '${pid}' 2>/dev/null || true"
done
for pid in $(ssh node3 "lsof -t -iTCP:16379 2>/dev/null || true"); do
  ssh node3 "kill '${pid}' 2>/dev/null || true"
done
ssh node3 "rm -f /tmp/redis.sock /tmp/lite_Redis /dev/shm/lite_shared_memory"
echo "  [ OK ] Redis proxy runtime cleaned"
