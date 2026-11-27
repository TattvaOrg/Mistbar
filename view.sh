#!/usr/bin/env bash
# Mistbar Live Preview Script

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

cleanup() {
    echo ""
    echo "Closing Mistbar preview..."
    pkill -x "mistbar-daemon" 2>/dev/null || true
    pkill -x "mistbar-settings" 2>/dev/null || true
}

trap cleanup EXIT INT TERM

echo "Mistbar Live Preview"
if [ ! -x "$BUILD_DIR/mistbar-daemon" ] || [ ! -x "$BUILD_DIR/mistbar-settings" ]; then
    echo "Building Mistbar..."
    cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
    cmake --build "$BUILD_DIR" -j
fi

echo "Starting bar and Mistbar Settings preview (Ctrl+C to exit)..."
"$BUILD_DIR/mistbar-daemon" &
DAEMON_PID=$!
sleep 0.4
"$BUILD_DIR/mistbar-settings" &
wait "$DAEMON_PID"
