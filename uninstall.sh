#!/usr/bin/env bash
# ============================================================================
# Mistbar Uninstaller
# ============================================================================

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
        -y|--yes|--non-interactive)
            NON_INTERACTIVE=true
            ;;
        -h|--help)
            echo "Mistbar Uninstaller"
            echo ""
            echo "Usage: ./uninstall.sh [options]"
            echo ""
            echo "Options:"
            echo "  -y, --yes           Non-interactive mode (use defaults, preserves configs/repo)"
            echo "  -h, --help          Show this help message"
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

    if [ -z "$reply" ]; then
        reply="$default_val"
    fi
    echo "$reply"
}

echo -e "${CYAN}${BOLD}"
echo "  ========================================"
echo "            Mistbar Uninstaller           "
echo "  ========================================"
echo -e "${NC}"

# 1. Stop Mistbar if running
echo -e "${BLUE}[1/5] Checking running instances...${NC}"
if pgrep -x "mistbar-daemon" >/dev/null 2>&1 || pgrep -x "mistbar-settings" >/dev/null 2>&1; then
    echo -e "  Stopping active Mistbar processes..."
    pkill -x "mistbar-daemon" 2>/dev/null || true
    pkill -x "mistbar-settings" 2>/dev/null || true
    sleep 0.3
    echo -e "${GREEN}  [ok] Mistbar stopped${NC}"
else
    echo -e "${DIM}  No active Mistbar instance found.${NC}"
fi

# 2. Remove CLI symlink, binaries, desktop entry, and icons
echo -e "${BLUE}[2/5] Removing binaries, desktop entry, and icons...${NC}"
removed_any=false
for path in \
    "$BIN_TARGET" \
    "$LOCAL_PREFIX/bin/mistbar-daemon" \
    "$LOCAL_PREFIX/bin/mistbar-settings" \
    "$LOCAL_PREFIX/share/applications/mistbar.desktop" \
    "$LOCAL_PREFIX/share/icons/hicolor/scalable/apps/mistbar.svg"; do
    if [ -L "$path" ] || [ -f "$path" ]; then
        rm -f "$path"
        echo -e "${GREEN}  [ok] Removed $path${NC}"
        removed_any=true
    fi
done
if [ -d "$LOCAL_PREFIX/share/mistbar" ]; then
    rm -rf "$LOCAL_PREFIX/share/mistbar"
    echo -e "${GREEN}  [ok] Removed $LOCAL_PREFIX/share/mistbar${NC}"
    removed_any=true
fi
if [ "$removed_any" = false ]; then
    echo -e "${DIM}  No installed binaries or assets found.${NC}"
fi

# 3. Remove universal XDG autostart
echo -e "${BLUE}[3/5] Removing XDG autostart desktop entry...${NC}"
AUTOSTART_FILE="$HOME/.config/autostart/mistbar.desktop"
if [ -f "$AUTOSTART_FILE" ]; then
    rm -f "$AUTOSTART_FILE"
    echo -e "${GREEN}  [ok] Removed $AUTOSTART_FILE${NC}"
else
    echo -e "${DIM}  XDG autostart file was not present.${NC}"
fi

# 4. Clean up compositor configs & restore previous bars
echo -e "${BLUE}[4/5] Cleaning compositor autostart configs...${NC}"

NIRI_CONFIG="$HOME/.config/niri/config.kdl"
if [ -f "$NIRI_CONFIG" ]; then
    python3 -c "
import re

kdl_path = '$NIRI_CONFIG'
with open(kdl_path, 'r') as f:
    content = f.read()

content = re.sub(r'//\s*spawn-at-startup\s+\"waybar\"\s+//\s*Disabled by Mistbar installer', 'spawn-at-startup \"waybar\"', content)
content = re.sub(r'\n?// Start Mistbar on desktop launch\n?spawn-at-startup\s+\"mistbar\"\s+\"start\"\n?', '\n', content)
content = re.sub(r'spawn-at-startup\s+\"mistbar\"\s+\"start\"\n?', '', content)

