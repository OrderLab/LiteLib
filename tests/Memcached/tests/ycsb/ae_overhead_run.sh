#!/bin/bash
# Run fixed non-crash YCSB overhead experiments.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MAIN=${LITELIB_MAIN_DIR:-${HOME}/LiteLib}
REPEATS=${AE_REPEATS:-3}
MODES=${AE_MODES:-"vanilla embedded proxy"}
TARGET=${AE_TARGET:-40000}
OPERATION_COUNT=${AE_OPERATION_COUNT:-$((TARGET * 60))}
OUT=${AE_OUTPUT_DIR:-${MAIN}/results/memcached-overhead/ycsb-$(date +%Y%m%d-%H%M%S)}
mkdir -p "${OUT}"

ssh node3 "rm -rf '${SCRIPT_DIR}/logs' && mkdir -p '${SCRIPT_DIR}/logs'"

for mode in ${MODES}; do
  for i in $(seq 1 "${REPEATS}"); do
    echo "==> ${mode} ${i}/${REPEATS}"
    ssh node3 "cd '${SCRIPT_DIR}' &&
      AE_TARGET='${TARGET}' AE_OPERATION_COUNT='${OPERATION_COUNT}' \
      ./run-single-experiment.sh '${mode}' '${i}'"
  done
done

rsync -az -e ssh "node3:${SCRIPT_DIR}/logs/" "${OUT}/"
echo "  [ OK ] YCSB logs -> ${OUT}"
