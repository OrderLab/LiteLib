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
mkdir -p "${OUT}"

run_one() {
  local mode=$1 rep=$2
  local prefix="${mode}-${rep}"
  local experiment_yaml cpu
  case "${mode}" in
  vanilla) experiment_yaml="    experiment_type: full"; cpu=4 ;;
  ebpf) experiment_yaml=$'    experiment_type:\n      lite: [6, "87400"]'; cpu=6 ;;
  checkpoint) experiment_yaml=$'    experiment_type:\n      checkpoint: 60'; cpu=4 ;;
  esac

  cat > /tmp/leveldb-env.yaml <<EOF
benchmark:
  num_keys: 6000000
  key_length: 16
  value_length: 100
  test_duration: ${DURATION}
  rps: 28000
  key_distribution: { zipf: 1.0 }
  write_ratio: 0.2
  timeout: 1s
  retry_count: 5
  inital_iter_count: 5
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
  pool: { max_size: 128 }
EOF
  scp /tmp/leveldb-env.yaml "node1:${REMOTE}/tests/LevelDB/src/tests/client/env.yaml"
  ssh node1 "cd '${REMOTE}/tests/LevelDB/src/tests/client' &&
    ./target/release/client"
  sleep 10
}

for mode in vanilla ebpf checkpoint; do
  for rep in $(seq 1 "${REPEATS}"); do
    echo "==> ${mode} ${rep}/${REPEATS}"
    run_one "${mode}" "${rep}"
  done
done
echo "  [ OK ] LevelDB raw output -> ${OUT}"
