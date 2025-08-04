FROM makeappdev/cpp-dev:24.04

RUN apt-get update
RUN apt-get install -y vim
RUN apt-get install -y --no-install-recommends curl ca-certificates
RUN apt-get install -y ninja-build nodejs npm
#RUN apt-get install -y python3-pip

# Download the latest installer
ADD https://astral.sh/uv/install.sh /uv-installer.sh

# Run the installer then remove it
RUN sh /uv-installer.sh && rm /uv-installer.sh

# Create user with sudo privileges
#RUN useradd -m -s /bin/bash claude && \
#    echo 'claude ALL=(ALL) NOPASSWD:ALL' >> /etc/sudoers

# Switch to claude user
#USER claude
WORKDIR /workspace

# Configure npm global directory and install Claude Code
RUN npm config set prefix '/root/.npm-global' && \
    npm install -g @anthropic-ai/claude-code

# Add npm global bin to PATH
ENV PATH="/root/.npm-global/bin:$PATH"


# Ensure the installed binary is on the `PATH`
ENV PATH="/root/.local/bin/:$PATH"


RUN claude mcp add serena -- uvx --from git+https://github.com/oraios/serena serena-mcp-server --context ide-assistant --project /workspace

#CMD ["claude"]
