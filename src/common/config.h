/* Mistbar - MIT License */

#pragma once

#include <string>
#include <vector>

namespace mistbar::config {

struct MistbarConfig {
    // Theme and geometry
    std::string theme            = "dark";       // "dark" | "light"
    std::string glass_preset     = "clear";      // "clear" | "frosted" | "smoked" | "aqua" | "regular" | "dark" | "solid"
    std::string look             = "attached";   // "attached" | "pill"
    std::string icon_style       = "default";    // "default" | "dark" | "clear" | "tinted"
    std::string accent_color     = "#0A84FF";
    std::string glassy_text_color = "auto";      // "auto" | "white" | "dark"
    std::string wallpaper        = "";
    bool        auto_hide        = false;

    int bar_height    = 26;
    int bar_margin    = 6;
    int border_radius = 14;

    // Optical Liquid Glass shader parameters (0..1 floats except blur_radius)
    int   blur_radius          = 20;     // 0..48 px
    float refraction_strength  = 0.45f;  // 0.0..1.0
    float bezel_thickness      = 0.35f;  // 0.0..1.0
    float chromatic_aberration = 0.25f;  // 0.0..1.0
    float specular_intensity   = 0.55f;  // 0.0..1.0
    float tint_opacity         = 0.14f;  // 0.0..1.0
    float glass_opacity        = 0.85f;  // 0.1..1.0
    bool  cursor_reactive      = true;
    bool  gpu_acceleration     = true;

    // Bar sections and widgets
    bool show_app_menu       = true;
    bool show_workspaces     = false;
    bool show_privacy        = true;
    bool show_tray           = false;
    bool show_battery        = true;
    bool show_battery_pct    = false;
    bool show_brightness     = false;
    bool show_volume         = false;
    bool show_network        = true;
    bool show_bluetooth      = false;
    bool show_spotlight      = true;
    bool show_control_center = true;
    bool show_power_menu     = false;

    // Control Center toggles
    bool cc_airdrop_on       = false;
    bool cc_focus_on         = false;

    std::string clock_format = "12h"; // "12h" | "24h"
    bool        show_seconds = false;
};

// Applies preset defaults to optical glass parameters while preserving layout/widget flags.
void apply_glass_preset(MistbarConfig& cfg, const std::string& preset_name);

std::string config_file_path();
void ensure_default_config();
MistbarConfig load_config();
bool save_config(const MistbarConfig& cfg);

} // namespace mistbar::config
