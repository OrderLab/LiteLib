echo "[base]
name=CentOS-\$releasever - Base
baseurl=https://vault.centos.org/7.9.2009/os/\$basearch
gpgcheck=1
gpgkey=file:///etc/pki/rpm-gpg/RPM-GPG-KEY-CentOS-7

#released updates 
[updates]
name=CentOS-\$releasever - Updates
baseurl=https://vault.centos.org/7.9.2009/updates/\$basearch
gpgcheck=1
gpgkey=file:///etc/pki/rpm-gpg/RPM-GPG-KEY-CentOS-7

#additional packages that may be useful
[extras]
name=CentOS-\$releasever - Extras
baseurl=https://vault.centos.org/7.9.2009/extras/\$basearch
gpgcheck=1
gpgkey=file:///etc/pki/rpm-gpg/RPM-GPG-KEY-CentOS-7

#additional packages that extend functionality of existing packages
[centosplus]
name=CentOS-\$releasever - Plus
baseurl=https://vault.centos.org/7.9.2009/centosplus/\$basearch
gpgcheck=1
enabled=0
gpgkey=file:///etc/pki/rpm-gpg/RPM-GPG-KEY-CentOS-7" > /etc/yum.repos.d/CentOS-Base.repo

yum clean all
yum makecache
yum update -y

#  yum install -y \
#         gcc \
#         gcc-c++ \
#         make \
#         autoconf \
#         automake \
#         libtool \
#         pkgconfig \
#         ca-certificates \
#         openssl-devel \
#         wget \
#         tar \
#         git \
#         curl \
#         glibc-common \
#         vim \
#         gdb \
#         glog \
#         valgrind \
#         protobuf-compiler && \
# yum clean all


# export  BOOST_VERSION=1.83.0
# export  CMAKE_VERSION=3.27.7
# export  NUM_JOBS=32

# # Install CMake
# cd /tmp && \
#     wget https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}.tar.gz && \
#     tar xzf cmake-${CMAKE_VERSION}.tar.gz && \
#     cd cmake-${CMAKE_VERSION} && \
#     ./bootstrap && \
#     make -j${NUM_JOBS} && \
#     make install && \
#     rm -rf /tmp/*

# # Install Boost
# # https://www.boost.org/doc/libs/1_80_0/more/getting_started/unix-variants.html
# cd /tmp && \
#     BOOST_VERSION_MOD=$(echo $BOOST_VERSION | tr . _) && \
# #    wget https://boostorg.jfrog.io/artifactory/main/release/${BOOST_VERSION}/source/boost_${BOOST_VERSION_MOD}.tar.bz2 && \
#     wget https://github.com/boostorg/boost/releases/download/boost-${BOOST_VERSION}/boost-${BOOST_VERSION}.tar.xz && \
#     tar -xavf boost-${BOOST_VERSION}.tar.xz && \
#     cd boost-${BOOST_VERSION} && \
#     ./bootstrap.sh --prefix=/usr/local && \
#     ./b2 install && \
#     rm -rf /tmp/*

# yum install -y --no-install-recommends \
#         ssh && \
#     yum clean && \
#     mkdir -p /root/.ssh && \
#     chmod 700 /root/.ssh && \
#     touch /root/.ssh/authorized_keys && \
#     echo "#PasswordAuthentication no" >> /etc/ssh/sshd_config && \
#     echo "PermitRootLogin yes" >> /etc/ssh/sshd_config

# cd /tmp && \
#     wget "https://github.com/libevent/libevent/releases/download/release-2.1.12-stable/libevent-2.1.12-stable.tar.gz" && \
#     tar -xzvf libevent-2.1.12-stable.tar.gz && \
#     cd libevent-2.1.12-stable && \
#     mkdir build && cd build && \
#     cmake .. && \
#     make && \
#     make install

# #install glog

# cd /tmp && \
#     git clone https://github.com/google/glog.git && \
#     cd glog && \
#     mkdir build && \
#     cd build && \
#     cmake .. && \
#     make && \
#     make install && \
#     cd /tmp && rm -r glog