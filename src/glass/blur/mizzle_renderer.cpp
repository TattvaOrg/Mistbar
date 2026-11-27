/* Mistbar - MIT License */

#include "glass/blur/mizzle_renderer.h"
#include "common/logger.h"

#if MISTBAR_HAS_OPENGL
#include <wayland-egl.h>
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <GL/gl.h>
#include <GL/glext.h>
#endif

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <unordered_map>

namespace fs = std::filesystem;

namespace mistbar::glass {

std::string resolve_shader_path(const std::string& filename) {
    std::error_code ec;
    char exe_buf[4096]{};
    const ssize_t n = ::readlink("/proc/self/exe", exe_buf, sizeof(exe_buf) - 1);
    if (n > 0) {
        const fs::path exe_dir = fs::path(std::string(exe_buf, static_cast<std::size_t>(n))).parent_path();
        const fs::path p1 = exe_dir / "shaders" / filename;
        if (fs::exists(p1, ec)) return p1.string();
        const fs::path p2 = exe_dir.parent_path() / "shaders" / filename;
        if (fs::exists(p2, ec)) return p2.string();
        const fs::path p3 = exe_dir.parent_path() / "share" / "mistbar" / "shaders" / filename;
        if (fs::exists(p3, ec)) return p3.string();
    }
#ifdef MISTBAR_SOURCE_DIR
    {
        const fs::path p = fs::path(MISTBAR_SOURCE_DIR) / "shaders" / filename;
        if (fs::exists(p, ec)) return p.string();
    }
#endif
#ifdef MISTBAR_INSTALL_PREFIX
    {
        const fs::path p = fs::path(MISTBAR_INSTALL_PREFIX) / "share" / "mistbar" / "shaders" / filename;
        if (fs::exists(p, ec)) return p.string();
    }
#endif
    return (fs::path("shaders") / filename).string();
}

#if MISTBAR_HAS_OPENGL
namespace {

constexpr int kPyramidLevels = 5;

bool read_file_text(const std::string& path, std::string& out_text) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out_text = ss.str();
    return true;
}

GLuint compile_stage(GLenum type, const std::string& src, const std::string& tag) {
    GLuint sh = glCreateShader(type);
    if (!sh) return 0;
    const char* ptr = src.c_str();
    glShaderSource(sh, 1, &ptr, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &len);
        std::string err(len > 1 ? static_cast<std::size_t>(len) : 1U, '\0');
        glGetShaderInfoLog(sh, len, nullptr, err.data());
        log::warn("glass", "Shader compile failed (" + tag + "): " + err);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

GLuint load_shader_program(const std::string& vert_name, const std::string& frag_name, bool is_gles) {
    std::string vert_body;
    std::string frag_body;
    const std::string vp = resolve_shader_path(vert_name);
    const std::string fp = resolve_shader_path(frag_name);
    if (!read_file_text(vp, vert_body)) {
        log::warn("glass", "Missing vertex shader: " + vp);
        return 0;
    }
    if (!read_file_text(fp, frag_body)) {
        log::warn("glass", "Missing fragment shader: " + fp);
        return 0;
    }
    const std::string header = is_gles
        ? "#version 300 es\nprecision highp float;\nprecision highp int;\n"
        : "#version 330 core\n";

    GLuint vs = compile_stage(GL_VERTEX_SHADER, header + vert_body, vert_name);
    if (!vs) return 0;
    GLuint fs = compile_stage(GL_FRAGMENT_SHADER, header + frag_body, frag_name);
    if (!fs) {
        glDeleteShader(vs);
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
        GLint len = 0;
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len);
        std::string err(len > 1 ? static_cast<std::size_t>(len) : 1U, '\0');
        glGetProgramInfoLog(prog, len, nullptr, err.data());
        log::warn("glass", "Shader link failed (" + frag_name + "): " + err);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

} // namespace
#endif

struct MizzleRenderer::Impl {
#if MISTBAR_HAS_OPENGL
    struct RenderTarget {
        GLuint fbo = 0;
        GLuint tex = 0;
        int w = 0;
        int h = 0;
    };

    struct SurfaceSlot {
        wl_egl_window* egl_win = nullptr;
        EGLSurface egl_surf = EGL_NO_SURFACE;
        GLuint ui_tex = 0;
        int ui_w = 0;
        int ui_h = 0;
        int win_w = 0;
        int win_h = 0;
    };

    EGLDisplay egl_dpy = EGL_NO_DISPLAY;
    EGLConfig egl_cfg = nullptr;
    EGLContext egl_ctx = EGL_NO_CONTEXT;
    EGLSurface pbuffer_surf = EGL_NO_SURFACE;
    bool is_gles = false;

    GLuint quad_vao = 0;
    GLuint quad_vbo = 0;
    GLuint desktop_tex = 0;

    RenderTarget backdrop_rt{};
    std::array<RenderTarget, kPyramidLevels> down_rts{};
    std::array<RenderTarget, kPyramidLevels> up_rts{};
    RenderTarget offscreen_rt{};
    GLuint offscreen_ui_tex = 0;
    int offscreen_ui_w = 0;
    int offscreen_ui_h = 0;

    GLuint prog_blit = 0;
    GLuint prog_down = 0;
    GLuint prog_up = 0;
    GLuint prog_glass = 0;
    GLuint prog_ui = 0;

    bool pyramid_dirty = true;
    float last_blur_scale = -1.0f;
    int last_bd_w = 0;
    int last_bd_h = 0;

    std::unordered_map<wl_surface*, SurfaceSlot> surfaces;

    bool make_offscreen_current() const {
        if (egl_dpy == EGL_NO_DISPLAY || egl_ctx == EGL_NO_CONTEXT) return false;
        if (pbuffer_surf != EGL_NO_SURFACE) {
            return eglMakeCurrent(egl_dpy, pbuffer_surf, pbuffer_surf, egl_ctx) == EGL_TRUE;
        }
        return eglMakeCurrent(egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, egl_ctx) == EGL_TRUE;
    }

    void destroy_rt(RenderTarget& rt) {
        if (rt.tex) { glDeleteTextures(1, &rt.tex); rt.tex = 0; }
        if (rt.fbo) { glDeleteFramebuffers(1, &rt.fbo); rt.fbo = 0; }
        rt.w = 0;
        rt.h = 0;
    }

    void alloc_rt(RenderTarget& rt, int w, int h) {
        const int tw = std::max(1, w);
        const int th = std::max(1, h);
        if (rt.w == tw && rt.h == th && rt.fbo != 0) return;
        destroy_rt(rt);
        rt.w = tw;
        rt.h = th;
        glGenTextures(1, &rt.tex);
        glBindTexture(GL_TEXTURE_2D, rt.tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glGenFramebuffers(1, &rt.fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, rt.fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rt.tex, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void draw_quad() const {
        glBindVertexArray(quad_vao);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glBindVertexArray(0);
    }

    void ensure_pyramid(int width, int height, float blur_scale) {
        if (width <= 0 || height <= 0) return;
        const bool same_size = (last_bd_w == width && last_bd_h == height);
        const bool same_blur = std::abs(last_blur_scale - blur_scale) < 0.002f;
        if (!pyramid_dirty && same_size && same_blur && up_rts[kPyramidLevels - 1].tex != 0) {
            return;
        }

        alloc_rt(backdrop_rt, width, height);
        int w = width;
        int h = height;
        for (int i = 0; i < kPyramidLevels; ++i) {
            w = std::max(w / 2, 1);
            h = std::max(h / 2, 1);
            alloc_rt(down_rts[i], w, h);
        }
        for (int i = 0; i < kPyramidLevels; ++i) {
            const int up_w = (i == kPyramidLevels - 1) ? width : down_rts[kPyramidLevels - 2 - i].w;
            const int up_h = (i == kPyramidLevels - 1) ? height : down_rts[kPyramidLevels - 2 - i].h;
            alloc_rt(up_rts[i], up_w, up_h);
        }

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);

        // 1. Copy desktop_tex (top-down BGRA) into backdrop_rt (bottom-up RGBA)
        glBindFramebuffer(GL_FRAMEBUFFER, backdrop_rt.fbo);
        glViewport(0, 0, backdrop_rt.w, backdrop_rt.h);
        glUseProgram(prog_blit);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, desktop_tex);
        glUniform1i(glGetUniformLocation(prog_blit, "uTex"), 0);
        glUniform1i(glGetUniformLocation(prog_blit, "uFlipY"), 1);
        glUniform1i(glGetUniformLocation(prog_blit, "uSwapRB"), 1);
        draw_quad();

        // 2. Dual-Kawase downsample + upsample pyramid
        GLuint prev_tex = backdrop_rt.tex;
        int prev_w = backdrop_rt.w;
        int prev_h = backdrop_rt.h;

        glUseProgram(prog_down);
        glUniform1i(glGetUniformLocation(prog_down, "uTex"), 0);
        const GLint down_hp = glGetUniformLocation(prog_down, "uHalfPixel");
        for (int i = 0; i < kPyramidLevels; ++i) {
            const auto& dst = down_rts[i];
            glBindFramebuffer(GL_FRAMEBUFFER, dst.fbo);
            glViewport(0, 0, dst.w, dst.h);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, prev_tex);
            glUniform2f(down_hp,
                        (0.5f * blur_scale) / static_cast<float>(prev_w),
                        (0.5f * blur_scale) / static_cast<float>(prev_h));
            draw_quad();
            prev_tex = dst.tex;
            prev_w = dst.w;
            prev_h = dst.h;
        }

        glUseProgram(prog_up);
        glUniform1i(glGetUniformLocation(prog_up, "uTex"), 0);
        const GLint up_hp = glGetUniformLocation(prog_up, "uHalfPixel");
        for (int i = 0; i < kPyramidLevels; ++i) {
            const auto& dst = up_rts[i];
            glBindFramebuffer(GL_FRAMEBUFFER, dst.fbo);
            glViewport(0, 0, dst.w, dst.h);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, prev_tex);
            glUniform2f(up_hp,
                        (0.5f * blur_scale) / static_cast<float>(prev_w),
                        (0.5f * blur_scale) / static_cast<float>(prev_h));
            draw_quad();
            prev_tex = dst.tex;
            prev_w = dst.w;
            prev_h = dst.h;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        pyramid_dirty = false;
        last_blur_scale = blur_scale;
        last_bd_w = width;
        last_bd_h = height;
    }

    void upload_ui_texture(GLuint& tex_id, int& cur_w, int& cur_h, cairo_surface_t* ui_overlay) {
        if (!ui_overlay) return;
        const int uw = cairo_image_surface_get_width(ui_overlay);
        const int uh = cairo_image_surface_get_height(ui_overlay);
        const int stride = cairo_image_surface_get_stride(ui_overlay);
        if (uw <= 0 || uh <= 0 || stride < uw * 4) return;

        cairo_surface_flush(ui_overlay);
        const unsigned char* data = cairo_image_surface_get_data(ui_overlay);
        if (!data) return;

        if (tex_id == 0) {
            glGenTextures(1, &tex_id);
            glBindTexture(GL_TEXTURE_2D, tex_id);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        } else {
            glBindTexture(GL_TEXTURE_2D, tex_id);
        }

        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, stride / 4);
        if (cur_w != uw || cur_h != uh) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, uw, uh, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
            cur_w = uw;
            cur_h = uh;
        } else {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, uw, uh, GL_RGBA, GL_UNSIGNED_BYTE, data);
        }
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void ensure_fallback_backdrop(float blur_scale) {
        if (backdrop_rt.tex != 0 && up_rts[kPyramidLevels - 1].tex != 0) return;
        std::array<std::uint8_t, 16 * 16 * 4> pixels{};
        for (std::size_t i = 0; i < pixels.size(); i += 4) {
            pixels[i + 0] = 68;  // B
            pixels[i + 1] = 46;  // G
            pixels[i + 2] = 32;  // R
            pixels[i + 3] = 255; // A
        }
        glBindTexture(GL_TEXTURE_2D, desktop_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        pyramid_dirty = true;
        ensure_pyramid(16, 16, blur_scale);
    }

    void draw_panels_and_ui(GLuint target_fbo,
                            int surf_w,
                            int surf_h,
                            int bd_w,
                            int bd_h,
                            float origin_x,
                            float origin_y,
                            float time_sec,
                            const ClickPulse& click,
                            const GlassMaterial& material,
                            const std::vector<GlassPanelSpec>& panels,
                            GLuint ui_tex,
                            int ui_w,
                            int ui_h,
                            bool translucent_glass) {
        glBindFramebuffer(GL_FRAMEBUFFER, target_fbo);
        glViewport(0, 0, surf_w, surf_h);
        glDisable(GL_DEPTH_TEST);

        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

        if (backdrop_rt.tex != 0 && up_rts[kPyramidLevels - 1].tex != 0 && !panels.empty()) {
            glUseProgram(prog_glass);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, backdrop_rt.tex);
            glUniform1i(glGetUniformLocation(prog_glass, "uBackdrop"), 0);

            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, up_rts[kPyramidLevels - 1].tex);
            glUniform1i(glGetUniformLocation(prog_glass, "uBlurred"), 1);

            glUniform2f(glGetUniformLocation(prog_glass, "uResolution"),
                        static_cast<float>(surf_w), static_cast<float>(surf_h));
            glUniform2f(glGetUniformLocation(prog_glass, "uBackdropSize"),
                        static_cast<float>(bd_w), static_cast<float>(bd_h));
            glUniform2f(glGetUniformLocation(prog_glass, "uSurfaceOrigin"), origin_x, origin_y);
            glUniform1f(glGetUniformLocation(prog_glass, "uTime"), time_sec);
            glUniform4f(glGetUniformLocation(prog_glass, "uClick"),
                        click.x, click.y, click.age_sec, click.active ? 1.0f : 0.0f);
            glUniform1f(glGetUniformLocation(prog_glass, "uTranslucentGlass"), translucent_glass ? 1.0f : 0.0f);

            const GLint loc_rect = glGetUniformLocation(prog_glass, "uRect");
            const GLint loc_rad  = glGetUniformLocation(prog_glass, "uRadius");
            const GLint loc_ref  = glGetUniformLocation(prog_glass, "uRefraction");
            const GLint loc_disp = glGetUniformLocation(prog_glass, "uDispersion");
            const GLint loc_frst = glGetUniformLocation(prog_glass, "uFrost");
            const GLint loc_spec = glGetUniformLocation(prog_glass, "uSpecular");
            const GLint loc_tint = glGetUniformLocation(prog_glass, "uTintColor");
            const GLint loc_shad = glGetUniformLocation(prog_glass, "uShadowAlpha");
            const GLint loc_pill = glGetUniformLocation(prog_glass, "uIsPill");
            const GLint loc_opac = glGetUniformLocation(prog_glass, "uCustomOpacity");

            for (const auto& p : panels) {
                if (p.w <= 1.0f || p.h <= 1.0f) continue;
                const GlassMaterial& m = p.overrideMaterial ? p.customMaterial : material;
                glUniform4f(loc_rect, p.x, p.y, p.w, p.h);
                glUniform1f(loc_rad, p.radius);
                glUniform1f(loc_ref, m.refraction);
                glUniform1f(loc_disp, m.dispersion);
                glUniform1f(loc_frst, m.frost);
                glUniform1f(loc_spec, m.specular);
                glUniform4f(loc_tint, m.tintR, m.tintG, m.tintB, m.tintAlpha);
                glUniform1f(loc_shad, m.shadowAlpha);
                glUniform1f(loc_pill, p.isPill ? 1.0f : 0.0f);
                glUniform1f(loc_opac, p.customOpacity);
                draw_quad();
            }
        }

