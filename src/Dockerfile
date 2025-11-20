FROM docker.io/alpine:edge AS base

# Install run-time and compile-time system dependencies
USER root
RUN apk add --no-cache \
    build-base \
    zig \
    git \
    python3 \
    bash

# Create and switch to non-root user
RUN addgroup -g 1000 runner \
    && adduser -G runner -u 1000 -D runner
USER runner

# Install run-time and compile-time local dependencies


# Create required folders and change to source directory
RUN mkdir -p /home/runner/src \
    /var/tmp/deps
WORKDIR /home/runner/src

# Install run-time and compile-time project dependencies


#
# Production (WebAssembly Build)
#

# Stage 1: Build with Emscripten
FROM docker.io/emscripten/emsdk:4.0.20 AS web-builder

WORKDIR /src
COPY . .

# Build WebAssembly artifacts
RUN bash -c "./scripts/build_web.sh"

# Stage 2: Production server
FROM python:3.12-alpine AS production

WORKDIR /app

# Copy built WebAssembly artifacts from builder stage
COPY --from=web-builder /src/web-build ./web-build

# Expose HTTP server port
EXPOSE 8080

# Serve the web application
CMD ["python3", "-m", "http.server", "8080", "--directory", "web-build"]

#
# Development
#

FROM base AS development

# Install dev-time system dependencies
USER root
RUN apk add --no-cache \
    wayland-dev \
    libxkbcommon-dev \
    mesa-dev \
    libx11-dev \
    libxext-dev \
    libxrender-dev \
    libxrandr-dev \
    libxinerama-dev \
    libxcursor-dev \
    libxi-dev \
    libxfixes-dev \
    curl \
    inotify-tools \
    ripgrep

# TODO: merge dependencies
RUN apk add --no-cache \
    python3-dev \
    ffmpeg

# Change to non-root user
USER runner

# Install dev-time local dependencies

# Change to source directory
WORKDIR /home/runner/src

# Install dev-time project dependencies
ENV XDG_RUNTIME_DIR=/tmp/runtime-dir
RUN mkdir $XDG_RUNTIME_DIR

# Move temporally project dependencies and clean source directory

# Expose required ports

# Define entrypoint
COPY --chown=runner:runner ./scripts/entrypoint-dev.sh /var/tmp
ENTRYPOINT ["/var/tmp/entrypoint-dev.sh"]

