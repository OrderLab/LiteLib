FROM ubuntu:22.04

# Set labels for better container management
LABEL maintainer="DeathStarBench"
LABEL description="Memcached service with LiteSys"

# Prevent interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Copy the dependency script
COPY litesys_dependency.sh /tmp/litesys_dependency.sh

# Install dependencies using the script
RUN chmod +x /tmp/litesys_dependency.sh && \
    /tmp/litesys_dependency.sh

# Install memcached
RUN apt-get update && apt-get install -y memcached && \
    rm -rf /var/lib/apt/lists/*

# Create a startup script
COPY start-memcached.sh /usr/local/bin/
RUN chmod +x /usr/local/bin/start-memcached.sh

# Expose the default memcached port
EXPOSE 11211

# Use our custom script as the entrypoint
ENTRYPOINT ["/usr/local/bin/start-memcached.sh"]
