# Mistbar

**Native C++20 / GLSL macOS Tahoe Liquid Glass Menu Bar & Desktop Settings App for Wayland**

Mistbar provides an authentic macOS Tahoe top menu bar (`mistbar-daemon`) and a compact boxy macOS-style Desktop Settings application (`mistbar-settings`) built on Wayland (`wlr-layer-shell`, `wlr-foreign-toplevel-management`, `xdg-shell`), Cairo, Pango, `librsvg`, and OpenGL GLSL optical shaders.

## Features

- **Native C++/GLSL Liquid Glass Engine**: SDF rounded-pill refraction, curved bezel dome normals, RGB chromatic dispersion, Fresnel rim reflections, Blinn-Phong specular highlights, cursor-reactive lighting, and separable Gaussian frost blur (with CPU fallback).
- **Presets & Geometry**: Switch between `Regular`, `Frosted`, `Dark`, `Clear`, and `Solid` presets, `Dark` / `Light` theme modes, `Floating Pill` vs `Attached Edge` geometry, and intelligent `Auto-Hide`.
- **Mistbar Settings Desktop App**: Compact macOS-style settings window (`mistbar` or `mistbar-settings`) with a live interactive Liquid Glass preview card, optical shader sliders, widget visibility toggles, and live Wi-Fi, Bluetooth, Sound, Display, and About panes that sync instantly via Unix socket IPC.
- **Authentic macOS Menu Bar Layout & Custom SVGs**:
  - Left: Minimal Liquid Glass `M` menu (About, Settings, Sleep, Restart, Shut Down, Lock, Log Out), Bold Active App Name, and File / Edit / View / Go / Window / Help menus.
  - Center: Clickable Niri workspace indicators.
  - Right: Privacy microphone indicator, D-Bus StatusNotifier system tray, Battery + power profiles, Wi-Fi network switcher, Bluetooth paired devices, Volume + output switcher + scroll wheel, optional Brightness, Spotlight search, macOS Control Center (tiles, capsule sliders, media playback), and Clock + interactive Calendar at the far right.

## Requirements

- Wayland compositor supporting `wlr-layer-shell` (Niri recommended)
- C++20 compiler, CMake >= 3.20, `wayland-scanner`
- `wayland-client`, `xkbcommon`, `cairo`, `pangocairo`, `gdk-pixbuf-2.0`, `librsvg-2.0`, `dbus-1`
- Optional: `wayland-egl`, `egl`, `gl` (for hardware GLSL Liquid Glass), `libsystemd`
- Runtime tools: `nmcli`, `bluetoothctl`, `wpctl`, `brightnessctl`, `fuzzel`, `playerctl`, `powerprofilesctl`

## Build & Run

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Preview without installing:

```bash
./view.sh
```

Install system-wide to `~/.local`:

```bash
./install.sh --local
```

## CLI Usage

```bash
mistbar                  # Open Mistbar Settings desktop app
mistbar start            # Start the top bar daemon
mistbar stop             # Stop the top bar daemon
mistbar restart          # Restart the top bar daemon
mistbar status           # Show daemon & GPU glass status
mistbar toggle           # Toggle intelligent auto-hide
mistbar theme dark|light # Switch theme mode
mistbar preset regular|frosted|dark|clear|solid
mistbar look pill|attached
```
