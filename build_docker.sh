#!/bin/bash
set -e

# Build the docker image
docker build -t fcitx5-tq9-builder .

# Run the build inside the container using the current directory as volume
# We map the current user ID to avoid permission issues on Linux host
docker run --rm -v "$(pwd):/app" -u "$(id -u):$(id -g)" fcitx5-tq9-builder bash build_package.sh
