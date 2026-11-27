#!/usr/bin/env bash
# ============================================================================
# Mistbar Updater Script
# ============================================================================

set -euo pipefail

INSTALL_DIR="${MISTBAR_DIR:-$HOME/.mistbar}"
INSTALL_URL="https://raw.githubusercontent.com/TattvaOrg/Mistbar/main/install.sh"

SCRIPT_DIR=""
if [ -n "${BASH_SOURCE[0]:-}" ] && [ -f "${BASH_SOURCE[0]}" ]; then
    SCRIPT_DIR="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")" && pwd)"
fi

if [ -n "$SCRIPT_DIR" ] && [ -f "$SCRIPT_DIR/install.sh" ]; then
    exec bash "$SCRIPT_DIR/install.sh" "$@"
elif [ -f "$INSTALL_DIR/install.sh" ]; then
    exec bash "$INSTALL_DIR/install.sh" "$@"
else
    exec bash -c "$(curl -fsSL "$INSTALL_URL")" -- "$@"
fi
