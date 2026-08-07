#!/bin/bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(git -C "${SCRIPT_DIR}" rev-parse --show-toplevel)"
REMOTE=${LITELIB_WORKTREE_DIR:-${ROOT}}
for node in node0 node1 node2 node3; do
  ssh "${node}" "
    helper='${REMOTE}/tests/MySQL/src/tests/scripts/ae_overhead_node.sh'
    if [ -x \"\${helper}\" ]; then
      \"\${helper}\" cleanup
    else
      for name in mysqld LiteMySQL ndbd ndb_mgmd; do
        for pid in \$(pgrep -x \"\${name}\" 2>/dev/null || true); do
          kill \"\${pid}\" 2>/dev/null || true
        done
      done
      sudo -n systemctl stop proxysql orchestrator 2>/dev/null || true
      sudo -n iptables -D INPUT -p tcp --dport 50000 \
        -m comment --comment litelib-table2-ndb -j DROP \
        >/dev/null 2>&1 || true
      rm -rf /tmp/litelib-ae-mysql
      sudo -n rm -f /tmp/mysql.sock /tmp/lite_mysql \
        /tmp/mysql_full_to_lite /tmp/mysql_lite_to_full
    fi
  " || true
done
echo "  [ OK ] MySQL runtime cleaned; builds/results preserved"