        if (ui_tex != 0 && ui_w > 0 && ui_h > 0) {
            glUseProgram(prog_ui);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, ui_tex);
            glUniform1i(glGetUniformLocation(prog_ui, "uUiTex"), 0);
            draw_quad();
        }

        glDisable(GL_BLEND);
        glUseProgram(0);
    }
#endif
};

MizzleRenderer::MizzleRenderer() : impl_(std::make_unique<Impl>()) {}

MizzleRenderer::~MizzleRenderer() {
    shutdown();
}

bool MizzleRenderer::init(wl_display* wl_dpy) {
    shutdown();
#if MISTBAR_HAS_OPENGL
    if (wl_dpy) {
        impl_->egl_dpy = eglGetDisplay(reinterpret_cast<EGLNativeDisplayType>(wl_dpy));
    } else {
        impl_->egl_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    }
    if (impl_->egl_dpy == EGL_NO_DISPLAY) {
        return false;
    }
    if (!eglInitialize(impl_->egl_dpy, nullptr, nullptr)) {
        impl_->egl_dpy = EGL_NO_DISPLAY;
        return false;
    }

    auto try_api = [&](bool gles) -> bool {
        if (!eglBindAPI(gles ? EGL_OPENGL_ES_API : EGL_OPENGL_API)) {
            return false;
        }
        const EGLint rend_bit = gles ? EGL_OPENGL_ES3_BIT : EGL_OPENGL_BIT;
        EGLConfig cfg = nullptr;
        EGLint num_cfg = 0;
        bool use_pbuffer = false;

        if (wl_dpy) {
            const EGLint attribs[] = {
                EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                EGL_RENDERABLE_TYPE, rend_bit,
                EGL_RED_SIZE, 8,
                EGL_GREEN_SIZE, 8,
                EGL_BLUE_SIZE, 8,
                EGL_ALPHA_SIZE, 8,
                EGL_NONE
            };
            if (!eglChooseConfig(impl_->egl_dpy, attribs, &cfg, 1, &num_cfg) || num_cfg == 0) {
                return false;
            }
        } else {
            const EGLint pb_cfg_attribs[] = {
                EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                EGL_RENDERABLE_TYPE, rend_bit,
                EGL_RED_SIZE, 8,
                EGL_GREEN_SIZE, 8,
                EGL_BLUE_SIZE, 8,
                EGL_ALPHA_SIZE, 8,
                EGL_NONE
            };
            if (eglChooseConfig(impl_->egl_dpy, pb_cfg_attribs, &cfg, 1, &num_cfg) && num_cfg > 0) {
                use_pbuffer = true;
            } else {
                const EGLint sl_cfg_attribs[] = {
                    EGL_RENDERABLE_TYPE, rend_bit,
                    EGL_RED_SIZE, 8,
                    EGL_GREEN_SIZE, 8,
                    EGL_BLUE_SIZE, 8,
                    EGL_ALPHA_SIZE, 8,
                    EGL_NONE
                };
                if (!eglChooseConfig(impl_->egl_dpy, sl_cfg_attribs, &cfg, 1, &num_cfg) || num_cfg == 0) {
                    return false;
                }
            }
        }

        const EGLint ctx_attribs[] = {
            EGL_CONTEXT_CLIENT_VERSION, 3,
            EGL_NONE
        };
        EGLContext ctx = eglCreateContext(impl_->egl_dpy, cfg, EGL_NO_CONTEXT,
                                          gles ? ctx_attribs : nullptr);
        if (ctx == EGL_NO_CONTEXT) {
            return false;
        }
        EGLSurface pbuf = EGL_NO_SURFACE;
        if (use_pbuffer) {
            const EGLint pb_attribs[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
            pbuf = eglCreatePbufferSurface(impl_->egl_dpy, cfg, pb_attribs);
            if (pbuf == EGL_NO_SURFACE) {
                eglDestroyContext(impl_->egl_dpy, ctx);
                return false;
            }
            if (!eglMakeCurrent(impl_->egl_dpy, pbuf, pbuf, ctx)) {
                eglDestroySurface(impl_->egl_dpy, pbuf);
                eglDestroyContext(impl_->egl_dpy, ctx);
                return false;
            }
        } else {
            if (!eglMakeCurrent(impl_->egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
                eglDestroyContext(impl_->egl_dpy, ctx);
                return false;
            }
        }
        impl_->egl_cfg = cfg;
        impl_->egl_ctx = ctx;
        impl_->pbuffer_surf = pbuf;
        impl_->is_gles = gles;
        return true;
    };

    if (!try_api(false) && !try_api(true)) {
        shutdown();
        return false;
    }

    impl_->prog_blit  = load_shader_program("fullscreen.vert", "blit.frag", impl_->is_gles);
    impl_->prog_down  = load_shader_program("fullscreen.vert", "kawase_down.frag", impl_->is_gles);
    impl_->prog_up    = load_shader_program("fullscreen.vert", "kawase_up.frag", impl_->is_gles);
    impl_->prog_glass = load_shader_program("fullscreen.vert", "liquid_glass.frag", impl_->is_gles);
    impl_->prog_ui    = load_shader_program("fullscreen.vert", "composite_ui.frag", impl_->is_gles);
    if (!impl_->prog_blit || !impl_->prog_down || !impl_->prog_up ||
        !impl_->prog_glass || !impl_->prog_ui) {
        shutdown();
        return false;
    }

    static const float kQuadVerts[] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
        -1.0f,  1.0f,
         1.0f,  1.0f,
    };
    glGenVertexArrays(1, &impl_->quad_vao);
    glBindVertexArray(impl_->quad_vao);
    glGenBuffers(1, &impl_->quad_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, impl_->quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuadVerts), kQuadVerts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);

    glGenTextures(1, &impl_->desktop_tex);
    glBindTexture(GL_TEXTURE_2D, impl_->desktop_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    available_ = true;
    has_wl_egl_ = (wl_dpy != nullptr);
    log::info("glass", std::string("Mizzle Dual-Kawase Liquid Glass renderer initialized (") +
                       (has_wl_egl_ ? "wayland-egl" : "offscreen") +
                       (impl_->is_gles ? ", GLES3)" : ", GL3.3)"));
    return true;
#else
    (void)wl_dpy;
    return false;
#endif
}

void MizzleRenderer::shutdown() {
#if MISTBAR_HAS_OPENGL
    if (impl_->egl_dpy != EGL_NO_DISPLAY) {
        if (impl_->egl_ctx != EGL_NO_CONTEXT) {
            impl_->make_offscreen_current();
            for (auto& [wl_s, slot] : impl_->surfaces) {
                (void)wl_s;
                if (slot.ui_tex) glDeleteTextures(1, &slot.ui_tex);
                if (slot.egl_surf != EGL_NO_SURFACE) eglDestroySurface(impl_->egl_dpy, slot.egl_surf);
                if (slot.egl_win) wl_egl_window_destroy(slot.egl_win);
            }
            impl_->surfaces.clear();

            impl_->destroy_rt(impl_->backdrop_rt);
            impl_->destroy_rt(impl_->offscreen_rt);
            for (auto& rt : impl_->down_rts) impl_->destroy_rt(rt);
            for (auto& rt : impl_->up_rts) impl_->destroy_rt(rt);

            if (impl_->offscreen_ui_tex) { glDeleteTextures(1, &impl_->offscreen_ui_tex); impl_->offscreen_ui_tex = 0; }
            if (impl_->desktop_tex) { glDeleteTextures(1, &impl_->desktop_tex); impl_->desktop_tex = 0; }
            if (impl_->quad_vbo) { glDeleteBuffers(1, &impl_->quad_vbo); impl_->quad_vbo = 0; }
            if (impl_->quad_vao) { glDeleteVertexArrays(1, &impl_->quad_vao); impl_->quad_vao = 0; }

            if (impl_->prog_blit)  { glDeleteProgram(impl_->prog_blit);  impl_->prog_blit = 0; }
            if (impl_->prog_down)  { glDeleteProgram(impl_->prog_down);  impl_->prog_down = 0; }
            if (impl_->prog_up)    { glDeleteProgram(impl_->prog_up);    impl_->prog_up = 0; }
            if (impl_->prog_glass) { glDeleteProgram(impl_->prog_glass); impl_->prog_glass = 0; }
            if (impl_->prog_ui)    { glDeleteProgram(impl_->prog_ui);    impl_->prog_ui = 0; }

            eglMakeCurrent(impl_->egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            eglDestroyContext(impl_->egl_dpy, impl_->egl_ctx);
            impl_->egl_ctx = EGL_NO_CONTEXT;
        }
        if (impl_->pbuffer_surf != EGL_NO_SURFACE) {
            eglDestroySurface(impl_->egl_dpy, impl_->pbuffer_surf);
            impl_->pbuffer_surf = EGL_NO_SURFACE;
        }
        eglTerminate(impl_->egl_dpy);
        impl_->egl_dpy = EGL_NO_DISPLAY;
    }
    impl_->egl_cfg = nullptr;
    impl_->pyramid_dirty = true;
    impl_->last_bd_w = 0;
    impl_->last_bd_h = 0;
    impl_->offscreen_ui_w = 0;
    impl_->offscreen_ui_h = 0;
#endif
    available_ = false;
    has_wl_egl_ = false;
    backdrop_w_ = 0;
    backdrop_h_ = 0;
}

void MizzleRenderer::set_backdrop_bgra(const std::uint8_t* bgra, int width, int height, int stride_bytes) {
#if MISTBAR_HAS_OPENGL
    if (!available_ || !bgra || width <= 0 || height <= 0 || stride_bytes < width * 4) return;
    if (!impl_->make_offscreen_current()) return;

    glBindTexture(GL_TEXTURE_2D, impl_->desktop_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, stride_bytes / 4);
    if (backdrop_w_ != width || backdrop_h_ != height) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, bgra);
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, bgra);
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    backdrop_w_ = width;
    backdrop_h_ = height;
    impl_->pyramid_dirty = true;
#else
    (void)bgra; (void)width; (void)height; (void)stride_bytes;
#endif
}

