/* Mistbar - MIT License */

#pragma once

namespace mistbar::anim {

class CubicBezier {
public:
    constexpr CubicBezier(double x1, double y1, double x2, double y2) noexcept
        : x1_(x1), y1_(y1), x2_(x2), y2_(y2) {}

    double evaluate(double t) const noexcept;

    static constexpr CubicBezier ease_out() noexcept { return {0.16, 1.0, 0.3, 1.0}; }
    static constexpr CubicBezier ease_in_out() noexcept { return {0.42, 0.0, 0.58, 1.0}; }

private:
    double x1_, y1_, x2_, y2_;

    double sample_x(double u) const noexcept;
    double sample_y(double u) const noexcept;
    double sample_dx(double u) const noexcept;
};

} // namespace mistbar::anim
