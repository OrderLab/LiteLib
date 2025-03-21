FROM memcached:latest

# Set labels for better container management
LABEL maintainer="DeathStarBench"
LABEL description="Memcached service with LiteSys"

# Install bash for scripting
USER root
RUN apt-get update && apt-get install -y \
    bash \
    procps \
    && rm -rf /var/lib/apt/lists/*

# Create a startup script
COPY start-memcached.sh /usr/local/bin/
RUN chmod +x /usr/local/bin/start-memcached.sh

# Expose the default memcached port
EXPOSE 11211

# Use our custom script as the entrypoint
ENTRYPOINT ["/usr/local/bin/start-memcached.sh"]
