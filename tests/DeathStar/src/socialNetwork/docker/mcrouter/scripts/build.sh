# https://gerrit.wikimedia.org/r/plugins/gitiles/operations/debs/mcrouter/+/6c3139d8746f63990b4c5bb577360cd3f6b97951/docker_entry.sh

#!/usr/bin/env bash
# based of scripts in https://github.com/facebook/mcrouter/tree/master/mcrouter/scripts
set -ex
#dir=$(mktemp -d  -p /var/tmp)
dir=/var/tmp/tmp.AJvPCDTbXG
shared_dir="/build"
pkg_dir="${dir}/pkgs"
install_dir="${dir}/install"
parallel="-j$(grep processor /proc/cpuinfo | wc -l)"
mcrouter_version="v2023.07.17.00"
FBTHRIFT_COMMIT=f19dcc844c67fda832ec068968b58d5c0e08e761
FIZZ_COMMIT=54f0dfddf1e56b37d7fa04b7f26cf1dcdc8b2fe4
FOLLY_COMMIT=61c11d77eb9a8bdc60f673017fccfbe900125cb6
MVFST_COMMIT=a76144e18addb14edbe6d232b7f72051cb774bdf
WANGLE_COMMIT=5e139c2dcf78c4e97d7fdd187ce43d598f1b5c86
export LDFLAGS="-L${install_dir}/lib -ldl -ljemalloc $LDFLAGS"
export CPPFLAGS="-I${install_dir}/include -O2 $CPPFLAGS"
# Reexport CPPFLAGS as CXXFLAGS for use by dependencies that use CMake
# to generate their build system.
# CMake will not apply any optimization flags by default unless a preset
# (such as Debug or Release) target was explicitly specified, so supply
# those via CXXFLAGS instead.
export CXXFLAGS="${CPPFLAGS}"
apt-get update
# could create an image with these preloaded
apt-get install -y \
    autoconf \
    binutils-dev \
    bison \
    cmake \
    flex \
    g++ \
    gcc \
    git \
    libboost1.74-all-dev \
    libbz2-dev \
    libdouble-conversion-dev \
    libevent-dev \
    libgflags-dev \
    libgtest-dev \
    libgoogle-glog-dev \
    libjemalloc-dev \
    liblz4-dev \
    liblzma-dev \
    liblzma5 \
    libsnappy-dev \
    libsodium-dev \
    libssl-dev \
    libtool \
    libunwind-dev \
    libfmt-dev \
    zlib1g-dev \
    libzstd-dev \
    make \
    pkg-config \
    python-is-python3 \
    dpkg-dev \
    debhelper \
    ragel \
    ca-certificates \
    build-essential
function build_git {
  repo=$1
  checkout=$2
  cmake_extra=$3
  cmake_dir=${4:-.}
  build_dir=$5
  cxxflags=$6
  checkout_dir=$( sed s'/.git$//' <<<"${repo##*/}")
  [ -z "${build_dir}" ] && build_dir=${checkout_dir}
  [ -d "${checkout_dir}" ] || git clone "${repo}"
  pushd "${checkout_dir}"
  [ -n "${checkout}" ] && git checkout "${checkout}"
  popd
  mkdir -p "${pkg_dir}/${build_dir}"
  pushd "${pkg_dir}/${build_dir}"
  cmake_args="${cmake_extra} -DCMAKE_INSTALL_PREFIX=${install_dir}"
  CXXFLAGS="$CXXFLAGS ${cxxflags}" \
    LD_LIBRARY_PATH="$install_dir/lib:$LD_LIBRARY_PATH" \
    LD_RUN_PATH="$install_dir/lib:$LD_RUN_PATH" \
    cmake ${cmake_args} "${cmake_dir}"
  make ${parallel}
  make install
  popd
}
function build_mcrouter {
  [ -d "${pkg_dir}/googletest" ] || git clone https://github.com/google/googletest.git
  mkdir -p ./lib/gtest
  cp -r -f -t ./lib/gtest "$pkg_dir/googletest/googletest"/*
  pushd "${pkg_dir}/mcrouter/mcrouter"
  autoreconf --install
  LD_LIBRARY_PATH="${install_dir}/lib:$LD_LIBRARY_PATH" \
    LD_RUN_PATH="${install_dir}/lib:$LD_RUN_PATH" \
    LDFLAGS="-L${install_dir}/lib $LDFLAGS" \
    CPPFLAGS="-I${install_dir}/include $CPPFLAGS" \
    FBTHRIFT_BIN="${install_dir}/bin/" \
    PYTHONWARNINGS="ignore::DeprecationWarning" \
    ./configure --prefix="${shared_dir}/mcrouter"
  make ${parallel}
  make install
  popd
}
mkdir -p "${pkg_dir}" "${install_dir}"
pushd "${pkg_dir}"
[ -d "${pkg_dir}/mcrouter" ] || git clone https://github.com/facebook/mcrouter.git
pushd "${pkg_dir}/mcrouter"
[ -z "${mcrouter_version}" ] || git checkout "${mcrouter_version}"
cd mcrouter && git apply /tmp/scripts/mcrouter.patch && cd ..
popd
mcrouter_base="${pkg_dir}/mcrouter/mcrouter"
test -f "${mcrouter_base}/VERSION" \
  || echo $mcrouter_version > "${mcrouter_base}/VERSION"
test -f "${mcrouter_base}/FBTHRIFT_COMMIT" \
  || echo $FBTHRIFT_COMMIT > "${mcrouter_base}/FBTHRIFT_COMMIT"
test -f "${mcrouter_base}/FIZZ_COMMIT" \
  || echo $FIZZ_COMMIT > "${mcrouter_base}/FIZZ_COMMIT"
test -f "${mcrouter_base}/FOLLY_COMMIT" \
  || echo $FOLLY_COMMIT > "${mcrouter_base}/FOLLY_COMMIT"
test -f "${mcrouter_base}/MVFST_COMMIT" \
  || echo $MVFST_COMMIT > "${mcrouter_base}/MVFST_COMMIT"
test -f "${mcrouter_base}/WANGLE_COMMIT" \
  || echo $WANGLE_COMMIT > "${mcrouter_base}/WANGLE_COMMIT"
build_git https://github.com/facebook/folly \
  "$(<${mcrouter_base}/FOLLY_COMMIT)" "" ".." "folly/folly" "-fPIC"
build_git https://github.com/facebookincubator/fizz \
  "$(<${mcrouter_base}/FIZZ_COMMIT)" "-DBUILD_TESTS=OFF" "." "fizz/fizz"
build_git https://github.com/facebook/wangle \
  "$(<${mcrouter_base}/WANGLE_COMMIT)" "-DBUILD_TESTS=OFF" "." "wangle/wangle"
build_git https://github.com/facebook/mvfst \
  "$(<${mcrouter_base}/MVFST_COMMIT)" "" ".."  "mvfst/build" "-fPIC"
build_git https://github.com/facebook/fbthrift \
  "$(<${mcrouter_base}/FBTHRIFT_COMMIT)" "" ".."  "fbthrift/build" "-fPIC"
build_mcrouter
pushd "${shared_dir}/mcrouter"
# dpkg-buildpackage -us -uc