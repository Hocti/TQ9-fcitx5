#!/bin/bash

# install.sh - Installer for Custom Fcitx5 Input Method

set -e

# Default to user local install if not root
if [ "$EUID" -ne 0 ]; then
    PREFIX="$HOME/.local"
    echo "Running as non-root user. Installing to $PREFIX..."
    echo "Note: Ensure $PREFIX/bin is in your PATH and fcitx5 is configured to load addons from $PREFIX."
else
    PREFIX="/usr"
    echo "Running as root. Installing to $PREFIX..."
fi

# Create directories if they don't exist
mkdir -p "$PREFIX/lib/fcitx5"
mkdir -p "$PREFIX/share/fcitx5"

# Find local lib dir in the package (might be lib or lib64 or lib/x86_64...)
# We assume the package has 'lib' or 'lib64' at its root.
if [ -d "lib64" ]; then
    echo "Copying libraries from lib64..."
    cp -r lib64/* "$PREFIX/lib64/" 2>/dev/null || cp -r lib64/* "$PREFIX/lib/"
elif [ -d "lib" ]; then
    echo "Copying libraries from lib..."
    cp -r lib/* "$PREFIX/lib/"
fi

# Copy share
if [ -d "share" ]; then
    echo "Copying data files..."
    cp -r share/* "$PREFIX/share/"
fi

echo "Installation successfully completed."
echo "To apply changes, please restart Fcitx5 by running: fcitx5 -r"
