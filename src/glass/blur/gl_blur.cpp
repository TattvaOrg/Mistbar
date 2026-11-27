/* Mistbar - MIT License */

#include "glass/blur/gl_blur.h"
#include "glass/blur/gaussian_blur.h"
#include "common/logger.h"

#if MISTBAR_HAS_OPENGL
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <GL/gl.h>
#include <GL/glext.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace mistbar::glass {

#if MISTBAR_HAS_OPENGL
namespace {

const char* kVertexShaderSrc = R"(#version 120
attribute vec2 a_pos;
attribute vec2 a_uv;
varying vec2 v_uv;
void main() {
    v_uv = a_uv;
    gl_Position = vec4(a_pos, 0.0, 1.0);
}
)";

const char* kBlurFragShaderSrc = R"(#version 120
uniform sampler2D u_tex;
uniform vec2 u_dir;
uniform int u_radius;
uniform float u_weights[32];
varying vec2 v_uv;
void main() {
    vec4 sum = texture2D(u_tex, v_uv) * u_weights[0];
    for (int i = 1; i <= 31; ++i) {
        if (i > u_radius) break;
        vec2 off = u_dir * float(i);
        sum += texture2D(u_tex, clamp(v_uv + off, vec2(0.0), vec2(1.0))) * u_weights[i];
        sum += texture2D(u_tex, clamp(v_uv - off, vec2(0.0), vec2(1.0))) * u_weights[i];
    }
    gl_FragColor = sum;
}
)";

// Optical Liquid Glass fragment shader in top-left Cairo coordinate space.
// Note: FBO texture row 0 maps to top-left when rendering top-to-bottom into glReadPixels.
const char* kGlassFragShaderSrc = R"(#version 120
uniform sampler2D u_backdrop;
uniform int u_has_backdrop;
uniform vec2 u_panel_size;
uniform vec2 u_backdrop_size;
uniform vec2 u_src_offset;
uniform float u_radius;
uniform float u_refraction;
uniform float u_bezel;
uniform float u_chromatic;
uniform float u_specular;
uniform vec3 u_tint_color;
uniform float u_tint_opacity;
uniform float u_glass_opacity;
uniform vec2 u_pointer;
uniform int u_cursor_reactive;
uniform int u_dark_mode;
uniform int u_overlay_only;
varying vec2 v_uv;

float sdRoundedBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + vec2(r);
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

vec2 sdRoundedBoxGrad(vec2 p, vec2 b, float r) {
    float eps = 1.0;
    float dx = sdRoundedBox(p + vec2(eps, 0.0), b, r) - sdRoundedBox(p - vec2(eps, 0.0), b, r);
    float dy = sdRoundedBox(p + vec2(0.0, eps), b, r) - sdRoundedBox(p - vec2(0.0, eps), b, r);
    vec2 g = vec2(dx, dy);
    float len = length(g);
    return (len > 1e-5) ? (g / len) : vec2(0.0, -1.0);
}

vec4 sampleBackdrop(vec2 panel_px) {
    if (u_has_backdrop == 0) {
        return vec4(u_tint_color, 1.0);
    }
    vec2 uv = (u_src_offset + panel_px) / max(u_backdrop_size, vec2(1.0));
    uv = clamp(uv, vec2(0.001), vec2(0.999));
    return texture2D(u_backdrop, uv);
}

