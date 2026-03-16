#!/bin/bash

# Configuration
BUILD_DIR="build_release"
PKG_ROOT="$BUILD_DIR/pkg_root"

# 1. Setup
echo "Preparing build environment..."
mkdir -p "$BUILD_DIR"
rm -rf "$PKG_ROOT"

# 2. Build Release
echo "Building Release version..."
# Optimize: avoid full clean of $BUILD_DIR for faster incremental builds
cmake -B "$BUILD_DIR" -S . -DCMAKE_BUILD_TYPE=Release
if [ $? -ne 0 ]; then
	echo "CMake configuration failed."
	exit 1
fi

cmake --build "$BUILD_DIR" --config Release -j$(nproc)
if [ $? -ne 0 ]; then
	echo "Build failed."
	exit 1
fi

# 3. Install to Staging Area
echo "Staging files..."
cmake --install "$BUILD_DIR" --prefix "$PKG_ROOT"

# 4. Prepare Install Script
cp install_template.sh "$PKG_ROOT/install.sh"
chmod +x "$PKG_ROOT/install.sh"

# 5. Execute Installation
echo "----------------------------------------------------------------"
echo "Executing installation(執行安裝)..."
cd "$PKG_ROOT" || exit 1
./install.sh

cd ../..
echo "----------------------------------------------------------------"
echo "Build and Install completed successfully!"
echo "----------------------------------------------------------------"
