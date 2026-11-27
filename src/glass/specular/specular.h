/* Mistbar - MIT License */

#pragma once

#include "themes/theme.h"

#include <cairo.h>

namespace mistbar::glass {

void append_rounded_rect(cairo_t* cr, double x, double y, double w, double h, double radius);

void draw_glass_specular_and_border(cairo_t* cr,
                                    double x,
                                    double y,
                                    double w,
                                    double h,
                                    double radius,
                                    const themes::ThemePalette& palette,
                                    double pointer_norm_x = -1.0);

} // namespace mistbar::glass