void main() {
    // v_uv.y == 0 at GL bottom -> glReadPixels row 0 is Cairo top, so use v_uv directly for Cairo top-down coords!
    //Wait: a_pos y=-1 -> v_uv.y=0 -> GL framebuffer row 0 -> glReadPixels row 0 -> Cairo top row 0!
    // So v_uv = (0,0) IS top-left in Cairo output AND top-left in Cairo-uploaded backdrop!
    vec2 px = v_uv * u_panel_size;
    vec2 center = u_panel_size * 0.5;
    vec2 p = px - center;
    float r = clamp(u_radius, 0.0, min(center.x, center.y));
    float d = sdRoundedBox(p, center, r);

    float shape_alpha = 1.0 - smoothstep(-0.75, 0.75, d);
    if (shape_alpha <= 0.001) {
        gl_FragColor = vec4(0.0);
        return;
    }

    float min_half = max(4.0, min(center.x, center.y));
    float bezel_px = max(3.5, min_half * (0.34 + u_bezel * 0.66));
    float edge_dist = max(0.0, -d);
    float edge_t = 1.0 - clamp(edge_dist / bezel_px, 0.0, 1.0);

    // Plano-convex lens dome profile (steep optical curvature near outer rim, smooth flat center)
    float dome = pow(edge_t, 2.15);
    float meniscus = sqrt(max(0.0, 1.0 - (1.0 - edge_t) * (1.0 - edge_t)));
    vec2 grad = sdRoundedBoxGrad(p, center, r);
    vec3 normal = normalize(vec3(grad * (0.25 * meniscus + 0.85 * dome) * (0.45 + u_bezel * 0.95),
                                 sqrt(max(0.05, 1.0 - dome * dome * 0.72))));

    // Strong bezel UV magnification/compression & RGB chromatic dispersion
    float refract_px = u_refraction * 34.0 * (0.14 * meniscus + 0.86 * dome);
    vec2 refract_off = -grad * refract_px;
    float disp_px = u_chromatic * 10.5 * (0.25 * meniscus + 0.75 * dome);
    vec2 disp_off = grad * disp_px;

    vec3 refracted = u_tint_color;
    if (u_has_backdrop != 0) {
        float cr = sampleBackdrop(px + refract_off + disp_off).r;
        float cg = sampleBackdrop(px + refract_off).g;
        float cb = sampleBackdrop(px + refract_off - disp_off).b;
        refracted = vec3(cr, cg, cb);
    }

    // View & lighting vectors (Y down in Cairo space: top light has y < 0)
    vec3 V = vec3(0.0, 0.0, 1.0);
    float fresnel = pow(1.0 - clamp(dot(normal, V), 0.0, 1.0), 2.0);

    vec3 L1 = normalize(vec3(-0.34, -0.84, 0.42));
    vec3 H1 = normalize(L1 + V);
    float spec1 = pow(max(dot(normal, H1), 0.0), 26.0);

    vec3 L2 = normalize(vec3(0.42, 0.78, 0.46));
    vec3 H2 = normalize(L2 + V);
    float spec2 = pow(max(dot(normal, H2), 0.0), 20.0) * 0.55;

    float cursor_spec = 0.0;
    if (u_cursor_reactive != 0 && u_pointer.x >= 0.0) {
        vec2 lp = u_pointer * u_panel_size;
        vec2 delta = (lp - px) / max(min_half * 3.6, 52.0);
        float glow = exp(-dot(delta, delta) * 1.25);
        vec3 Lc = normalize(vec3(delta.x, delta.y, 0.55));
        vec3 Hc = normalize(Lc + V);
        cursor_spec = (0.38 + 0.62 * pow(max(dot(normal, Hc), 0.0), 16.0)) * glow;
    }

    // Apple Liquid Glass signature double-corner directional rim + luminous inner caustic
    float top_sheen = exp(-max(0.0, px.y) / max(3.0, u_panel_size.y * 0.22)) * (u_dark_mode != 0 ? 0.24 : 0.36);
    float rim_band = smoothstep(2.0, 0.0, edge_dist);
    float diag_glint = pow(abs(dot(grad, normalize(vec2(-0.68, -0.73)))), 1.6);
    float top_bias = clamp(0.68 - grad.y * 0.55, 0.18, 1.0);
    float rim_caustic = rim_band * (0.42 * top_bias + 0.58 * diag_glint);
    float spec_total = u_specular * (spec1 * 0.95 + spec2 * 0.52 + fresnel * 0.72 + top_sheen + cursor_spec * 0.75);

    vec3 prism = vec3(
        0.5 + 0.5 * sin(grad.x * 2.6 + grad.y * 1.4 + 0.0),
        0.5 + 0.5 * sin(grad.x * 2.6 + grad.y * 1.4 + 2.094),
        0.5 + 0.5 * sin(grad.x * 2.6 + grad.y * 1.4 + 4.188)
    );
    vec3 rim_color = mix(vec3(1.0), prism, clamp(u_chromatic * 0.65, 0.0, 0.65));

    if (u_overlay_only != 0) {
        // Compositor provides real window blur behind surface; keep interior clean & layer rim caustics
        vec3 base_col = mix(u_tint_color, refracted, clamp(u_refraction * pow(dome, 1.4) * 0.28, 0.0, 0.28));
        if (u_dark_mode == 0) {
            base_col = mix(base_col, vec3(1.0), fresnel * 0.18);
        }
        vec3 lit_col = base_col + rim_color * (spec_total * 0.85 + rim_caustic * (0.24 + u_specular * 0.58));
        float interior_a = clamp(u_tint_opacity * (u_dark_mode != 0 ? 0.74 : 0.66), 0.02, 0.85);
        float edge_a = clamp(dome * u_refraction * 0.16 + spec_total * 0.52 + rim_caustic * 0.38, 0.0, 0.92);
        float out_a = clamp(max(interior_a, edge_a) * u_glass_opacity, 0.02, 0.95) * shape_alpha;
        gl_FragColor = vec4(clamp(lit_col, 0.0, 1.0) * out_a, out_a);
    } else {
        vec3 glass_body = mix(refracted, u_tint_color, clamp(u_tint_opacity * (u_dark_mode != 0 ? 0.95 : 0.78), 0.0, 1.0));
        if (u_dark_mode == 0) {
            glass_body = mix(glass_body, vec3(1.0), 0.08 + fresnel * 0.22);
        }
        vec3 lit_col = glass_body + rim_color * (spec_total * 0.82 + rim_caustic * (0.22 + u_specular * 0.55));
        float out_a = clamp(u_glass_opacity, 0.08, 1.0) * shape_alpha;
        gl_FragColor = vec4(clamp(lit_col, 0.0, 1.0) * out_a, out_a);
    }
}
)";

