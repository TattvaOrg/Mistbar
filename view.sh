#!/usr/bin/env bash
# ============================================================================
# Mistbar Live Preview Script
# Run Mistbar temporarily without installing or modifying your system
# ============================================================================

set -euo pipefail

CLEANUP_DIR=""

cleanup() {
    echo ""
    echo "Closing Mistbar preview..."
    pkill -x "mistbar-daemon" 2>/dev/null || true
    pkill -x "mistbar-settings" 2>/dev/null || true
    if [ -n "$CLEANUP_DIR" ] && [ -d "$CLEANUP_DIR" ]; then
        rm -rf "$CLEANUP_DIR"
    fi
    echo "Preview session ended."
}

trap cleanup EXIT INT TERM

echo "Mistbar Live Preview"

REPO_DIR=""
if [ -n "${BASH_SOURCE[0]:-}" ] && [ -f "${BASH_SOURCE[0]}" ]; then
    SCRIPT_DIR="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")" && pwd)"
    if [ -f "$SCRIPT_DIR/CMakeLists.txt" ] && [ -d "$SCRIPT_DIR/src" ]; then
        REPO_DIR="$SCRIPT_DIR"
    fi
fi

if [ -z "$REPO_DIR" ]; then
    echo "Fetching Mistbar for live preview..."
    CLEANUP_DIR="$(mktemp -d /tmp/mistbar-preview.XXXXXX)"
    git clone --depth 1 https://github.com/TattvaOrg/Mistbar.git "$CLEANUP_DIR" >/dev/null 2>&1
    REPO_DIR="$CLEANUP_DIR"
fi

BUILD_DIR="$REPO_DIR/build"
if [ ! -x "$BUILD_DIR/mistbar-daemon" ] || [ ! -x "$BUILD_DIR/mistbar-settings" ]; then
    echo "Building Mistbar..."
    cmake -S "$REPO_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
    cmake --build "$BUILD_DIR" -j
fi

echo "Starting bar and Mistbar Settings preview (Ctrl+C to exit)..."
"$BUILD_DIR/mistbar-daemon" &
DAEMON_PID=$!
sleep 0.4
"$BUILD_DIR/mistbar-settings" &
wait "$DAEMON_PID"
