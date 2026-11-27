#!/usr/bin/env bash
# ============================================================================
# Mistbar Installation & Update Script
# ============================================================================

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

# Parse flags
TARGET_CHANNEL="" # "release", "main", or "local"
NON_INTERACTIVE=false

for arg in "$@"; do
    case "$arg" in
        --release)
            TARGET_CHANNEL="release"
            ;;
        --main)
            TARGET_CHANNEL="main"
            ;;
        --local)
            TARGET_CHANNEL="local"
            ;;
        -y|--yes|--non-interactive)
            NON_INTERACTIVE=true
            ;;
        -h|--help)
            echo "Mistbar Installer & Updater"
            echo ""
            echo "Usage: ./install.sh [options]"
            echo ""
            echo "Options:"
            echo "  --release           Install/update to the latest stable release (recommended)"
            echo "  --main              Install/update to the latest development commit on main"
            echo "  --local             Install from the current local repository directory"
            echo "  -y, --yes           Non-interactive mode (use defaults)"
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

# Determine script location if running from a file
SCRIPT_SOURCE=""
SCRIPT_DIR=""
if [ -n "${BASH_SOURCE[0]:-}" ] && [ -f "${BASH_SOURCE[0]}" ]; then
    SCRIPT_SOURCE="$(readlink -f "${BASH_SOURCE[0]}")"
    SCRIPT_DIR="$(cd "$(dirname "$SCRIPT_SOURCE")" && pwd)"
fi

IS_LOCAL_REPO=false
if [ -n "$SCRIPT_DIR" ] && [ "$SCRIPT_DIR" != "$INSTALL_DIR" ]; then
    if [ -f "$SCRIPT_DIR/CMakeLists.txt" ] && [ -f "$SCRIPT_DIR/mistbar" ] && [ -d "$SCRIPT_DIR/src" ]; then
        IS_LOCAL_REPO=true
    fi
fi

# Detect existing installation
IS_UPDATE=false
if [ -d "$INSTALL_DIR" ]; then
    IS_UPDATE=true
elif [ -L "$BIN_TARGET" ] || [ -f "$BIN_TARGET" ]; then
    IS_UPDATE=true
fi

WAS_RUNNING=false
if pgrep -x "mistbar-daemon" >/dev/null 2>&1; then
    WAS_RUNNING=true
fi

echo -e "${CYAN}${BOLD}"
echo "  ========================================"
if [ "$IS_UPDATE" = true ]; then
    echo "              Mistbar Updater             "
else
    echo "             Mistbar Installer            "
fi
echo "  ========================================"
echo -e "${NC}"

if [ "$IS_UPDATE" = true ]; then
    echo -e "${BLUE}Detected existing installation. Updating Mistbar in $INSTALL_DIR...${NC}"
else
    echo -e "${BLUE}Setting up new Mistbar installation into $INSTALL_DIR...${NC}"
fi
echo ""

# Handle local clone execution option
if [ "$IS_LOCAL_REPO" = true ] && [ -z "$TARGET_CHANNEL" ]; then
    echo -e "${CYAN}Running installer from local repository: ${BOLD}$SCRIPT_DIR${NC}"
    echo "Choose installation source:"
    echo -e "  ${BOLD}1)${NC} Fetch & install from GitHub to $INSTALL_DIR ${GREEN}(Recommended)${NC}"
    echo -e "  ${BOLD}2)${NC} Install from current local directory into $INSTALL_DIR ${DIM}(Developer mode)${NC}"
    echo ""
    src_choice="$(prompt_user "Select option [1/2] (default: 1): " "1")"
    if [ "$src_choice" = "2" ]; then
        TARGET_CHANNEL="local"
    fi
    echo ""
fi

# 1. Source Preparation
echo -e "${BLUE}[1/7] Preparing Mistbar source files...${NC}"

