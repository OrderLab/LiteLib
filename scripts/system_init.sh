#!/bin/bash
#
# System-wise LiteLib cluster initialization for a self-reserved CloudLab
# cluster.  This wrapper deliberately reuses setup_cluster.sh for all work.
#
# Usage:
#   ./system_init.sh                 # install persistent state on all nodes
#   ./system_init.sh reboot          # reboot peers; reboot node0 separately
#   ./system_init.sh post-reboot     # restore runtime state and verify it
#   ./system_init.sh check           # verify all system prerequisites

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export LITELIB_NODES="node0 node1 node2 node3"

if [ "$#" -gt 1 ]; then
  echo "usage: $0 [setup|reboot|post-reboot|check]" 1>&2
  exit 2
fi

case "${1:-setup}" in
setup)
  # The system scripts must first be reachable on every node.  These stages
  # use only the node0-node3 aliases and are safe to repeat.
  "${SCRIPT_DIR}/setup_cluster.sh" clone
  "${SCRIPT_DIR}/setup_cluster.sh" system-init
  ;;
reboot)
  "${SCRIPT_DIR}/setup_cluster.sh" reboot
  ;;
post-reboot)
  "${SCRIPT_DIR}/setup_cluster.sh" post-reboot
  "${SCRIPT_DIR}/setup_cluster.sh" system-check
  ;;
check)
  "${SCRIPT_DIR}/setup_cluster.sh" system-check
  ;;
*)
  echo "usage: $0 [setup|reboot|post-reboot|check]" 1>&2
  exit 2
  ;;
esac