void MizzleRenderer::set_backdrop_cairo(cairo_surface_t* surf) {
    if (!surf) return;
    cairo_surface_flush(surf);
    set_backdrop_bgra(cairo_image_surface_get_data(surf),
                      cairo_image_surface_get_width(surf),
                      cairo_image_surface_get_height(surf),
                      cairo_image_surface_get_stride(surf));
}

bool MizzleRenderer::register_surface(wl_surface* surf, int px_w, int px_h) {
#if MISTBAR_HAS_OPENGL
    if (!available_ || !has_wl_egl_ || !surf) return false;
    if (impl_->surfaces.count(surf)) {
        resize_surface(surf, px_w, px_h);
        return true;
    }
    const int w = std::max(1, px_w);
    const int h = std::max(1, px_h);
    wl_egl_window* win = wl_egl_window_create(surf, w, h);
    if (!win) return false;

    EGLSurface es = eglCreateWindowSurface(impl_->egl_dpy, impl_->egl_cfg,
                                           reinterpret_cast<EGLNativeWindowType>(win), nullptr);
    if (es == EGL_NO_SURFACE) {
        wl_egl_window_destroy(win);
        return false;
    }

    eglMakeCurrent(impl_->egl_dpy, es, es, impl_->egl_ctx);
    eglSwapInterval(impl_->egl_dpy, 0);

    Impl::SurfaceSlot slot;
    slot.egl_win = win;
    slot.egl_surf = es;
    slot.win_w = w;
    slot.win_h = h;
    impl_->surfaces.emplace(surf, slot);
    return true;
#else
    (void)surf; (void)px_w; (void)px_h;
    return false;
#endif
}

