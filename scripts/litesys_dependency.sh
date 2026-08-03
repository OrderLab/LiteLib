#!/bin/bash
#
# Install every build/run-time dependency LiteLib needs on a single node.
# Re-running the script is cheap: apt is idempotent and the Boost/libevent
# source builds are skipped once the pinned version is already installed.

set -e
set -x

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=config.sh
source "${SCRIPT_DIR}/config.sh"

BOOST_VERSION=${LITELIB_BOOST_VERSION}
LIBEVENT_VERSION=${LITELIB_LIBEVENT_VERSION}
KERNEL_RELEASE=${LITELIB_KERNEL_RELEASE}
KERNEL_PKG_VERSION=${LITELIB_KERNEL_PKG_VERSION}
NUM_JOBS=${LITELIB_NUM_JOBS}
FREQUENCY=${LITELIB_CPU_FREQ_GHZ}
PREFIX=${LITELIB_PREFIX}

install_dependencies() {
  litelib_phase "installing distro packages (${#LITELIB_APT_PACKAGES[@]} packages + kernel ${KERNEL_RELEASE})"
  apt-get install "${APT_OPTS[@]}" --no-install-recommends \
    "${LITELIB_APT_PACKAGES[@]}" \
    linux-tools-"$(uname -r)" \
    linux-image-"${KERNEL_RELEASE}"="${KERNEL_PKG_VERSION}" \
    linux-headers-"${KERNEL_RELEASE}"="${KERNEL_PKG_VERSION}"

  # `perf` is invoked by the profiling experiments; once the node has been
  # rebooted into ${KERNEL_RELEASE} the matching tools package must be present.
  if [ "$(uname -r)" = "${KERNEL_RELEASE}" ]; then
    apt-get install "${APT_OPTS[@]}" --no-install-recommends \
      linux-tools-"${KERNEL_RELEASE}"
  fi
}

boost_installed() {
  local header="${PREFIX}/include/boost/version.hpp"
  local expected
  expected=$(echo "${BOOST_VERSION}" | tr . _)
  [ -f "${header}" ] && grep -q "BOOST_LIB_VERSION \"${expected%_0}\"" "${header}"
}

install_boost() {
  if boost_installed; then
    echo "Boost ${BOOST_VERSION} already installed in ${PREFIX}, skipping."
    return
  fi
  litelib_phase "building Boost ${BOOST_VERSION} from source (this is the slowest step, ~15 min)"
  mkdir -p "${HOME}/dependencies/boost"
  cd "${HOME}/dependencies/boost"
  if [ ! -d "boost-${BOOST_VERSION}" ]; then
    wget -nv -c "https://github.com/boostorg/boost/releases/download/boost-${BOOST_VERSION}/boost-${BOOST_VERSION}-cmake.tar.xz"
    tar -xaf "boost-${BOOST_VERSION}-cmake.tar.xz"
  fi
  cd "boost-${BOOST_VERSION}"
  ./bootstrap.sh --prefix="${PREFIX}"
  ./b2 -j"${NUM_JOBS}" install
  ldconfig
}

libevent_installed() {
  local header="${PREFIX}/include/event2/event-config.h"
  [ -f "${header}" ] &&
    grep -q "EVENT__VERSION \"${LIBEVENT_VERSION}" "${header}" &&
    ls "${PREFIX}"/lib/libevent.so >/dev/null 2>&1
}

install_libevent() {
  if libevent_installed; then
    echo "libevent ${LIBEVENT_VERSION} already installed in ${PREFIX}, skipping."
    return
  fi
  litelib_phase "building libevent ${LIBEVENT_VERSION} from source"
  mkdir -p "${HOME}/dependencies/libevent"
  cd "${HOME}/dependencies/libevent"
  if [ ! -d "libevent-${LIBEVENT_VERSION}-stable" ]; then
    wget -nv -c "https://github.com/libevent/libevent/releases/download/release-${LIBEVENT_VERSION}-stable/libevent-${LIBEVENT_VERSION}-stable.tar.gz"
    tar -xzf "libevent-${LIBEVENT_VERSION}-stable.tar.gz"
  fi
  cd "libevent-${LIBEVENT_VERSION}-stable"
  mkdir -p build && cd build
  cmake -DCMAKE_INSTALL_PREFIX="${PREFIX}" ..
  make -j"${NUM_JOBS}"
  make install
  ldconfig
}

configure_ssh() {
  litelib_phase "configuring ssh"
  apt-get install "${APT_OPTS[@]}" --no-install-recommends ssh
  local ssh_dir="${LITELIB_USER_HOME}/.ssh"
  mkdir -p "${ssh_dir}"
  chmod 700 "${ssh_dir}"
  touch "${ssh_dir}/authorized_keys"
  chmod 600 "${ssh_dir}/authorized_keys"
  chown -R "${LITELIB_SSH_USER}" "${ssh_dir}" 2>/dev/null || true
  # echo "#PasswordAuthentication no" >>/etc/ssh/sshd_config
  # echo "PermitRootLogin yes" >>/etc/ssh/sshd_config
}

set_cpu_frequency() {
  # Pinning the frequency keeps latency measurements stable across runs.  This
  # is *runtime* state and has to be re-applied after every reboot.
  litelib_phase "pinning CPU frequency to ${FREQUENCY}GHz"
  cpupower frequency-set -g performance
  cpupower frequency-set -d "${FREQUENCY}GHz" -u "${FREQUENCY}GHz"
}

main() {
  install_dependencies
  set_cpu_frequency
  install_boost
  install_libevent
  configure_ssh

  echo "Dependencies installed successfully!"
}

main
