# Mistbar

<img width="1361" height="41" alt="bar" src="https://github.com/user-attachments/assets/c00213bb-c705-40ba-b789-558eaeba2c9e" />
<img width="1352" height="40" alt="glass" src="https://github.com/user-attachments/assets/d35bd5cb-e3e3-46b2-9856-ffe23173c9eb" />
<img width="1365" height="40" alt="glasss" src="https://github.com/user-attachments/assets/f2d739d0-7f9d-4fd9-af03-ac0b35a7b3b4" />

>[!IMPORTANT]
>Make sure that you choose latest update during installetion , because of the main branch is under new devlopmentes, and if fails cause of cmake or that things, just download a release and install it locally  so.....

**macOS Tahoe Liquid Glass Top Bar & Black-Only Desktop Settings for Linux (Wayland)**
- A native C++20 / GLSL Liquid Glass menu bar (`mistbar-daemon`) and pure-black System Settings desktop app (`mistbar-settings`) built on Wayland (`wlr-layer-shell`, `wlr-foreign-toplevel-management`, `xdg-shell`), Cairo, Pango, `librsvg`, and OpenGL shaders.

## Features

- **Native C++20 / GLSL Liquid Glass Engine** - Perimeter meniscus refraction, fluid surface normals, 5-tap spectral chromatic dispersion, anisotropic diagonal specular rim highlights, cursor-reactive lighting, and GPU Gaussian blur (with CPU fallback)
- **Edge-to-Edge Transparent & Floating Pill Bar** - Switch dynamically between edge-to-edge transparent bar (`attached`) and floating capsule (`pill`), plus `Auto-Hide`
- **macOS Menu Bar Layout** - Clean `M` menu, bold active app name, File / Edit / View / Go / Window / Help menus (left), optional Niri workspace dots (center), and crisp monochrome status icons + Clock (right)
- **Slim Control Center & Live Shader Tuner** - `mizzle`-inspired compact Control Center (Wi-Fi, Bluetooth, AirDrop, Focus, Media playback, Sound & Display sliders, 4x2 vector quick actions) with `"Edit Controls"` opening the real-time Shader Tuner
- **Built-in Liquid Glass Spotlight** - Floating search pill scanning installed `.desktop` applications, Mistbar Settings pages, and quick actions
- **Black-Only Settings App** - Pure `#000000` obsidian desktop settings window matching macOS Appearance with live Liquid Glass preview, optical sliders, widget toggles, Wi-Fi, Bluetooth, Energy, Sound, Display, and About panes
- **Real Hardware Backends** - Live integration with `nmcli`, `bluetoothctl`, `wpctl`, `brightnessctl`, `playerctl`, `powerprofilesctl`, and D-Bus StatusNotifier tray
- **CLI Interface** - `mistbar start/stop/restart/status/update/uninstall` with colored output

## Dependencies

| Package | Purpose |
|---|---|
| C++20 compiler & CMake (>= 3.20) | Native build |
| `wayland-client`, `wayland-scanner`, `xkbcommon` | Wayland `wlr-layer-shell` & input |
| `cairo`, `pangocairo`, `gdk-pixbuf-2.0`, `librsvg-2.0` | Vector UI & SVG icon rendering |
| `wayland-egl`, `egl`, `gl` | Hardware GLSL Liquid Glass shaders |
| `dbus-1` | System tray / D-Bus integration |
| [Niri](https://github.com/YaLTeR/niri) | Wayland compositor (recommended) |
| NetworkManager (`nmcli`) | Wi-Fi status & switching |
| WirePlumber (`wpctl`) | Audio volume & output sinks |
| BlueZ (`bluetoothctl`) | Bluetooth devices |
| `brightnessctl`, `playerctl`, `powerprofilesctl` | Display brightness, media, power profiles |

## Live Preview (Try Without Installing)

You can try Mistbar live without downloading or installing anything permanently:

```bash
# One-liner remote preview:
curl -sSL https://raw.githubusercontent.com/TattvaOrg/Mistbar/main/view.sh | bash

# Or from local clone:
./view.sh
```

Press `Ctrl+C` at any time to close the preview and clean up.

## Installation & Updates

### Quick Install (Recommended)

Run this one-liner in your terminal to install Mistbar directly:

```bash
bash -c "$(curl -fsSL https://raw.githubusercontent.com/TattvaOrg/Mistbar/main/install.sh)"
```

The installer will:
- Install Mistbar into `~/.mistbar`
- Prompt you to select your installation channel:
  - **1) Latest Release** *(Recommended - stable releases)*
  - **2) Main Branch** *(Bleeding edge - latest features)*
