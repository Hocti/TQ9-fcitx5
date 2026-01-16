#!/bin/bash

# install.sh - Installer for TQ9 Fcitx5 Input Method

set -e

# Check for dependencies
# Check for dependencies
echo "Checking for dependencies..."

if command -v apt-get &> /dev/null; then
    DEPENDENCIES=("fcitx5" "fcitx5-frontend-gtk3" "fcitx5-frontend-qt5" "fcitx5-config-qt" "fcitx5-chinese-addons")
    MISSING_DEPS=()
    for dep in "${DEPENDENCIES[@]}"; do
        if ! dpkg-query -W -f='${Status}' "$dep" 2>/dev/null | grep -q "ok installed"; then
            MISSING_DEPS+=("$dep")
        fi
    done

    if [ ${#MISSING_DEPS[@]} -gt 0 ]; then
        echo "The following dependencies appear to be missing: ${MISSING_DEPS[*]}"
        echo "Attempting to install missing dependencies..."
        if [ "$EUID" -ne 0 ]; then
            sudo apt-get update || true
            sudo apt-get install -y "${MISSING_DEPS[@]}" || echo "Warning: Some packages could not be installed."
        else
            apt-get update || true
            apt-get install -y "${MISSING_DEPS[@]}" || echo "Warning: Some packages could not be installed."
        fi
    else
        echo "Dependencies check completed."
    fi
elif command -v pacman &> /dev/null; then
    DEPENDENCIES=("fcitx5" "fcitx5-gtk" "fcitx5-qt" "fcitx5-configtool" "fcitx5-chinese-addons")
    MISSING_DEPS=()
    for dep in "${DEPENDENCIES[@]}"; do
        if ! pacman -Qs "^$dep$" > /dev/null 2>&1; then
            MISSING_DEPS+=("$dep")
        fi
    done

    if [ ${#MISSING_DEPS[@]} -gt 0 ]; then
        echo "Installing missing dependencies using pacman..."
        if [ "$EUID" -ne 0 ]; then
            sudo pacman -Sy --noconfirm "${MISSING_DEPS[@]}"
        else
            pacman -Sy --noconfirm "${MISSING_DEPS[@]}"
        fi
    fi
else
    echo "Warning: No supported package manager found (apt-get or pacman). Please ensure dependencies are installed manually."
fi


# Get the directory of the script
SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

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
if [ -d "$SCRIPT_DIR/lib" ]; then
    echo "Copying library to $FCITX_LIB_DIR..."
    cp "$SCRIPT_DIR/lib/fcitx5/"*.so "$FCITX_LIB_DIR/"
fi

# Copy bin
if [ -d "$SCRIPT_DIR/bin" ]; then
    echo "Copying UI executable..."
    cp "$SCRIPT_DIR/bin/"* "$PREFIX/bin/"
fi

# Copy share
if [ -d "$SCRIPT_DIR/share" ]; then
    echo "Copying data files..."
    cp -r "$SCRIPT_DIR/share/"* "$PREFIX/share/"
fi

echo ""
echo "Installation successfully completed!"
echo "To apply changes, please restart Fcitx5 by running: fcitx5 -r"
echo "Then add TQ9 input method in Fcitx5 Configuration."
