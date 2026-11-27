/* Mistbar - MIT License */

#pragma once

#include "animations/bezier/bezier.h"

#include <chrono>

namespace mistbar::anim {

class ValueTransition {
public:
    explicit ValueTransition(double initial = 0.0,
                             double duration_sec = 0.22,
                             CubicBezier curve = CubicBezier::ease_out()) noexcept;

    void snap(double value) noexcept;
    void animate_to(double target, double duration_sec = -1.0) noexcept;
    bool tick() noexcept;
    bool advance(double dt_sec) noexcept;

    double value() const noexcept { return current_; }
    double target() const noexcept { return target_; }
    bool active() const noexcept { return active_; }

private:
    double start_ = 0.0;
    double current_ = 0.0;
    double target_ = 0.0;
    double elapsed_ = 0.0;
    double duration_ = 0.22;
    CubicBezier curve_;
    bool active_ = false;
    std::chrono::steady_clock::time_point last_tick_;
    bool has_tick_ = false;
};

} // namespace mistbar::anim
