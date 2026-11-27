/* Mistbar - MIT License */

#pragma once

#include <cairo.h>
#include <vector>

namespace mistbar::glass {

std::vector<float> build_gaussian_kernel_1d(int radius, double sigma);
bool cpu_gaussian_blur(cairo_surface_t* src, cairo_surface_t* dst, int radius);

} // namespace mistbar::glass
