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

# Copy data files (images and database) to the install share path
DATA_DEST="$SHARE_PATH/fcitx5/custom-input"
echo "Copying data files to $DATA_DEST..."
mkdir -p "$DATA_DEST"
cp -r "$(pwd)/data/img" "$DATA_DEST/"
cp "$(pwd)/data/dataset.db" "$DATA_DEST/"
echo "Data files copied."

# 3. Setup Environment for Debugging
# Create a temp config dir to avoid messing with user's real config
CONFIG_DIR="$INSTALL_DIR/config"
mkdir -p "$CONFIG_DIR/fcitx5"

# Setup XDG paths
export XDG_DATA_DIRS="$SHARE_PATH:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
export XDG_CONFIG_HOME="$CONFIG_DIR"
export PATH="$INSTALL_DIR/bin:$PATH"
# Define FCITX_ADDON_DIRS to include our built lib and system fcitx5 libs
export FCITX_ADDON_DIRS="$LIB_PATH:/usr/lib/x86_64-linux-gnu/fcitx5"

# Pre-configure profile to enable our input method
# We need to create 'profile' file in $CONFIG_DIR/fcitx5/
cat > "$CONFIG_DIR/fcitx5/profile" <<EOF
[Groups/0]
# Group Name
Name=Default
# Layout
Default Layout=us
# Default Input Method
DefaultIM=custom_input

[Groups/0/Items/0]
# Name
Name=custom_input
# Layout
Layout=

[Groups/0/Items/1]
# Name
Name=keyboard-us
# Layout
Layout=

[GroupOrder]
0=Default
EOF

echo "----------------------------------------------------------------"
echo "Starting Fcitx5 in Debug Mode..."
echo "Environment configured:"
echo "XDG_DATA_DIRS=$XDG_DATA_DIRS"
echo "XDG_CONFIG_HOME=$XDG_CONFIG_HOME"
echo "FCITX_ADDON_DIRS=$FCITX_ADDON_DIRS"
echo ""
# 5. Run & Monitor
export XDG_CACHE_HOME="$INSTALL_DIR/cache"
mkdir -p "$XDG_CACHE_HOME"

echo "----------------------------------------------------------------"
echo "Starting Fcitx5 in Debug Mode..."
echo "Logs are being written to fcitx5_debug.log"
echo "Tailing log file (Ctrl+C to stop)..."
echo "----------------------------------------------------------------"

# Run fcitx5
fcitx5 -r -d > fcitx5_debug.log 2>&1 &
FCITX_PID=$!

# Function to clean up
cleanup() {
    echo ""
    echo "Stopping Debug Fcitx5 (PID $FCITX_PID)..."
    kill $FCITX_PID 2>/dev/null
    wait $FCITX_PID 2>/dev/null
    
    # Kill background monitor if it exists
    if [ -n "$MONITOR_PID" ]; then
        kill $MONITOR_PID 2>/dev/null
    fi
    if [ -n "$TAIL_PID" ]; then
        kill $TAIL_PID 2>/dev/null
    fi

    echo "Restoring system Fcitx5..."
    unset XDG_DATA_DIRS
    unset XDG_CONFIG_HOME
    unset FCITX_ADDON_DIRS
    unset XDG_CACHE_HOME
    
    fcitx5 -r -d > /dev/null 2>&1 &
    echo "System Fcitx5 restored."
}

# Trap signals
trap cleanup EXIT INT TERM

# Tail logs in background
tail -f fcitx5_debug.log &
TAIL_PID=$!

# Monitor status in background
(
    # Try to switch to custom_input if not active
    fcitx5-remote -s custom_input
    
    while kill -0 $FCITX_PID 2>/dev/null; do
        CURRENT=$(fcitx5-remote -n)
        echo "[MONITOR] Current IM: $CURRENT"
        
        # Verify if our input method is active
        if [ "$CURRENT" == "custom_input" ]; then
             echo -e "\n[STATUS] Q9 is ACTIVE!"
        elif [ "$CURRENT" == "keyboard-us" ]; then
             # Try to force toggle again?
             # fcitx5-remote -s custom_input
             :
        fi
        sleep 2
    done
) &
MONITOR_PID=$!

# Wait for user to press Enter to stop
echo ">>> PRESS ENTER TO STOP DEBUGGING <<<"
read -r

