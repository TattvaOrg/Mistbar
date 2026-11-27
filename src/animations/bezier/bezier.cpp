/* Mistbar - MIT License */

#include "animations/bezier/bezier.h"

#include <cmath>

namespace mistbar::anim {

double CubicBezier::sample_x(double u) const noexcept {
    const double inv = 1.0 - u;
    return 3.0 * inv * inv * u * x1_ + 3.0 * inv * u * u * x2_ + u * u * u;
}

double CubicBezier::sample_y(double u) const noexcept {
    const double inv = 1.0 - u;
    return 3.0 * inv * inv * u * y1_ + 3.0 * inv * u * u * y2_ + u * u * u;
}

double CubicBezier::sample_dx(double u) const noexcept {
    const double inv = 1.0 - u;
    return 3.0 * inv * inv * x1_ + 6.0 * inv * u * (x2_ - x1_) + 3.0 * u * u * (1.0 - x2_);
}

double CubicBezier::evaluate(double t) const noexcept {
    if (t <= 0.0) return 0.0;
    if (t >= 1.0) return 1.0;

    double lo = 0.0;
    double hi = 1.0;
    double u = t;

    for (int i = 0; i < 8; ++i) {
        const double x = sample_x(u) - t;
        if (std::abs(x) < 1e-6) {
            return sample_y(u);
        }
        if (x < 0.0) lo = u;
        else hi = u;

        const double dx = sample_dx(u);
        if (std::abs(dx) < 1e-6) break;
        const double next = u - x / dx;
        u = (next > lo && next < hi) ? next : 0.5 * (lo + hi);
    }

    for (int i = 0; i < 16; ++i) {
        const double x = sample_x(u) - t;
        if (std::abs(x) < 1e-6) break;
        if (x < 0.0) lo = u;
        else hi = u;
        u = 0.5 * (lo + hi);
    }
    return sample_y(u);
}

} // namespace mistbar::anim