- Compile with CMake and install binaries (`mistbar-daemon`, `mistbar-settings`) into `~/.local/bin`
- Symlink `mistbar` CLI into `~/.local/bin/mistbar`
- Configure Wayland blur rules and autostart

### Updating Mistbar

If Mistbar is already installed, update anytime with:

```bash
mistbar update
# or one-liner:
bash -c "$(curl -fsSL https://raw.githubusercontent.com/TattvaOrg/Mistbar/main/update.sh)"
```

*(Or simply re-run the one-liner installer above).* The updater automatically detects your existing installation in `~/.mistbar`, prompts for release or main branch (defaulting to the latest release), rebuilds, and restarts your active bar smoothly.

### Manual Installation (From Local Clone)

```bash
git clone https://github.com/TattvaOrg/Mistbar.git
cd Mistbar
./install.sh
```

### System Dependencies

```bash
# On Arch Linux / CachyOS:
mistbar install-deps
```

## Uninstallation

To remove Mistbar, run:

```bash
mistbar uninstall
# or:
~/.mistbar/uninstall.sh
# or one-liner:
bash -c "$(curl -fsSL https://raw.githubusercontent.com/TattvaOrg/Mistbar/main/uninstall.sh)"
```

The uninstaller stops running Mistbar processes, removes binaries, CLI symlinks, desktop entries, and compositor autostart/blur rules, and optionally prompts whether to delete the `~/.mistbar` installation and `~/.config/mistbar` configuration directories.

<!-- 

## Usage

```bash
mistbar                   # Open Mistbar Settings desktop app
mistbar settings [page]   # Open a specific Settings page (appearance, liquid-glass, menubar, wifi, ...)
mistbar start             # Start the bar daemon
mistbar stop              # Stop the bar daemon
mistbar restart           # Restart the bar daemon
mistbar update            # Update Mistbar to latest release or main
mistbar uninstall         # Uninstall Mistbar from system
mistbar tuner             # Open the live Liquid Glass Shader Tuner popover
mistbar spotlight         # Open the built-in Liquid Glass Spotlight search pill
mistbar toggle            # Toggle bar visibility / intelligent auto-hide
mistbar show              # Show the bar (always visible)
mistbar hide              # Hide the bar completely
mistbar auto-hide on      # Enable intelligent auto-hide (or mistbar --auto-hide)
mistbar auto-hide off     # Disable auto-hide (or mistbar --always-visible)
mistbar auto-hide toggle  # Toggle auto-hide state
mistbar preset regular    # Switch Liquid Glass preset (regular, frosted, dark, clear, solid)
mistbar look attached     # Switch to Edge-to-Edge Transparent Top Bar (default)
mistbar look pill         # Switch to Floating Rounded Pill capsule
mistbar theme dark        # Apply dark/black theme
mistbar status            # Check daemon and GPU glass status
mistbar config            # View or edit ~/.config/mistbar/config.json
mistbar build             # Build Mistbar locally with CMake
mistbar install-deps      # Install system dependencies
mistbar --help            # Show all commands and options
mistbar --version         # Show version
```

## Configuration & Themes

Mistbar can be customized live via `mistbar-settings`, the bar Shader Tuner, or by editing `~/.config/mistbar/config.json` (via `mistbar config`):

```json
{
  "theme": "dark",
  "glassPreset": "regular",
  "look": "attached",
  "iconStyle": "default",
  "accentColor": "#0A84FF",
  "autoHide": false,
  "barHeight": 26,
  "barMargin": 6,
  "borderRadius": 14,
  "blurRadius": 20,
  "refractionStrength": 0.45,
  "bezelThickness": 0.35,
  "chromaticAberration": 0.25,
  "specularIntensity": 0.55,
  "tintOpacity": 0.14,
  "glassOpacity": 0.85,
  "showAppMenu": true,
  "showWorkspaces": false,
  "showBattery": true,
  "showNetwork": true,
  "showBluetooth": false,
  "showVolume": false,
  "showSpotlight": true,
  "showControlCenter": true
}
```

- **Dynamic Look & Shape**: Switch between edge-to-edge transparent bar and floating capsule on-the-fly (`mistbar look attached` and `mistbar look pill`).
- **Dynamic Background Presets**: Switch between `regular`, `frosted`, `dark`, `clear`, and `solid` (`mistbar preset <name>`).
- **Real-Time Shader Tuning**: Tune refraction, dispersion, frost blur, specular shine, and tint opacity live via `mistbar tuner` or Mistbar Settings.

-->