void MizzleRenderer::resize_surface(wl_surface* surf, int px_w, int px_h) {
#if MISTBAR_HAS_OPENGL
    auto it = impl_->surfaces.find(surf);
    if (it == impl_->surfaces.end()) return;
    const int w = std::max(1, px_w);
    const int h = std::max(1, px_h);
    if (it->second.win_w != w || it->second.win_h != h) {
        wl_egl_window_resize(it->second.egl_win, w, h, 0, 0);
        it->second.win_w = w;
        it->second.win_h = h;
    }
#else
    (void)surf; (void)px_w; (void)px_h;
#endif
}

void MizzleRenderer::unregister_surface(wl_surface* surf) {
#if MISTBAR_HAS_OPENGL
    auto it = impl_->surfaces.find(surf);
    if (it == impl_->surfaces.end()) return;
    impl_->make_offscreen_current();
    if (it->second.ui_tex) glDeleteTextures(1, &it->second.ui_tex);
    if (it->second.egl_surf != EGL_NO_SURFACE) eglDestroySurface(impl_->egl_dpy, it->second.egl_surf);
    if (it->second.egl_win) wl_egl_window_destroy(it->second.egl_win);
    impl_->surfaces.erase(it);
#else
    (void)surf;
#endif
}

bool MizzleRenderer::render_surface(wl_surface* surf,
                                    int surf_px_w,
                                    int surf_px_h,
                                    float origin_px_x,
                                    float origin_px_y,
                                    float time_sec,
                                    const ClickPulse& click,
                                    const GlassMaterial& material,
                                    const std::vector<GlassPanelSpec>& panels,
                                    cairo_surface_t* ui_overlay,
                                    bool translucent_glass) {
#if MISTBAR_HAS_OPENGL
    if (!available_ || !has_wl_egl_ || !surf || surf_px_w <= 0 || surf_px_h <= 0) return false;
    auto it = impl_->surfaces.find(surf);
    if (it == impl_->surfaces.end()) {
        if (!register_surface(surf, surf_px_w, surf_px_h)) return false;
        it = impl_->surfaces.find(surf);
    }
    auto& slot = it->second;
    if (slot.win_w != surf_px_w || slot.win_h != surf_px_h) {
        wl_egl_window_resize(slot.egl_win, surf_px_w, surf_px_h, 0, 0);
        slot.win_w = surf_px_w;
        slot.win_h = surf_px_h;
    }

    if (!eglMakeCurrent(impl_->egl_dpy, slot.egl_surf, slot.egl_surf, impl_->egl_ctx)) {
        return false;
    }

    if (backdrop_w_ > 0 && backdrop_h_ > 0) {
        impl_->ensure_pyramid(backdrop_w_, backdrop_h_, material.blurScale);
    } else {
        impl_->ensure_fallback_backdrop(material.blurScale);
    }
    if (ui_overlay) {
        impl_->upload_ui_texture(slot.ui_tex, slot.ui_w, slot.ui_h, ui_overlay);
    }

    impl_->draw_panels_and_ui(0, surf_px_w, surf_px_h,
                              std::max(1, backdrop_w_), std::max(1, backdrop_h_),
                              origin_px_x, origin_px_y,
                              time_sec, click, material, panels,
                              slot.ui_tex, slot.ui_w, slot.ui_h,
                              translucent_glass);

    return eglSwapBuffers(impl_->egl_dpy, slot.egl_surf) == EGL_TRUE;
#else
    (void)surf; (void)surf_px_w; (void)surf_px_h; (void)origin_px_x; (void)origin_px_y;
    (void)time_sec; (void)click; (void)material; (void)panels; (void)ui_overlay;
    (void)translucent_glass;
    return false;
#endif
}