if [ "$TARGET_CHANNEL" = "local" ]; then
    echo -e "  Syncing local files from $SCRIPT_DIR to $INSTALL_DIR..."
    mkdir -p "$INSTALL_DIR"
    if command -v rsync &>/dev/null; then
        rsync -a --exclude='.git' --exclude='build' "$SCRIPT_DIR/" "$INSTALL_DIR/"
    else
        cp -rf "$SCRIPT_DIR/CMakeLists.txt" "$SCRIPT_DIR/mistbar" "$SCRIPT_DIR/install.sh" \
               "$SCRIPT_DIR/update.sh" "$SCRIPT_DIR/uninstall.sh" "$SCRIPT_DIR/view.sh" \
               "$SCRIPT_DIR/README.md" "$SCRIPT_DIR/LICENSE" "$INSTALL_DIR/" 2>/dev/null || true
        mkdir -p "$INSTALL_DIR/src" "$INSTALL_DIR/icons" "$INSTALL_DIR/shaders" "$INSTALL_DIR/protocols" "$INSTALL_DIR/data" "$INSTALL_DIR/tests"
        cp -rf "$SCRIPT_DIR/src/." "$INSTALL_DIR/src/" 2>/dev/null || true
        cp -rf "$SCRIPT_DIR/icons/." "$INSTALL_DIR/icons/" 2>/dev/null || true
        cp -rf "$SCRIPT_DIR/shaders/." "$INSTALL_DIR/shaders/" 2>/dev/null || true
        cp -rf "$SCRIPT_DIR/protocols/." "$INSTALL_DIR/protocols/" 2>/dev/null || true
        cp -rf "$SCRIPT_DIR/data/." "$INSTALL_DIR/data/" 2>/dev/null || true
        cp -rf "$SCRIPT_DIR/tests/." "$INSTALL_DIR/tests/" 2>/dev/null || true
    fi
    echo -e "${GREEN}  [ok] Synced local repository to $INSTALL_DIR${NC}"
else
    if ! command -v git &>/dev/null; then
        echo -e "${RED}Error: git is required to install or update Mistbar.${NC}"
        echo "Please install git (e.g. sudo pacman -S git) and run the installer again."
        exit 1
    fi

    if [ ! -d "$INSTALL_DIR/.git" ]; then
        if [ -d "$INSTALL_DIR" ]; then
            BACKUP_DIR="${INSTALL_DIR}.bak.$(date +%s)"
            echo -e "${YELLOW}  Notice: $INSTALL_DIR exists but is not a git repository. Backing up to $BACKUP_DIR...${NC}"
            mv "$INSTALL_DIR" "$BACKUP_DIR"
        fi
        echo -e "  Cloning repository into $INSTALL_DIR..."
        git clone "$REPO_URL" "$INSTALL_DIR"
    fi

    echo -e "  Fetching latest release tags and commits..."
    git -C "$INSTALL_DIR" fetch origin --tags --quiet

    LATEST_TAG="$(git -C "$INSTALL_DIR" tag -l --sort=-v:refname | head -n 1)"

    if [ -z "$TARGET_CHANNEL" ]; then
        echo ""
        if [ "$IS_UPDATE" = true ]; then
            echo -e "${BOLD}Select update channel:${NC}"
        else
            echo -e "${BOLD}Select installation channel:${NC}"
        fi
        echo -e "  ${BOLD}1)${NC} Latest Release ${GREEN}(Recommended - stable: ${LATEST_TAG:-v0.3.0})${NC}"
        echo -e "  ${BOLD}2)${NC} Main Branch ${DIM}(Bleeding edge - latest features)${NC}"
        echo ""
        channel_choice="$(prompt_user "Choice [1/2] (default: 1): " "1")"
        if [ "$channel_choice" = "2" ]; then
            TARGET_CHANNEL="main"
        else
            TARGET_CHANNEL="release"
        fi
        echo ""
    fi

    git -C "$INSTALL_DIR" stash --quiet 2>/dev/null || true
    if [ "$TARGET_CHANNEL" = "release" ]; then
        if [ -n "$LATEST_TAG" ]; then
            echo -e "  Checking out latest release: ${BOLD}$LATEST_TAG${NC}..."
            git -C "$INSTALL_DIR" checkout "$LATEST_TAG" --quiet
            echo -e "${GREEN}  [ok] Checked out release $LATEST_TAG${NC}"
        else
            echo -e "${YELLOW}  No release tags found, defaulting to main branch...${NC}"
            git -C "$INSTALL_DIR" checkout -B main origin/main --quiet
            echo -e "${GREEN}  [ok] Checked out main branch${NC}"
        fi
    elif [ "$TARGET_CHANNEL" = "main" ]; then
        echo -e "  Checking out ${BOLD}main${NC} branch (latest development)..."
        git -C "$INSTALL_DIR" checkout -B main origin/main --quiet
        echo -e "${GREEN}  [ok] Checked out main branch${NC}"
    fi
fi

