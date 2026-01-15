#!/bin/bash

# install.sh - Installer for TQ9 Fcitx5 Input Method

set -e

# Default to user local install if not root
if [ "$EUID" -ne 0 ]; then
    PREFIX="$HOME/.local"
    echo "Running as non-root user. Installing to $PREFIX..."
    echo "Note: For user installation to work, you may need to set FCITX_ADDON_DIRS."
else
    PREFIX="/usr"
    echo "Running as root. Installing to $PREFIX..."
fi

# Create directories if they don't exist
mkdir -p "$PREFIX/bin"
mkdir -p "$PREFIX/share/fcitx5"

# Determine the correct fcitx5 library path
# Different distros use different paths
if [ -d "/usr/lib/x86_64-linux-gnu/fcitx5" ]; then
    FCITX_LIB_DIR="$PREFIX/lib/x86_64-linux-gnu/fcitx5"
elif [ -d "/usr/lib64/fcitx5" ]; then
    FCITX_LIB_DIR="$PREFIX/lib64/fcitx5"
else
    FCITX_LIB_DIR="$PREFIX/lib/fcitx5"
fi

mkdir -p "$FCITX_LIB_DIR"

# Copy library
if [ -d "lib" ]; then
    echo "Copying library to $FCITX_LIB_DIR..."
    cp lib/fcitx5/*.so "$FCITX_LIB_DIR/"
fi

# Copy bin
if [ -d "bin" ]; then
    echo "Copying UI executable..."
    cp bin/* "$PREFIX/bin/"
fi

# Copy share
if [ -d "share" ]; then
    echo "Copying data files..."
    cp -r share/* "$PREFIX/share/"
fi

echo ""
echo "Installation successfully completed!"
echo "To apply changes, please restart Fcitx5 by running: fcitx5 -r"
echo "Then add TQ9 input method in Fcitx5 Configuration."
