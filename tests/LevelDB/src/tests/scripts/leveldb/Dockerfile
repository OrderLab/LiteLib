FROM debian:12-slim

COPY ./redis-leveldb /opt/redis-leveldb

WORKDIR /opt/redis-leveldb

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
    && rm -rf /var/lib/apt/lists/*

RUN cpanm --quiet --notest --skip-satisfied --force Redis

RUN make -j && \
    make test