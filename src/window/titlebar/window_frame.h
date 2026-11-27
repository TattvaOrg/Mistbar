/* Mistbar - MIT License */

#pragma once

#include "common/config.h"
#include "common/svg_loader.h"
#include "common/wayland_client.h"
#include "glass/translucent/glass_panel.h"
#include "themes/theme.h"
#include "window/buttons/traffic_lights.h"

#include <cairo.h>
#include <chrono>
#include <functional>
#include <string>

namespace mistbar::window {

void draw_pango_text(cairo_t* cr,
                     const std::string& text,
                     double x,
                     double y,
                     double max_w,
                     double size_pt,
                     bool bold,
                     const themes::Color& color,
                     bool center_align = false,
                     bool right_align = false);

// Vertically centers Pango text inside [y, y + height] using actual layout metrics.
void draw_pango_text_centered(cairo_t* cr,
                              const std::string& text,
                              double x,
                              double y,
                              double max_w,
                              double height,
                              double size_pt,
                              bool bold,
                              const themes::Color& color,
                              bool center_align = false,
                              bool right_align = false);

double measure_pango_text_width(cairo_t* cr, const std::string& text, double size_pt, bool bold);

class MistbarWindow {
public:
    using ContentDrawFn    = std::function<void(cairo_t* cr, int w, int h)>;
    using ContentPointerFn = std::function<void(const wl::PointerEvent& ev)>;
    using ContentKeyFn     = std::function<void(const wl::KeyEvent& ev)>;
    using CloseFn          = std::function<void()>;

    explicit MistbarWindow(wl::Display& display);
    ~MistbarWindow() = default;

    MistbarWindow(const MistbarWindow&) = delete;
    MistbarWindow& operator=(const MistbarWindow&) = delete;

    bool create(const std::string& title,
                const std::string& app_id,
                int content_width,
                int content_height,
                const config::MistbarConfig& cfg,
                int shadow_margin = 16,
                bool headless_titlebar = false);

    void apply_config(const config::MistbarConfig& cfg);

    void set_content_draw(ContentDrawFn fn) { on_content_draw_ = std::move(fn); }
    void set_content_pointer(ContentPointerFn fn) { on_content_pointer_ = std::move(fn); }
    void set_content_key(ContentKeyFn fn) { on_content_key_ = std::move(fn); }
    void set_close_callback(CloseFn fn) { on_close_ = std::move(fn); }

    void set_title(const std::string& title);
    void request_redraw();
    bool is_open() const noexcept { return open_; }
    void close();

    const themes::ThemePalette& palette() const noexcept { return palette_; }
    icons::IconCache& icon_cache() noexcept { return icon_cache_; }
    glass::GlassBackdrop& backdrop() noexcept { return backdrop_; }
    wl::ToplevelSurface& surface() noexcept { return surface_; }

private:
    wl::Display& display_;
    wl::ToplevelSurface surface_;
    themes::ThemePalette palette_;
    glass::GlassBackdrop backdrop_;
    icons::IconCache icon_cache_;
    TrafficLights traffic_lights_;

    std::string title_;
    int shadow_margin_ = 16;
    bool headless_titlebar_ = false;
    bool open_ = false;
    std::chrono::steady_clock::time_point last_click_time_{};

    ContentDrawFn on_content_draw_;
    ContentPointerFn on_content_pointer_;
    ContentKeyFn on_content_key_;
    CloseFn on_close_;

    void handle_draw(cairo_t* cr, int total_w, int total_h);
    void handle_pointer(const wl::PointerEvent& ev);
    void handle_key(const wl::KeyEvent& ev);
};

} // namespace mistbar::window
