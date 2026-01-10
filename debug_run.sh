#!/bin/bash

# Configuration
BUILD_DIR="build_debug"
INSTALL_DIR="$(pwd)/install_debug"

# 1. Build
echo "Building project..."
cmake -B "$BUILD_DIR" -S . \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
    -DCMAKE_BUILD_TYPE=Debug

if [ $? -ne 0 ]; then
    echo "CMake configuration failed."
    exit 1
fi

cmake --build "$BUILD_DIR" --target install -j$(nproc)

if [ $? -ne 0 ]; then
    echo "Build failed."
    exit 1
fi

# 2. Locate paths
# Find where the lib was installed (lib, lib64, etc.)
LIB_PATH=$(find "$INSTALL_DIR" -name "libcustom_input.so" | xargs dirname)
SHARE_PATH="$INSTALL_DIR/share"

if [ -z "$LIB_PATH" ]; then
    echo "Error: Could not find installed library."
    exit 1
fi

# 3. Setup Environment
# Append existing XDG_DATA_DIRS if it exists, otherwise default to system paths
# (Usually /usr/local/share:/usr/share)
export XDG_DATA_DIRS="$SHARE_PATH:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"

# FCITX_ADDON_DIRS tells fcitx5 where to load the shared library
export FCITX_ADDON_DIRS="$LIB_PATH:/usr/lib/x86_64-linux-gnu/fcitx5"

echo "----------------------------------------------------------------"
echo "Starting Fcitx5 in Debug Mode..."
echo "Environment configured:"
echo "XDG_DATA_DIRS=$XDG_DATA_DIRS"
echo "FCITX_ADDON_DIRS=$FCITX_ADDON_DIRS"
echo ""
echo "Replacing running Fcitx5 instance..."
echo "Logs are being written to fcitx5_debug.log"
echo "----------------------------------------------------------------"
echo ">>> PRESS ENTER TO STOP DEBUGGING AND RESTORE SYSTEM FCITX5 <<<"
echo "----------------------------------------------------------------"

# Run fcitx5 in background (-r replaces existing). 
# We assume fcitx5 is in path. 
# We use nohup to ensure it detaches slightly but we keep track of it if needed.
# Actually standard fcitx5 -r returns 0 after launching the daemon.
fcitx5 -r -d > fcitx5_debug.log 2>&1

# Wait for user input
read -p ""

# 4. Restore
echo "Restoring system Fcitx5..."

# Unset the variables so we don't restart the debug version
unset XDG_DATA_DIRS
unset FCITX_ADDON_DIRS

# Restart normal fcitx5
fcitx5 -r -d > /dev/null 2>&1 &

echo "System Fcitx5 restored."
