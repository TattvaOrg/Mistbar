/* Mistbar - MIT License */

#include "common/config.h"
#include "common/json.h"
#include "common/svg_loader.h"
#include "glass/blur/gaussian_blur.h"
#include "glass/blur/gl_blur.h"
#include "themes/theme.h"

#include <cstdlib>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#define MISTBAR_CHECK(cond)                                                  \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::cerr << "Test check failed: " << #cond                      \
                      << " at " << __FILE__ << ":" << __LINE__ << "\n";      \
            std::exit(1);                                                    \
        }                                                                    \
    } while (0)

namespace {

void test_json_roundtrip() {
    const std::string raw = R"({"theme":"dark","barHeight":28,"autoHide":false,"refractionStrength":0.45})";
    auto parsed = mistbar::json::parse(raw);
    MISTBAR_CHECK(parsed.has_value());
    MISTBAR_CHECK(parsed->get_string("theme") == "dark");
    MISTBAR_CHECK(parsed->get_int("barHeight") == 28);
    MISTBAR_CHECK(parsed->get_bool("autoHide") == false);
    MISTBAR_CHECK(std::abs(parsed->get_double("refractionStrength") - 0.45) < 1e-6);
}

void test_config_presets() {
    mistbar::config::MistbarConfig cfg;
    mistbar::config::apply_glass_preset(cfg, "frosted");
    MISTBAR_CHECK(cfg.glass_preset == "frosted");
    MISTBAR_CHECK(cfg.blur_radius == 30);

    mistbar::config::apply_glass_preset(cfg, "clear");
    MISTBAR_CHECK(cfg.glass_preset == "clear");
    MISTBAR_CHECK(cfg.refraction_strength > 0.6f);

    mistbar::config::apply_glass_preset(cfg, "solid");
    MISTBAR_CHECK(cfg.glass_preset == "solid");
    MISTBAR_CHECK(cfg.blur_radius == 0);
    MISTBAR_CHECK(cfg.glass_opacity == 1.0f);

    auto pal = mistbar::themes::resolve_palette(cfg);
    MISTBAR_CHECK(pal.solid_mode == true);
}

void test_gaussian_blur_and_liquid_glass() {
    cairo_surface_t* src = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 64, 32);
    cairo_t* cr = cairo_create(src);
    cairo_set_source_rgba(cr, 0.2, 0.5, 0.9, 1.0);
    cairo_paint(cr);
    cairo_destroy(cr);

    cairo_surface_t* dst = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 64, 32);
    MISTBAR_CHECK(mistbar::glass::cpu_gaussian_blur(src, dst, 6));

    mistbar::glass::GlBlurEngine gl;
    if (gl.init()) {
        MISTBAR_CHECK(gl.blur(src, dst, 6));
        mistbar::glass::LiquidGlassParams lp;
        lp.corner_radius = 12.0;
        lp.refraction_strength = 0.5;
        lp.chromatic_aberration = 0.3;
        lp.specular_intensity = 0.6;
        MISTBAR_CHECK(gl.render_liquid_glass(dst, 0, 0, src, lp));
        gl.shutdown();
    }

    cairo_surface_destroy(dst);
    cairo_surface_destroy(src);
}

void test_svg_icons() {
    mistbar::icons::IconCache cache;
    const std::vector<std::string> icon_files = {
        "mistbar-logo.svg",
        "mistbar-mark.svg",
        "wifi.svg",
        "wifi-off.svg",
        "bluetooth.svg",
        "bluetooth-connected.svg",
        "bluetooth-off.svg",
        "volume.svg",
        "volume-low.svg",
        "volume-mute.svg",
        "battery.svg",
        "battery-charging.svg",
        "search.svg",
        "control-center.svg",
        "sun.svg",
        "moon.svg",
        "power.svg",
        "lock.svg",
        "mic.svg",
        "play.svg",
        "pause.svg",
        "prev.svg",
        "next.svg",
        "refresh.svg",
        "settings.svg",
        "check.svg",
        "traffic-close.svg",
        "traffic-minimize.svg",
        "traffic-maximize.svg",
        "categories/appearance.svg",
        "categories/liquid-glass.svg",
        "categories/menubar.svg",
        "categories/wifi.svg",
        "categories/bluetooth.svg",
        "categories/sound.svg",
        "categories/display.svg",
        "categories/about.svg"
    };

    for (const auto& name : icon_files) {
        const std::string path = mistbar::icons::resolve_icon(name);
        MISTBAR_CHECK(!path.empty());
        cairo_surface_t* surf = cache.get(path, 24, 24, 0);
        MISTBAR_CHECK(surf != nullptr);
    }
}

} // namespace

int main() {
    test_json_roundtrip();
    test_config_presets();
    test_gaussian_blur_and_liquid_glass();
    test_svg_icons();
    std::cout << "All Mistbar unit tests passed.\n";
    return 0;
}
