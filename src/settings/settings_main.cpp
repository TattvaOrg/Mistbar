/* Mistbar - MIT License */

#include "common/config.h"
#include "common/defaults.h"
#include "common/ipc.h"
#include "common/logger.h"
#include "common/sysinfo.h"
#include "common/wayland_client.h"
#include "glass/blur/gaussian_blur.h"
#include "glass/specular/specular.h"
#include "glass/translucent/glass_panel.h"
#include "themes/theme.h"
#include "window/buttons/traffic_lights.h"
#include "window/shadows/window_shadow.h"
#include "window/titlebar/window_frame.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <functional>
#include <linux/input-event-codes.h>
#include <poll.h>
#include <string>
#include <tuple>
#include <unistd.h>
#include <vector>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace fs = std::filesystem;

namespace {

volatile sig_atomic_t g_stop = 0;

void handle_sig(int) {
    g_stop = 1;
}

std::string resolve_sibling_binary(const std::string& name) {
    std::error_code ec;
    char buf[4096]{};
    const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) {
        const fs::path p = fs::path(std::string(buf, static_cast<std::size_t>(n))).parent_path() / name;
        if (fs::exists(p, ec)) return p.string();
    }
    return name;
}

std::string to_lower_copy(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

void draw_tonal_wallpaper(cairo_t* cr, double pw, double ph, bool dark) {
    cairo_pattern_t* base = cairo_pattern_create_linear(0.0, 0.0, pw, ph);
    if (dark) {
        cairo_pattern_add_color_stop_rgb(base, 0.0, 0.07, 0.11, 0.20);
        cairo_pattern_add_color_stop_rgb(base, 0.55, 0.10, 0.16, 0.28);
        cairo_pattern_add_color_stop_rgb(base, 1.0, 0.06, 0.08, 0.14);
    } else {
        cairo_pattern_add_color_stop_rgb(base, 0.0, 0.62, 0.76, 0.92);
        cairo_pattern_add_color_stop_rgb(base, 0.55, 0.74, 0.84, 0.95);
        cairo_pattern_add_color_stop_rgb(base, 1.0, 0.56, 0.68, 0.86);
    }
    cairo_set_source(cr, base);
    cairo_paint(cr);
    cairo_pattern_destroy(base);

    cairo_pattern_t* bloom = cairo_pattern_create_radial(
        pw * 0.62, ph * 0.42, 8.0, pw * 0.62, ph * 0.42, std::max(pw, ph) * 0.65);
    if (dark) {
        cairo_pattern_add_color_stop_rgba(bloom, 0.0, 0.22, 0.46, 0.78, 0.44);
        cairo_pattern_add_color_stop_rgba(bloom, 1.0, 0.00, 0.00, 0.00, 0.0);
    } else {
        cairo_pattern_add_color_stop_rgba(bloom, 0.0, 0.98, 0.99, 1.00, 0.52);
        cairo_pattern_add_color_stop_rgba(bloom, 1.0, 0.00, 0.00, 0.00, 0.0);
    }
    cairo_set_source(cr, bloom);
    cairo_paint(cr);
    cairo_pattern_destroy(bloom);

    // Soft organic coastal ridge bands for glass refraction visibility without wireframe grids
    cairo_set_source_rgba(cr, dark ? 0.04 : 0.36, dark ? 0.06 : 0.48, dark ? 0.11 : 0.66, 0.42);
    cairo_move_to(cr, 0.0, ph * 0.74);
    cairo_curve_to(cr, pw * 0.28, ph * 0.50, pw * 0.58, ph * 0.72, pw, ph * 0.46);
    cairo_line_to(cr, pw, ph);
    cairo_line_to(cr, 0.0, ph);
    cairo_close_path(cr);
    cairo_fill(cr);
}

class MistbarSettingsApp {
public:
    MistbarSettingsApp(mistbar::wl::Display& dpy, mistbar::config::MistbarConfig cfg, int initial_cat)
        : dpy_(dpy), win_(dpy), cfg_(std::move(cfg)), active_cat_(std::clamp(initial_cat, 0, 9)) {
        categories_ = {
            {"Appearance",     "categories/appearance.svg",   "#3A3A3C", 0},
            {"Liquid Glass",   "categories/liquid-glass.svg", "#5E5CE6", 0},
            {"Menu Bar",       "categories/menubar.svg",      "#636366", 0},
            {"Control Center", "control-center.svg",          "#636366", 0},
            {"Wi-Fi",          "categories/wifi.svg",         "#0A84FF", 1},
            {"Bluetooth",      "categories/bluetooth.svg",    "#0A84FF", 1},
            {"Sound",          "categories/sound.svg",        "#FF375F", 1},
            {"Display",        "categories/display.svg",      "#0A84FF", 1},
            {"Energy",         "battery-charging.svg",        "#30D158", 1},
            {"About",          "categories/about.svg",        "#636366", 2}
        };
    }

    ~MistbarSettingsApp() {
        if (preview_raw_) cairo_surface_destroy(preview_raw_);
        if (preview_blurred_) cairo_surface_destroy(preview_blurred_);
    }

    bool init() {
        refresh_hw();
        win_.set_dim_min_max(true);
        win_.set_content_draw([this](cairo_t* cr, int w, int h) { draw(cr, w, h); });
        win_.set_content_pointer([this](const mistbar::wl::PointerEvent& ev) { handle_pointer(ev); });
        win_.set_content_key([this](const mistbar::wl::KeyEvent& ev) { handle_key(ev); });
        const std::string title = categories_[static_cast<std::size_t>(active_cat_)].name;
        return win_.create(title, "mistbar-settings", 860, 650, cfg_, 18, true);
    }

    bool is_open() const noexcept { return win_.is_open(); }

    void refresh_hw() {
        if (active_cat_ == 4) wifi_ = mistbar::sysinfo::get_wifi_status(true);
        else if (active_cat_ == 5) bt_ = mistbar::sysinfo::get_bluetooth_status(true);
        else if (active_cat_ == 6) audio_ = mistbar::sysinfo::get_audio_status(true);
        else if (active_cat_ == 7) brightness_ = mistbar::sysinfo::get_brightness_status();
        else if (active_cat_ == 8) battery_ = mistbar::sysinfo::get_battery_status(true);
        std::string st;
        daemon_running_ = mistbar::ipc::send_command(mistbar::ipc::default_socket_path(), "status", &st);
        daemon_status_ = daemon_running_ ? st : "Not Running";
    }

    void request_redraw() { win_.request_redraw(); }

    void render_offscreen_png(const std::string& out_path) {
        const int w = 860;
        const int h = 650;
        const int m = 18;
        win_.apply_config(cfg_);
        wifi_ = {true, true, "Studio-5G", {}};
        bt_ = {true, true, "AirPods Pro", {}};
        battery_ = {true, true, 92, "Charging", "balanced"};
        audio_ = {72, false, {}};
        brightness_ = {true, 85};
        daemon_running_ = true;
        daemon_status_ = "Active";

        cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w + m * 2, h + m * 2);
        cairo_t* cr = cairo_create(surf);

        const auto& pal = win_.palette();
        mistbar::window::draw_window_shadow(cr, m, m, w, h, pal.window_radius, 24, true);
        cairo_save(cr);
        mistbar::glass::append_rounded_rect(cr, m, m, w, h, pal.window_radius);
        cairo_clip(cr);
        cairo_set_source_rgba(cr, pal.surface_bg.r, pal.surface_bg.g, pal.surface_bg.b, pal.surface_bg.a);
        cairo_paint(cr);

        cairo_save(cr);
        cairo_translate(cr, m, m);
        draw(cr, w, h);
        cairo_restore(cr);

        mistbar::window::TrafficLights tl;
        tl.draw(cr, win_.icon_cache(), m + 22.0, m + 22.0, true, true);
        cairo_restore(cr);
        mistbar::glass::draw_glass_specular_and_border(cr, m, m, w, h, pal.window_radius, pal);

        cairo_destroy(cr);
        cairo_surface_write_to_png(surf, out_path.c_str());
        cairo_surface_destroy(surf);
    }

private:
    struct CategoryItem {
        std::string name;
        std::string icon;
        std::string badge_hex;
        int group = 0;
    };

    struct ClickZone {
        double x = 0.0, y = 0.0, w = 0.0, h = 0.0;
        std::string hover_id;
        std::function<void()> on_click;
    };

    struct SliderZone {
        double x = 0.0, y = 0.0, w = 0.0, h = 0.0;
        double min_v = 0.0, max_v = 1.0;
        std::function<void(double)> on_change;
    };

    struct GroupRow {
        std::string title;
        std::string subtitle;
        std::function<void(cairo_t*, double row_x, double row_y, double row_w, double row_h)> render_right;
        std::function<void(double row_x, double row_y, double row_w, double row_h)> register_hit;
        double fixed_height = 0.0;
    };

    mistbar::wl::Display& dpy_;
    mistbar::window::MistbarWindow win_;
    mistbar::config::MistbarConfig cfg_;
    std::vector<CategoryItem> categories_;
    int active_cat_ = 0;
    std::vector<int> back_hist_;
    std::vector<int> fwd_hist_;
    double scroll_y_ = 0.0;
    double max_scroll_y_ = 0.0;

    std::string search_query_;
    bool search_focused_ = false;
    std::string hovered_id_;

    std::vector<ClickZone> clicks_;
    std::vector<SliderZone> sliders_;
    int dragging_slider_idx_ = -1;

    double preview_ptr_x_ = 0.35;
    double preview_ptr_y_ = 0.45;
    double preview_x_ = 0.0, preview_y_ = 0.0, preview_w_ = 0.0, preview_h_ = 0.0;
    cairo_surface_t* preview_raw_ = nullptr;
    cairo_surface_t* preview_blurred_ = nullptr;
    int preview_blur_cached_ = -1;
    bool preview_dark_cached_ = true;
    int preview_w_cached_ = 0;
    int preview_h_cached_ = 0;

    mistbar::sysinfo::WifiStatus wifi_;
    mistbar::sysinfo::BluetoothStatus bt_;
    mistbar::sysinfo::BatteryStatus battery_;
    mistbar::sysinfo::AudioStatus audio_;
    mistbar::sysinfo::BrightnessStatus brightness_;
    bool daemon_running_ = false;
    std::string daemon_status_ = "Checking...";

    void select_category(int idx) {
        if (idx < 0 || idx >= static_cast<int>(categories_.size()) || idx == active_cat_) return;
        back_hist_.push_back(active_cat_);
        fwd_hist_.clear();
        active_cat_ = idx;
        scroll_y_ = 0.0;
        search_focused_ = false;
        win_.set_title(categories_[static_cast<std::size_t>(active_cat_)].name);
        refresh_hw();
        win_.request_redraw();
    }

    void ensure_preview_backdrop(int pw, int ph, int blur_r, bool dark) {
        if (pw <= 0 || ph <= 0) return;
        if (!preview_raw_ || preview_w_cached_ != pw || preview_h_cached_ != ph ||
            preview_dark_cached_ != dark) {
            if (preview_raw_) cairo_surface_destroy(preview_raw_);
            if (preview_blurred_) cairo_surface_destroy(preview_blurred_);
            preview_raw_ = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pw, ph);
            preview_blurred_ = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pw, ph);
            preview_w_cached_ = pw;
            preview_h_cached_ = ph;
            preview_dark_cached_ = dark;
            preview_blur_cached_ = -1;

            cairo_t* pcr = cairo_create(preview_raw_);
            draw_tonal_wallpaper(pcr, static_cast<double>(pw), static_cast<double>(ph), dark);
            cairo_destroy(pcr);
        }
        if (preview_blur_cached_ != blur_r) {
            preview_blur_cached_ = blur_r;
            const int r = std::clamp(blur_r / 2, 0, 24);
            if (!win_.backdrop().using_gpu() || !win_.backdrop().gl_engine().blur(preview_raw_, preview_blurred_, r)) {
                mistbar::glass::cpu_gaussian_blur(preview_raw_, preview_blurred_, r);
            }
        }
    }

    void save_and_reload() {
        mistbar::config::save_config(cfg_);
        win_.apply_config(cfg_);
        mistbar::ipc::send_command(mistbar::ipc::default_socket_path(), "reload");
        win_.request_redraw();
    }

    void draw_macos_switch(cairo_t* cr, double x, double y, bool on, const std::string& hid) {
        const auto& pal = win_.palette();
        const bool hov = (!hid.empty() && hovered_id_ == hid);
        mistbar::glass::append_rounded_rect(cr, x, y, 40.0, 22.0, 11.0);
        if (on) {
            cairo_set_source_rgba(cr, pal.accent.r, pal.accent.g, pal.accent.b, hov ? 1.0 : 0.94);
        } else {
            const double v = pal.dark_mode ? 1.0 : 0.0;
            cairo_set_source_rgba(cr, v, v, v, hov ? 0.24 : 0.16);
        }
        cairo_fill(cr);

        const double kx = on ? (x + 29.0) : (x + 11.0);
        const double ky = y + 11.0;
        cairo_arc(cr, kx, ky + 1.0, 9.0, 0.0, 6.283185);
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.22);
        cairo_fill(cr);

        cairo_arc(cr, kx, ky, 8.8, 0.0, 6.283185);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.99);
        cairo_fill(cr);
    }

    void draw_segmented_control(cairo_t* cr,
                                double x, double y, double w, double h,
                                const std::vector<std::string>& labels,
                                int active_idx,
                                const std::string& id_prefix,
                                std::function<void(int)> on_pick,
                                double body_top, double body_bottom) {
        const auto& pal = win_.palette();
        mistbar::glass::append_rounded_rect(cr, x, y, w, h, 8.0);
        if (pal.dark_mode) {
            cairo_set_source_rgba(cr, 0.07, 0.07, 0.08, 0.96);
        } else {
            cairo_set_source_rgba(cr, 0.89, 0.89, 0.91, 0.98);
        }
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, pal.border_outer.r, pal.border_outer.g, pal.border_outer.b, pal.dark_mode ? 0.12 : 0.08);
        cairo_set_line_width(cr, 0.8);
        cairo_stroke(cr);

        const int n = static_cast<int>(labels.size());
        if (n <= 0) return;
        const double seg_w = (w - 4.0) / static_cast<double>(n);
        for (int i = 0; i < n; ++i) {
            const double sx = x + 2.0 + static_cast<double>(i) * seg_w;
            const double sy = y + 2.0;
            const double sh = h - 4.0;
            const std::string hid = id_prefix + ":" + std::to_string(i);
            const bool hov = (hovered_id_ == hid);
            if (i == active_idx) {
                mistbar::glass::append_rounded_rect(cr, sx, sy, seg_w, sh, 6.5);
                if (pal.dark_mode) {
                    cairo_set_source_rgba(cr, 0.24, 0.24, 0.27, 0.99);
                } else {
                    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.99);
                }
                cairo_fill_preserve(cr);
                cairo_set_source_rgba(cr, pal.border_outer.r, pal.border_outer.g, pal.border_outer.b,
                                      pal.dark_mode ? 0.18 : 0.10);
                cairo_set_line_width(cr, 0.8);
                cairo_stroke(cr);
            } else if (hov) {
                mistbar::glass::append_rounded_rect(cr, sx, sy, seg_w, sh, 6.5);
                const double hv = pal.dark_mode ? 1.0 : 0.0;
                cairo_set_source_rgba(cr, hv, hv, hv, 0.06);
                cairo_fill(cr);
            }
            mistbar::window::draw_pango_text_centered(
                cr, labels[static_cast<std::size_t>(i)], sx, sy, seg_w, sh,
                8.8, i == active_idx, pal.text_primary, true, false);

            if (sy + sh > body_top && sy < body_bottom) {
                clicks_.push_back({sx, std::max(body_top, sy), seg_w, sh, hid, [on_pick, i]() { on_pick(i); }});
            }
        }
    }

    void draw_pill_button(cairo_t* cr, double x, double y, double w, double h,
                          const std::string& label, bool primary, bool destructive,
                          const std::string& hid, std::function<void()> on_click,
                          double body_top, double body_bottom) {
        const auto& pal = win_.palette();
        const bool hov = (!hid.empty() && hovered_id_ == hid);
        mistbar::glass::append_rounded_rect(cr, x, y, w, h, 8.0);
        if (primary) {
            cairo_set_source_rgba(cr, pal.accent.r, pal.accent.g, pal.accent.b, hov ? 1.0 : 0.92);
        } else if (destructive) {
            const double bg = pal.dark_mode ? 0.19 : 0.96;
            cairo_set_source_rgba(cr, bg, pal.dark_mode ? 0.13 : 0.91, pal.dark_mode ? 0.13 : 0.91, hov ? 1.0 : 0.94);
        } else {
            const double bg = pal.dark_mode ? (hov ? 0.24 : 0.19) : (hov ? 0.92 : 0.96);
            cairo_set_source_rgba(cr, bg, bg, pal.dark_mode ? bg + 0.02 : bg + 0.01, 0.98);
        }
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, pal.border_outer.r, pal.border_outer.g, pal.border_outer.b,
                              primary ? 0.24 : (pal.dark_mode ? 0.16 : 0.12));
        cairo_set_line_width(cr, 0.85);
        cairo_stroke(cr);

        const mistbar::themes::Color txt_col = primary
            ? mistbar::themes::Color{1.0, 1.0, 1.0, 1.0}
            : (destructive
                   ? (pal.dark_mode ? mistbar::themes::Color{1.0, 0.38, 0.34, 1.0}
                                    : mistbar::themes::Color{0.84, 0.17, 0.13, 1.0})
                   : pal.text_primary);
        mistbar::window::draw_pango_text_centered(
            cr, label, x, y, w, h, 9.0, primary, txt_col, true, false);
        if (y + h > body_top && y < body_bottom) {
            clicks_.push_back({x, std::max(body_top, y), w, h, hid, std::move(on_click)});
        }
    }

    void draw_preview_glass(cairo_t* cr, double cx, double ry,
                            double gx, double gy, double gw, double gh, double gr,
                            const mistbar::themes::Color& fill_col) {
        const auto& pal = win_.palette();
        const int gpw = std::max(1, static_cast<int>(std::ceil(gw)));
        const int gph = std::max(1, static_cast<int>(std::ceil(gh)));
        if (!pal.solid_mode && win_.backdrop().using_gpu() && preview_blurred_) {
            cairo_surface_t* psurf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, gpw, gph);
            mistbar::glass::LiquidGlassParams lp;
            lp.corner_radius = gr;
            lp.refraction_strength = pal.refraction_strength;
            lp.bezel_thickness = pal.bezel_thickness;
            lp.chromatic_aberration = pal.chromatic_aberration;
            lp.specular_intensity = pal.specular_intensity;
            lp.tint_r = fill_col.r;
            lp.tint_g = fill_col.g;
            lp.tint_b = fill_col.b;
            lp.tint_opacity = fill_col.a;
            lp.glass_opacity = pal.glass_opacity;
            lp.pointer_x = preview_ptr_x_;
            lp.pointer_y = preview_ptr_y_;
            lp.cursor_reactive = false;
            lp.dark_mode = pal.dark_mode;
            lp.overlay_only = false;
            if (win_.backdrop().gl_engine().render_liquid_glass(
                    preview_blurred_, static_cast<int>(gx - cx), static_cast<int>(gy - ry), psurf, lp)) {
                cairo_save(cr);
                cairo_set_source_surface(cr, psurf, gx, gy);
                cairo_paint(cr);
                cairo_restore(cr);
                cairo_surface_destroy(psurf);
                return;
            }
            cairo_surface_destroy(psurf);
        }
        mistbar::glass::draw_glass_panel(cr, &win_.backdrop(), gx, gy, gx - cx, gy - ry,
                                         gw, gh, gr, pal, &fill_col, false,
                                         preview_ptr_x_, preview_ptr_y_);
    }

    void draw_theme_thumbnail(cairo_t* cr, double bx, double ty, double tw, double th,
                              bool thumb_dark, bool selected, const std::string& label,
                              const std::string& hid, std::function<void()> on_pick,
                              double body_top, double body_bottom) {
        const auto& pal = win_.palette();
        const bool hov = (hovered_id_ == hid);

        cairo_save(cr);
        mistbar::glass::append_rounded_rect(cr, bx, ty, tw, th, 8.0);
        cairo_clip(cr);
        draw_tonal_wallpaper(cr, bx + tw, ty + th, thumb_dark);

        // Miniature macOS menu bar strip
        cairo_rectangle(cr, bx, ty, tw, 9.0);
        cairo_set_source_rgba(cr, thumb_dark ? 0.12 : 0.94, thumb_dark ? 0.12 : 0.95, thumb_dark ? 0.14 : 0.97, 0.85);
        cairo_fill(cr);

        // Miniature floating window
        mistbar::glass::append_rounded_rect(cr, bx + 12.0, ty + 14.0, tw - 20.0, th - 18.0, 5.0);
        cairo_set_source_rgba(cr, thumb_dark ? 0.15 : 0.97, thumb_dark ? 0.15 : 0.97, thumb_dark ? 0.17 : 0.98, 0.96);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, thumb_dark ? 1.0 : 0.0, thumb_dark ? 1.0 : 0.0, thumb_dark ? 1.0 : 0.0, 0.12);
        cairo_set_line_width(cr, 0.7);
        cairo_stroke(cr);

        const double dots[3][3] = {{1.0, 0.37, 0.34}, {1.0, 0.74, 0.18}, {0.16, 0.78, 0.25}};
        for (int d = 0; d < 3; ++d) {
            cairo_arc(cr, bx + 18.0 + d * 4.5, ty + 20.0, 1.3, 0.0, 6.283185);
            cairo_set_source_rgba(cr, dots[d][0], dots[d][1], dots[d][2], 0.95);
            cairo_fill(cr);
        }
        mistbar::glass::append_rounded_rect(cr, bx + 18.0, ty + 27.0, 26.0, 6.0, 3.0);
        cairo_set_source_rgba(cr, pal.accent.r, pal.accent.g, pal.accent.b, 0.94);
        cairo_fill(cr);
        cairo_restore(cr);

        mistbar::glass::append_rounded_rect(cr, bx, ty, tw, th, 8.0);
        if (selected) {
            cairo_set_source_rgba(cr, pal.accent.r, pal.accent.g, pal.accent.b, 0.98);
            cairo_set_line_width(cr, 2.2);
        } else {
            cairo_set_source_rgba(cr, pal.border_outer.r, pal.border_outer.g, pal.border_outer.b,
                                  hov ? 0.42 : (pal.dark_mode ? 0.22 : 0.16));
            cairo_set_line_width(cr, 1.0);
        }
        cairo_stroke(cr);

        mistbar::window::draw_pango_text_centered(
            cr, label, bx - 4.0, ty + th + 5.0, tw + 8.0, 16.0,
            8.8, selected, selected ? pal.text_primary : pal.text_secondary, true, false);

        if (ty + th + 22.0 > body_top && ty < body_bottom) {
            clicks_.push_back({bx, std::max(body_top, ty), tw, th + 22.0, hid, std::move(on_pick)});
        }
    }

    void draw(cairo_t* cr, int w, int h) {
        clicks_.clear();
        sliders_.clear();
        const auto& pal = win_.palette();
        const bool dark = pal.dark_mode;

        // Main content surface fill
        cairo_set_source_rgba(cr, pal.surface_bg.r, pal.surface_bg.g, pal.surface_bg.b, 1.0);
        cairo_paint(cr);

        // Left Sidebar (macOS System Settings split view)
        const double sb_w = 228.0;
        cairo_rectangle(cr, 0.0, 0.0, sb_w, static_cast<double>(h));
        cairo_set_source_rgba(cr, pal.sidebar_bg.r, pal.sidebar_bg.g, pal.sidebar_bg.b, 1.0);
        cairo_fill(cr);

        cairo_rectangle(cr, sb_w, 0.0, 1.0, static_cast<double>(h));
        cairo_set_source_rgba(cr, pal.border_outer.r, pal.border_outer.g, pal.border_outer.b,
                              dark ? 0.09 : 0.10);
        cairo_fill(cr);

        // Sidebar Search Pill (clean, directly below traffic lights)
        const double search_x = 14.0;
        const double search_y = 50.0;
        const double search_w = sb_w - 28.0;
        const double search_h = 28.0;
        mistbar::glass::append_rounded_rect(cr, search_x, search_y, search_w, search_h, 8.0);
        if (dark) {
            cairo_set_source_rgba(cr, 0.16, 0.16, 0.18, 1.0);
        } else {
            cairo_set_source_rgba(cr, 0.85, 0.85, 0.88, 1.0);
        }
        cairo_fill_preserve(cr);
        if (search_focused_) {
            cairo_set_source_rgba(cr, pal.accent.r, pal.accent.g, pal.accent.b, 0.92);
            cairo_set_line_width(cr, 1.6);
        } else {
            cairo_set_source_rgba(cr, pal.border_outer.r, pal.border_outer.g, pal.border_outer.b,
                                  dark ? 0.08 : 0.08);
            cairo_set_line_width(cr, 0.8);
        }
        cairo_stroke(cr);

        const std::string srch_svg = mistbar::icons::resolve_icon("search.svg");
        if (!srch_svg.empty()) {
            win_.icon_cache().draw(cr, srch_svg, search_x + 9.0, search_y + 7.5, 13.0, 13.0,
                                   dark ? 0xFFA1A1AAU : 0xFF6E6E73U);
        }
        const std::string search_lbl = search_query_.empty() ? "Search" : search_query_;
        mistbar::window::draw_pango_text_centered(
            cr, search_lbl, search_x + 28.0, search_y, search_w - 36.0, search_h,
            9.2, false, search_query_.empty() ? pal.text_secondary : pal.text_primary);
        clicks_.push_back({search_x, search_y, search_w, search_h, "sb:search", [this]() {
            search_focused_ = true;
            win_.request_redraw();
        }});

        // Grouped Sidebar Categories (no fake account header or repeated brand logos)
        double sy = 92.0;
        int prev_group = 0;
        const std::string q_low = to_lower_copy(search_query_);
        for (int i = 0; i < static_cast<int>(categories_.size()); ++i) {
            const auto& cat = categories_[static_cast<std::size_t>(i)];
            if (!q_low.empty() && to_lower_copy(cat.name).find(q_low) == std::string::npos) {
                continue;
            }
            if (cat.group != prev_group) {
                cairo_rectangle(cr, 16.0, sy + 5.0, sb_w - 32.0, 0.8);
                cairo_set_source_rgba(cr, pal.border_outer.r, pal.border_outer.g, pal.border_outer.b,
                                      dark ? 0.08 : 0.08);
                cairo_fill(cr);
                sy += 14.0;
                prev_group = cat.group;
            }
            const bool sel = (i == active_cat_);
            const std::string hid = "sb:" + std::to_string(i);
            const bool hov = (!sel && hovered_id_ == hid);
            const double rx = 12.0;
            const double rw = sb_w - 24.0;
            if (sel) {
                mistbar::glass::append_rounded_rect(cr, rx, sy, rw, 31.0, 8.0);
                cairo_set_source_rgba(cr, pal.accent.r, pal.accent.g, pal.accent.b, 0.96);
                cairo_fill(cr);
            } else if (hov) {
                mistbar::glass::append_rounded_rect(cr, rx, sy, rw, 31.0, 8.0);
                const double hv = dark ? 1.0 : 0.0;
                cairo_set_source_rgba(cr, hv, hv, hv, 0.06);
                cairo_fill(cr);
            }

            const std::string ic = mistbar::icons::resolve_icon(cat.icon);
            if (cat.icon.rfind("categories/", 0) == 0) {
                if (!ic.empty()) {
                    win_.icon_cache().draw(cr, ic, rx + 7.0, sy + 5.0, 21.0, 21.0, 0);
                }
            } else {
                const auto badge_col = mistbar::themes::Color::from_hex(cat.badge_hex);
                mistbar::glass::append_rounded_rect(cr, rx + 7.0, sy + 5.0, 21.0, 21.0, 5.5);
                cairo_set_source_rgba(cr, badge_col.r, badge_col.g, badge_col.b, 0.98);
                cairo_fill(cr);
                if (!ic.empty()) {
                    win_.icon_cache().draw(cr, ic, rx + 11.0, sy + 9.0, 13.0, 13.0, 0xFFFFFFFFU);
                }
            }
            const mistbar::themes::Color tc = sel ? mistbar::themes::Color{1, 1, 1, 1} : pal.text_primary;
            mistbar::window::draw_pango_text_centered(
                cr, cat.name, rx + 35.0, sy, rw - 42.0, 31.0, 9.6, sel, tc);
            clicks_.push_back({rx, sy, rw, 31.0, hid, [this, i]() {
                select_category(i);
            }});
            sy += 34.0;
        }

        // Right Content Header & Pane
        const double cx = sb_w + 26.0;
        const double cw = static_cast<double>(w) - cx - 26.0;
        const double header_h = 56.0;

        // Segmented < | > history buttons
        const double pill_x = cx;
        const double pill_y = 14.0;
        const double pill_w = 64.0;
        const double pill_h = 28.0;
        mistbar::glass::append_rounded_rect(cr, pill_x, pill_y, pill_w, pill_h, 14.0);
        if (dark) {
            cairo_set_source_rgba(cr, 0.16, 0.16, 0.18, 1.0);
        } else {
            cairo_set_source_rgba(cr, 0.92, 0.92, 0.94, 1.0);
        }
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, pal.border_outer.r, pal.border_outer.g, pal.border_outer.b,
                              dark ? 0.14 : 0.10);
        cairo_set_line_width(cr, 0.9);
        cairo_stroke(cr);

        cairo_rectangle(cr, pill_x + pill_w * 0.5 - 0.5, pill_y + 6.0, 1.0, pill_h - 12.0);
        cairo_set_source_rgba(cr, pal.border_outer.r, pal.border_outer.g, pal.border_outer.b,
                              dark ? 0.14 : 0.10);
        cairo_fill(cr);

        auto draw_nav_chevron = [&](double bx, bool is_back, bool enabled, bool hov) {
            if (enabled && hov) {
                const double hx = is_back ? (pill_x + 2.0) : (pill_x + pill_w * 0.5 + 1.0);
                mistbar::glass::append_rounded_rect(cr, hx, pill_y + 2.0, pill_w * 0.5 - 3.0, pill_h - 4.0, 11.0);
                const double hv = dark ? 1.0 : 0.0;
                cairo_set_source_rgba(cr, hv, hv, hv, 0.08);
                cairo_fill(cr);
            }
            cairo_new_path(cr);
            const double mx = bx + (is_back ? -1.0 : 1.0);
            const double my = pill_y + pill_h * 0.5;
            const double dir = is_back ? 3.8 : -3.8;
            cairo_move_to(cr, mx + dir, my - 4.4);
            cairo_line_to(cr, mx - dir * 0.3, my);
            cairo_line_to(cr, mx + dir, my + 4.4);
            cairo_set_source_rgba(cr, pal.text_primary.r, pal.text_primary.g, pal.text_primary.b,
                                  enabled ? 0.92 : 0.28);
            cairo_set_line_width(cr, 1.55);
            cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
            cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
            cairo_stroke(cr);
        };
        draw_nav_chevron(pill_x + 16.0, true, !back_hist_.empty(), hovered_id_ == "nav:back");
        draw_nav_chevron(pill_x + 48.0, false, !fwd_hist_.empty(), hovered_id_ == "nav:fwd");

        if (!back_hist_.empty()) {
            clicks_.push_back({pill_x, pill_y, pill_w * 0.5, pill_h, "nav:back", [this]() {
                fwd_hist_.push_back(active_cat_);
                active_cat_ = back_hist_.back();
                back_hist_.pop_back();
                scroll_y_ = 0.0;
                win_.set_title(categories_[static_cast<std::size_t>(active_cat_)].name);
                refresh_hw();
                win_.request_redraw();
            }});
        }
        if (!fwd_hist_.empty()) {
            clicks_.push_back({pill_x + pill_w * 0.5, pill_y, pill_w * 0.5, pill_h, "nav:fwd", [this]() {
                back_hist_.push_back(active_cat_);
                active_cat_ = fwd_hist_.back();
                fwd_hist_.pop_back();
                scroll_y_ = 0.0;
                win_.set_title(categories_[static_cast<std::size_t>(active_cat_)].name);
                refresh_hw();
                win_.request_redraw();
            }});
        }

        mistbar::window::draw_pango_text_centered(
            cr, categories_[static_cast<std::size_t>(active_cat_)].name,
            cx + 78.0, 10.0, cw - 78.0, 36.0, 13.5, true, pal.text_primary);

        const double body_top = header_h;
        const double body_bottom = static_cast<double>(h) - 14.0;
        cairo_save(cr);
        cairo_rectangle(cr, sb_w + 2.0, body_top, static_cast<double>(w) - (sb_w + 2.0),
                        body_bottom - body_top);
        cairo_clip(cr);

        double ry = body_top + 4.0 - scroll_y_;

        auto draw_section_label = [&](const std::string& label) {
            mistbar::window::draw_pango_text_centered(
                cr, label, cx + 6.0, ry, cw - 12.0, 22.0, 9.4, true, pal.text_primary);
            ry += 26.0;
        };

        auto draw_grouped_card = [&](const std::vector<GroupRow>& rows) {
            if (rows.empty()) return;
            double total_h = 0.0;
            std::vector<double> heights;
            heights.reserve(rows.size());
            for (const auto& r : rows) {
                const double rh = (r.fixed_height > 0.0) ? r.fixed_height : (r.subtitle.empty() ? 44.0 : 56.0);
                heights.push_back(rh);
                total_h += rh;
            }

            mistbar::glass::append_rounded_rect(cr, cx, ry, cw, total_h, 11.0);
            cairo_set_source_rgba(cr, pal.surface_elevated.r, pal.surface_elevated.g, pal.surface_elevated.b, 1.0);
            cairo_fill_preserve(cr);
            cairo_set_source_rgba(cr, pal.border_outer.r, pal.border_outer.g, pal.border_outer.b,
                                  dark ? 0.09 : 0.08);
            cairo_set_line_width(cr, 1.0);
            cairo_stroke(cr);

            double cur_y = ry;
            for (std::size_t i = 0; i < rows.size(); ++i) {
                const auto& r = rows[i];
                const double rh = heights[i];
                if (!r.title.empty()) {
                    if (r.fixed_height > 60.0) {
                        mistbar::window::draw_pango_text_centered(
                            cr, r.title, cx + 16.0, cur_y + 14.0, 170.0, 20.0, 9.8, false, pal.text_primary);
                    } else if (r.subtitle.empty()) {
                        mistbar::window::draw_pango_text_centered(
                            cr, r.title, cx + 16.0, cur_y, cw - 180.0, rh, 9.8, false, pal.text_primary);
                    } else {
                        mistbar::window::draw_pango_text_centered(
                            cr, r.title, cx + 16.0, cur_y + 8.0, cw - 180.0, 20.0, 9.8, false, pal.text_primary);
                        mistbar::window::draw_pango_text_centered(
                            cr, r.subtitle, cx + 16.0, cur_y + 29.0, cw - 180.0, 18.0, 8.4, false, pal.text_secondary);
                    }
                }
                if (r.render_right) r.render_right(cr, cx, cur_y, cw, rh);
                if (r.register_hit && cur_y + rh > body_top && cur_y < body_bottom) {
                    r.register_hit(cx, std::max(body_top, cur_y), cw, rh);
                }
                if (i + 1 < rows.size()) {
                    cairo_rectangle(cr, cx + 16.0, cur_y + rh - 0.5, cw - 32.0, 0.8);
                    cairo_set_source_rgba(cr, pal.border_outer.r, pal.border_outer.g, pal.border_outer.b,
                                          dark ? 0.08 : 0.08);
                    cairo_fill(cr);
                }
                cur_y += rh;
            }
            ry += total_h + 16.0;
        };

        auto make_toggle_row = [&](const std::string& title, const std::string& sub, bool val,
                                   const std::string& hid, std::function<void()> cb) -> GroupRow {
            return {
                title, sub,
                [this, val, hid](cairo_t* c, double rx, double rry, double rw, double rh) {
                    draw_macos_switch(c, rx + rw - 56.0, rry + (rh - 22.0) * 0.5, val, hid);
                },
                [this, hid, cb](double rx, double rry, double rw, double rh) {
                    clicks_.push_back({rx, rry, rw, rh, hid, cb});
                },
                0.0
            };
        };

        auto make_action_row = [&](const std::string& title, const std::string& sub, const std::string& btn,
                                   bool primary, bool destructive, const std::string& hid,
                                   std::function<void()> cb) -> GroupRow {
            return {
                title, sub,
                [this, btn, primary, destructive, hid, cb, body_top, body_bottom](
                    cairo_t* c, double rx, double rry, double rw, double rh) {
                    const double bw = std::max(96.0, mistbar::window::measure_pango_text_width(c, btn, 9.0, primary) + 28.0);
                    draw_pill_button(c, rx + rw - 16.0 - bw, rry + (rh - 28.0) * 0.5, bw, 28.0,
                                     btn, primary, destructive, hid, cb, body_top, body_bottom);
                },
                nullptr,
                0.0
            };
        };

        auto draw_slider_group = [&](const std::vector<std::tuple<std::string, std::string, double, double, double, std::function<void(double)>>>& items) {
            const double row_h = 56.0;
            const double total_h = static_cast<double>(items.size()) * row_h;
            mistbar::glass::append_rounded_rect(cr, cx, ry, cw, total_h, 11.0);
            cairo_set_source_rgba(cr, pal.surface_elevated.r, pal.surface_elevated.g, pal.surface_elevated.b, 1.0);
            cairo_fill_preserve(cr);
            cairo_set_source_rgba(cr, pal.border_outer.r, pal.border_outer.g, pal.border_outer.b,
                                  dark ? 0.09 : 0.08);
            cairo_set_line_width(cr, 1.0);
            cairo_stroke(cr);

            double cur_y = ry;
            for (std::size_t i = 0; i < items.size(); ++i) {
                const auto& [lbl, val_txt, norm, min_v, max_v, on_ch] = items[i];
                mistbar::window::draw_pango_text_centered(
                    cr, lbl, cx + 16.0, cur_y + 7.0, cw - 110.0, 20.0, 9.8, false, pal.text_primary);
                mistbar::window::draw_pango_text_centered(
                    cr, val_txt, cx + cw - 94.0, cur_y + 7.0, 78.0, 20.0, 9.0, true, pal.accent, false, true);

                const double tx = cx + 16.0;
                const double ty = cur_y + 36.0;
                const double tw = cw - 32.0;
                const double th = 4.5;
                mistbar::glass::append_rounded_rect(cr, tx, ty, tw, th, 2.25);
                const double tk = dark ? 1.0 : 0.0;
                cairo_set_source_rgba(cr, tk, tk, tk, 0.14);
                cairo_fill(cr);

                const double cl = std::clamp(norm, 0.0, 1.0);
                mistbar::glass::append_rounded_rect(cr, tx, ty, std::max(5.0, cl * tw), th, 2.25);
                cairo_set_source_rgba(cr, pal.accent.r, pal.accent.g, pal.accent.b, 0.96);
                cairo_fill(cr);

                const double kx = tx + cl * tw;
                cairo_arc(cr, kx, ty + 2.25, 8.0, 0.0, 6.283185);
                cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.22);
                cairo_fill(cr);
                cairo_arc(cr, kx, ty + 2.25, 7.2, 0.0, 6.283185);
                cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.99);
                cairo_fill(cr);

                if (cur_y + row_h > body_top && cur_y < body_bottom) {
                    sliders_.push_back({tx - 6.0, std::max(body_top, ty - 12.0), tw + 12.0, 28.0, min_v, max_v, on_ch});
                }
                if (i + 1 < items.size()) {
                    cairo_rectangle(cr, cx + 16.0, cur_y + row_h - 0.5, cw - 32.0, 0.8);
                    cairo_set_source_rgba(cr, pal.border_outer.r, pal.border_outer.g, pal.border_outer.b,
                                          dark ? 0.08 : 0.08);
                    cairo_fill(cr);
                }
                cur_y += row_h;
            }
            ry += total_h + 16.0;
        };

        if (active_cat_ == 0) {
            // Appearance: genuine Light / Dark window thumbnails + Accent swatches + Label contrast
            draw_section_label("Interface Theme");
            draw_grouped_card({
                {"Appearance", "Choose Dark or Light appearance for Settings and Menu Bar",
                 [&](cairo_t* c, double rx, double rry, double rw, double rh) {
                     (void)rh;
                     const double tw = 90.0;
                     const double th = 54.0;
                     const double gap = 14.0;
                     const double start_x = rx + rw - 16.0 - (2.0 * tw + gap);
                     const double ty = rry + 12.0;

                     draw_theme_thumbnail(c, start_x, ty, tw, th, true, cfg_.theme != "light",
                                          "Dark", "th:dark", [this]() {
                         cfg_.theme = "dark";
                         save_and_reload();
                     }, body_top, body_bottom);

                     draw_theme_thumbnail(c, start_x + tw + gap, ty, tw, th, false, cfg_.theme == "light",
                                          "Light", "th:light", [this]() {
                         cfg_.theme = "light";
                         save_and_reload();
                     }, body_top, body_bottom);
                 }, nullptr, 90.0}
            });

            draw_section_label("Color & Contrast");
            struct SwatchInfo { const char* hex; const char* name; };
            const SwatchInfo swatches[8] = {
                {"#0A84FF", "Blue"},
                {"#BF5AF2", "Purple"},
                {"#FF375F", "Pink"},
                {"#FF453A", "Red"},
                {"#FF9F0A", "Orange"},
                {"#FFD60A", "Yellow"},
                {"#30D158", "Green"},
                {"#8E8E93", "Graphite"}
            };
            draw_grouped_card({
                {"Accent Color", "",
                 [&](cairo_t* c, double rx, double rry, double rw, double rh) {
                     (void)rh;
                     const double start_x = rx + rw - 16.0 - 8.0 * 28.0;
                     const double sy_c = rry + 22.0;
                     for (int i = 0; i < 8; ++i) {
                         const double sx = start_x + static_cast<double>(i) * 28.0 + 11.0;
                         const auto col = mistbar::themes::Color::from_hex(swatches[i].hex);
                         const bool sel = (cfg_.accent_color == swatches[i].hex);
                         const std::string shid = "sw:" + std::to_string(i);
                         const bool hov = (hovered_id_ == shid);
                         cairo_arc(c, sx, sy_c, 9.5, 0.0, 6.283185);
                         cairo_set_source_rgba(c, col.r, col.g, col.b, 1.0);
                         cairo_fill(c);
                         if (sel || hov) {
                             cairo_arc(c, sx, sy_c, 12.5, 0.0, 6.283185);
                             cairo_set_source_rgba(c, pal.accent.r, pal.accent.g, pal.accent.b, sel ? 0.96 : 0.40);
                             cairo_set_line_width(c, 1.8);
                             cairo_stroke(c);
                         }
                         if (sel) {
                             mistbar::window::draw_pango_text_centered(
                                 c, swatches[i].name, sx - 26.0, sy_c + 13.0, 52.0, 15.0,
                                 7.8, false, pal.text_secondary, true, false);
                         }
                         const std::string hex = swatches[i].hex;
                         if (rry + 46.0 > body_top && rry < body_bottom) {
                             clicks_.push_back({sx - 13.0, sy_c - 13.0, 26.0, 26.0, shid, [this, hex]() {
                                 cfg_.accent_color = hex;
                                 save_and_reload();
                             }});
                         }
                     }
                 }, nullptr, 58.0},
                {"Label Contrast", "Ink color on translucent glass surfaces",
                 [&](cairo_t* c, double rx, double rry, double rw, double rh) {
                     int act = 0;
                     if (cfg_.glassy_text_color == "white") act = 1;
                     else if (cfg_.glassy_text_color == "dark") act = 2;
                     draw_segmented_control(c, rx + rw - 236.0, rry + (rh - 28.0) * 0.5, 220.0, 28.0,
                                            {"Auto", "White", "Dark"}, act, "lc",
                                            [this](int idx) {
                                                cfg_.glassy_text_color = (idx == 1) ? "white" : (idx == 2 ? "dark" : "auto");
                                                save_and_reload();
                                            }, body_top, body_bottom);
                 }, nullptr, 56.0}
            });
        } else if (active_cat_ == 1) {
            // Liquid Glass preview & real optical controls
            preview_x_ = cx;
            preview_y_ = ry;
            preview_w_ = cw;
            preview_h_ = 132.0;
            ensure_preview_backdrop(static_cast<int>(cw), 132, cfg_.blur_radius, dark);

            cairo_save(cr);
            mistbar::glass::append_rounded_rect(cr, cx, ry, cw, 132.0, 12.0);
            cairo_clip(cr);
            if (preview_raw_) {
                cairo_set_source_surface(cr, preview_raw_, cx, ry);
                cairo_paint(cr);
            }
            const double bar_w = (cfg_.look == "pill") ? (cw - 24.0) : cw;
            const double bar_h = std::clamp(static_cast<double>(cfg_.bar_height), 22.0, 44.0);
            const double bar_x = cx + (cw - bar_w) * 0.5;
            const double bar_y = ry + ((cfg_.look == "pill") ? 10.0 : 0.0);
            const double bar_r = (cfg_.look == "pill") ? static_cast<double>(cfg_.border_radius) : 0.0;
            draw_preview_glass(cr, cx, ry, bar_x, bar_y, bar_w, bar_h, bar_r, pal.bar_bg);

            const mistbar::themes::Color bar_ink = (cfg_.glassy_text_color == "dark")
                ? mistbar::themes::Color{0.08, 0.10, 0.14, 0.96}
                : mistbar::themes::Color{0.96, 0.98, 1.00, 0.96};
            mistbar::window::draw_pango_text_centered(cr, "Finder", bar_x + 16.0, bar_y, 60.0, bar_h, 9.4, true, bar_ink);
            mistbar::window::draw_pango_text_centered(cr, "File   Edit   View   Go", bar_x + 74.0, bar_y, 190.0, bar_h, 9.0, false, bar_ink);
            mistbar::window::draw_pango_text_centered(cr, "9:41 AM", bar_x + bar_w - 72.0, bar_y, 58.0, bar_h, 9.0, false, bar_ink, false, true);

            // Sample Control Center pill inside preview
            const double pill_pw = 160.0;
            const double pill_ph = 44.0;
            const double pill_px = cx + cw - pill_pw - 22.0;
            const double pill_py = ry + 62.0;
            draw_preview_glass(cr, cx, ry, pill_px, pill_py, pill_pw, pill_ph, 22.0, pal.panel_bg);
            mistbar::window::draw_pango_text_centered(cr, "Control Center", pill_px + 14.0, pill_py, pill_pw - 28.0, pill_ph,
                                                     9.2, true, bar_ink, true, false);
            cairo_restore(cr);

            mistbar::glass::append_rounded_rect(cr, cx + 0.5, ry + 0.5, cw - 1.0, 131.0, 12.0);
            cairo_set_source_rgba(cr, pal.border_outer.r, pal.border_outer.g, pal.border_outer.b,
                                  dark ? 0.14 : 0.12);
            cairo_set_line_width(cr, 1.0);
            cairo_stroke(cr);
            ry += 146.0;

            draw_section_label("Material Preset");
            draw_grouped_card({
                {"Material Style", "Select glass or opaque Solid",
                 [&](cairo_t* c, double rx, double rry, double rw, double rh) {
                     static const char* kIds[5] = {"clear", "frosted", "smoked", "aqua", "solid"};
                     int act = 0;
                     for (int i = 0; i < 5; ++i) {
                         if (cfg_.glass_preset == kIds[i] ||
                             (i == 0 && cfg_.glass_preset == "regular") ||
                             (i == 2 && cfg_.glass_preset == "dark")) {
                             act = i;
                             break;
                         }
                     }
                     draw_segmented_control(c, rx + rw - 320.0, rry + (rh - 28.0) * 0.5, 304.0, 28.0,
                                            {"Clear", "Frosted", "Smoked", "Aqua", "Solid"}, act, "gp",
                                            [this](int idx) {
                                                mistbar::config::apply_glass_preset(cfg_, kIds[idx]);
                                                save_and_reload();
                                            }, body_top, body_bottom);
                 }, nullptr, 56.0}
            });

            draw_section_label("Optical Parameters");
            draw_slider_group({
                {"Blur Radius", std::to_string(cfg_.blur_radius) + " px",
                 static_cast<double>(cfg_.blur_radius) / 48.0, 0.0, 48.0,
                 [this](double v) { cfg_.blur_radius = static_cast<int>(std::round(v)); save_and_reload(); }},
                {"Refraction Strength", std::to_string(static_cast<int>(std::round(cfg_.refraction_strength * 100.0f))) + "%",
                 cfg_.refraction_strength, 0.0, 1.0,
                 [this](double v) { cfg_.refraction_strength = static_cast<float>(v); save_and_reload(); }},
                {"Chromatic Dispersion", std::to_string(static_cast<int>(std::round(cfg_.chromatic_aberration * 100.0f))) + "%",
                 cfg_.chromatic_aberration, 0.0, 1.0,
                 [this](double v) { cfg_.chromatic_aberration = static_cast<float>(v); save_and_reload(); }},
                {"Specular Rim Shine", std::to_string(static_cast<int>(std::round(cfg_.specular_intensity * 100.0f))) + "%",
                 cfg_.specular_intensity, 0.0, 1.0,
                 [this](double v) { cfg_.specular_intensity = static_cast<float>(v); save_and_reload(); }},
                {"Tint Opacity", std::to_string(static_cast<int>(std::round(cfg_.tint_opacity * 100.0f))) + "%",
                 cfg_.tint_opacity, 0.0, 1.0,
                 [this](double v) { cfg_.tint_opacity = static_cast<float>(v); save_and_reload(); }},
                {"Glass Opacity", std::to_string(static_cast<int>(std::round(cfg_.glass_opacity * 100.0f))) + "%",
                 (cfg_.glass_opacity - 0.05) / 0.95, 0.05, 1.0,
                 [this](double v) { cfg_.glass_opacity = static_cast<float>(v); save_and_reload(); }}
            });

            draw_grouped_card({
                make_action_row("Restore Default Glass", "Reset shader preset and optical values to Clear",
                                "Restore Defaults", false, false, "btn:reset-glass", [this]() {
                    mistbar::config::apply_glass_preset(cfg_, "clear");
                    save_and_reload();
                })
            });
        } else if (active_cat_ == 2) {
            draw_section_label("Bar Geometry");
            draw_grouped_card({
                {"Bar Style", "Attached to top edge or floating rounded pill",
                 [&](cairo_t* c, double rx, double rry, double rw, double rh) {
                     draw_segmented_control(c, rx + rw - 236.0, rry + (rh - 28.0) * 0.5, 220.0, 28.0,
                                            {"Full Width", "Floating Pill"}, cfg_.look == "pill" ? 1 : 0, "bl",
                                            [this](int idx) {
                                                cfg_.look = (idx == 1) ? "pill" : "attached";
                                                save_and_reload();
                                            }, body_top, body_bottom);
                 }, nullptr, 56.0}
            });

            draw_slider_group({
                {"Bar Height", std::to_string(cfg_.bar_height) + " px",
                 static_cast<double>(cfg_.bar_height - 22) / 22.0, 22.0, 44.0,
                 [this](double v) { cfg_.bar_height = static_cast<int>(std::round(v)); save_and_reload(); }},
                {"Corner Radius (Pill)", std::to_string(cfg_.border_radius) + " px",
                 static_cast<double>(cfg_.border_radius) / 24.0, 0.0, 24.0,
                 [this](double v) { cfg_.border_radius = static_cast<int>(std::round(v)); save_and_reload(); }},
                {"Top Margin (Pill)", std::to_string(cfg_.bar_margin) + " px",
                 static_cast<double>(cfg_.bar_margin) / 24.0, 0.0, 24.0,
                 [this](double v) { cfg_.bar_margin = static_cast<int>(std::round(v)); save_and_reload(); }}
            });

            draw_section_label("Behavior & Clock");
            draw_grouped_card({
                make_toggle_row("Automatically Hide Bar", "Reveal when pointer touches top screen edge",
                                cfg_.auto_hide, "tg:autohide", [this]() { cfg_.auto_hide = !cfg_.auto_hide; save_and_reload(); }),
                {"Clock Format", "12-hour or 24-hour display",
                 [&](cairo_t* c, double rx, double rry, double rw, double rh) {
                     draw_segmented_control(c, rx + rw - 146.0, rry + (rh - 28.0) * 0.5, 130.0, 28.0,
                                            {"12h", "24h"}, cfg_.clock_format == "24h" ? 1 : 0, "cf",
                                            [this](int idx) {
                                                cfg_.clock_format = (idx == 1) ? "24h" : "12h";
                                                save_and_reload();
                                            }, body_top, body_bottom);
                 }, nullptr, 56.0},
                make_toggle_row("Show Seconds", "", cfg_.show_seconds, "tg:sec",
                                [this]() { cfg_.show_seconds = !cfg_.show_seconds; save_and_reload(); }),
                make_toggle_row("Show Battery Percentage", "", cfg_.show_battery_pct, "tg:batpct",
                                [this]() { cfg_.show_battery_pct = !cfg_.show_battery_pct; save_and_reload(); })
            });

            draw_section_label("Menu Bar Extras");
            draw_grouped_card({
                make_toggle_row("Application Menus", "File, Edit, View, Go, Window, Help",
                                cfg_.show_app_menu, "tg:appmenu", [this]() { cfg_.show_app_menu = !cfg_.show_app_menu; save_and_reload(); }),
                make_toggle_row("Workspace Dots", "Compositor workspace indicators",
                                cfg_.show_workspaces, "tg:ws", [this]() { cfg_.show_workspaces = !cfg_.show_workspaces; save_and_reload(); }),
                make_toggle_row("Microphone Indicator", "",
                                cfg_.show_privacy, "tg:mic", [this]() { cfg_.show_privacy = !cfg_.show_privacy; save_and_reload(); }),
                make_toggle_row("System Tray", "StatusNotifier background icons",
                                cfg_.show_tray, "tg:tray", [this]() { cfg_.show_tray = !cfg_.show_tray; save_and_reload(); }),
                make_toggle_row("Battery", "", cfg_.show_battery, "tg:bat",
                                [this]() { cfg_.show_battery = !cfg_.show_battery; save_and_reload(); }),
                make_toggle_row("Wi-Fi", "", cfg_.show_network, "tg:wifi",
                                [this]() { cfg_.show_network = !cfg_.show_network; save_and_reload(); }),
                make_toggle_row("Bluetooth", "", cfg_.show_bluetooth, "tg:bt",
                                [this]() { cfg_.show_bluetooth = !cfg_.show_bluetooth; save_and_reload(); }),
                make_toggle_row("Sound", "", cfg_.show_volume, "tg:vol",
                                [this]() { cfg_.show_volume = !cfg_.show_volume; save_and_reload(); }),
                make_toggle_row("Brightness", "", cfg_.show_brightness, "tg:bri",
                                [this]() { cfg_.show_brightness = !cfg_.show_brightness; save_and_reload(); }),
                make_toggle_row("Spotlight Search", "", cfg_.show_spotlight, "tg:spot",
                                [this]() { cfg_.show_spotlight = !cfg_.show_spotlight; save_and_reload(); }),
                make_toggle_row("Control Center", "", cfg_.show_control_center, "tg:cc",
                                [this]() { cfg_.show_control_center = !cfg_.show_control_center; save_and_reload(); }),
                make_toggle_row("Power Icon", "Sleep and restart items also live in the top-left menu",
                                cfg_.show_power_menu, "tg:pwr", [this]() { cfg_.show_power_menu = !cfg_.show_power_menu; save_and_reload(); })
            });

            draw_section_label("Menu Bar Service");
            std::vector<GroupRow> svc_rows;
            svc_rows.push_back({"Status", daemon_status_, nullptr, nullptr, 0.0});
            svc_rows.push_back(make_action_row(
                "Menu Bar Daemon", "", daemon_running_ ? "Restart" : "Start", true, false, "btn:daemon-restart", [this]() {
                    if (daemon_running_) {
                        mistbar::ipc::send_command(mistbar::ipc::default_socket_path(), "quit");
                    }
                    mistbar::sysinfo::spawn_detached({resolve_sibling_binary("mistbar-daemon")});
                    refresh_hw();
                    win_.request_redraw();
                }));
            if (daemon_running_) {
                svc_rows.push_back(make_action_row("Stop Service", "", "Stop", false, true, "btn:daemon-stop", [this]() {
                    mistbar::ipc::send_command(mistbar::ipc::default_socket_path(), "quit");
                    refresh_hw();
                    win_.request_redraw();
                }));
            }
            draw_grouped_card(svc_rows);
        } else if (active_cat_ == 3) {
            draw_section_label("Control Center");
            draw_grouped_card({
                make_toggle_row("Show in Menu Bar", "Display Control Center icon on trailing edge",
                                cfg_.show_control_center, "tg:cc2", [this]() { cfg_.show_control_center = !cfg_.show_control_center; save_and_reload(); }),
                make_action_row("Control Center Popover", "Preview quick toggles, media controls, and sliders",
                                "Open...", false, false, "btn:open-cc", [this]() {
                    mistbar::ipc::send_command(mistbar::ipc::default_socket_path(), "cc");
                }),
                make_action_row("Floating Glass Editor", "Open live optical tuner window",
                                "Open...", true, false, "btn:open-tuner", [this]() {
                    mistbar::ipc::send_command(mistbar::ipc::default_socket_path(), "tuner");
                }),
                make_action_row("Spotlight Search", "Open floating search bar",
                                "Open...", false, false, "btn:open-spot", [this]() {
                    mistbar::ipc::send_command(mistbar::ipc::default_socket_path(), "spotlight");
                })
            });
        } else if (active_cat_ == 4) {
            draw_section_label("Wi-Fi");
            std::vector<GroupRow> rows;
            rows.push_back(make_toggle_row("Wi-Fi",
                                           wifi_.connected ? ("Connected to " + wifi_.ssid) : (wifi_.enabled ? "Not Connected" : "Off"),
                                           wifi_.enabled, "tg:hw-wifi", [this]() {
                mistbar::sysinfo::set_wifi_enabled(!wifi_.enabled);
                refresh_hw();
                win_.request_redraw();
            }));
            rows.push_back(make_action_row("Scan for Networks", "", "Rescan", false, false, "btn:wifi-scan", [this]() {
                mistbar::sysinfo::rescan_wifi();
                refresh_hw();
                win_.request_redraw();
            }));
            if (wifi_.connected) {
                rows.push_back(make_action_row("Current Network", wifi_.ssid, "Disconnect", false, false, "btn:wifi-disc", [this]() {
                    mistbar::sysinfo::disconnect_wifi(wifi_.ssid);
                    refresh_hw();
                    win_.request_redraw();
                }));
            }
            draw_grouped_card(rows);

            draw_section_label("Known & Nearby Networks");
            if (!wifi_.networks.empty()) {
                std::vector<GroupRow> net_rows;
                for (std::size_t ni = 0; ni < wifi_.networks.size(); ++ni) {
                    const auto& net = wifi_.networks[ni];
                    net_rows.push_back(make_action_row(
                        net.ssid, "Signal " + std::to_string(net.signal) + "%",
                        net.in_use ? "Connected" : "Connect", net.in_use, false,
                        "btn:wifi-net-" + std::to_string(ni), [this, net]() {
                            if (!net.in_use) {
                                mistbar::sysinfo::connect_wifi(net.ssid);
                                refresh_hw();
                                win_.request_redraw();
                            }
                        }));
                }
                draw_grouped_card(net_rows);
            } else {
                draw_grouped_card({
                    {wifi_.enabled ? "No Other Networks Found" : "Wi-Fi Is Off",
                     wifi_.enabled ? "Select Rescan to search nearby access points" : "Turn on Wi-Fi to view nearby networks",
                     nullptr, nullptr, 0.0}
                });
            }
        } else if (active_cat_ == 5) {
            draw_section_label("Bluetooth");
            draw_grouped_card({
                make_toggle_row("Bluetooth",
                                bt_.powered ? (bt_.connected_name.empty() ? "On" : bt_.connected_name) : "Off",
                                bt_.powered, "tg:hw-bt", [this]() {
                    mistbar::sysinfo::set_bluetooth_enabled(!bt_.powered);
                    refresh_hw();
                    win_.request_redraw();
                })
            });
            draw_section_label("My Devices");
            if (!bt_.devices.empty()) {
                std::vector<GroupRow> dev_rows;
                for (std::size_t di = 0; di < bt_.devices.size(); ++di) {
                    const auto& dev = bt_.devices[di];
                    dev_rows.push_back(make_action_row(
                        dev.name, dev.mac, dev.connected ? "Disconnect" : "Connect", dev.connected, false,
                        "btn:bt-dev-" + std::to_string(di), [this, dev]() {
                            mistbar::sysinfo::connect_bluetooth_device(dev.mac, !dev.connected);
                            refresh_hw();
                            win_.request_redraw();
                        }));
                }
                draw_grouped_card(dev_rows);
            } else {
                draw_grouped_card({
                    {bt_.powered ? "No Paired Devices Found" : "Bluetooth Is Off",
                     bt_.powered ? "Devices paired via bluetoothctl appear here" : "Turn on Bluetooth to manage paired devices",
                     nullptr, nullptr, 0.0}
                });
            }
        } else if (active_cat_ == 6) {
            draw_section_label("Output");
            draw_slider_group({
                {"Output Volume", std::to_string(audio_.volume_percent) + "%",
                 static_cast<double>(audio_.volume_percent) / 100.0, 0.0, 100.0,
                 [this](double v) {
                     audio_.volume_percent = static_cast<int>(std::round(v));
                     mistbar::sysinfo::set_audio_volume(audio_.volume_percent);
                     win_.request_redraw();
                 }}
            });
            std::vector<GroupRow> rows;
            rows.push_back(make_toggle_row("Mute Output", "", audio_.muted, "tg:mute", [this]() {
                mistbar::sysinfo::toggle_audio_mute();
                refresh_hw();
                win_.request_redraw();
            }));
            for (std::size_t si = 0; si < audio_.sinks.size(); ++si) {
                const auto& s = audio_.sinks[si];
                rows.push_back(make_action_row(
                    s.name, "", s.is_default ? "Active" : "Use", s.is_default, false,
                    "btn:sink-" + std::to_string(si), [this, s]() {
                        mistbar::sysinfo::set_default_audio_sink(s.id);
                        refresh_hw();
                        win_.request_redraw();
                    }));
            }
            draw_grouped_card(rows);
        } else if (active_cat_ == 7) {
            draw_section_label("Built-In Display");
            draw_slider_group({
                {"Brightness", std::to_string(brightness_.percent) + "%",
                 static_cast<double>(brightness_.percent - 5) / 95.0, 5.0, 100.0,
                 [this](double v) {
                     brightness_.percent = static_cast<int>(std::round(v));
                     mistbar::sysinfo::set_brightness_percent(brightness_.percent);
                     win_.request_redraw();
                 }}
            });
            draw_grouped_card({
                {"Quick Levels", "",
                 [&](cairo_t* c, double rx, double rry, double rw, double rh) {
                     const int vals[4] = {25, 50, 75, 100};
                     int act = -1;
                     for (int i = 0; i < 4; ++i) {
                         if (std::abs(brightness_.percent - vals[i]) <= 3) act = i;
                     }
                     draw_segmented_control(c, rx + rw - 246.0, rry + (rh - 28.0) * 0.5, 230.0, 28.0,
                                            {"25%", "50%", "75%", "100%"}, act, "bri",
                                            [this](int idx) {
                                                const int vals_cb[4] = {25, 50, 75, 100};
                                                brightness_.percent = vals_cb[idx];
                                                mistbar::sysinfo::set_brightness_percent(brightness_.percent);
                                                win_.request_redraw();
                                            }, body_top, body_bottom);
                 }, nullptr, 48.0}
            });
        } else if (active_cat_ == 8) {
            draw_section_label("Battery & Power Mode");
            draw_grouped_card({
                {"Battery Health & Charge", battery_.present ? (battery_.state_text + " (" + std::to_string(battery_.percent) + "%)") : "AC Power",
                 [this, &pal](cairo_t* c, double rx, double rry, double rw, double rh) {
                     const std::string pct = battery_.present ? (std::to_string(battery_.percent) + "%") : "AC";
                     mistbar::window::draw_pango_text_centered(
                         c, pct, rx + rw - 80.0, rry, 64.0, rh, 10.2, true, pal.text_primary, false, true);
                 }, nullptr, 56.0},
                {"Energy Mode", "Adjust performance and battery longevity",
                 [&](cairo_t* c, double rx, double rry, double rw, double rh) {
                     const char* profs[3] = {"power-saver", "balanced", "performance"};
                     int act = 1;
                     for (int i = 0; i < 3; ++i) {
                         if (battery_.power_profile == profs[i]) act = i;
                     }
                     draw_segmented_control(c, rx + rw - 256.0, rry + (rh - 28.0) * 0.5, 240.0, 28.0,
                                            {"Low Power", "Automatic", "High"}, act, "pw",
                                            [this, profs](int idx) {
                                                mistbar::sysinfo::set_power_profile(profs[idx]);
                                                refresh_hw();
                                                win_.request_redraw();
                                            }, body_top, body_bottom);
                 }, nullptr, 56.0}
            });
        } else {
            // About
            mistbar::glass::append_rounded_rect(cr, cx, ry, cw, 134.0, 11.0);
            cairo_set_source_rgba(cr, pal.surface_elevated.r, pal.surface_elevated.g, pal.surface_elevated.b, 1.0);
            cairo_fill_preserve(cr);
            cairo_set_source_rgba(cr, pal.border_outer.r, pal.border_outer.g, pal.border_outer.b,
                                  dark ? 0.09 : 0.08);
            cairo_set_line_width(cr, 1.0);
            cairo_stroke(cr);

            const std::string mark_svg = mistbar::icons::resolve_icon("mistbar-mark.svg");
            if (!mark_svg.empty()) {
                win_.icon_cache().draw(cr, mark_svg, cx + cw * 0.5 - 22.0, ry + 18.0, 44.0, 44.0,
                                       dark ? 0xFFF5F5F7U : 0xFF1D1D1FU);
            }
            mistbar::window::draw_pango_text_centered(
                cr, "Mistbar", cx, ry + 72.0, cw, 22.0, 13.0, true, pal.text_primary, true, false);
            mistbar::window::draw_pango_text_centered(
                cr, std::string("Version ") + MISTBAR_VERSION + " - Wayland Menu Bar",
                cx, ry + 96.0, cw, 18.0, 8.8, false, pal.text_secondary, true, false);
            ry += 150.0;

            auto make_info = [&pal](const std::string& k, const std::string& v) -> GroupRow {
                return {
                    k, "",
                    [v, &pal](cairo_t* c, double rx, double rry, double rw, double rh) {
                        mistbar::window::draw_pango_text_centered(
                            c, v, rx + 150.0, rry, rw - 166.0, rh, 9.0, false,
                            pal.text_secondary, false, true);
                    },
                    nullptr,
                    0.0
                };
            };
            std::string cfg_display = mistbar::config::config_file_path();
            const char* home_env = std::getenv("HOME");
            if (home_env && cfg_display.rfind(home_env, 0) == 0) {
                cfg_display = std::string("~") + cfg_display.substr(std::strlen(home_env));
            }
            draw_grouped_card({
                make_info("Processor", mistbar::sysinfo::get_cpu_model()),
                make_info("Memory", mistbar::sysinfo::get_ram_total()),
                make_info("Renderer", win_.backdrop().using_gpu() ? "OpenGL GLSL" : "Cairo Software"),
                make_info("Menu Bar Status", daemon_running_ ? "Running" : "Stopped"),
                make_info("Preferences", cfg_display),
                make_action_row("Factory Reset", "Restore all Mistbar settings to defaults",
                                "Reset All", false, true, "btn:factory-reset", [this]() {
                    cfg_ = mistbar::config::MistbarConfig{};
                    save_and_reload();
                })
            });
        }

        const double content_end = ry + scroll_y_;
        max_scroll_y_ = std::max(0.0, content_end - body_bottom + 12.0);
        scroll_y_ = std::clamp(scroll_y_, 0.0, max_scroll_y_);

        cairo_restore(cr);

        if (max_scroll_y_ > 2.0) {
            const double view_h = body_bottom - body_top - 12.0;
            const double thumb_h = std::max(28.0, view_h * (view_h / (view_h + max_scroll_y_)));
            const double thumb_y = body_top + 6.0 + (scroll_y_ / max_scroll_y_) * (view_h - thumb_h);
            mistbar::glass::append_rounded_rect(cr, static_cast<double>(w) - 9.0, thumb_y, 3.0, thumb_h, 1.5);
            cairo_set_source_rgba(cr, pal.text_secondary.r, pal.text_secondary.g, pal.text_secondary.b, 0.34);
            cairo_fill(cr);
        }
    }

    void update_slider(std::size_t idx, double px) {
        if (idx >= sliders_.size()) return;
        const auto& s = sliders_[idx];
        const double norm = std::clamp((px - (s.x + 6.0)) / std::max(1.0, s.w - 12.0), 0.0, 1.0);
        const double val = s.min_v + norm * (s.max_v - s.min_v);
        s.on_change(val);
    }

    void handle_pointer(const mistbar::wl::PointerEvent& ev) {
        if (ev.type == mistbar::wl::PointerEvent::Type::Leave) {
            if (!hovered_id_.empty()) {
                hovered_id_.clear();
                win_.request_redraw();
            }
            return;
        }
        if (ev.type == mistbar::wl::PointerEvent::Type::Axis) {
            scroll_y_ = std::clamp(scroll_y_ + ev.axis_dy * 1.6, 0.0, max_scroll_y_);
            win_.request_redraw();
            return;
        }
        if (ev.type == mistbar::wl::PointerEvent::Type::ButtonRelease) {
            dragging_slider_idx_ = -1;
            return;
        }
        if (ev.type == mistbar::wl::PointerEvent::Type::Motion) {
            if (dragging_slider_idx_ >= 0) {
                update_slider(static_cast<std::size_t>(dragging_slider_idx_), ev.x);
                return;
            }
            std::string next_hov;
            for (const auto& c : clicks_) {
                if (!c.hover_id.empty() &&
                    ev.x >= c.x && ev.x <= c.x + c.w && ev.y >= c.y && ev.y <= c.y + c.h) {
                    next_hov = c.hover_id;
                    break;
                }
            }
            bool redraw = (next_hov != hovered_id_);
            hovered_id_ = std::move(next_hov);

            if (active_cat_ == 1 &&
                ev.x >= preview_x_ && ev.x <= preview_x_ + preview_w_ &&
                ev.y >= preview_y_ && ev.y <= preview_y_ + preview_h_) {
                preview_ptr_x_ = std::clamp((ev.x - preview_x_) / std::max(1.0, preview_w_), 0.0, 1.0);
                preview_ptr_y_ = std::clamp((ev.y - preview_y_) / std::max(1.0, preview_h_), 0.0, 1.0);
                redraw = true;
            }
            if (redraw) win_.request_redraw();
            return;
        }
        if (ev.type != mistbar::wl::PointerEvent::Type::ButtonPress || ev.button != BTN_LEFT) {
            return;
        }

        search_focused_ = false;
        for (std::size_t i = 0; i < sliders_.size(); ++i) {
            const auto& s = sliders_[i];
            if (ev.x >= s.x && ev.x <= s.x + s.w && ev.y >= s.y && ev.y <= s.y + s.h) {
                dragging_slider_idx_ = static_cast<int>(i);
                update_slider(i, ev.x);
                return;
            }
        }
        for (const auto& c : clicks_) {
            if (ev.x >= c.x && ev.x <= c.x + c.w && ev.y >= c.y && ev.y <= c.y + c.h) {
                if (c.on_click) c.on_click();
                return;
            }
        }
        win_.request_redraw();
    }

    void handle_key(const mistbar::wl::KeyEvent& ev) {
        if (!ev.pressed) return;
        if ((ev.modifiers & 2U) != 0U && (ev.keysym == XKB_KEY_f || ev.keysym == XKB_KEY_F)) {
            search_focused_ = true;
            win_.request_redraw();
            return;
        }
        if (ev.keysym == XKB_KEY_Escape) {
            if (!search_query_.empty()) {
                search_query_.clear();
                search_focused_ = false;
                win_.request_redraw();
            } else {
                win_.close();
            }
            return;
        }
        if (search_focused_) {
            if (ev.keysym == XKB_KEY_BackSpace) {
                if (!search_query_.empty()) search_query_.pop_back();
                win_.request_redraw();
            } else if (!ev.utf8.empty() && static_cast<unsigned char>(ev.utf8[0]) >= 32) {
                search_query_ += ev.utf8;
                win_.request_redraw();
            }
            return;
        }
        if (ev.keysym == XKB_KEY_Down) {
            select_category((active_cat_ + 1) % static_cast<int>(categories_.size()));
        } else if (ev.keysym == XKB_KEY_Up) {
            const int n = static_cast<int>(categories_.size());
            select_category((active_cat_ + n - 1) % n);
        }
    }
};

