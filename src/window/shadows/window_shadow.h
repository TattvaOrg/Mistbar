/* Mistbar - MIT License */

#pragma once

#include <cairo.h>

namespace mistbar::window {

void draw_window_shadow(cairo_t* cr,
                        double x,
                        double y,
                        double w,
                        double h,
                        double corner_radius,
                        int shadow_radius,
                        bool active);

} // namespace mistbar::window
