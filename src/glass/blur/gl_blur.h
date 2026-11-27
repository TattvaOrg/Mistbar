/* Mistbar - MIT License */

#pragma once

#include <cairo.h>
#include <memory>

namespace mistbar::glass {

struct LiquidGlassParams {
    double corner_radius        = 16.0;
    double refraction_strength  = 0.45;
    double bezel_thickness      = 0.35;
    double chromatic_aberration = 0.25;
    double specular_intensity   = 0.55;
    double tint_r               = 0.09;
    double tint_g               = 0.12;
    double tint_b               = 0.18;
    double tint_opacity         = 0.18;
    double glass_opacity        = 0.85;
    double pointer_x            = -1.0; // normalized [0, 1], <0 means centered
    double pointer_y            = -1.0;
    bool   cursor_reactive      = true;
    bool   dark_mode            = true;
    bool   overlay_only         = false; // true when Wayland compositor provides backdrop blur
};

// Hardware-accelerated OpenGL Gaussian blur and optical Liquid Glass shader via offscreen EGL.
class GlBlurEngine {
public:
    GlBlurEngine();
    ~GlBlurEngine();

    GlBlurEngine(const GlBlurEngine&) = delete;
    GlBlurEngine& operator=(const GlBlurEngine&) = delete;

    bool init();
    void shutdown();

    bool is_available() const noexcept { return available_; }

    bool blur(cairo_surface_t* src, cairo_surface_t* dst, int radius);

    // Renders optical Liquid Glass panel (curved bezel refraction, chromatic aberration,
    // Fresnel rim, Blinn-Phong specular highlights, and frosted tint) into dst_panel.
    bool render_liquid_glass(cairo_surface_t* blurred_backdrop,
                             int src_x, int src_y,
                             cairo_surface_t* dst_panel,
                             const LiquidGlassParams& params);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool available_ = false;
};

} // namespace mistbar::glass
