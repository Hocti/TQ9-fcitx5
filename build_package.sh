#!/bin/bash

# Configuration
BUILD_DIR="build_release"
PACKAGE_NAME="fcitx5-custom-input"
OUTPUT_DIR="release_output"
PKG_ROOT="$BUILD_DIR/pkg_root"

# 1. Clean and Setup
echo "Cleaning up..."
rm -rf "$BUILD_DIR" "$OUTPUT_DIR"
mkdir -p "$OUTPUT_DIR"

# 2. Build Release
echo "Building Release version..."
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

# 4. Prepare Package
cp install_template.sh "$PKG_ROOT/install.sh"
chmod +x "$PKG_ROOT/install.sh"

# 5. Compress
echo "Creating package..."
cd "$PKG_ROOT"
tar -czf "../../$OUTPUT_DIR/$PACKAGE_NAME.tar.gz" .

cd ../..
echo "----------------------------------------------------------------"
echo "Package created successfully!"
echo "Location: $OUTPUT_DIR/$PACKAGE_NAME.tar.gz"
echo "You can share this file. To install, extract it and run ./install.sh"
echo "----------------------------------------------------------------"
