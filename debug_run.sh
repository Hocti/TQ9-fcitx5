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
LIB_PATH=$(find "$INSTALL_DIR" -name "libtq9.so" | xargs dirname)
SHARE_PATH="$INSTALL_DIR/share"

if [ -z "$LIB_PATH" ]; then
    echo "Error: Could not find installed library."
    exit 1
fi

# 3. Setup Environment for Debugging
# Create a temp config dir to avoid messing with user's real config
CONFIG_DIR="$INSTALL_DIR/config"
mkdir -p "$CONFIG_DIR/fcitx5"
mkdir -p "$INSTALL_DIR/cache"

# IMPORTANT: these are NOT exported into this shell. They are handed to the
# debug fcitx5 through `env` only, so the restored system fcitx5 can never
# inherit them. Leaking PATH here previously caused the system fcitx5 (old
# addon) to spawn the freshly built UI binary - a half-old, half-new hybrid
# that is very confusing to debug(非常難除錯).
DEBUG_ENV=(
    "XDG_DATA_HOME=$SHARE_PATH"
    "XDG_DATA_DIRS=$SHARE_PATH:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
    "XDG_CONFIG_HOME=$CONFIG_DIR"
    "XDG_CACHE_HOME=$INSTALL_DIR/cache"
    "PATH=$INSTALL_DIR/bin:$PATH"
    "FCITX_ADDON_DIRS=$LIB_PATH:/usr/lib/x86_64-linux-gnu/fcitx5"
)

# 3b. Detect how the KDE session launches its input method.
# "Fcitx 5"                -> KWin execs /usr/bin/fcitx5 on the normal wayland
#                             socket, so `fcitx5 -r` here can take over and
#                             still bind the input-method protocol.
# "Fcitx5 wayland launcher"-> KWin execs /usr/libexec/fcitx5-wayland-launcher,
#                             which builds a PRIVATE wayland socket and only
#                             hands the input-method protocol to the fcitx5 it
#                             spawns itself. A manually started fcitx5 can never
#                             get it, so debug_run.sh cannot work in that mode.
KWIN_IM=$(grep -m1 '^InputMethod' "${XDG_CONFIG_HOME:-$HOME/.config}/kwinrc" 2>/dev/null | cut -d= -f2-)
LAUNCHER_MODE=0
case "$KWIN_IM" in
    *wayland-launcher*) LAUNCHER_MODE=1 ;;
esac

if [ "$LAUNCHER_MODE" -eq 1 ]; then
    echo "----------------------------------------------------------------"
    echo "WARNING: KDE is set to 'Fcitx 5 Wayland Launcher (Experimental)'."
    echo ""
    echo "  That launcher keeps the wayland input-method protocol on a private"
    echo "  socket that only its own child fcitx5 can use. The debug instance"
    echo "  started here will replace it, fail to bind that protocol, and no"
    echo "  typing will reach it - selecting TQ9 will appear to do nothing."
    echo ""
    echo "  Fix: System Settings > Keyboard > Virtual Keyboard > 'Fcitx 5'"
    echo "       (the plain entry, not the Experimental one), then re-run."
    echo "----------------------------------------------------------------"
    read -r -p "Continue anyway? [y/N] " REPLY
    case "$REPLY" in
        [yY]*) ;;
        *) echo "Aborted."; exit 1 ;;
    esac
fi

# Pre-configure profile to enable our input method
# We need to create 'profile' file in $CONFIG_DIR/fcitx5/
cat > "$CONFIG_DIR/fcitx5/profile" <<EOF
[Groups/0]
# Group Name
Name=Default
# Layout
Default Layout=us
# Default Input Method
DefaultIM=tq9

[Groups/0/Items/0]
# Name
Name=tq9
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
echo "Environment handed to the debug instance only:"
printf '  %s\n' "${DEBUG_ENV[@]}"
echo ""
echo "STT settings/logs for this session live in:"
echo "  $CONFIG_DIR/fcitx5/tq9/"
echo "Logs are being written to fcitx5_debug.log"
echo "----------------------------------------------------------------"

