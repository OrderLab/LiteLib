#!/bin/bash
# Run 3 non-crash repetitions of vanilla, eBPF LiteLib and checkpoint.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(git -C "${SCRIPT_DIR}" rev-parse --show-toplevel)"
REMOTE=${LITELIB_WORKTREE_DIR:-${ROOT}}
MAIN=${LITELIB_MAIN_DIR:-${HOME}/LiteLib}
OUT=${AE_OUTPUT_DIR:-${MAIN}/results/leveldb-overhead/$(date +%Y%m%d-%H%M%S)}
REPEATS=${AE_REPEATS:-3}
DURATION=${AE_DURATION:-3m}
CRASH_TIME=${AE_CRASH_TIME:-180s}
NUM_KEYS=${AE_NUM_KEYS:-6000000}
INIT_RPS=${AE_INIT_RPS:-40000}
RPS=${AE_RPS:-40000}
MODES=${AE_MODES:-"vanilla ebpf checkpoint"}
mkdir -p "${OUT}"
ENV_FILE=$(mktemp --tmpdir leveldb-overhead-env.XXXXXX.yaml)
trap 'rm -f "${ENV_FILE}"' EXIT

run_one() {
  local mode=$1 rep=$2
  local prefix="${mode}-${rep}"
  local experiment_yaml cpu
  case "${mode}" in
  vanilla) experiment_yaml="    experiment_type: full"; cpu=3 ;;
  ebpf) experiment_yaml=$'    experiment_type:\n      ebpf: [3, "131100"]'; cpu=4.5 ;;
  checkpoint) experiment_yaml=$'    experiment_type:\n      checkpoint: 60'; cpu=3 ;;
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
  scp "${ENV_FILE}" "node1:${REMOTE}/tests/LevelDB/src/tests/client/env.yaml"
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

for mode in ${MODES}; do
  for rep in $(seq 1 "${REPEATS}"); do
    echo "==> ${mode} ${rep}/${REPEATS}"
    run_one "${mode}" "${rep}"
  done
done
echo "  [ OK ] LevelDB raw output -> ${OUT}"