bool MizzleRenderer::render_to_cairo(cairo_surface_t* dst,
                                     float origin_px_x,
                                     float origin_px_y,
                                     float time_sec,
                                     const ClickPulse& click,
                                     const GlassMaterial& material,
                                     const std::vector<GlassPanelSpec>& panels,
                                     cairo_surface_t* ui_overlay,
                                     bool translucent_glass) {
#if MISTBAR_HAS_OPENGL
    if (!available_ || !dst) return false;
    const int pw = cairo_image_surface_get_width(dst);
    const int ph = cairo_image_surface_get_height(dst);
    if (pw <= 0 || ph <= 0) return false;

    if (!impl_->make_offscreen_current()) return false;

    impl_->alloc_rt(impl_->offscreen_rt, pw, ph);
    if (backdrop_w_ > 0 && backdrop_h_ > 0) {
        impl_->ensure_pyramid(backdrop_w_, backdrop_h_, material.blurScale);
    } else {
        impl_->ensure_fallback_backdrop(material.blurScale);
    }
    if (ui_overlay) {
        impl_->upload_ui_texture(impl_->offscreen_ui_tex, impl_->offscreen_ui_w, impl_->offscreen_ui_h, ui_overlay);
    }

    impl_->draw_panels_and_ui(impl_->offscreen_rt.fbo, pw, ph,
                              std::max(1, backdrop_w_), std::max(1, backdrop_h_),
                              origin_px_x, origin_px_y,
                              time_sec, click, material, panels,
                              ui_overlay ? impl_->offscreen_ui_tex : 0,
                              ui_overlay ? impl_->offscreen_ui_w : 0,
                              ui_overlay ? impl_->offscreen_ui_h : 0,
                              translucent_glass);

    std::vector<std::uint8_t> px(static_cast<std::size_t>(pw) * static_cast<std::size_t>(ph) * 4U);
    glBindFramebuffer(GL_FRAMEBUFFER, impl_->offscreen_rt.fbo);
    glReadPixels(0, 0, pw, ph, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    cairo_surface_flush(dst);
    auto* ddata = cairo_image_surface_get_data(dst);
    const int dstride = cairo_image_surface_get_stride(dst);
    for (int y = 0; y < ph; ++y) {
        const std::uint8_t* src_row = px.data() + static_cast<std::size_t>(ph - 1 - y) * static_cast<std::size_t>(pw) * 4U;
        std::uint8_t* dst_row = ddata + y * dstride;
        for (int x = 0; x < pw; ++x) {
            dst_row[x * 4 + 0] = src_row[x * 4 + 2]; // B
            dst_row[x * 4 + 1] = src_row[x * 4 + 1]; // G
            dst_row[x * 4 + 2] = src_row[x * 4 + 0]; // R
            dst_row[x * 4 + 3] = src_row[x * 4 + 3]; // A
        }
    }
    cairo_surface_mark_dirty(dst);
    return true;
#else
    (void)dst; (void)origin_px_x; (void)origin_px_y; (void)time_sec;
    (void)click; (void)material; (void)panels; (void)ui_overlay; (void)translucent_glass;
    return false;
#endif
}

GlassMaterial make_glass_material(const MaterialConfigInput& in) {
    GlassMaterial m;
    if (in.glass_preset == "solid") {
        m.refraction  = 0.0f;
        m.dispersion  = 0.0f;
        m.frost       = 0.0f;
        m.specular    = 0.30f;
        m.tintR       = 0.04f;
        m.tintG       = 0.04f;
        m.tintB       = 0.06f;
        m.tintAlpha   = 0.96f;
        m.shadowAlpha = 0.24f;
        m.blurScale   = 1.0f;
        return m;
    }

    m.refraction = std::clamp(in.refraction_strength * 2.0f, 0.0f, 2.0f);
    m.dispersion = std::clamp(in.chromatic_aberration * 2.5f, 0.0f, 2.5f);
    m.frost      = std::clamp(static_cast<float>(in.blur_radius) / 48.0f, 0.0f, 1.0f);
    m.specular   = std::clamp(in.specular_intensity * 2.0f, 0.0f, 2.0f);
    m.tintAlpha  = std::clamp(in.tint_opacity, 0.0f, 0.65f);
    m.blurScale  = 1.05f + m.frost * 1.10f;

    if (in.glass_preset == "smoked" || in.glass_preset == "dark") {
        m.tintR       = 0.09f;
        m.tintG       = 0.12f;
        m.tintB       = 0.18f;
        m.shadowAlpha = 0.22f;
    } else if (in.glass_preset == "aqua") {
        m.tintR       = 0.20f;
        m.tintG       = 0.56f;
        m.tintB       = 0.96f;
        m.shadowAlpha = 0.15f;
    } else if (in.glass_preset == "frosted") {
        m.tintR       = 0.95f;
        m.tintG       = 0.97f;
        m.tintB       = 1.00f;
        m.shadowAlpha = 0.16f;
    } else {
        // "clear" / "regular"
        m.tintR       = 0.92f;
        m.tintG       = 0.96f;
        m.tintB       = 1.00f;
        m.shadowAlpha = 0.14f;
    }
    return m;
}

} // namespace mistbar::glass
