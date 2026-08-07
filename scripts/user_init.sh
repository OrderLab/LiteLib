#!/bin/bash
#
# User-wise LiteLib initialization.  This configures only the evaluator
# account's cluster SSH and repository checkout, then verifies (without
# reinstalling) the system state prepared by the authors or system_init.sh.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export LITELIB_NODES="node0 node1 node2 node3"

if [ "$#" -ne 0 ]; then
  echo "usage: $0" 1>&2
  exit 2
fi

exec "${SCRIPT_DIR}/setup_cluster.sh" user-init
