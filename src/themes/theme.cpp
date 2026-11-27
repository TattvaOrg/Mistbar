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
    return {"clear", "frosted", "smoked", "aqua", "regular", "dark", "solid"};
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
        // Authentic macOS Dark Mode palette (refined charcoal layers, crisp contrast)
        const bool is_aqua = (cfg.glass_preset == "aqua");
        const bool is_smoked = (cfg.glass_preset == "smoked" || cfg.glass_preset == "dark");
        const Color tint_base = is_aqua
            ? Color{0.20, 0.56, 0.96, 1.0}
            : (is_smoked ? Color{0.09, 0.12, 0.18, 1.0} : Color{0.92, 0.96, 1.00, 1.0});
        p.glass_tint        = {tint_base.r, tint_base.g, tint_base.b, p.solid_mode ? 1.0 : tint_a * 0.72};
        p.panel_bg          = p.solid_mode
                                  ? Color{0.11, 0.11, 0.13, 0.96}
                                  : (p.clear_mode
                                         ? Color{0.92, 0.96, 1.00, std::clamp(tint_a, 0.03, 0.14)}
                                         : Color{tint_base.r, tint_base.g, tint_base.b, std::clamp(tint_a, 0.06, 0.42)});
        p.surface_bg        = {0.094, 0.094, 0.106, 0.99}; // #18181B
        p.surface_elevated  = {0.137, 0.137, 0.153, 0.99}; // #232327
        p.sidebar_bg        = {0.118, 0.118, 0.130, 0.98}; // #1E1E21
        p.text_primary      = {0.96, 0.96, 0.97, 1.0};
        p.text_secondary    = {0.63, 0.64, 0.69, 1.0};
        p.border_outer      = {1.0, 1.0, 1.0, p.solid_mode ? 0.12 : (0.14 + spec_a * 0.22)};
        p.specular_top      = {1.0, 1.0, 1.0, p.solid_mode ? 0.12 : (0.24 + spec_a * 0.42)};
        p.inner_shadow      = {0.0, 0.0, 0.0, p.solid_mode ? 0.35 : 0.18};
        p.bar_bg            = p.solid_mode
                                  ? Color{0.08, 0.08, 0.10, 1.0}
                                  : (p.clear_mode
                                         ? Color{0.06, 0.08, 0.12, std::clamp(tint_a * 0.28, 0.02, 0.14)}
                                         : Color{0.07, 0.09, 0.14, std::clamp(tint_a * 0.55 + 0.04, 0.04, 0.32)});
        p.titlebar_active   = {0.118, 0.118, 0.130, 0.96};
        p.titlebar_inactive = {0.094, 0.094, 0.106, 0.92};
    } else {
        // Authentic macOS Light Mode palette for Settings & Menu Bar
        p.dark_mode         = false;
        p.glass_tint        = {0.96, 0.97, 1.00, p.solid_mode ? 1.0 : tint_a * 0.65};
        p.panel_bg          = p.solid_mode
                                  ? Color{0.96, 0.96, 0.98, 0.98}
                                  : (p.clear_mode
                                         ? Color{0.96, 0.98, 1.00, std::clamp(tint_a * 0.45, 0.04, 0.18)}
                                         : Color{0.94, 0.96, 0.99, std::clamp(tint_a * 0.65 + 0.08, 0.08, 0.36)});
        p.surface_bg        = {0.961, 0.961, 0.969, 0.99}; // #F5F5F7
        p.surface_elevated  = {1.000, 1.000, 1.000, 0.99}; // #FFFFFF
        p.sidebar_bg        = {0.918, 0.918, 0.937, 0.98}; // #EAEAEF
        p.text_primary      = {0.114, 0.114, 0.122, 1.0};  // #1D1D1F
        p.text_secondary    = {0.431, 0.431, 0.451, 1.0};  // #6E6E73
        p.border_outer      = {0.0, 0.0, 0.0, 0.10};
        p.specular_top      = {1.0, 1.0, 1.0, p.solid_mode ? 0.55 : 0.72};
        p.inner_shadow      = {0.0, 0.0, 0.0, 0.06};
        p.bar_bg            = p.solid_mode
                                  ? Color{0.95, 0.95, 0.97, 1.0}
                                  : Color{0.95, 0.96, 0.98, std::clamp(tint_a * 0.55 + 0.08, 0.06, 0.34)};
        p.titlebar_active   = {0.93, 0.93, 0.95, 0.98};
        p.titlebar_inactive = {0.91, 0.91, 0.93, 0.94};
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
