FROM makeappdev/cpp-dev:24.04

RUN apt-get update
RUN apt-get install -y ninja-build nodejs npm
RUN npm install -g @anthropic-ai/claude-code


