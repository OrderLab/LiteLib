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
    && rm -rf /var/lib/apt/lists/*

RUN cpanm --quiet --notest --skip-satisfied --force Redis
