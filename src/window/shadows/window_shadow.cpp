/* Mistbar - MIT License */

#include "window/shadows/window_shadow.h"
#include "glass/specular/specular.h"

#include <algorithm>

namespace mistbar::window {

void draw_window_shadow(cairo_t* cr,
                        double x,
                        double y,
                        double w,
                        double h,
                        double corner_radius,
                        int shadow_radius,
                        bool active) {
    if (!cr || shadow_radius <= 0 || w <= 0.0 || h <= 0.0) return;

    const int layers = std::clamp(shadow_radius / 3, 3, 10);
    const double max_alpha = active ? 0.28 : 0.14;
    const double y_offset = active ? 5.0 : 2.0;

    cairo_save(cr);
    for (int i = layers; i >= 1; --i) {
        const double t = static_cast<double>(i) / static_cast<double>(layers);
        const double spread = static_cast<double>(shadow_radius) * t;
        const double alpha = (max_alpha / static_cast<double>(layers)) * (1.15 - 0.85 * t);
        glass::append_rounded_rect(cr,
                                   x - spread * 0.5,
                                   y - spread * 0.35 + y_offset * t,
                                   w + spread,
                                   h + spread,
                                   corner_radius + spread * 0.35);
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, alpha);
        cairo_fill(cr);
    }
    cairo_restore(cr);
}

} // namespace mistbar::window
