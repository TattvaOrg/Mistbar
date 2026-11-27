/* Mistbar - MIT License */

#include "window/titlebar/window_frame.h"
#include "common/defaults.h"
#include "glass/specular/specular.h"
#include "window/shadows/window_shadow.h"

#include <algorithm>
#include <linux/input-event-codes.h>
#include <pango/pangocairo.h>

namespace mistbar::window {

namespace {

PangoLayout* create_text_layout(cairo_t* cr,
                                const std::string& text,
                                double max_w,
                                double size_pt,
                                bool bold,
                                bool center_align,
                                bool right_align) {
    PangoLayout* layout = pango_cairo_create_layout(cr);
    PangoFontDescription* desc =
        pango_font_description_from_string("SF Pro Text, Inter, Adwaita Sans, Cantarell, Noto Sans");
    pango_font_description_set_size(desc, static_cast<int>(size_pt * PANGO_SCALE));
    pango_font_description_set_weight(desc, bold ? PANGO_WEIGHT_SEMIBOLD : PANGO_WEIGHT_NORMAL);
    pango_layout_set_font_description(layout, desc);
    pango_layout_set_text(layout, text.c_str(), static_cast<int>(text.size()));
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    if (max_w > 0.0) {
        pango_layout_set_width(layout, static_cast<int>(max_w * PANGO_SCALE));
    }
    if (center_align) pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
    else if (right_align) pango_layout_set_alignment(layout, PANGO_ALIGN_RIGHT);
    else pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);
    pango_font_description_free(desc);
    return layout;
}

} // namespace

void draw_pango_text(cairo_t* cr,
                     const std::string& text,
                     double x,
                     double y,
                     double max_w,
                     double size_pt,
                     bool bold,
                     const themes::Color& color,
                     bool center_align,
                     bool right_align) {
    if (!cr || text.empty()) return;
    PangoLayout* layout = create_text_layout(cr, text, max_w, size_pt, bold, center_align, right_align);
    cairo_save(cr);
    cairo_move_to(cr, x, y);
    cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);
    pango_cairo_show_layout(cr, layout);
    cairo_new_path(cr);
    cairo_restore(cr);
    g_object_unref(layout);
}

void draw_pango_text_centered(cairo_t* cr,
                              const std::string& text,
                              double x,
                              double y,
                              double max_w,
                              double height,
                              double size_pt,
                              bool bold,
                              const themes::Color& color,
                              bool center_align,
                              bool right_align) {
    if (!cr || text.empty()) return;
    PangoLayout* layout = create_text_layout(cr, text, max_w, size_pt, bold, center_align, right_align);
    int pw = 0;
    int ph = 0;
    pango_layout_get_pixel_size(layout, &pw, &ph);
    const double draw_y = y + (height - static_cast<double>(ph)) * 0.5;

    cairo_save(cr);
    cairo_move_to(cr, x, draw_y);
    cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);
    pango_cairo_show_layout(cr, layout);
    cairo_new_path(cr);
    cairo_restore(cr);
    g_object_unref(layout);
}

double measure_pango_text_width(cairo_t* cr, const std::string& text, double size_pt, bool bold) {
    if (!cr || text.empty()) return 0.0;
    PangoLayout* layout = create_text_layout(cr, text, -1.0, size_pt, bold, false, false);
    int pw = 0;
    int ph = 0;
    pango_layout_get_pixel_size(layout, &pw, &ph);
    g_object_unref(layout);
    return static_cast<double>(pw);
}

MistbarWindow::MistbarWindow(wl::Display& display)
    : display_(display), surface_(display) {}

