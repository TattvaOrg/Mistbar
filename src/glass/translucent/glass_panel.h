/* Mistbar - MIT License */

#pragma once

#include "common/config.h"
#include "common/wayland_client.h"
#include "glass/blur/gl_blur.h"
#include "themes/theme.h"

#include <cairo.h>
#include <chrono>
#include <string>

namespace mistbar::glass {

class GlassBackdrop {
public:
    GlassBackdrop() = default;
    ~GlassBackdrop();

    GlassBackdrop(const GlassBackdrop&) = delete;
    GlassBackdrop& operator=(const GlassBackdrop&) = delete;

    void init(const config::MistbarConfig& cfg);
    bool load_wallpaper(const std::string& path, int output_w, int output_h, int blur_radius);
    bool refresh_screencopy(wl::Display& display, int blur_radius, bool force = false);

    cairo_surface_t* raw_wallpaper() const noexcept { return raw_wallpaper_; }
    cairo_surface_t* blurred_surface() const noexcept { return blurred_backdrop_; }
    GlBlurEngine& gl_engine() noexcept { return gl_blur_; }
    const GlBlurEngine& gl_engine() const noexcept { return gl_blur_; }

    bool using_gpu() const noexcept { return gpu_enabled_ && gl_blur_.is_available(); }

private:
    GlBlurEngine gl_blur_;
    bool gpu_enabled_ = true;
    cairo_surface_t* raw_wallpaper_ = nullptr;
    cairo_surface_t* blurred_backdrop_ = nullptr;
    std::string loaded_wallpaper_path_;
    int cached_w_ = 0;
    int cached_h_ = 0;
    int cached_radius_ = -1;
    std::chrono::steady_clock::time_point last_capture_{};

    void blur_from_surface(cairo_surface_t* source, int blur_radius);
};

// Renders a full Liquid Glass surface (GPU GLSL optical shader when available, Cairo fallback otherwise).
void draw_glass_panel(cairo_t* cr,
                      GlassBackdrop* backdrop,
                      double dst_x, double dst_y,
                      double screen_x, double screen_y,
                      double w, double h,
                      double corner_radius,
                      const themes::ThemePalette& palette,
                      const themes::Color* override_fill = nullptr,
                      bool overlay_only = false,
                      double pointer_norm_x = -1.0,
                      double pointer_norm_y = -1.0);

} // namespace mistbar::glass
