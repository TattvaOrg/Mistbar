/* Mistbar - MIT License */

#include "animations/physics/spring.h"

#include <algorithm>
#include <cmath>

namespace mistbar::anim {

Spring::Spring(double stiffness, double damping, double mass) noexcept
    : stiffness_(stiffness), damping_(damping), mass_(std::max(0.01, mass)) {}

void Spring::configure(double stiffness, double damping, double mass) noexcept {
    stiffness_ = stiffness;
    damping_ = damping;
    mass_ = std::max(0.01, mass);
}

void Spring::snap(double value) noexcept {
    value_ = value;
    target_ = value;
    velocity_ = 0.0;
}

void Spring::set_target(double target) noexcept {
    target_ = target;
}

void Spring::impulse(double dv) noexcept {
    velocity_ += dv;
}

bool Spring::is_settled() const noexcept {
    return std::abs(value_ - target_) < 1e-3 && std::abs(velocity_) < 1e-3;
}

bool Spring::step(double dt_sec) noexcept {
    if (is_settled()) {
        value_ = target_;
        velocity_ = 0.0;
        return false;
    }

    const double clamped = std::clamp(dt_sec, 0.0005, 0.05);
    const int substeps = 4;
    const double h = clamped / static_cast<double>(substeps);

    for (int i = 0; i < substeps; ++i) {
        const double force = -stiffness_ * (value_ - target_) - damping_ * velocity_;
        const double accel = force / mass_;
        velocity_ += accel * h;
        value_ += velocity_ * h;
    }

    if (is_settled()) {
        value_ = target_;
        velocity_ = 0.0;
        return false;
    }
    return true;
}

} // namespace mistbar::anim
