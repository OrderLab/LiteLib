#!/bin/bash
# Run fixed non-crash YCSB overhead experiments.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MAIN=${LITELIB_MAIN_DIR:-${HOME}/LiteLib}
REPEATS=${AE_REPEATS:-3}
OUT=${AE_OUTPUT_DIR:-${MAIN}/results/memcached-overhead/ycsb-$(date +%Y%m%d-%H%M%S)}
mkdir -p "${OUT}"

for mode in vanilla embedded proxy; do
  for i in $(seq 1 "${REPEATS}"); do
    echo "==> ${mode} ${i}/${REPEATS}"
    ssh node3 "cd '${SCRIPT_DIR}' && ./run-single-experiment.sh '${mode}' '${i}'"
  done
done

rsync -az -e ssh "node3:${SCRIPT_DIR}/logs/" "${OUT}/"
echo "  [ OK ] YCSB logs -> ${OUT}"
