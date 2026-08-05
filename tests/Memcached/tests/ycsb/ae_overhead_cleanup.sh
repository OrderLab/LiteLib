#!/bin/bash
# Stop YCSB/Memcached runtime state while preserving builds and copied results.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "==> Stopping YCSB client"
ssh node2 '
  for pid in $(pgrep -f "site.ycsb.Client" 2>/dev/null || true); do
    [ "$pid" = "$$" ] || kill "$pid" 2>/dev/null || true
  done
' || true

echo "==> Stopping Memcached variants"
ssh node3 "
  cd '${SCRIPT_DIR}'
  ./stop-all.sh
  for pid in \$(pgrep -f 'ssh node2.*YCSB' 2>/dev/null || true); do
    [ \"\$pid\" = \"\$\$\" ] || kill \"\$pid\" 2>/dev/null || true
  done
  rm -rf '${SCRIPT_DIR}/logs'
" || true

echo "  [ OK ] overhead runtime cleaned; builds and ~/LiteLib/results are preserved"