bool MistbarWindow::create(const std::string& title,
                           const std::string& app_id,
                           int content_width,
                           int content_height,
                           const config::MistbarConfig& cfg,
                           int shadow_margin,
                           bool headless_titlebar) {
    title_ = title;
    shadow_margin_ = std::max(0, shadow_margin);
    headless_titlebar_ = headless_titlebar;
    apply_config(cfg);

    surface_.set_draw_callback([this](cairo_t* cr, int w, int h) { handle_draw(cr, w, h); });
    surface_.set_pointer_callback([this](const wl::PointerEvent& ev) { handle_pointer(ev); });
    surface_.set_key_callback([this](const wl::KeyEvent& ev) { handle_key(ev); });
    surface_.set_close_callback([this]() { close(); });

    const int tb = headless_titlebar_ ? 0 : defaults::TITLEBAR_HEIGHT;
    const int total_w = content_width + shadow_margin_ * 2;
    const int total_h = content_height + tb + shadow_margin_ * 2;
    open_ = surface_.create(title, app_id, total_w, total_h);
    return open_;
}

void MistbarWindow::apply_config(const config::MistbarConfig& cfg) {
    palette_ = themes::resolve_palette(cfg);
    backdrop_.init(cfg);
    const auto out = display_.primary_output();
    backdrop_.load_wallpaper(palette_.wallpaper_path, out.width, out.height, palette_.blur_radius);
    if (open_) {
        surface_.request_redraw();
    }
}

void MistbarWindow::set_title(const std::string& title) {
    title_ = title;
    surface_.set_title(title);
    request_redraw();
}

void MistbarWindow::request_redraw() {
    if (open_) surface_.request_redraw();
}

void MistbarWindow::close() {
    if (!open_) return;
    open_ = false;
    surface_.set_visible(false);
    if (on_close_) on_close_();
}

void MistbarWindow::handle_draw(cairo_t* cr, int total_w, int total_h) {
    const double m = surface_.is_maximized() ? 0.0 : static_cast<double>(shadow_margin_);
    const double wx = m;
    const double wy = m;
    const double ww = std::max(1.0, static_cast<double>(total_w) - 2.0 * m);
    const double wh = std::max(1.0, static_cast<double>(total_h) - 2.0 * m);
    const double r = surface_.is_maximized() ? 0.0 : static_cast<double>(palette_.window_radius);
    const bool active = surface_.is_activated();

    if (m > 0.0) {
        draw_window_shadow(cr, wx, wy, ww, wh, r, defaults::WINDOW_SHADOW_RADIUS, active);
    }

    cairo_save(cr);
    glass::append_rounded_rect(cr, wx, wy, ww, wh, r);
    cairo_clip(cr);

    cairo_set_source_rgba(cr, palette_.surface_bg.r, palette_.surface_bg.g,
                          palette_.surface_bg.b, palette_.surface_bg.a);
    cairo_paint(cr);

    if (headless_titlebar_) {
        const int content_w = std::max(1, static_cast<int>(ww));
        const int content_h = std::max(1, static_cast<int>(wh));
        if (on_content_draw_) {
            cairo_save(cr);
            cairo_rectangle(cr, wx, wy, static_cast<double>(content_w), static_cast<double>(content_h));
            cairo_clip(cr);
            cairo_translate(cr, wx, wy);
            on_content_draw_(cr, content_w, content_h);
            cairo_restore(cr);
        }
        // Traffic lights integrated into top-left of floating sidebar
        traffic_lights_.draw(cr, icon_cache_, wx + 22.0, wy + 22.0, active);
    } else {
        const double tb_h = static_cast<double>(defaults::TITLEBAR_HEIGHT);
        const themes::Color tb_fill = active ? palette_.titlebar_active : palette_.titlebar_inactive;
        glass::draw_glass_panel(cr, &backdrop_, wx, wy, wx, wy, ww, tb_h, 0.0, palette_, &tb_fill);

        cairo_rectangle(cr, wx, wy + tb_h - 1.0, ww, 1.0);
        cairo_set_source_rgba(cr, palette_.border_outer.r, palette_.border_outer.g,
                              palette_.border_outer.b, palette_.border_outer.a * 0.45);
        cairo_fill(cr);

        const double tl_x = wx + 14.0;
        const double tl_y = wy + (tb_h - static_cast<double>(defaults::TRAFFIC_LIGHT_SIZE)) * 0.5;
        traffic_lights_.draw(cr, icon_cache_, tl_x, tl_y, active);

        themes::Color title_col = palette_.text_primary;
        if (!active) title_col.a *= 0.55;
        const double title_pad = 84.0;
        if (ww > title_pad * 2.0) {
            draw_pango_text_centered(cr, title_, wx + title_pad, wy, ww - title_pad * 2.0, tb_h,
                                     10.0, true, title_col, true, false);
        }

        const int content_w = std::max(1, static_cast<int>(ww));
        const int content_h = std::max(1, static_cast<int>(wh - tb_h));
        if (on_content_draw_) {
            cairo_save(cr);
            cairo_rectangle(cr, wx, wy + tb_h, static_cast<double>(content_w), static_cast<double>(content_h));
            cairo_clip(cr);
            cairo_translate(cr, wx, wy + tb_h);
            on_content_draw_(cr, content_w, content_h);
            cairo_restore(cr);
        }
    }

    cairo_restore(cr);

    glass::draw_glass_specular_and_border(cr, wx, wy, ww, wh, r, palette_);
}

