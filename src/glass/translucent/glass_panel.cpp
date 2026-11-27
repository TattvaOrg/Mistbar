/* Mistbar - MIT License */

#include "glass/translucent/glass_panel.h"
#include "glass/blur/gaussian_blur.h"
#include "glass/specular/specular.h"
#include "common/logger.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <librsvg/rsvg.h>

namespace mistbar::glass {

namespace {

cairo_surface_t* load_scaled_image(const std::string& path, int w, int h) {
    if (path.empty() || w <= 0 || h <= 0) return nullptr;
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t* cr = cairo_create(surf);

    const bool is_svg = path.size() >= 4 &&
                        (path.rfind(".svg") == path.size() - 4 || path.rfind(".SVG") == path.size() - 4);
    bool ok = false;

    if (is_svg) {
        GError* err = nullptr;
        RsvgHandle* handle = rsvg_handle_new_from_file(path.c_str(), &err);
        if (handle) {
            RsvgRectangle vp{0.0, 0.0, static_cast<double>(w), static_cast<double>(h)};
            ok = rsvg_handle_render_document(handle, cr, &vp, &err) == TRUE;
            g_object_unref(handle);
        }
        if (err) {
            log::warn("glass", std::string("Wallpaper SVG error: ") + err->message);
            g_error_free(err);
        }
    } else {
        GError* err = nullptr;
        GdkPixbuf* pb = gdk_pixbuf_new_from_file_at_scale(path.c_str(), w, h, FALSE, &err);
        if (pb) {
            const int pw = gdk_pixbuf_get_width(pb);
            const int ph = gdk_pixbuf_get_height(pb);
            const int nc = gdk_pixbuf_get_n_channels(pb);
            const int ps = gdk_pixbuf_get_rowstride(pb);
            const guchar* px = gdk_pixbuf_get_pixels(pb);
            cairo_surface_flush(surf);
            auto* dst = cairo_image_surface_get_data(surf);
            const int ds = cairo_image_surface_get_stride(surf);
            for (int y = 0; y < std::min(h, ph); ++y) {
                const guchar* sr = px + y * ps;
                auto* dr = reinterpret_cast<std::uint32_t*>(dst + y * ds);
                for (int x = 0; x < std::min(w, pw); ++x) {
                    const std::uint32_t r = sr[x * nc + 0];
                    const std::uint32_t g = sr[x * nc + 1];
                    const std::uint32_t b = sr[x * nc + 2];
                    dr[x] = (0xFFU << 24) | (r << 16) | (g << 8) | b;
                }
            }
            cairo_surface_mark_dirty(surf);
            g_object_unref(pb);
            ok = true;
        } else if (err) {
            log::warn("glass", std::string("Wallpaper load error: ") + err->message);
            g_error_free(err);
        }
    }

    cairo_destroy(cr);
    if (!ok) {
        cairo_surface_destroy(surf);
        return nullptr;
    }
    return surf;
}

// Pure black monochrome geometric backdrop when no wallpaper is active (zero random images/colors).
cairo_surface_t* create_tahoe_backdrop(int w, int h) {
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t* cr = cairo_create(surf);

    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    cairo_paint(cr);

    // Subtle monochrome architectural lines so refraction & edge caustics remain legible on black
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.04);
    cairo_set_line_width(cr, 1.0);
    const double step = 36.0;
    for (double x = 0.0; x < static_cast<double>(w); x += step) {
        cairo_move_to(cr, x, 0.0);
        cairo_line_to(cr, x, static_cast<double>(h));
    }
    for (double y = 0.0; y < static_cast<double>(h); y += step) {
        cairo_move_to(cr, 0.0, y);
        cairo_line_to(cr, static_cast<double>(w), y);
    }
    cairo_stroke(cr);

    cairo_pattern_t* sheen = cairo_pattern_create_radial(w * 0.5, h * 0.35, 8.0,
                                                          w * 0.5, h * 0.35, std::max(w, h) * 0.45);
    cairo_pattern_add_color_stop_rgba(sheen, 0.0, 0.18, 0.18, 0.20, 0.32);
    cairo_pattern_add_color_stop_rgba(sheen, 1.0, 0.00, 0.00, 0.00, 0.0);
    cairo_set_source(cr, sheen);
    cairo_paint(cr);
    cairo_pattern_destroy(sheen);

    cairo_destroy(cr);
    return surf;
}

} // namespace

GlassBackdrop::~GlassBackdrop() {
    if (raw_wallpaper_) {
        cairo_surface_destroy(raw_wallpaper_);
        raw_wallpaper_ = nullptr;
    }
    if (blurred_backdrop_) {
        cairo_surface_destroy(blurred_backdrop_);
        blurred_backdrop_ = nullptr;
    }
    gl_blur_.shutdown();
}

void GlassBackdrop::init(const config::MistbarConfig& cfg) {
    gpu_enabled_ = cfg.gpu_acceleration;
    if (gpu_enabled_ && !gl_blur_.is_available()) {
        if (!gl_blur_.init()) {
            log::info("glass", "GPU Liquid Glass unavailable; using CPU Gaussian fallback");
        }
    }
}

void GlassBackdrop::blur_from_surface(cairo_surface_t* source, int blur_radius) {
    if (!source) return;
    const int w = cairo_image_surface_get_width(source);
    const int h = cairo_image_surface_get_height(source);
    if (w <= 0 || h <= 0) return;

    if (!blurred_backdrop_ ||
        cairo_image_surface_get_width(blurred_backdrop_) != w ||
        cairo_image_surface_get_height(blurred_backdrop_) != h) {
        if (blurred_backdrop_) cairo_surface_destroy(blurred_backdrop_);
        blurred_backdrop_ = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    }

    if (gpu_enabled_ && gl_blur_.is_available()) {
        if (gl_blur_.blur(source, blurred_backdrop_, blur_radius)) {
            return;
        }
    }
    (void)cpu_gaussian_blur(source, blurred_backdrop_, blur_radius);
}

