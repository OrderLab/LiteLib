#!/bin/bash
# Fast readiness check for the LiteLib artifact. No long experiment is run.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
NODES=(node0 node1 node2 node3)

ok() { echo "  [ OK ] $*"; }
fail() { echo "  [FAIL] $*" >&2; exit 1; }

echo "==> Checking cluster and evaluator account"
"${SCRIPT_DIR}/setup_cluster.sh" check >/dev/null ||
  fail "cluster/user initialization check failed"
ok "system and user initialization"

for node in "${NODES[@]}"; do
  address=$(getent ahostsv4 "${node}" | awk 'NR == 1 {print $1}')
  [[ "${address}" == 10.10.1.* ]] ||
    fail "${node} does not resolve to the dedicated 10.10.1.x network"
  ssh -o BatchMode=yes -o ConnectTimeout=5 "${node}" true ||
    fail "cannot reach ${node} over its cluster alias"
done
ok "node0-node3 aliases and SSH"

echo "==> Checking required tools and artifact entry points"
for command in git docker python3 ssh rsync; do
  command -v "${command}" >/dev/null || fail "missing command: ${command}"
done
sudo -n true || fail "passwordless sudo is unavailable"
sudo -n docker info >/dev/null ||
  fail "Docker daemon is unavailable"

wrappers=(
  ae_run_with_retry.sh
  ae_fig1_2_setup.sh ae_fig1_2_run.sh ae_fig1_2_plot.sh
  ae_leveldb_recovery_setup.sh ae_leveldb_recovery_run.sh
  ae_fig13_setup.sh ae_fig13_run.sh ae_fig13_plot.sh
  ae_memory_overhead_plot.sh ae_overhead_plot.sh
  ae_table2_setup.sh ae_table2_run.sh ae_table2_collect.sh
)
for wrapper in "${wrappers[@]}"; do
  path="${SCRIPT_DIR}/${wrapper}"
  [ -x "${path}" ] || fail "missing executable wrapper: ${path}"
  bash -n "${path}" || fail "invalid shell syntax: ${path}"
done
python3 -m py_compile \
  "${SCRIPT_DIR}/ae_memory_overhead.py" \
  "${SCRIPT_DIR}/ae_overhead_merge.py" \
  "${SCRIPT_DIR}/ae_table2_collect.py"
rm -rf "${SCRIPT_DIR}/__pycache__"
ok "experiment wrappers and collectors"

mkdir -p "${ROOT}/results" "${ROOT}/figures" "${ROOT}/logs"
for directory in results figures logs; do
  [ -w "${ROOT}/${directory}" ] ||
    fail "${ROOT}/${directory} is not writable"
done
ok "result, figure, and log directories"

snapshot=/srv/litelib-ae/fig13/mysql-snapshot.tar.zst
if [ -e "${snapshot}" ] || [ -e "${snapshot}.sha256" ]; then
  [ -r "${snapshot}" ] && [ -r "${snapshot}.sha256" ] ||
    fail "Figure 13 shared snapshot is not readable"
  ok "Figure 13 shared snapshot"
else
  echo "  [INFO] shared Figure 13 snapshot absent (expected on self-reserved clusters)"
fi

echo "==> Kick-the-Tires passed"