void MistbarWindow::handle_pointer(const wl::PointerEvent& ev) {
    const double m = surface_.is_maximized() ? 0.0 : static_cast<double>(shadow_margin_);
    const double tb_h = headless_titlebar_ ? 46.0 : static_cast<double>(defaults::TITLEBAR_HEIGHT);
    const double tl_x = headless_titlebar_ ? (m + 22.0) : (m + 14.0);
    const double tl_y = headless_titlebar_
                            ? (m + 22.0)
                            : (m + (tb_h - static_cast<double>(defaults::TRAFFIC_LIGHT_SIZE)) * 0.5);

    if (ev.type == wl::PointerEvent::Type::Leave) {
        if (traffic_lights_.clear_hover()) request_redraw();
        if (on_content_pointer_) on_content_pointer_(ev);
        return;
    }

    if (ev.type == wl::PointerEvent::Type::Motion || ev.type == wl::PointerEvent::Type::Enter) {
        if (traffic_lights_.update_hover(ev.x, ev.y, tl_x, tl_y)) {
            request_redraw();
        }
    }

    const bool in_drag_strip = (ev.y >= m && ev.y < m + tb_h &&
                                ev.x >= m && ev.x < static_cast<double>(surface_.width()) - m);

    if (in_drag_strip && ev.type == wl::PointerEvent::Type::ButtonPress && ev.button == BTN_LEFT) {
        const auto act = traffic_lights_.hit_test(ev.x, ev.y, tl_x, tl_y);
        if (act == TrafficAction::Close) {
            close();
            return;
        }
        if (act == TrafficAction::Minimize) {
            surface_.minimize();
            return;
        }
        if (act == TrafficAction::Maximize) {
            surface_.toggle_maximize();
            return;
        }

        const bool in_nav_pills = headless_titlebar_ && (ev.x >= m + 230.0 && ev.x <= m + 336.0);
        if ((!headless_titlebar_ || ev.x > m + 230.0) && !in_nav_pills) {
            const auto now = std::chrono::steady_clock::now();
            if (last_click_time_.time_since_epoch().count() != 0 &&
                std::chrono::duration_cast<std::chrono::milliseconds>(now - last_click_time_).count() < 350) {
                surface_.toggle_maximize();
                last_click_time_ = {};
            } else {
                last_click_time_ = now;
                surface_.start_move(ev.serial);
            }
            return;
        }
    }

    if (on_content_pointer_) {
        const double y_base = headless_titlebar_ ? m : (m + tb_h);
        if (ev.y >= y_base) {
            wl::PointerEvent local = ev;
            local.x -= m;
            local.y -= y_base;
            on_content_pointer_(local);
        }
    }
}

void MistbarWindow::handle_key(const wl::KeyEvent& ev) {
    if (on_content_key_) on_content_key_(ev);
}

} // namespace mistbar::window
