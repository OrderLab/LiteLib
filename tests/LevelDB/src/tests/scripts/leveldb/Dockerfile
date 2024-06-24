FROM debian:12-slim

RUN apt-get update && \
    apt-get install -y \
        libsnappy-dev \
        libev-dev \
        libgmp-dev \
        cpanminus \
        perl \
        build-essential \
        gdb \
        vim \
        procps \
        git \
        openssh-client \
        openssh-server \
        python3 \
        python3-pip \
        wget \
        tar \
        cmake \
        libssl-dev \
        libgoogle-glog-dev \
        htop \
        iotop \
        iftop \
        tcpdump \
    && rm -rf /var/lib/apt/lists/*

RUN cpanm --quiet --notest --skip-satisfied --force Redis

WORKDIR /tmp

RUN wget "https://github.com/libevent/libevent/releases/download/release-2.1.12-stable/libevent-2.1.12-stable.tar.gz" && \
    tar -xzvf libevent-2.1.12-stable.tar.gz && \
    cd libevent-2.1.12-stable && \
    mkdir build && cd build && \
    cmake .. && \
    make -j && \
    make install

WORKDIR /root