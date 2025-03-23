FROM mongo:6.0.21-jammy

RUN apt update && apt install -y cgroup-tools htop && rm -rf /var/lib/apt/lists/*

EXPOSE 27017