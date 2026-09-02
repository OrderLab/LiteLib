#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(git -C "${SCRIPT_DIR}" rev-parse --show-toplevel)"
REMOTE=${LITELIB_WORKTREE_DIR:-${ROOT}}
JOBS=${AE_JOBS:-32}
SYSBENCH_COMMIT=805825fa81f633a7477f15ecdc152441e4ef4c83
NDB_TARBALL=mysql-cluster-gpl-7.6.36-linux-glibc2.17-x86_64.tar.gz
NDB_URL="https://cdn.mysql.com//Downloads/MySQL-Cluster-7.6/${NDB_TARBALL}"

echo "==> Syncing MySQL experiment source"
for node in node0 node1 node2 node3; do
  [ "${node}" = "$(hostname -s)" ] && continue
  ssh "${node}" "mkdir -p '${REMOTE}/src' '${REMOTE}/tests/MySQL'"
  rsync -az --delete --exclude .git --exclude build/ \
    -e ssh "${ROOT}/src/" "${node}:${REMOTE}/src/"
  rsync -az --delete --exclude .git --exclude build-ae/ \
    -e ssh "${ROOT}/tests/MySQL/" "${node}:${REMOTE}/tests/MySQL/"
done

echo "==> Installing dependencies"
pids=()
for node in node0 node1 node2 node3; do
  ssh "${node}" "sudo -n apt-get update -qq &&
    sudo -n DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
      automake bison build-essential cmake flex git libaio-dev libevent-dev \
      libgoogle-glog-dev libncurses5 libncurses5-dev libssl-dev libtool \
      default-libmysqlclient-dev numactl pkg-config python3-pip &&
    python3 -m pip install --user -q psutil numpy" &
  pids+=("$!")
done
for pid in "${pids[@]}"; do wait "${pid}"; done

echo "==> Building MySQL 5.7 on node2"
ssh node2 bash -s -- "${REMOTE}" "${JOBS}" <<'REMOTE_SCRIPT'
set -euo pipefail
ROOT=$1
JOBS=$2
SOURCE="${ROOT}/tests/MySQL/src/mysql-server"
BUILD="${SOURCE}/build-ae"
INSTALL="${HOME}/mysql-ae"
sudo -n mkdir -p "${HOME}/dependencies"
sudo -n chown -R "$(id -u):$(id -g)" "${HOME}/dependencies"
rm -rf "${BUILD}" "${INSTALL}"
cmake -S "${SOURCE}" -B "${BUILD}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="${INSTALL}" \
  -DDOWNLOAD_BOOST=1 \
  -DWITH_BOOST="${HOME}/dependencies/mysql-boost" \
  -DWITH_SSL=system
cmake --build "${BUILD}" -j"${JOBS}"
cmake --install "${BUILD}"
test -x "${INSTALL}/bin/mysqld"
REMOTE_SCRIPT

echo "==> Distributing MySQL binaries"
for node in node0 node3; do
  ssh "${node}" "rm -rf '${HOME}/mysql-ae' && mkdir -p '${HOME}/mysql-ae'"
  ssh node2 "tar -C '${HOME}/mysql-ae' -cf - ." |
    ssh "${node}" "tar -C '${HOME}/mysql-ae' -xf -"
done

echo "==> Building LiteMySQL on node0"
ssh node0 bash -s -- "${REMOTE}" "${JOBS}" <<'REMOTE_SCRIPT'
set -euo pipefail
ROOT=$1
JOBS=$2
LITE="${ROOT}/tests/MySQL/src/lite-version"
make -C "${LITE}/lib/sql-parser" -j"${JOBS}"
sudo -n make -C "${LITE}/lib/sql-parser" install
cmake -S "${LITE}" -B "${LITE}/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "${LITE}/build" -j"${JOBS}"
test -x "${LITE}/build/LiteMySQL"
git -C "${ROOT}" restore \
  tests/MySQL/src/lite-version/lib/sql-parser/src/parser/flex_lexer.cpp \
  tests/MySQL/src/lite-version/lib/sql-parser/src/parser/flex_lexer.h
REMOTE_SCRIPT

echo "==> Installing ProxySQL and Orchestrator on node0"
ssh node0 bash -s <<'REMOTE_SCRIPT'
set -euo pipefail
if ! command -v proxysql >/dev/null; then
  sudo -n wget -q -O /usr/share/keyrings/proxysql-2.7.x-keyring.gpg \
    https://repo.proxysql.com/ProxySQL/proxysql-2.7.x/repo_pub_key.gpg
  echo "deb [signed-by=/usr/share/keyrings/proxysql-2.7.x-keyring.gpg] https://repo.proxysql.com/ProxySQL/proxysql-2.7.x/$(lsb_release -sc)/ ./" |
    sudo -n tee /etc/apt/sources.list.d/proxysql.list >/dev/null
  sudo -n apt-get update -qq
  sudo -n DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
    proxysql=2.7.3 mysql-client
fi
if ! command -v /usr/local/orchestrator/orchestrator >/dev/null; then
  wget -q -O /tmp/orchestrator.deb \
    https://github.com/openark/orchestrator/releases/download/v3.2.6/orchestrator_3.2.6_amd64.deb
  sudo -n apt-get install -y -qq /tmp/orchestrator.deb
fi
sudo -n systemctl stop proxysql orchestrator 2>/dev/null || true
REMOTE_SCRIPT

echo "==> Installing NDB Cluster binaries"
sudo -n mkdir -p "${HOME}/dependencies"
sudo -n chown "$(id -u):$(id -g)" "${HOME}/dependencies"
if [ ! -s "${HOME}/dependencies/${NDB_TARBALL}" ]; then
  wget -q -O "${HOME}/dependencies/${NDB_TARBALL}" "${NDB_URL}"
fi
for node in node0 node2 node3; do
  ssh "${node}" "sudo -n rm -rf /opt/mysql-ndb &&
    sudo -n mkdir -p /opt/mysql-ndb"
  cat "${HOME}/dependencies/${NDB_TARBALL}" |
    ssh "${node}" "sudo -n tar -xzf - -C /opt/mysql-ndb --strip-components=1"
  ssh "${node}" "test -x /opt/mysql-ndb/bin/mysqld"
done

echo "==> Building the patched sysbench client on node1"
ssh node1 bash -s -- "${REMOTE}" "${SYSBENCH_COMMIT}" "${JOBS}" <<'REMOTE_SCRIPT'
set -euo pipefail
ROOT=$1
SYSBENCH_COMMIT=$2
JOBS=$3
DIR="${HOME}/sysbench"

# Execute the setup commands documented in client/entrypoint.sh.
rm -rf "${DIR}"
git clone -q https://github.com/akopytov/sysbench.git "${DIR}"
git -C "${DIR}" checkout -q --detach "${SYSBENCH_COMMIT}"
git -C "${DIR}" apply \
  "${ROOT}/tests/MySQL/src/tests/scripts/client/sysbench.patch"
cd "${DIR}"
./autogen.sh
./configure
make -j"${JOBS}"
sudo -n make install
sudo -n cp \
  "${ROOT}/tests/MySQL/src/tests/scripts/client/oltp_common.lua" \
  "${ROOT}/tests/MySQL/src/tests/scripts/client/oltp_read_write.lua" \
  /usr/local/share/sysbench/
test -x /usr/local/bin/sysbench
REMOTE_SCRIPT

echo "  [ OK ] MySQL overhead environment ready"