GLuint compile_shader(GLenum type, const char* src) {
    GLuint sh = glCreateShader(type);
    if (!sh) return 0;
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

GLuint link_program(const char* vs_src, const char* fs_src) {
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return 0;
    }
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint linked = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (!linked) {
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

void setup_quad(GLuint program) {
    static const GLfloat kQuad[] = {
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f, 1.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
    };
    const GLint pos_loc = glGetAttribLocation(program, "a_pos");
    const GLint uv_loc  = glGetAttribLocation(program, "a_uv");
    if (pos_loc >= 0) {
        glEnableVertexAttribArray(static_cast<GLuint>(pos_loc));
        glVertexAttribPointer(static_cast<GLuint>(pos_loc), 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), kQuad);
    }
    if (uv_loc >= 0) {
        glEnableVertexAttribArray(static_cast<GLuint>(uv_loc));
        glVertexAttribPointer(static_cast<GLuint>(uv_loc), 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), kQuad + 2);
    }
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    if (pos_loc >= 0) glDisableVertexAttribArray(static_cast<GLuint>(pos_loc));
    if (uv_loc >= 0)  glDisableVertexAttribArray(static_cast<GLuint>(uv_loc));
}

} // namespace
#endif

struct GlBlurEngine::Impl {
#if MISTBAR_HAS_OPENGL
    EGLDisplay egl_dpy = EGL_NO_DISPLAY;
    EGLContext egl_ctx = EGL_NO_CONTEXT;
    EGLSurface egl_surf = EGL_NO_SURFACE;
    GLuint blur_program = 0;
    GLuint glass_program = 0;
    GLuint fbo[2]{0, 0};
    GLuint tex[2]{0, 0};
    GLuint glass_fbo = 0;
    GLuint glass_tex = 0;
    GLuint backdrop_tex = 0;
    int tex_w = 0;
    int tex_h = 0;
    int glass_w = 0;
    int glass_h = 0;
    const void* cached_backdrop_ptr = nullptr;
    int cached_bd_w = 0;
    int cached_bd_h = 0;
#endif
};

GlBlurEngine::GlBlurEngine() : impl_(std::make_unique<Impl>()) {}

GlBlurEngine::~GlBlurEngine() {
    shutdown();
}

