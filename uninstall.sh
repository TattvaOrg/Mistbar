#!/usr/bin/env bash
# Mistbar Uninstaller

set -euo pipefail

INSTALL_DIR="${MISTBAR_DIR:-$HOME/.mistbar}"
CONFIG_DIR="$HOME/.config/mistbar"
LOCAL_PREFIX="$HOME/.local"
BIN_TARGET="$LOCAL_PREFIX/bin/mistbar"

RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
DIM='\033[2m'
BOLD='\033[1m'
NC='\033[0m'

NON_INTERACTIVE=false
for arg in "$@"; do
    case "$arg" in
        -y|--yes|--non-interactive) NON_INTERACTIVE=true ;;
        -h|--help)
            echo "Mistbar Uninstaller"
            echo "Usage: ./uninstall.sh [-y|--yes]"
            exit 0
            ;;
    esac
done

prompt_user() {
    local prompt_msg="$1"
    local default_val="${2:-}"
    local reply=""
    if [ "$NON_INTERACTIVE" = true ]; then
        echo "$default_val"
        return
    fi
    if [ -t 0 ]; then
        read -r -p "$prompt_msg" reply
    elif [ -e /dev/tty ]; then
        read -r -p "$prompt_msg" reply < /dev/tty
    else
        reply="$default_val"
    fi
    echo "${reply:-$default_val}"
}

echo -e "${CYAN}${BOLD}Mistbar Uninstaller${NC}"
echo ""

echo -e "${BLUE}[1/5] Stopping running processes...${NC}"
pkill -x "mistbar-daemon" 2>/dev/null || true
pkill -x "mistbar-settings" 2>/dev/null || true

echo -e "${BLUE}[2/5] Removing installed binaries, desktop entry, and icons...${NC}"
rm -f "$BIN_TARGET" "$LOCAL_PREFIX/bin/mistbar-daemon" "$LOCAL_PREFIX/bin/mistbar-settings"
rm -f "$LOCAL_PREFIX/share/applications/mistbar.desktop"
rm -f "$LOCAL_PREFIX/share/icons/hicolor/scalable/apps/mistbar.svg"
rm -rf "$LOCAL_PREFIX/share/mistbar"

echo -e "${BLUE}[3/5] Removing autostart entry...${NC}"
rm -f "$HOME/.config/autostart/mistbar.desktop"

echo -e "${BLUE}[4/5] Cleaning compositor configuration...${NC}"
NIRI_CONFIG="$HOME/.config/niri/config.kdl"
if [ -f "$NIRI_CONFIG" ]; then
    python3 -c "
import re
kdl_path = '$NIRI_CONFIG'
with open(kdl_path, 'r') as f:
    content = f.read()
content = re.sub(r'\n?spawn-at-startup\s+\"mistbar\"\s+\"start\"\n?', '\n', content)
with open(kdl_path, 'w') as f:
    f.write(content)
" 2>/dev/null || true
fi

echo -e "${BLUE}[5/5] Cleaning user data directories...${NC}"
if [ -d "$CONFIG_DIR" ]; then
    del_cfg="$(prompt_user "Remove user configuration directory ($CONFIG_DIR)? [y/N]: " "n")"
    if [[ "$del_cfg" =~ ^[Yy]$ ]]; then
        rm -rf "$CONFIG_DIR"
    fi
fi
if [ -d "$INSTALL_DIR" ]; then
    del_inst="$(prompt_user "Remove Mistbar installation directory ($INSTALL_DIR)? [y/N]: " "n")"
    if [[ "$del_inst" =~ ^[Yy]$ ]]; then
        rm -rf "$INSTALL_DIR"
    fi
fi

echo -e "${GREEN}${BOLD}Mistbar has been uninstalled.${NC}"
