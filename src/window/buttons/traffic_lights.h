/* Mistbar - MIT License */

#pragma once

#include "common/svg_loader.h"

#include <cairo.h>
#include <string>

namespace mistbar::window {

enum class TrafficAction { None, Close, Minimize, Maximize };

class TrafficLights {
public:
    TrafficAction hit_test(double px, double py, double origin_x, double origin_y) const noexcept;
    bool update_hover(double px, double py, double origin_x, double origin_y) noexcept;
    bool clear_hover() noexcept;

    void draw(cairo_t* cr,
              icons::IconCache& icon_cache,
              double origin_x,
              double origin_y,
              bool window_active,
              bool dim_min_max = false) const;

    double cluster_width() const noexcept;

private:
    bool cluster_hovered_ = false;
    TrafficAction hovered_btn_ = TrafficAction::None;
};

} // namespace mistbar::window
