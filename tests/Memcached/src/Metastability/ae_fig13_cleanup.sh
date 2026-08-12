#!/bin/bash
# Remove Figure 13 runtime state. The verified archive remains reusable.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=ae_fig13_common.sh
source "${SCRIPT_DIR}/ae_fig13_common.sh"

fig13_info "stopping Figure 13 processes and containers"
if fig13_docker inspect memcached >/dev/null 2>&1; then
  fig13_docker exec memcached bash -lc '
    for name in memcached LiteMemcached lite_cli; do
      for pid in $(pgrep -x "$name" 2>/dev/null || true); do
        kill "$pid" 2>/dev/null || true
      done
    done
    rm -rf /tmp/memcached.sock /tmp/lite_memcached /tmp/checkpoint-data \
      /tmp/memcached.log /tmp/lite_memcached.log /tmp/lite_cli-*.log \
      /tmp/checkpoint-*.log
  ' || true
fi

if fig13_docker inspect mysql >/dev/null 2>&1; then
  fig13_docker update --cpus 4 mysql >/dev/null || true
fi

if [ "${FIG13_PRESERVE_DB_VOLUME:-0}" -eq 1 ]; then
  fig13_compose down --remove-orphans
else
  fig13_compose down --volumes --remove-orphans
fi

fig13_info "removing generated working files"
sudo -n rm -rf -- \
  "${FIG13_DIR}/LoadGenerator/traces" \
  "${FIG13_DIR}/LoadGenerator/result_stats" \
  "${FIG13_DIR}/LoadGenerator/experiment_plots" \
  "${FIG13_DIR}/LoadGenerator/result_plot"

if [ "${FIG13_PRESERVE_DB_VOLUME:-0}" -eq 1 ]; then
  fig13_ok "runtime cleaned; database volume, archive, builds and results preserved"
else
  fig13_ok "runtime and database volume cleaned; archive, builds and results preserved"
fi