# 2. Build & Install Native C++20 Binaries
echo -e "${BLUE}[2/7] Building Mistbar with CMake...${NC}"
cmake -S "$INSTALL_DIR" -B "$INSTALL_DIR/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$LOCAL_PREFIX"
cmake --build "$INSTALL_DIR/build" -j
cmake --install "$INSTALL_DIR/build"
echo -e "${GREEN}  [ok] Compiled and installed binaries to $LOCAL_PREFIX/bin${NC}"

# 3. Ensure scripts are executable, install desktop entry & link CLI
echo -e "${BLUE}[3/7] Installing desktop application & linking CLI...${NC}"
mkdir -p "$LOCAL_PREFIX/bin" "$LOCAL_PREFIX/share/applications" "$LOCAL_PREFIX/share/icons/hicolor/scalable/apps"
chmod +x "$INSTALL_DIR/mistbar" "$INSTALL_DIR/install.sh" "$INSTALL_DIR/update.sh" "$INSTALL_DIR/uninstall.sh" "$INSTALL_DIR/view.sh"
ln -sf "$INSTALL_DIR/mistbar" "$BIN_TARGET"

DESKTOP_TARGET="$LOCAL_PREFIX/share/applications/mistbar.desktop"
ICON_TARGET="$LOCAL_PREFIX/share/icons/hicolor/scalable/apps/mistbar.svg"
if [ -f "$INSTALL_DIR/icons/mistbar-logo.svg" ]; then
    cp -f "$INSTALL_DIR/icons/mistbar-logo.svg" "$ICON_TARGET"
fi
cat << EOF > "$DESKTOP_TARGET"
[Desktop Entry]
Type=Application
Name=Mistbar
GenericName=Menu Bar Settings
Comment=Configure Mistbar macOS Liquid Glass top bar
Exec=$LOCAL_PREFIX/bin/mistbar-settings
Icon=$ICON_TARGET
StartupWMClass=mistbar-settings
Terminal=false
Categories=Settings;DesktopSettings;Utility;
StartupNotify=false
EOF
chmod 644 "$DESKTOP_TARGET"

if command -v update-desktop-database &>/dev/null; then
    update-desktop-database "$LOCAL_PREFIX/share/applications" 2>/dev/null || true
fi
if command -v gtk-update-icon-cache &>/dev/null; then
    gtk-update-icon-cache -f -t "$LOCAL_PREFIX/share/icons/hicolor" 2>/dev/null || true
fi
if [ ! -x "$LOCAL_PREFIX/bin/mistbar-settings" ] || [ ! -x "$LOCAL_PREFIX/bin/mistbar-daemon" ]; then
    echo -e "${RED}Error: Expected binaries missing in $LOCAL_PREFIX/bin after cmake --install.${NC}"
    exit 1
fi
echo -e "${GREEN}  [ok] Installed Mistbar app ($DESKTOP_TARGET) and CLI ($BIN_TARGET)${NC}"

# 4. Clean Up Legacy Niri Compositor Blur Rule (prevents full-box compositor blur behind applets/bar)
echo -e "${BLUE}[4/7] Ensuring clean transparent Mistbar surfaces in Niri...${NC}"
NIRI_CONFIG="$HOME/.config/niri/config.kdl"
if [ -f "$NIRI_CONFIG" ]; then
    python3 -c "
kdl_path = '$NIRI_CONFIG'
with open(kdl_path, 'r') as f:
    content = f.read()

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

updated = remove_mistbar_layer_rules(content)
lines = updated.splitlines(True)
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
updated = ''.join(cleaned)

if updated != content:
    with open(kdl_path, 'w') as f:
        f.write(updated)
" 2>/dev/null || true

    if command -v niri &>/dev/null; then
        if niri validate 2>/dev/null; then
            echo -e "${GREEN}  [ok] Verified clean Niri configuration (no compositor blur box behind applets/bar)${NC}"
            niri msg action load-config-file 2>/dev/null || true
        else
            echo -e "${YELLOW}  [warn] Niri config validation reported an issue; please check $NIRI_CONFIG${NC}"
        fi
    else
        echo -e "${GREEN}  [ok] Removed legacy mistbar blur rule from Niri config${NC}"
    fi
else
    echo -e "${DIM}  Niri config not detected, skipping Niri cleanup${NC}"
fi

# 5. Configure default auto-start on login
echo -e "${BLUE}[5/7] Configuring default auto-start on login...${NC}"

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
echo -e "${GREEN}  [ok] Configured universal XDG autostart (~/.config/autostart/mistbar.desktop)${NC}"

if [ -f "$NIRI_CONFIG" ]; then
    python3 -c "
