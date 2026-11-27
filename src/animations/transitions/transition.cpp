/* Mistbar - MIT License */

#include "animations/transitions/transition.h"

#include <algorithm>
#include <cmath>

namespace mistbar::anim {

ValueTransition::ValueTransition(double initial, double duration_sec, CubicBezier curve) noexcept
    : start_(initial), current_(initial), target_(initial),
      duration_(std::max(0.01, duration_sec)), curve_(curve) {}

void ValueTransition::snap(double value) noexcept {
    start_ = value;
    current_ = value;
    target_ = value;
    elapsed_ = 0.0;
    active_ = false;
    has_tick_ = false;
}

void ValueTransition::animate_to(double target, double duration_sec) noexcept {
    if (duration_sec > 0.0) {
        duration_ = std::max(0.01, duration_sec);
    }
    if (std::abs(target - current_) < 1e-4) {
        snap(target);
        return;
    }
    start_ = current_;
    target_ = target;
    elapsed_ = 0.0;
    active_ = true;
    last_tick_ = std::chrono::steady_clock::now();
    has_tick_ = true;
}

bool ValueTransition::tick() noexcept {
    if (!active_) return false;
    const auto now = std::chrono::steady_clock::now();
    double dt = 0.016;
    if (has_tick_) {
        dt = std::chrono::duration<double>(now - last_tick_).count();
    }
    last_tick_ = now;
    has_tick_ = true;
    return advance(dt);
}

bool ValueTransition::advance(double dt_sec) noexcept {
    if (!active_) return false;
    elapsed_ += std::max(0.0, dt_sec);
    const double t = std::clamp(elapsed_ / duration_, 0.0, 1.0);
    const double eased = curve_.evaluate(t);
    current_ = start_ + (target_ - start_) * eased;
    if (t >= 1.0) {
        current_ = target_;
        active_ = false;
    }
    return active_;
}

} // namespace mistbar::anim
