/* Mistbar - MIT License */

#pragma once

#include <cairo.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct wl_display;
struct wl_surface;

namespace mistbar::glass {

struct GlassMaterial {
    float refraction  = 1.10f;
    float dispersion  = 1.15f;
    float frost       = 0.26f;
    float specular    = 0.95f;
    float tintR       = 0.92f;
    float tintG       = 0.96f;
    float tintB       = 1.00f;
    float tintAlpha   = 0.08f;
    float shadowAlpha = 0.14f;
    float blurScale   = 1.35f;
};

struct GlassPanelSpec {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    float radius = 20.0f;
    bool isPill = false;
    bool overrideMaterial = false;
    GlassMaterial customMaterial{};
};

struct ClickPulse {
    float x = 0.0f;
    float y = 0.0f;
    float age_sec = 0.0f;
    bool active = false;
};

// Shared OpenGL/Wayland-EGL renderer implementing mizzle's 5-level Dual-Kawase blur pyramid,
// SDF meniscus liquid_glass.frag shader, and crisp Cairo UI overlay compositing.
class MizzleRenderer {
public:
    MizzleRenderer();
    ~MizzleRenderer();

    MizzleRenderer(const MizzleRenderer&) = delete;
    MizzleRenderer& operator=(const MizzleRenderer&) = delete;

    // Initialize with an active wl_display (enables zero-copy wayland-egl window surfaces)
    // or nullptr for offscreen Pbuffer rendering only.
    bool init(wl_display* wl_dpy = nullptr);
    void shutdown();

    bool is_available() const noexcept { return available_; }
    bool has_wayland_egl() const noexcept { return has_wl_egl_; }

    // Upload full-screen desktop or wallpaper backdrop (BGRA/ARGB32 top-down pixels).
    void set_backdrop_bgra(const std::uint8_t* bgra, int width, int height, int stride_bytes);
    void set_backdrop_cairo(cairo_surface_t* surf);
    bool has_backdrop() const noexcept { return backdrop_w_ > 0 && backdrop_h_ > 0; }
    int backdrop_width() const noexcept { return backdrop_w_; }
    int backdrop_height() const noexcept { return backdrop_h_; }

    // Manage hardware wl_egl_window surfaces for Wayland LayerSurfaces.
    bool register_surface(wl_surface* surf, int px_w, int px_h);
    void resize_surface(wl_surface* surf, int px_w, int px_h);
    void unregister_surface(wl_surface* surf);

    // Render multi-panel Liquid Glass + crisp Cairo UI overlay directly to a registered Wayland EGL surface.
    bool render_surface(wl_surface* surf,
                        int surf_px_w,
                        int surf_px_h,
                        float origin_px_x,
                        float origin_px_y,
                        float time_sec,
                        const ClickPulse& click,
                        const GlassMaterial& material,
                        const std::vector<GlassPanelSpec>& panels,
                        cairo_surface_t* ui_overlay);

    // Offscreen render into a Cairo ARGB32 surface (used for --dump-png, tests, and SHM fallback).
    bool render_to_cairo(cairo_surface_t* dst,
                         float origin_px_x,
                         float origin_px_y,
                         float time_sec,
                         const ClickPulse& click,
                         const GlassMaterial& material,
                         const std::vector<GlassPanelSpec>& panels,
                         cairo_surface_t* ui_overlay = nullptr);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool available_ = false;
    bool has_wl_egl_ = false;
    int backdrop_w_ = 0;
    int backdrop_h_ = 0;
};

std::string resolve_shader_path(const std::string& filename);

// Convert Mistbar config values into mizzle's optical GlassMaterial parameters.
struct MaterialConfigInput {
    std::string glass_preset;
    int blur_radius = 12;
    float refraction_strength = 0.55f;
    float chromatic_aberration = 0.46f;
    float specular_intensity = 0.48f;
    float tint_opacity = 0.08f;
    float glass_opacity = 0.85f;
};
GlassMaterial make_glass_material(const MaterialConfigInput& in);

} // namespace mistbar::glass
