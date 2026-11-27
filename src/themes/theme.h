/* Mistbar - MIT License */

#pragma once

#include "common/config.h"

#include <string>
#include <vector>

namespace mistbar::themes {

struct Color {
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    double a = 1.0;

    static Color from_hex(const std::string& hex, double alpha_override = -1.0);
};

struct ThemePalette {
    std::string id = "tahoe";
    std::string glass_preset = "regular";
    bool dark_mode = true;
    bool solid_mode = false;
    bool clear_mode = false;

    Color accent;
    Color glass_tint;
    Color panel_bg;
    Color surface_bg;
    Color surface_elevated;
    Color sidebar_bg;
    Color text_primary;
    Color text_secondary;
    Color border_outer;
    Color specular_top;
    Color inner_shadow;
    Color bar_bg;
    Color titlebar_active;
    Color titlebar_inactive;

    int corner_radius = 16;
    int window_radius = 14;
    int blur_radius = 20;
    double glass_opacity = 0.85;
    double tint_opacity = 0.18;
    double specular_intensity = 0.55;
    double refraction_strength = 0.45;
    double bezel_thickness = 0.35;
    double chromatic_aberration = 0.25;
    bool cursor_reactive = true;
    std::string wallpaper_path;
};

std::vector<std::string> available_presets();
ThemePalette resolve_palette(const config::MistbarConfig& cfg);

} // namespace mistbar::themes
