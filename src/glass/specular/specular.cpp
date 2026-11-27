/* Mistbar - MIT License */

#include "glass/specular/specular.h"

#include <algorithm>
#include <cmath>

namespace mistbar::glass {

namespace {
constexpr double PI = 3.14159265358979323846;
} // namespace

void append_rounded_rect(cairo_t* cr, double x, double y, double w, double h, double radius) {
    cairo_new_path(cr);
    const double r = std::clamp(radius, 0.0, std::min(w, h) * 0.5);
    if (r <= 0.0) {
        cairo_rectangle(cr, x, y, w, h);
        return;
    }
    cairo_arc(cr, x + w - r, y + r, r, -PI * 0.5, 0.0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0.0, PI * 0.5);
    cairo_arc(cr, x + r, y + h - r, r, PI * 0.5, PI);
    cairo_arc(cr, x + r, y + r, r, PI, PI * 1.5);
    cairo_close_path(cr);
}

void draw_glass_specular_and_border(cairo_t* cr,
                                    double x,
                                    double y,
                                    double w,
                                    double h,
                                    double radius,
                                    const themes::ThemePalette& palette,
                                    double pointer_norm_x) {
    if (w <= 2.0 || h <= 2.0) return;

    const double spec_mul = std::clamp(palette.specular_intensity, 0.0, 1.0);

    cairo_save(cr);
    append_rounded_rect(cr, x, y, w, h, radius);
    cairo_clip(cr);

    // Top specular sheen
    cairo_pattern_t* sheen = cairo_pattern_create_linear(x, y, x, y + std::min(h * 0.48, 22.0));
    cairo_pattern_add_color_stop_rgba(sheen, 0.0, 1.0, 1.0, 1.0,
                                      std::min(0.85, palette.specular_top.a * 0.50 * (0.4 + spec_mul)));
    cairo_pattern_add_color_stop_rgba(sheen, 0.30, 1.0, 1.0, 1.0,
                                      palette.specular_top.a * 0.14 * (0.4 + spec_mul));
    cairo_pattern_add_color_stop_rgba(sheen, 1.0, 1.0, 1.0, 1.0, 0.0);
    cairo_set_source(cr, sheen);
    cairo_paint(cr);
    cairo_pattern_destroy(sheen);

    // Cursor-reactive horizontal spotlight sheen on fallback path
    if (palette.cursor_reactive && pointer_norm_x >= 0.0 && pointer_norm_x <= 1.0) {
        const double lx = x + pointer_norm_x * w;
        cairo_pattern_t* spot = cairo_pattern_create_radial(lx, y + h * 0.5, 4.0,
                                                            lx, y + h * 0.5, std::max(48.0, w * 0.18));
        cairo_pattern_add_color_stop_rgba(spot, 0.0, 1.0, 1.0, 1.0, 0.18 * spec_mul);
        cairo_pattern_add_color_stop_rgba(spot, 1.0, 1.0, 1.0, 1.0, 0.0);
        cairo_set_source(cr, spot);
        cairo_paint(cr);
        cairo_pattern_destroy(spot);
    }

    // Bottom inner shadow
    cairo_pattern_t* inner = cairo_pattern_create_linear(x, y + h - std::min(h * 0.25, 14.0), x, y + h);
    cairo_pattern_add_color_stop_rgba(inner, 0.0, 0.0, 0.0, 0.0, 0.0);
    cairo_pattern_add_color_stop_rgba(inner, 1.0,
                                      palette.inner_shadow.r,
                                      palette.inner_shadow.g,
                                      palette.inner_shadow.b,
                                      palette.inner_shadow.a);
    cairo_set_source(cr, inner);
    cairo_paint(cr);
    cairo_pattern_destroy(inner);

    cairo_restore(cr);

    // Crisp 1px top-lit outer rim
    cairo_save(cr);
    append_rounded_rect(cr, x + 0.5, y + 0.5, w - 1.0, h - 1.0, std::max(0.0, radius - 0.5));
    cairo_pattern_t* rim = cairo_pattern_create_linear(x, y, x, y + h);
    cairo_pattern_add_color_stop_rgba(rim, 0.0, 1.0, 1.0, 1.0,
                                      std::min(1.0, palette.specular_top.a * 1.10));
    cairo_pattern_add_color_stop_rgba(rim, 0.5,
                                      palette.border_outer.r,
                                      palette.border_outer.g,
                                      palette.border_outer.b,
                                      palette.border_outer.a * 0.78);
    cairo_pattern_add_color_stop_rgba(rim, 1.0,
                                      palette.border_outer.r,
                                      palette.border_outer.g,
                                      palette.border_outer.b,
                                      palette.border_outer.a * 0.52);
    cairo_set_source(cr, rim);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
    cairo_pattern_destroy(rim);
    cairo_restore(cr);
}

} // namespace mistbar::glass
