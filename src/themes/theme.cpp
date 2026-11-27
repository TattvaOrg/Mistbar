/* Mistbar - MIT License */

#include "themes/theme.h"
#include "common/sysinfo.h"

#include <algorithm>
#include <cstdlib>

namespace mistbar::themes {

namespace {

double parse_hex_byte(const char* p) {
    char buf[3]{p[0], p[1], '\0'};
    return static_cast<double>(std::strtoul(buf, nullptr, 16)) / 255.0;
}

} // namespace

Color Color::from_hex(const std::string& hex, double alpha_override) {
    Color c;
    if (hex.size() >= 7 && hex[0] == '#') {
        c.r = parse_hex_byte(&hex[1]);
        c.g = parse_hex_byte(&hex[3]);
        c.b = parse_hex_byte(&hex[5]);
        c.a = (hex.size() >= 9) ? parse_hex_byte(&hex[7]) : 1.0;
    }
    if (alpha_override >= 0.0) {
        c.a = std::clamp(alpha_override, 0.0, 1.0);
    }
    return c;
}

std::vector<std::string> available_presets() {
    return {"regular", "frosted", "dark", "clear", "solid"};
}

ThemePalette resolve_palette(const config::MistbarConfig& cfg) {
    ThemePalette p;
    p.glass_preset         = cfg.glass_preset;
    p.dark_mode            = (cfg.theme != "light");
    if (cfg.glass_preset == "dark") {
        p.dark_mode = true;
    }
    p.solid_mode           = (cfg.glass_preset == "solid");
    p.clear_mode           = (cfg.glass_preset == "clear");
    p.corner_radius        = cfg.border_radius;
    p.window_radius        = 20;
    p.blur_radius          = cfg.blur_radius;
    p.glass_opacity        = cfg.glass_opacity;
    p.tint_opacity         = cfg.tint_opacity;
    p.specular_intensity   = cfg.specular_intensity;
    p.refraction_strength  = cfg.refraction_strength;
    p.bezel_thickness      = cfg.bezel_thickness;
    p.chromatic_aberration = cfg.chromatic_aberration;
    p.cursor_reactive      = cfg.cursor_reactive;

    p.accent = Color::from_hex(cfg.accent_color.empty() ? "#0A84FF" : cfg.accent_color);

    const double tint_a = std::clamp(static_cast<double>(cfg.tint_opacity), 0.0, 1.0);
    const double spec_a = std::clamp(static_cast<double>(cfg.specular_intensity), 0.0, 1.0);

    if (p.dark_mode) {
        // Pure black-only / deep obsidian palette matching macOS dark System Settings
        p.glass_tint        = {0.02, 0.02, 0.03, p.solid_mode ? 1.0 : tint_a * 0.85};
        p.panel_bg          = {0.04, 0.04, 0.05, p.solid_mode ? 0.99 : (p.clear_mode ? 0.18 : 0.72)};
        p.surface_bg        = {0.00, 0.00, 0.00, 0.99};
        p.surface_elevated  = {0.075, 0.075, 0.085, 0.99};
        p.sidebar_bg        = {0.055, 0.055, 0.065, 0.98};
        p.text_primary      = {0.96, 0.96, 0.97, 1.0};
        p.text_secondary    = {0.56, 0.56, 0.60, 1.0};
        p.border_outer      = {1.0, 1.0, 1.0, p.solid_mode ? 0.10 : (0.14 + spec_a * 0.22)};
        p.specular_top      = {1.0, 1.0, 1.0, p.solid_mode ? 0.10 : (0.22 + spec_a * 0.50)};
        p.inner_shadow      = {0.0, 0.0, 0.0, p.solid_mode ? 0.45 : 0.30};
        p.bar_bg            = p.solid_mode
                                  ? Color{0.00, 0.00, 0.00, 1.0}
                                  : (p.clear_mode
                                         ? Color{0.02, 0.02, 0.03, std::clamp(tint_a * 0.35, 0.02, 0.24)}
                                         : Color{0.03, 0.03, 0.04, std::clamp(tint_a * 0.72 + 0.03, 0.03, 0.72)});
        p.titlebar_active   = {0.04, 0.04, 0.05, 0.95};
        p.titlebar_inactive = {0.02, 0.02, 0.03, 0.90};
    } else {
        // Keep dark obsidian UI even if legacy config specifies light, honoring black-only UI
        p.dark_mode         = true;
        p.glass_tint        = {0.02, 0.02, 0.03, p.solid_mode ? 1.0 : tint_a * 0.85};
        p.panel_bg          = {0.04, 0.04, 0.05, p.solid_mode ? 0.99 : (p.clear_mode ? 0.18 : 0.72)};
        p.surface_bg        = {0.00, 0.00, 0.00, 0.99};
        p.surface_elevated  = {0.075, 0.075, 0.085, 0.99};
        p.sidebar_bg        = {0.055, 0.055, 0.065, 0.98};
        p.text_primary      = {0.96, 0.96, 0.97, 1.0};
        p.text_secondary    = {0.56, 0.56, 0.60, 1.0};
        p.border_outer      = {1.0, 1.0, 1.0, p.solid_mode ? 0.10 : (0.14 + spec_a * 0.22)};
        p.specular_top      = {1.0, 1.0, 1.0, p.solid_mode ? 0.10 : (0.22 + spec_a * 0.50)};
        p.inner_shadow      = {0.0, 0.0, 0.0, p.solid_mode ? 0.45 : 0.30};
        p.bar_bg            = p.solid_mode
                                  ? Color{0.00, 0.00, 0.00, 1.0}
                                  : Color{0.03, 0.03, 0.04, std::clamp(tint_a * 0.72 + 0.03, 0.03, 0.72)};
        p.titlebar_active   = {0.04, 0.04, 0.05, 0.95};
        p.titlebar_inactive = {0.02, 0.02, 0.03, 0.90};
    }

    if (cfg.glassy_text_color == "white") {
        p.text_primary   = {0.98, 0.99, 1.0, 1.0};
        p.text_secondary = {0.78, 0.82, 0.88, 1.0};
    } else if (cfg.glassy_text_color == "dark") {
        p.text_primary   = {0.08, 0.09, 0.11, 1.0};
        p.text_secondary = {0.32, 0.35, 0.40, 1.0};
    }

    p.wallpaper_path = !cfg.wallpaper.empty() ? cfg.wallpaper : sysinfo::get_active_wallpaper();
    return p;
}

} // namespace mistbar::themes