kdl_path = '$NIRI_CONFIG'
with open(kdl_path, 'r') as f:
    lines = f.readlines()

new_lines = []
modified = False
has_mistbar_startup = False

for line in lines:
    stripped = line.strip()
    if stripped.startswith('spawn-at-startup') and 'mistbar' in stripped:
        has_mistbar_startup = True
        new_lines.append(line)
    elif stripped.startswith('spawn-at-startup') and 'waybar' in stripped:
        new_lines.append('// spawn-at-startup \"waybar\" // Disabled by Mistbar installer\n')
        modified = True
    else:
        new_lines.append(line)

if not has_mistbar_startup:
    new_lines.append('\n// Start Mistbar on desktop launch\nspawn-at-startup \"mistbar\" \"start\"\n')
    modified = True

if modified:
    with open(kdl_path, 'w') as f:
        f.writelines(new_lines)
" 2>/dev/null || true
    echo -e "${GREEN}  [ok] Configured Mistbar as default bar in Niri config${NC}"

    if pgrep -x waybar &>/dev/null; then
        echo -e "${BLUE}  Stopping currently running Waybar...${NC}"
        pkill -x waybar 2>/dev/null || true
    fi

    if command -v niri &>/dev/null; then
        niri msg action load-config-file 2>/dev/null || true
    fi
fi

HYPR_CONFIG="$HOME/.config/hypr/hyprland.conf"
if [ -f "$HYPR_CONFIG" ]; then
    python3 -c "
conf_path = '$HYPR_CONFIG'
with open(conf_path, 'r') as f:
    lines = f.readlines()

new_lines = []
modified = False
has_mistbar = False

for line in lines:
    stripped = line.strip()
    if 'mistbar' in stripped and ('exec-once' in stripped or 'exec' in stripped):
        has_mistbar = True
        new_lines.append(line)
    elif stripped.startswith('exec-once') and 'waybar' in stripped:
        new_lines.append('# exec-once = waybar # Disabled by Mistbar installer\n')
        modified = True
    else:
        new_lines.append(line)

if not has_mistbar:
    new_lines.append('\n# Start Mistbar on desktop launch\nexec-once = mistbar start\n')
    modified = True

if modified:
    with open(conf_path, 'w') as f:
        f.writelines(new_lines)
" 2>/dev/null || true
    echo -e "${GREEN}  [ok] Configured Mistbar in Hyprland config${NC}"
fi

# 6. Restart active daemon if updating
echo -e "${BLUE}[6/7] Checking active Mistbar service...${NC}"
if [ "$WAS_RUNNING" = true ]; then
    echo -e "  Restarting active Mistbar instance to apply updates..."
    "$BIN_TARGET" restart || true
else
    echo -e "${DIM}  Mistbar daemon was not running.${NC}"
fi

# 7. PATH check
echo -e "${BLUE}[7/7] Verifying PATH...${NC}"
if echo "$PATH" | grep -q "$LOCAL_PREFIX/bin"; then
    echo -e "${GREEN}  [ok] $LOCAL_PREFIX/bin is in your PATH${NC}"
else
    echo -e "${YELLOW}  [warn] $LOCAL_PREFIX/bin is NOT in your PATH${NC}"
    echo "  Add this to your shell profile (~/.bashrc, ~/.zshrc, etc.):"
    echo "    export PATH=\"\$HOME/.local/bin:\$PATH\""
fi

echo ""
if [ "$IS_UPDATE" = true ]; then
    echo -e "${GREEN}${BOLD}Mistbar successfully updated in $INSTALL_DIR.${NC}"
else
    echo -e "${GREEN}${BOLD}Mistbar installation complete in $INSTALL_DIR.${NC}"
fi

echo ""
echo "Usage:"
echo -e "  ${CYAN}mistbar${NC}              Open Mistbar Settings"
echo -e "  ${CYAN}mistbar start${NC}        Start the bar"
echo -e "  ${CYAN}mistbar stop${NC}         Stop the bar"
echo -e "  ${CYAN}mistbar restart${NC}      Restart the bar"
echo -e "  ${CYAN}mistbar update${NC}       Update Mistbar to latest release or main"
echo -e "  ${CYAN}mistbar status${NC}       Check status"
echo -e "  ${CYAN}mistbar --help${NC}       Show all commands"
echo ""
echo -e "To uninstall anytime, run:"
echo -e "  ${CYAN}mistbar uninstall${NC} (or ${CYAN}$INSTALL_DIR/uninstall.sh${NC})"
