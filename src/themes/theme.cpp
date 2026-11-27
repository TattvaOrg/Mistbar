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
        p.glass_tint        = {0.10, 0.13, 0.20, p.solid_mode ? 1.0 : tint_a * 0.85};
        p.panel_bg          = {0.11, 0.13, 0.17, p.solid_mode ? 0.98 : (p.clear_mode ? 0.24 : 0.62)};
        p.surface_bg        = {0.115, 0.120, 0.140, 0.97};
        p.surface_elevated  = {0.165, 0.175, 0.205, 0.97};
        p.sidebar_bg        = {0.16, 0.18, 0.23, 0.72};
        p.text_primary      = {0.96, 0.97, 0.99, 1.0};
        p.text_secondary    = {0.64, 0.68, 0.75, 1.0};
        p.border_outer      = {1.0, 1.0, 1.0, p.solid_mode ? 0.14 : (0.18 + spec_a * 0.30)};
        p.specular_top      = {1.0, 1.0, 1.0, p.solid_mode ? 0.14 : (0.28 + spec_a * 0.62)};
        p.inner_shadow      = {0.0, 0.0, 0.0, p.solid_mode ? 0.30 : 0.22};
        p.bar_bg            = p.solid_mode
                                  ? Color{0.08, 0.09, 0.11, 1.0}
                                  : (p.clear_mode
                                         ? Color{0.12, 0.15, 0.22, std::clamp(tint_a * 0.45, 0.02, 0.35)}
                                         : Color{0.10, 0.13, 0.20, std::clamp(tint_a * 0.82 + 0.04, 0.04, 0.85)});
        p.titlebar_active   = {0.14, 0.16, 0.21, 0.86};
        p.titlebar_inactive = {0.11, 0.12, 0.15, 0.78};
    } else {
        p.glass_tint        = {0.98, 0.99, 1.00, p.solid_mode ? 1.0 : tint_a * 0.82};
        p.panel_bg          = {0.98, 0.99, 1.00, p.solid_mode ? 0.98 : (p.clear_mode ? 0.34 : 0.72)};
        p.surface_bg        = {0.95, 0.955, 0.97, 0.98};
        p.surface_elevated  = {1.00, 1.00, 1.00, 0.99};
        p.sidebar_bg        = {0.98, 0.99, 1.00, 0.80};
        p.text_primary      = {0.08, 0.09, 0.11, 1.0};
        p.text_secondary    = {0.40, 0.43, 0.48, 1.0};
        p.border_outer      = {1.0, 1.0, 1.0, p.solid_mode ? 0.60 : (0.58 + spec_a * 0.38)};
        p.specular_top      = {1.0, 1.0, 1.0, p.solid_mode ? 0.40 : (0.55 + spec_a * 0.42)};
        p.inner_shadow      = {0.0, 0.0, 0.0, 0.08};
        p.bar_bg            = p.solid_mode
                                  ? Color{0.97, 0.975, 0.985, 1.0}
                                  : (p.clear_mode
                                         ? Color{1.00, 1.00, 1.00, std::clamp(tint_a * 0.34, 0.04, 0.28)}
                                         : Color{0.98, 0.99, 1.00, std::clamp(tint_a * 0.72 + 0.08, 0.10, 0.84)});
        p.titlebar_active   = {0.96, 0.97, 0.99, 0.90};
        p.titlebar_inactive = {0.93, 0.94, 0.96, 0.82};
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
