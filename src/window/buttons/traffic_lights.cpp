/* Mistbar - MIT License */

#include "window/buttons/traffic_lights.h"
#include "common/defaults.h"

namespace mistbar::window {

namespace {

constexpr double PI = 3.14159265358979323846;

void set_argb(cairo_t* cr, std::uint32_t argb, double alpha_mul = 1.0) {
    const double a = (((argb >> 24) & 0xFFU) / 255.0) * alpha_mul;
    const double r = ((argb >> 16) & 0xFFU) / 255.0;
    const double g = ((argb >> 8) & 0xFFU) / 255.0;
    const double b = (argb & 0xFFU) / 255.0;
    cairo_set_source_rgba(cr, r, g, b, a);
}

} // namespace

double TrafficLights::cluster_width() const noexcept {
    return static_cast<double>(defaults::TRAFFIC_LIGHT_SIZE * 3 + defaults::TRAFFIC_LIGHT_GAP * 2);
}

TrafficAction TrafficLights::hit_test(double px, double py, double origin_x, double origin_y) const noexcept {
    const double r = defaults::TRAFFIC_LIGHT_SIZE * 0.5;
    const double step = defaults::TRAFFIC_LIGHT_SIZE + defaults::TRAFFIC_LIGHT_GAP;
    const double cy = origin_y + r;

    const TrafficAction actions[3] = {
        TrafficAction::Close, TrafficAction::Minimize, TrafficAction::Maximize
    };
    for (int i = 0; i < 3; ++i) {
        const double cx = origin_x + r + static_cast<double>(i) * step;
        const double dx = px - cx;
        const double dy = py - cy;
        if (dx * dx + dy * dy <= (r + 2.0) * (r + 2.0)) {
            return actions[i];
        }
    }
    return TrafficAction::None;
}

bool TrafficLights::update_hover(double px, double py, double origin_x, double origin_y) noexcept {
    const double pad = 4.0;
    const bool in_cluster =
        (px >= origin_x - pad && px <= origin_x + cluster_width() + pad &&
         py >= origin_y - pad && py <= origin_y + defaults::TRAFFIC_LIGHT_SIZE + pad);
    const TrafficAction btn = in_cluster ? hit_test(px, py, origin_x, origin_y) : TrafficAction::None;
    const bool changed = (in_cluster != cluster_hovered_) || (btn != hovered_btn_);
    cluster_hovered_ = in_cluster;
    hovered_btn_ = btn;
    return changed;
}

bool TrafficLights::clear_hover() noexcept {
    const bool changed = cluster_hovered_ || (hovered_btn_ != TrafficAction::None);
    cluster_hovered_ = false;
    hovered_btn_ = TrafficAction::None;
    return changed;
}

void TrafficLights::draw(cairo_t* cr,
                         icons::IconCache& icon_cache,
                         double origin_x,
                         double origin_y,
                         bool window_active,
                         bool dim_min_max) const {
    const double d = static_cast<double>(defaults::TRAFFIC_LIGHT_SIZE);
    const double r = d * 0.5;
    const double step = d + static_cast<double>(defaults::TRAFFIC_LIGHT_GAP);

    const std::uint32_t colors[3] = {
        defaults::COLOR_CLOSE,
        defaults::COLOR_MINIMIZE,
        defaults::COLOR_MAXIMIZE
    };
    const char* svg_names[3] = {
        "traffic-close.svg",
        "traffic-minimize.svg",
        "traffic-maximize.svg"
    };

    for (int i = 0; i < 3; ++i) {
        const double cx = origin_x + r + static_cast<double>(i) * step;
        const double cy = origin_y + r;

        const bool dimmed_slot = dim_min_max && (i > 0) && !cluster_hovered_;
        const bool show_color = !dimmed_slot && (window_active || cluster_hovered_);
        const std::uint32_t fill = show_color ? colors[i] : defaults::COLOR_INACTIVE;

        cairo_save(cr);
        cairo_new_path(cr);
        cairo_arc(cr, cx, cy, r, 0.0, 2.0 * PI);
        set_argb(cr, fill);
        cairo_fill_preserve(cr);

        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, show_color ? 0.18 : 0.08);
        cairo_set_line_width(cr, 0.75);
        cairo_stroke(cr);
        cairo_restore(cr);

        if (cluster_hovered_) {
            const std::string path = icons::resolve_icon(svg_names[i]);
            if (!path.empty()) {
                icon_cache.draw(cr, path, cx - 5.0, cy - 5.0, 10.0, 10.0, 0);
            }
        }
    }
}

} // namespace mistbar::window