bool GlassBackdrop::load_wallpaper(const std::string& path, int output_w, int output_h, int blur_radius) {
    const int w = std::max(64, output_w);
    const int h = std::max(64, output_h);
    if (path == loaded_wallpaper_path_ && cached_w_ == w && cached_h_ == h &&
        cached_radius_ == blur_radius && blurred_backdrop_ != nullptr) {
        return true;
    }

    if (raw_wallpaper_) {
        cairo_surface_destroy(raw_wallpaper_);
        raw_wallpaper_ = nullptr;
    }
    raw_wallpaper_ = load_scaled_image(path, w, h);
    if (!raw_wallpaper_) {
        raw_wallpaper_ = create_tahoe_backdrop(w, h);
    }

    loaded_wallpaper_path_ = path;
    cached_w_ = w;
    cached_h_ = h;
    cached_radius_ = blur_radius;
    blur_from_surface(raw_wallpaper_, blur_radius);
    return blurred_backdrop_ != nullptr;
}

bool GlassBackdrop::refresh_screencopy(wl::Display& display, int blur_radius, bool force) {
    if (!display.has_screencopy()) return false;
    const auto now = std::chrono::steady_clock::now();
    if (!force && last_capture_.time_since_epoch().count() != 0) {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_capture_).count();
        if (ms < 500) return false;
    }
    last_capture_ = now;

    wl::CapturedFrame frame;
    if (!display.capture_output(nullptr, frame, false) || frame.pixels.empty()) {
        return false;
    }

    cairo_surface_t* cap = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, frame.width, frame.height);
    cairo_surface_flush(cap);
    std::uint8_t* dst = cairo_image_surface_get_data(cap);
    const int dst_stride = cairo_image_surface_get_stride(cap);
    const int copy_bytes = std::min(dst_stride, frame.stride);
    for (int y = 0; y < frame.height; ++y) {
        std::memcpy(dst + y * dst_stride, frame.pixels.data() + y * frame.stride,
                    static_cast<std::size_t>(copy_bytes));
    }
    cairo_surface_mark_dirty(cap);

    cached_w_ = frame.width;
    cached_h_ = frame.height;
    cached_radius_ = blur_radius;
    blur_from_surface(cap, blur_radius);
    cairo_surface_destroy(cap);
    return true;
}

void draw_glass_panel(cairo_t* cr,
                      GlassBackdrop* backdrop,
                      double dst_x, double dst_y,
                      double screen_x, double screen_y,
                      double w, double h,
                      double corner_radius,
                      const themes::ThemePalette& palette,
                      const themes::Color* override_fill,
                      bool overlay_only,
                      double pointer_norm_x,
                      double pointer_norm_y) {
    if (!cr || w <= 0.0 || h <= 0.0) return;

    const themes::Color fill = override_fill ? *override_fill : palette.panel_bg;

    if (!palette.solid_mode && backdrop && backdrop->using_gpu()) {
        const int pw = std::max(1, static_cast<int>(std::ceil(w)));
        const int ph = std::max(1, static_cast<int>(std::ceil(h)));
        cairo_surface_t* panel_surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pw, ph);
        LiquidGlassParams lp;
        lp.corner_radius        = corner_radius;
        lp.refraction_strength  = palette.refraction_strength;
        lp.bezel_thickness      = palette.bezel_thickness;
        lp.chromatic_aberration = palette.chromatic_aberration;
        lp.specular_intensity   = palette.specular_intensity;
        lp.tint_r               = fill.r;
        lp.tint_g               = fill.g;
        lp.tint_b               = fill.b;
        lp.tint_opacity         = fill.a;
        lp.glass_opacity        = palette.glass_opacity;
        lp.pointer_x            = pointer_norm_x;
        lp.pointer_y            = pointer_norm_y;
        lp.cursor_reactive      = palette.cursor_reactive;
        lp.dark_mode            = palette.dark_mode;
        lp.overlay_only         = overlay_only;

        if (backdrop->gl_engine().render_liquid_glass(
                backdrop->blurred_surface(),
                static_cast<int>(screen_x),
                static_cast<int>(screen_y),
                panel_surf,
                lp)) {
            cairo_save(cr);
            cairo_set_source_surface(cr, panel_surf, dst_x, dst_y);
            cairo_paint(cr);
            cairo_restore(cr);
            cairo_surface_destroy(panel_surf);
            return;
        }
        cairo_surface_destroy(panel_surf);
    }

    // CPU Cairo fallback (also used cleanly for Solid preset)
    cairo_save(cr);
    append_rounded_rect(cr, dst_x, dst_y, w, h, corner_radius);
    cairo_clip(cr);

    if (!palette.solid_mode && !overlay_only && backdrop && backdrop->blurred_surface()) {
        cairo_set_source_surface(cr, backdrop->blurred_surface(), dst_x - screen_x, dst_y - screen_y);
        cairo_paint_with_alpha(cr, palette.glass_opacity);
    }

    cairo_set_source_rgba(cr, fill.r, fill.g, fill.b, fill.a);
    cairo_paint(cr);

    if (!palette.solid_mode) {
        cairo_set_source_rgba(cr, palette.glass_tint.r, palette.glass_tint.g,
                              palette.glass_tint.b, palette.glass_tint.a * 0.5);
        cairo_paint(cr);
    }
    cairo_restore(cr);

    draw_glass_specular_and_border(cr, dst_x, dst_y, w, h, corner_radius, palette, pointer_norm_x);
}

} // namespace mistbar::glass
