#!/usr/bin/env bash
# Mistbar Installation and Update Script

set -euo pipefail

INSTALL_DIR="${MISTBAR_DIR:-$HOME/.mistbar}"
LOCAL_PREFIX="$HOME/.local"
BIN_TARGET="$LOCAL_PREFIX/bin/mistbar"
REPO_URL="https://github.com/TattvaOrg/Mistbar.git"

RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
YELLOW='\033[1;33m'
DIM='\033[2m'
BOLD='\033[1m'
NC='\033[0m'

TARGET_CHANNEL=""
NON_INTERACTIVE=false

for arg in "$@"; do
    case "$arg" in
        --release) TARGET_CHANNEL="release" ;;
        --main)    TARGET_CHANNEL="main" ;;
        --local)   TARGET_CHANNEL="local" ;;
        -y|--yes|--non-interactive) NON_INTERACTIVE=true ;;
        -h|--help)
            echo "Mistbar Installer and Updater"
            echo "Usage: ./install.sh [--release|--main|--local] [-y|--yes]"
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

SCRIPT_DIR=""
if [ -n "${BASH_SOURCE[0]:-}" ] && [ -f "${BASH_SOURCE[0]}" ]; then
    SCRIPT_DIR="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")" && pwd)"
fi

IS_LOCAL_REPO=false
if [ -n "$SCRIPT_DIR" ] && [ -f "$SCRIPT_DIR/CMakeLists.txt" ] && [ -d "$SCRIPT_DIR/src" ]; then
    IS_LOCAL_REPO=true
fi

echo -e "${CYAN}${BOLD}Mistbar Installer (Native C++20 / Liquid Glass)${NC}"
echo ""

if [ "$IS_LOCAL_REPO" = true ] && [ -z "$TARGET_CHANNEL" ]; then
    echo "Choose installation source:"
    echo "  1) Fetch and install from GitHub to $INSTALL_DIR"
    echo "  2) Build and install from local directory ($SCRIPT_DIR)"
    choice="$(prompt_user "Select option [1/2] (default: 2): " "2")"
    if [ "$choice" = "2" ]; then
        TARGET_CHANNEL="local"
    else
        TARGET_CHANNEL="main"
    fi
fi

echo -e "${BLUE}[1/5] Preparing source files...${NC}"
if [ "$TARGET_CHANNEL" = "local" ] && [ "$SCRIPT_DIR" != "$INSTALL_DIR" ]; then
    mkdir -p "$INSTALL_DIR"
    if command -v rsync &>/dev/null; then
        rsync -a --exclude='.git' --exclude='build' "$SCRIPT_DIR/" "$INSTALL_DIR/"
    else
        cp -rf "$SCRIPT_DIR/." "$INSTALL_DIR/"
    fi
elif [ "$TARGET_CHANNEL" != "local" ]; then
    if [ -d "$INSTALL_DIR/.git" ]; then
        git -C "$INSTALL_DIR" fetch --all --tags >/dev/null 2>&1 || true
        git -C "$INSTALL_DIR" pull --ff-only >/dev/null 2>&1 || true
    else
        rm -rf "$INSTALL_DIR"
        git clone --depth 1 "$REPO_URL" "$INSTALL_DIR"
    fi
fi

echo -e "${BLUE}[2/5] Compiling Mistbar with CMake...${NC}"
cmake -S "$INSTALL_DIR" -B "$INSTALL_DIR/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$LOCAL_PREFIX"
cmake --build "$INSTALL_DIR/build" -j

echo -e "${BLUE}[3/5] Installing binaries, SVG icons, and desktop entry...${NC}"
cmake --install "$INSTALL_DIR/build"
mkdir -p "$LOCAL_PREFIX/bin"
chmod +x "$INSTALL_DIR/mistbar" "$INSTALL_DIR/install.sh" "$INSTALL_DIR/uninstall.sh" "$INSTALL_DIR/view.sh"
ln -sf "$INSTALL_DIR/mistbar" "$BIN_TARGET"

if command -v update-desktop-database &>/dev/null; then
    update-desktop-database "$LOCAL_PREFIX/share/applications" 2>/dev/null || true
fi

echo -e "${BLUE}[4/5] Configuring Niri blur rule and autostart...${NC}"
NIRI_CONFIG="$HOME/.config/niri/config.kdl"
if [ -f "$NIRI_CONFIG" ]; then
    python3 -c "
kdl_path = '$NIRI_CONFIG'
with open(kdl_path, 'r') as f:
    content = f.read()

if 'namespace=\"mistbar\"' not in content:
    rule = '''
layer-rule {
    match namespace=\"mistbar\"
    geometry-corner-radius 16
    background-effect {
        blur true
    }
}
'''
    content = content + '\n' + rule

if 'spawn-at-startup \"mistbar\"' not in content:
    content = content + '\nspawn-at-startup \"mistbar\" \"start\"\n'

with open(kdl_path, 'w') as f:
    f.write(content)
" 2>/dev/null || true
    if command -v niri &>/dev/null && niri validate 2>/dev/null; then
        niri msg action load-config-file 2>/dev/null || true
    fi
fi

AUTOSTART_DIR="$HOME/.config/autostart"
mkdir -p "$AUTOSTART_DIR"
cat << 'EOF' > "$AUTOSTART_DIR/mistbar.desktop"
[Desktop Entry]
Type=Application
Name=Mistbar
Comment=macOS Tahoe Liquid Glass Top Bar
Exec=mistbar start
Terminal=false
Categories=Utility;
X-GNOME-Autostart-enabled=true
EOF

echo -e "${BLUE}[5/5] Verifying installation...${NC}"
if ! echo "$PATH" | grep -q "$LOCAL_PREFIX/bin"; then
    echo -e "${YELLOW}Note: Add $LOCAL_PREFIX/bin to your PATH to invoke mistbar directly.${NC}"
fi

echo -e "${GREEN}${BOLD}Mistbar installation complete.${NC}"
echo "Run 'mistbar' to open Mistbar Settings or 'mistbar start' to start the bar."