bool GlBlurEngine::init() {
    shutdown();
#if MISTBAR_HAS_OPENGL
    impl_->egl_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (impl_->egl_dpy == EGL_NO_DISPLAY) {
        return false;
    }
    if (!eglInitialize(impl_->egl_dpy, nullptr, nullptr)) {
        impl_->egl_dpy = EGL_NO_DISPLAY;
        return false;
    }
    if (!eglBindAPI(EGL_OPENGL_API)) {
        shutdown();
        return false;
    }

    const EGLint cfg_attribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    EGLConfig cfg = nullptr;
    EGLint num_cfg = 0;
    if (!eglChooseConfig(impl_->egl_dpy, cfg_attribs, &cfg, 1, &num_cfg) || num_cfg == 0) {
        shutdown();
        return false;
    }

    const EGLint pb_attribs[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    impl_->egl_surf = eglCreatePbufferSurface(impl_->egl_dpy, cfg, pb_attribs);
    if (impl_->egl_surf == EGL_NO_SURFACE) {
        shutdown();
        return false;
    }

    impl_->egl_ctx = eglCreateContext(impl_->egl_dpy, cfg, EGL_NO_CONTEXT, nullptr);
    if (impl_->egl_ctx == EGL_NO_CONTEXT) {
        shutdown();
        return false;
    }

    if (!eglMakeCurrent(impl_->egl_dpy, impl_->egl_surf, impl_->egl_surf, impl_->egl_ctx)) {
        shutdown();
        return false;
    }

    impl_->blur_program = link_program(kVertexShaderSrc, kBlurFragShaderSrc);
    impl_->glass_program = link_program(kVertexShaderSrc, kGlassFragShaderSrc);
    if (!impl_->blur_program || !impl_->glass_program) {
        shutdown();
        return false;
    }

    glGenFramebuffers(2, impl_->fbo);
    glGenTextures(2, impl_->tex);
    glGenFramebuffers(1, &impl_->glass_fbo);
    glGenTextures(1, &impl_->glass_tex);
    glGenTextures(1, &impl_->backdrop_tex);
    available_ = true;
    log::info("glass", "OpenGL hardware Liquid Glass and Gaussian blur initialized");
    return true;
#else
    return false;
#endif
}

void GlBlurEngine::shutdown() {
#if MISTBAR_HAS_OPENGL
    if (impl_->egl_dpy != EGL_NO_DISPLAY) {
        if (impl_->egl_ctx != EGL_NO_CONTEXT) {
            eglMakeCurrent(impl_->egl_dpy, impl_->egl_surf, impl_->egl_surf, impl_->egl_ctx);
            if (impl_->tex[0]) glDeleteTextures(2, impl_->tex);
            if (impl_->fbo[0]) glDeleteFramebuffers(2, impl_->fbo);
            if (impl_->glass_tex) glDeleteTextures(1, &impl_->glass_tex);
            if (impl_->backdrop_tex) glDeleteTextures(1, &impl_->backdrop_tex);
            if (impl_->glass_fbo) glDeleteFramebuffers(1, &impl_->glass_fbo);
            if (impl_->blur_program) glDeleteProgram(impl_->blur_program);
            if (impl_->glass_program) glDeleteProgram(impl_->glass_program);
            impl_->tex[0] = impl_->tex[1] = 0;
            impl_->fbo[0] = impl_->fbo[1] = 0;
            impl_->glass_tex = impl_->backdrop_tex = impl_->glass_fbo = 0;
            impl_->blur_program = impl_->glass_program = 0;
            eglMakeCurrent(impl_->egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            eglDestroyContext(impl_->egl_dpy, impl_->egl_ctx);
        }
        if (impl_->egl_surf != EGL_NO_SURFACE) eglDestroySurface(impl_->egl_dpy, impl_->egl_surf);
        eglTerminate(impl_->egl_dpy);
    }
    impl_->egl_dpy = EGL_NO_DISPLAY;
    impl_->egl_ctx = EGL_NO_CONTEXT;
    impl_->egl_surf = EGL_NO_SURFACE;
    impl_->tex_w = impl_->tex_h = 0;
    impl_->glass_w = impl_->glass_h = 0;
    impl_->cached_backdrop_ptr = nullptr;
#endif
    available_ = false;
}

bool GlBlurEngine::blur(cairo_surface_t* src, cairo_surface_t* dst, int radius) {
#if MISTBAR_HAS_OPENGL
    if (!available_ || !src || !dst) return false;
    const int w = cairo_image_surface_get_width(src);
    const int h = cairo_image_surface_get_height(src);
    if (w <= 0 || h <= 0 ||
        cairo_image_surface_get_width(dst) != w ||
        cairo_image_surface_get_height(dst) != h) {
        return false;
    }

    if (!eglMakeCurrent(impl_->egl_dpy, impl_->egl_surf, impl_->egl_surf, impl_->egl_ctx)) {
        return false;
    }

    if (impl_->tex_w != w || impl_->tex_h != h) {
        for (int i = 0; i < 2; ++i) {
            glBindTexture(GL_TEXTURE_2D, impl_->tex[i]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);

            glBindFramebuffer(GL_FRAMEBUFFER, impl_->fbo[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, impl_->tex[i], 0);
        }
        impl_->tex_w = w;
        impl_->tex_h = h;
    }

    cairo_surface_flush(src);
    std::vector<std::uint8_t> packed(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4U);
    const auto* sdata = cairo_image_surface_get_data(src);
    const int sstride = cairo_image_surface_get_stride(src);
    for (int y = 0; y < h; ++y) {
        std::memcpy(packed.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(w) * 4U,
                    sdata + y * sstride,
                    static_cast<std::size_t>(w) * 4U);
    }

    glBindTexture(GL_TEXTURE_2D, impl_->tex[0]);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_BGRA, GL_UNSIGNED_BYTE, packed.data());

    const int r = std::clamp(radius, 0, 31);
    if (r == 0) {
        cairo_surface_flush(dst);
        auto* ddata = cairo_image_surface_get_data(dst);
        const int dstride = cairo_image_surface_get_stride(dst);
        for (int y = 0; y < h; ++y) {
            std::memcpy(ddata + y * dstride, sdata + y * sstride, static_cast<std::size_t>(w) * 4U);
        }
        cairo_surface_mark_dirty(dst);
        impl_->cached_backdrop_ptr = nullptr;
        return true;
    }

    const auto kernel = build_gaussian_kernel_1d(r, std::max(1.0, static_cast<double>(r) * 0.5));
    std::vector<GLfloat> weights(32, 0.0f);
    weights[0] = kernel[static_cast<std::size_t>(r)];
    for (int i = 1; i <= r; ++i) {
        weights[static_cast<std::size_t>(i)] = kernel[static_cast<std::size_t>(r + i)];
    }

    glViewport(0, 0, w, h);
    glUseProgram(impl_->blur_program);
    glUniform1i(glGetUniformLocation(impl_->blur_program, "u_tex"), 0);
    glUniform1i(glGetUniformLocation(impl_->blur_program, "u_radius"), r);
    glUniform1fv(glGetUniformLocation(impl_->blur_program, "u_weights"), 32, weights.data());

    glBindFramebuffer(GL_FRAMEBUFFER, impl_->fbo[1]);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, impl_->tex[0]);
    glUniform2f(glGetUniformLocation(impl_->blur_program, "u_dir"), 1.0f / static_cast<float>(w), 0.0f);
    setup_quad(impl_->blur_program);

    glBindFramebuffer(GL_FRAMEBUFFER, impl_->fbo[0]);
    glBindTexture(GL_TEXTURE_2D, impl_->tex[1]);
    glUniform2f(glGetUniformLocation(impl_->blur_program, "u_dir"), 0.0f, 1.0f / static_cast<float>(h));
    setup_quad(impl_->blur_program);

    glReadPixels(0, 0, w, h, GL_BGRA, GL_UNSIGNED_BYTE, packed.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    cairo_surface_flush(dst);
    auto* ddata = cairo_image_surface_get_data(dst);
    const int dstride = cairo_image_surface_get_stride(dst);
    for (int y = 0; y < h; ++y) {
        std::memcpy(ddata + y * dstride,
                    packed.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(w) * 4U,
                    static_cast<std::size_t>(w) * 4U);
    }
    cairo_surface_mark_dirty(dst);
    impl_->cached_backdrop_ptr = nullptr;
    return true;
#else
    (void)src; (void)dst; (void)radius;
    return false;
#endif
}

bool GlBlurEngine::render_liquid_glass(cairo_surface_t* blurred_backdrop,
                                       int src_x, int src_y,
                                       cairo_surface_t* dst_panel,
                                       const LiquidGlassParams& params) {
#if MISTBAR_HAS_OPENGL
    if (!available_ || !dst_panel) return false;
    const int pw = cairo_image_surface_get_width(dst_panel);
    const int ph = cairo_image_surface_get_height(dst_panel);
    if (pw <= 0 || ph <= 0) return false;

    if (!eglMakeCurrent(impl_->egl_dpy, impl_->egl_surf, impl_->egl_surf, impl_->egl_ctx)) {
        return false;
    }

    if (impl_->glass_w != pw || impl_->glass_h != ph) {
        glBindTexture(GL_TEXTURE_2D, impl_->glass_tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, pw, ph, 0, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);

        glBindFramebuffer(GL_FRAMEBUFFER, impl_->glass_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, impl_->glass_tex, 0);
        impl_->glass_w = pw;
        impl_->glass_h = ph;
    }

    int bw = 1;
    int bh = 1;
    int has_bd = 0;
    if (blurred_backdrop) {
        bw = cairo_image_surface_get_width(blurred_backdrop);
        bh = cairo_image_surface_get_height(blurred_backdrop);
        if (bw > 0 && bh > 0) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, impl_->backdrop_tex);
            if (impl_->cached_backdrop_ptr != blurred_backdrop ||
                impl_->cached_bd_w != bw || impl_->cached_bd_h != bh) {
                cairo_surface_flush(blurred_backdrop);
                std::vector<std::uint8_t> bpack(static_cast<std::size_t>(bw) * static_cast<std::size_t>(bh) * 4U);
                const auto* bdata = cairo_image_surface_get_data(blurred_backdrop);
                const int bstride = cairo_image_surface_get_stride(blurred_backdrop);
                for (int y = 0; y < bh; ++y) {
                    std::memcpy(bpack.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(bw) * 4U,
                                bdata + y * bstride, static_cast<std::size_t>(bw) * 4U);
                }
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, bw, bh, 0, GL_BGRA, GL_UNSIGNED_BYTE, bpack.data());
                impl_->cached_backdrop_ptr = blurred_backdrop;
                impl_->cached_bd_w = bw;
                impl_->cached_bd_h = bh;
            }
            has_bd = 1;
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, impl_->glass_fbo);
    glViewport(0, 0, pw, ph);
    glDisable(GL_BLEND);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(impl_->glass_program);
    glUniform1i(glGetUniformLocation(impl_->glass_program, "u_backdrop"), 0);
    glUniform1i(glGetUniformLocation(impl_->glass_program, "u_has_backdrop"), has_bd);
    glUniform2f(glGetUniformLocation(impl_->glass_program, "u_panel_size"),
                static_cast<float>(pw), static_cast<float>(ph));
    glUniform2f(glGetUniformLocation(impl_->glass_program, "u_backdrop_size"),
                static_cast<float>(bw), static_cast<float>(bh));
    glUniform2f(glGetUniformLocation(impl_->glass_program, "u_src_offset"),
                static_cast<float>(src_x), static_cast<float>(src_y));
    glUniform1f(glGetUniformLocation(impl_->glass_program, "u_radius"), static_cast<float>(params.corner_radius));
    glUniform1f(glGetUniformLocation(impl_->glass_program, "u_refraction"), static_cast<float>(params.refraction_strength));
    glUniform1f(glGetUniformLocation(impl_->glass_program, "u_bezel"), static_cast<float>(params.bezel_thickness));
    glUniform1f(glGetUniformLocation(impl_->glass_program, "u_chromatic"), static_cast<float>(params.chromatic_aberration));
    glUniform1f(glGetUniformLocation(impl_->glass_program, "u_specular"), static_cast<float>(params.specular_intensity));
    glUniform3f(glGetUniformLocation(impl_->glass_program, "u_tint_color"),
                static_cast<float>(params.tint_r),
                static_cast<float>(params.tint_g),
                static_cast<float>(params.tint_b));
    glUniform1f(glGetUniformLocation(impl_->glass_program, "u_tint_opacity"), static_cast<float>(params.tint_opacity));
    glUniform1f(glGetUniformLocation(impl_->glass_program, "u_glass_opacity"), static_cast<float>(params.glass_opacity));
    glUniform2f(glGetUniformLocation(impl_->glass_program, "u_pointer"),
                static_cast<float>(params.pointer_x), static_cast<float>(params.pointer_y));
    glUniform1i(glGetUniformLocation(impl_->glass_program, "u_cursor_reactive"), params.cursor_reactive ? 1 : 0);
    glUniform1i(glGetUniformLocation(impl_->glass_program, "u_dark_mode"), params.dark_mode ? 1 : 0);
    glUniform1i(glGetUniformLocation(impl_->glass_program, "u_overlay_only"), params.overlay_only ? 1 : 0);

    setup_quad(impl_->glass_program);

    std::vector<std::uint8_t> out_px(static_cast<std::size_t>(pw) * static_cast<std::size_t>(ph) * 4U);
    glReadPixels(0, 0, pw, ph, GL_BGRA, GL_UNSIGNED_BYTE, out_px.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    cairo_surface_flush(dst_panel);
    auto* ddata = cairo_image_surface_get_data(dst_panel);
    const int dstride = cairo_image_surface_get_stride(dst_panel);
    for (int y = 0; y < ph; ++y) {
        std::memcpy(ddata + y * dstride,
                    out_px.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(pw) * 4U,
                    static_cast<std::size_t>(pw) * 4U);
    }
    cairo_surface_mark_dirty(dst_panel);
    return true;
#else
    (void)blurred_backdrop; (void)src_x; (void)src_y; (void)dst_panel; (void)params;
    return false;
#endif
}

} // namespace mistbar::glass