def remove_mistbar_layer_rules(text):
    result = []
    i = 0
    while i < len(text):
        idx = text.find('layer-rule', i)
        if idx == -1:
            result.append(text[i:])
            break
        brace_start = text.find('{', idx)
        if brace_start == -1:
            result.append(text[i:])
            break
        depth = 1
        pos = brace_start + 1
        while pos < len(text) and depth > 0:
            if text[pos] == '{':
                depth += 1
            elif text[pos] == '}':
                depth -= 1
            pos += 1
        block = text[idx:pos]
        if 'namespace=\"mistbar\"' in block or \"namespace='mistbar'\" in block or 'namespace=\"^mistbar' in block:
            while pos < len(text) and text[pos] in ' \t\r\n':
                if text[pos] == '\n':
                    pos += 1
                    break
                pos += 1
            result.append(text[i:idx])
            i = pos
        else:
            result.append(text[i:pos])
            i = pos
    return ''.join(result)

content = remove_mistbar_layer_rules(content)

lines = content.splitlines(True)
cleaned = []
open_depth = 0
for line in lines:
    stripped = line.strip()
    if stripped == '}' and open_depth <= 0:
        continue
    open_depth += line.count('{') - line.count('}')
    if open_depth < 0:
        open_depth = 0
    cleaned.append(line)
content = ''.join(cleaned)

with open(kdl_path, 'w') as f:
    f.write(content)
" 2>/dev/null || true
    echo -e "${GREEN}  [ok] Cleaned Niri configuration and restored previous bar defaults${NC}"
    if command -v niri &>/dev/null; then
        niri msg action load-config-file 2>/dev/null || true
    fi
fi

HYPR_CONFIG="$HOME/.config/hypr/hyprland.conf"
if [ -f "$HYPR_CONFIG" ]; then
    python3 -c "
import re
conf_path = '$HYPR_CONFIG'
with open(conf_path, 'r') as f:
    content = f.read()

content = re.sub(r'#\s*exec-once\s*=\s*waybar\s*#\s*Disabled by Mistbar installer', 'exec-once = waybar', content)
content = re.sub(r'\n?# Start Mistbar on desktop launch\n?exec-once\s*=\s*mistbar\s+start\n?', '\n', content)

with open(conf_path, 'w') as f:
    f.write(content)
" 2>/dev/null || true
    echo -e "${GREEN}  [ok] Cleaned Hyprland configuration${NC}"
fi

# 5. Clean configuration & installation directories
echo -e "${BLUE}[5/5] Cleaning data directories...${NC}"

if [ -d "$CONFIG_DIR" ]; then
    del_config="$(prompt_user "Remove user configuration directory ($CONFIG_DIR)? [y/N]: " "n")"
    if [[ "$del_config" =~ ^[Yy]$ ]]; then
        rm -rf "$CONFIG_DIR"
        echo -e "${GREEN}  [ok] Removed $CONFIG_DIR${NC}"
    else
        echo -e "${DIM}  Preserved $CONFIG_DIR${NC}"
    fi
fi

DELETE_INSTALL_DIR=false
if [ -d "$INSTALL_DIR" ]; then
    del_install="$(prompt_user "Remove Mistbar installation directory ($INSTALL_DIR)? [y/N]: " "n")"
    if [[ "$del_install" =~ ^[Yy]$ ]]; then
        DELETE_INSTALL_DIR=true
    else
        echo -e "${DIM}  Preserved $INSTALL_DIR${NC}"
    fi
fi

echo ""
echo -e "${GREEN}${BOLD}Mistbar has been successfully uninstalled.${NC}"
echo -e "To reinstall anytime, run:"
echo -e "  ${CYAN}bash -c \"\$(curl -fsSL https://raw.githubusercontent.com/TattvaOrg/Mistbar/main/install.sh)\"${NC}"

if [ "$DELETE_INSTALL_DIR" = true ]; then
    rm -rf "$INSTALL_DIR"
    echo -e "${GREEN}  [ok] Removed $INSTALL_DIR${NC}"
fi
