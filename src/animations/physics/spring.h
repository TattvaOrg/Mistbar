/* Mistbar - MIT License */

#pragma once

namespace mistbar::anim {

class Spring {
public:
    Spring(double stiffness = 280.0, double damping = 22.0, double mass = 1.0) noexcept;

    void configure(double stiffness, double damping, double mass = 1.0) noexcept;
    void snap(double value) noexcept;
    void set_target(double target) noexcept;
    void impulse(double dv) noexcept;
    bool step(double dt_sec) noexcept;

    double value() const noexcept { return value_; }
    double target() const noexcept { return target_; }
    double velocity() const noexcept { return velocity_; }
    bool is_settled() const noexcept;

private:
    double stiffness_;
    double damping_;
    double mass_;
    double value_ = 0.0;
    double target_ = 0.0;
    double velocity_ = 0.0;
};

} // namespace mistbar::anim
