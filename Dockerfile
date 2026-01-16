FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies
RUN apt-get update && apt-get install -y \
	build-essential \
	cmake \
	git \
	pkg-config \
	libfcitx5core-dev \
	libfcitx5config-dev \
	libfcitx5utils-dev \
	qt6-base-dev \
	qtbase5-dev \
	libsqlite3-dev \
	extra-cmake-modules \
	libwayland-dev \
	wayland-protocols \
	libxkbcommon-dev \
	liblayershellqt-dev \
	&& rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /app

# The source code will be mounted at /app
CMD ["./build_package.sh"]
