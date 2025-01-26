export  BOOST_VERSION=1.83.0
export  CMAKE_VERSION=3.27.7
export  NUM_JOBS=32

export DEBIAN_FRONTEND=noninteractive

apt-get update && \
    apt-get install -y --no-install-recommends \
        build-essential \
        software-properties-common \
        autoconf \
        automake \
        libtool \
        pkg-config \
        ca-certificates \
        libssl-dev \
        wget \
        tar \
        git \
        curl \
        locales \
        locales-all \
        vim \
        gdb \
        libgoogle-glog-dev \
        valgrind \
        protobuf-compiler \
        libprotobuf-dev \
        libboost-all-dev && \
    apt-get clean

export LC_ALL=en_US.UTF-8
export LANG=en_US.UTF-8
export LANGUAGE=en_US.UTF-8

# Install CMake
cd /tmp && \
    wget https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}.tar.gz && \
    tar xzf cmake-${CMAKE_VERSION}.tar.gz && \
    cd cmake-${CMAKE_VERSION} && \
    ./bootstrap && \
    make -j${NUM_JOBS} && \
    make install && \
    rm -rf /tmp/*

# Install Boost
# https://www.boost.org/doc/libs/1_80_0/more/getting_started/unix-variants.html
cd /tmp && \
    BOOST_VERSION_MOD=$(echo $BOOST_VERSION | tr . _) && \
#    wget https://boostorg.jfrog.io/artifactory/main/release/${BOOST_VERSION}/source/boost_${BOOST_VERSION_MOD}.tar.bz2 && \
    wget https://github.com/boostorg/boost/releases/download/boost-${BOOST_VERSION}/boost-${BOOST_VERSION}.tar.xz && \
    tar -xavf boost-${BOOST_VERSION}.tar.xz && \
    cd boost-${BOOST_VERSION} && \
    ./bootstrap.sh --prefix=/usr/local && \
    ./b2 install && \
    rm -rf /tmp/*


cd /tmp && \
wget "https://github.com/libevent/libevent/releases/download/release-2.1.12-stable/libevent-2.1.12-stable.tar.gz" && \
    tar -xzvf libevent-2.1.12-stable.tar.gz && \
    cd libevent-2.1.12-stable && \
    mkdir build && cd build && \
    cmake .. && \
    make && \
    make install
    rm -rf /tmp/*

update_property() {
    local file=$1
    local property=$2
    local value=$3

    # if grep -q "<name>$property</name>" $file; then
    #     # Property exists, update its value
    #     sed -i "/<name>$property<\/name>/!b;n;c<value>$value</value>" $file
    # else
        # Property doesn't exist, add it
        sed -i "/<\/configuration>/ i <property><name>$property</name><value>$value</value></property>" $file
    # fi
}

# Update hdfs-site.xml
update_property /usr/local/hadoop/etc/hadoop/hdfs-site.xml "dfs.namenode.rpc-address" "dn1:11111"