# Run fcitx5 in the FOREGROUND of its own background job (-D = do not
# daemonize) so $! is the real PID and we can actually kill it later.
# With -d it forks away immediately and $! refers to a process that is
# already gone, which is why Ctrl+C used to leave it running.
env "${DEBUG_ENV[@]}" fcitx5 -r -D > fcitx5_debug.log 2>&1 &
FCITX_PID=$!

CLEANED=0
cleanup() {
    # Guard against Ctrl+C being pressed several times.
    [ "$CLEANED" -eq 1 ] && return
    CLEANED=1

    echo ""
    echo "Stopping Debug Fcitx5 (PID $FCITX_PID)..."
    kill "$FCITX_PID" 2>/dev/null

    # Give it a moment, then insist.
    for _ in 1 2 3 4 5 6 7 8 9 10; do
        kill -0 "$FCITX_PID" 2>/dev/null || break
        sleep 0.2
    done
    kill -9 "$FCITX_PID" 2>/dev/null
    wait "$FCITX_PID" 2>/dev/null

    [ -n "$TAIL_PID" ] && kill "$TAIL_PID" 2>/dev/null
    [ -n "$MONITOR_PID" ] && kill "$MONITOR_PID" 2>/dev/null
    [ -n "$CHECK_PID" ] && kill "$CHECK_PID" 2>/dev/null

    # This shell never had the debug vars exported, so the restored instance
    # gets a clean environment automatically.
    echo "Restoring system Fcitx5..."
    if [ "$LAUNCHER_MODE" -eq 1 ] && [ -x /usr/libexec/fcitx5-wayland-launcher ]; then
        # Plain `fcitx5 -r` would come back without the input-method protocol
        # and leave the session with no working IM. Re-run the launcher so it
        # rebuilds its private socket.
        pkill -x fcitx5 2>/dev/null
        sleep 0.3
        setsid /usr/libexec/fcitx5-wayland-launcher --reopen > /dev/null 2>&1 &
    else
        fcitx5 -r -d > /dev/null 2>&1 &
    fi
    disown 2>/dev/null
    echo "System Fcitx5 restored."
}

# INT/TERM must exit, not fall through to the rest of the script.
trap 'cleanup; exit 0' INT TERM
trap cleanup EXIT

# Tail logs in background
tail -f fcitx5_debug.log &
TAIL_PID=$!

# Watch for the one line that tells us whether this instance is actually the
# compositor's input method. Without it, nothing you type can ever reach fcitx5.
(
    for _ in $(seq 1 30); do
        sleep 1
        if grep -q "Using Wayland native input method protocol: 1" fcitx5_debug.log; then
            echo ""
            echo "[CHECK] Wayland input-method protocol bound - debug instance is live."
            exit 0
        fi
        if grep -q "Using Wayland native input method protocol: 0" fcitx5_debug.log; then
            echo ""
            echo "================================================================"
            echo "[CHECK] FAILED: 'Wayland native input method protocol: 0'"
            echo "  This fcitx5 is NOT the compositor's input method, so keys will"
            echo "  never reach it and selecting TQ9 does nothing."
            echo "  Usual cause: KDE Virtual Keyboard is set to the Experimental"
            echo "  'Fcitx 5 Wayland Launcher'. Switch it to plain 'Fcitx 5'."
            echo "================================================================"
            exit 0
        fi
    done
) &
CHECK_PID=$!

# Monitor status in background
(
    sleep 1
    env "${DEBUG_ENV[@]}" fcitx5-remote -s tq9 2>/dev/null

    while kill -0 "$FCITX_PID" 2>/dev/null; do
        CURRENT=$(env "${DEBUG_ENV[@]}" fcitx5-remote -n 2>/dev/null)
        if [ "$CURRENT" == "tq9" ]; then
            echo "[STATUS] Q9 is ACTIVE!"
        else
            echo "[MONITOR] Current IM: $CURRENT"
        fi
        sleep 5
    done
) &
MONITOR_PID=$!

echo ">>> PRESS ENTER (or Ctrl+C) TO STOP DEBUGGING <<<"
read -r

# Normal exit path; the EXIT trap does the actual cleanup.
exit 0
