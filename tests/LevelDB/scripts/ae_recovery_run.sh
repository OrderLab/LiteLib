#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(git -C "${SCRIPT_DIR}" rev-parse --show-toplevel)"
REMOTE=${LITELIB_WORKTREE_DIR:-${ROOT}}
MAIN=${LITELIB_MAIN_DIR:-${HOME}/LiteLib}
OUT=${AE_OUTPUT_DIR:-${MAIN}/results/leveldb-recovery/$(date +%Y%m%d-%H%M%S)}
MODES=${AE_MODES:-"full lite checkpoint"}
REPEATS=${AE_REPEATS:-1}
DURATION=${AE_DURATION:-3m}
CRASH_TIME=${AE_CRASH_TIME:-60s}
CRASH_SECOND=${CRASH_TIME%s}
NUM_KEYS=${AE_NUM_KEYS:-6000000}
INIT_RPS=${AE_INIT_RPS:-80000}
RPS=${AE_RPS:-28000}
FULL_CPU=${AE_FULL_CPU:-4}
LITE_CPU=${AE_LITE_CPU:-6}
CHECKPOINT_CPU=${AE_CHECKPOINT_CPU:-4}
LITE_THREADS=${AE_LITE_THREADS:-6}
LITE_SIZE=${AE_LITE_SIZE:-87400}
mkdir -p "${OUT}"
ENV_FILE=$(mktemp --tmpdir leveldb-recovery-env.XXXXXX.yaml)

cleanup_runtime() {
  rm -f "${ENV_FILE}"
  "${SCRIPT_DIR}/ae_overhead_cleanup.sh" >/dev/null 2>&1 || true
}
trap cleanup_runtime EXIT

run_one() {
  local mode=$1 rep=$2 prefix experiment_yaml cpu
  prefix=${mode}
  [ "${REPEATS}" -eq 1 ] || prefix="${mode}-${rep}"
  case "${mode}" in
  full)
    experiment_yaml="    experiment_type: full"
    cpu=${FULL_CPU}
    ;;
  lite)
    experiment_yaml=$'    experiment_type:\n      lite: ['"${LITE_THREADS}"$', "'"${LITE_SIZE}"'"]'
    cpu=${LITE_CPU}
    ;;
  checkpoint)
    experiment_yaml=$'    experiment_type:\n      checkpoint: 30'
    cpu=${CHECKPOINT_CPU}
    ;;
  *)
    echo "unknown mode: ${mode}" >&2
    return 2
    ;;
  esac

  cat >"${ENV_FILE}" <<EOF
benchmark:
  num_keys: ${NUM_KEYS}
  key_length: 16
  value_length: 100
  test_duration: ${DURATION}
  rps: ${RPS}
  init_rps: ${INIT_RPS}
  key_distribution: { zipf: 1.0 }
  write_ratio: 0.2
  timeout: 1s
  retry_count: 5
  inital_iter_count: 1
  enable_connection_pool: true
  check_correctness: false
  work_dir: ${OUT}
  file_prefix: ${prefix}
  remote_script:
    root_dir: ${REMOTE}
${experiment_yaml}
    cpu_limit: ${cpu}
    remote_addr: node0
    remote_ssh_port: "22"
    write_buffer_size: 536870912
    crash_time: ${CRASH_TIME}
redis:
  connection:
    addr: { Tcp: [node0, 6379] }
    db: 0
  pool: { max_size: 64 }
EOF
  scp -q "${ENV_FILE}" \
    "node1:${REMOTE}/tests/LevelDB/src/tests/client/env.yaml"
  if ! ssh node1 "cd '${REMOTE}/tests/LevelDB/src/tests/client' &&
      ./target/release/client" >"${OUT}/${prefix}-client.log" 2>&1; then
    tail -100 "${OUT}/${prefix}-client.log" >&2
    return 1
  fi
  sleep 10
  python3 "${ROOT}/tests/LevelDB/src/tests/scripts/client/plot_preprocess.py" \
    -f "${OUT}/${prefix}.jsonl" -j 1 \
    >"${OUT}/${prefix}-preprocess.log" 2>&1
  python3 "${ROOT}/tests/LevelDB/src/tests/scripts/client/plot_prune.py" \
    -f "${OUT}/${prefix}.stat.json" \
    >>"${OUT}/${prefix}-preprocess.log" 2>&1
  rm -f "${OUT}/${prefix}.stat.json"
}

cat >"${OUT}/metadata.json" <<EOF
{
  "crash_second": ${CRASH_SECOND},
  "rps": ${RPS},
  "full_cpu": ${FULL_CPU},
  "lite_cpu": ${LITE_CPU},
  "checkpoint_cpu": ${CHECKPOINT_CPU},
  "lite_threads": ${LITE_THREADS},
  "lite_size": ${LITE_SIZE}
}
EOF

for mode in ${MODES}; do
  for rep in $(seq 1 "${REPEATS}"); do
    echo "==> ${mode} ${rep}/${REPEATS}"
    run_one "${mode}" "${rep}"
  done
done
echo "  [ OK ] LevelDB recovery output -> ${OUT}"