int parse_initial_page(int argc, char** argv) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--page") == 0) {
            const std::string p = argv[i + 1];
            if (p == "appearance") return 0;
            if (p == "liquid-glass" || p == "glass") return 1;
            if (p == "menubar" || p == "menu-bar") return 2;
            if (p == "control-center" || p == "cc") return 3;
            if (p == "wifi" || p == "wi-fi") return 4;
            if (p == "bluetooth") return 5;
            if (p == "sound") return 6;
            if (p == "display") return 7;
            if (p == "energy" || p == "battery") return 8;
            if (p == "about") return 9;
        }
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    mistbar::log::init();
    mistbar::config::ensure_default_config();
    auto cfg = mistbar::config::load_config();

    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--dump-png") == 0) {
            mistbar::wl::Display dummy_dpy;
            MistbarSettingsApp preview(dummy_dpy, cfg, parse_initial_page(argc, argv));
            preview.render_offscreen_png(argv[i + 1]);
            return 0;
        }
    }

    std::signal(SIGINT, handle_sig);
    std::signal(SIGTERM, handle_sig);
    std::signal(SIGPIPE, SIG_IGN);

    mistbar::wl::Display dpy;
    if (!dpy.connect()) {
        mistbar::log::error("settings", "Could not connect to Wayland display");
        return 1;
    }

    MistbarSettingsApp app(dpy, cfg, parse_initial_page(argc, argv));
    if (!app.init()) {
        mistbar::log::error("settings", "Could not create Settings window");
        return 1;
    }

    auto last_hw = std::chrono::steady_clock::now();
    while (app.is_open() && !g_stop) {
        if (!dpy.prepare_and_flush()) break;
        pollfd pfd{dpy.fd(), POLLIN, 0};
        const int pr = ::poll(&pfd, 1, 500);
        if (pr > 0 && (pfd.revents & POLLIN)) {
            if (!dpy.dispatch_events()) break;
        } else {
            wl_display_cancel_read(dpy.handle());
        }

        const auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_hw).count() >= 3000) {
            last_hw = now;
            app.refresh_hw();
            app.request_redraw();
        }
    }
    return 0;
}
