/* Mistbar - MIT License */

#include "common/config.h"
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

class MistbarSettingsApp {
public:
    MistbarSettingsApp(mistbar::wl::Display& dpy, mistbar::config::MistbarConfig cfg, int initial_cat)
        : dpy_(dpy), win_(dpy), cfg_(std::move(cfg)), active_cat_(initial_cat) {
        categories_ = {
            {"Appearance",   "categories/appearance.svg"},
            {"Liquid Glass", "categories/liquid-glass.svg"},
            {"Menu Bar",     "categories/menubar.svg"},
            {"Wi-Fi",        "categories/wifi.svg"},
            {"Bluetooth",    "categories/bluetooth.svg"},
            {"Sound",        "categories/sound.svg"},
            {"Display",      "categories/display.svg"},
            {"About",        "categories/about.svg"}
        };
    }

    ~MistbarSettingsApp() {
        if (preview_raw_) cairo_surface_destroy(preview_raw_);
        if (preview_blurred_) cairo_surface_destroy(preview_blurred_);
    }

    bool init() {
        refresh_hw();
        win_.set_content_draw([this](cairo_t* cr, int w, int h) { draw(cr, w, h); });
        win_.set_content_pointer([this](const mistbar::wl::PointerEvent& ev) { handle_pointer(ev); });
        win_.set_content_key([this](const mistbar::wl::KeyEvent& ev) { handle_key(ev); });
        return win_.create("Mistbar Settings", "mistbar-settings", 780, 540, cfg_, 18, true);
    }

    bool is_open() const noexcept { return win_.is_open(); }

    void refresh_hw() {
        wifi_ = mistbar::sysinfo::get_wifi_status(active_cat_ == 3);
        bt_ = mistbar::sysinfo::get_bluetooth_status(active_cat_ == 4);
        audio_ = mistbar::sysinfo::get_audio_status(active_cat_ == 5);
        brightness_ = mistbar::sysinfo::get_brightness_status();
        std::string st;
        daemon_running_ = mistbar::ipc::send_command(mistbar::ipc::default_socket_path(), "status", &st);
        daemon_status_ = daemon_running_ ? st : "Not running";
    }

    void request_redraw() { win_.request_redraw(); }

    void render_offscreen_png(const std::string& out_path) {
        const int w = 780;
        const int h = 540;
        const int m = 18;
        win_.apply_config(cfg_);
        wifi_ = {true, true, "Tahoe-5G", {}};
        bt_ = {true, true, "AirPods Pro", {}};
        audio_ = {72, false, {}};
        brightness_ = {true, 85};
        daemon_status_ = "running (theme: dark, preset: regular, look: pill, gpu_glass: on)";

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
        tl.draw(cr, win_.icon_cache(), m + 22.0, m + 22.0, true);
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
    };

    struct ClickZone {
        double x = 0.0, y = 0.0, w = 0.0, h = 0.0;
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
    };

    mistbar::wl::Display& dpy_;
    mistbar::window::MistbarWindow win_;
    mistbar::config::MistbarConfig cfg_;
    std::vector<CategoryItem> categories_;
    int active_cat_ = 1;
    std::vector<int> back_hist_;
    std::vector<int> fwd_hist_;
    double scroll_y_ = 0.0;

    void select_category(int idx) {
        if (idx == active_cat_) return;
        back_hist_.push_back(active_cat_);
        fwd_hist_.clear();
        active_cat_ = idx;
        scroll_y_ = 0.0;
        search_focused_ = false;
        refresh_hw();
        win_.request_redraw();
    }
    double max_scroll_y_ = 0.0;

    std::string search_query_;
    bool search_focused_ = false;

    std::vector<ClickZone> clicks_;
    std::vector<SliderZone> sliders_;
    int dragging_slider_idx_ = -1;

    double preview_ptr_x_ = 0.35;
    double preview_ptr_y_ = 0.45;
    double preview_x_ = 0.0, preview_y_ = 0.0, preview_w_ = 0.0, preview_h_ = 0.0;
    cairo_surface_t* preview_raw_ = nullptr;
    cairo_surface_t* preview_blurred_ = nullptr;
    int preview_blur_cached_ = -1;
    int preview_w_cached_ = 0;

    void ensure_preview_backdrop(int pw, int ph, int blur_r) {
        if (pw <= 0 || ph <= 0) return;
        if (!preview_raw_ || preview_w_cached_ != pw) {
            if (preview_raw_) cairo_surface_destroy(preview_raw_);
            if (preview_blurred_) cairo_surface_destroy(preview_blurred_);
            preview_raw_ = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pw, ph);
            preview_blurred_ = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pw, ph);
            preview_w_cached_ = pw;
            preview_blur_cached_ = -1;

            cairo_t* pcr = cairo_create(preview_raw_);
            // Smooth macOS Tahoe layered silk aurora wallpaper so glass refraction & rim caustics look organic
            cairo_pattern_t* sky = cairo_pattern_create_linear(0.0, 0.0, static_cast<double>(pw), static_cast<double>(ph));
            cairo_pattern_add_color_stop_rgb(sky, 0.00, 0.04, 0.12, 0.30);
            cairo_pattern_add_color_stop_rgb(sky, 0.48, 0.08, 0.28, 0.56);
            cairo_pattern_add_color_stop_rgb(sky, 1.00, 0.05, 0.16, 0.38);
            cairo_set_source(pcr, sky);
            cairo_paint(pcr);
            cairo_pattern_destroy(sky);

            auto draw_silk_wave = [&](double y0, double y1, double y2,
                                      double r1, double g1, double b1, double a1,
                                      double r2, double g2, double b2, double a2) {
                cairo_new_path(pcr);
                cairo_move_to(pcr, -20.0, static_cast<double>(ph) + 20.0);
                cairo_line_to(pcr, -20.0, y0);
                cairo_curve_to(pcr, pw * 0.30, y1, pw * 0.68, y2, pw + 20.0, y0 * 0.55);
                cairo_line_to(pcr, pw + 20.0, static_cast<double>(ph) + 20.0);
                cairo_close_path(pcr);
                cairo_pattern_t* wp = cairo_pattern_create_linear(0.0, std::min(y0, y1) - 10.0,
                                                                  static_cast<double>(pw), static_cast<double>(ph));
                cairo_pattern_add_color_stop_rgba(wp, 0.0, r1, g1, b1, a1);
                cairo_pattern_add_color_stop_rgba(wp, 1.0, r2, g2, b2, a2);
                cairo_set_source(pcr, wp);
                cairo_fill(pcr);
                cairo_pattern_destroy(wp);
            };

            draw_silk_wave(ph * 0.56, ph * 0.10, ph * 0.78,
                           0.16, 0.72, 0.98, 0.78,
                           0.08, 0.42, 0.88, 0.58);
            draw_silk_wave(ph * 0.78, ph * 0.28, ph * 0.96,
                           0.18, 0.88, 0.76, 0.68,
                           0.10, 0.52, 0.78, 0.45);
            draw_silk_wave(ph * 0.92, ph * 0.52, ph * 0.34,
                           0.92, 0.38, 0.64, 0.52,
                           0.38, 0.28, 0.86, 0.42);

            cairo_pattern_t* rim_glow = cairo_pattern_create_radial(
                pw * 0.50, ph * 0.36, 8.0, pw * 0.50, ph * 0.36, pw * 0.48);
            cairo_pattern_add_color_stop_rgba(rim_glow, 0.0, 0.75, 0.94, 1.0, 0.32);
            cairo_pattern_add_color_stop_rgba(rim_glow, 1.0, 0.20, 0.50, 0.95, 0.0);
            cairo_set_source(pcr, rim_glow);
            cairo_paint(pcr);
            cairo_pattern_destroy(rim_glow);
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

    mistbar::sysinfo::WifiStatus wifi_;
    mistbar::sysinfo::BluetoothStatus bt_;
    mistbar::sysinfo::AudioStatus audio_;
    mistbar::sysinfo::BrightnessStatus brightness_;
    bool daemon_running_ = false;
    std::string daemon_status_ = "Checking...";

    void save_and_reload() {
        mistbar::config::save_config(cfg_);
        win_.apply_config(cfg_);
        mistbar::ipc::send_command(mistbar::ipc::default_socket_path(), "reload");
        win_.request_redraw();
    }

    void draw_macos_switch(cairo_t* cr, double x, double y, bool on) {
        const auto& pal = win_.palette();
        mistbar::glass::append_rounded_rect(cr, x, y, 38.0, 22.0, 11.0);
        if (on) {
            cairo_set_source_rgba(cr, pal.accent.r, pal.accent.g, pal.accent.b, 0.98);
        } else {
            cairo_set_source_rgba(cr, 0.5, 0.52, 0.58, pal.dark_mode ? 0.30 : 0.24);
        }
        cairo_fill(cr);

        const double kx = on ? (x + 27.0) : (x + 11.0);
        const double ky = y + 11.0;
        cairo_arc(cr, kx, ky + 1.0, 9.0, 0.0, 6.283185);
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.16);
        cairo_fill(cr);

        cairo_arc(cr, kx, ky, 8.8, 0.0, 6.283185);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.99);
        cairo_fill(cr);
    }

    void draw_segmented_control(cairo_t* cr,
                                double x, double y, double w, double h,
                                const std::vector<std::string>& labels,
                                int active_idx,
                                std::function<void(int)> on_pick,
                                double body_top, double body_bottom) {
        const auto& pal = win_.palette();
        mistbar::glass::append_rounded_rect(cr, x, y, w, h, 8.0);
        cairo_set_source_rgba(cr, pal.dark_mode ? 0.08 : 0.86,
                              pal.dark_mode ? 0.09 : 0.88,
                              pal.dark_mode ? 0.11 : 0.91,
                              pal.dark_mode ? 0.65 : 0.85);
        cairo_fill(cr);

        const int n = static_cast<int>(labels.size());
        if (n <= 0) return;
        const double seg_w = (w - 4.0) / static_cast<double>(n);
        for (int i = 0; i < n; ++i) {
            const double sx = x + 2.0 + static_cast<double>(i) * seg_w;
            const double sy = y + 2.0;
            const double sh = h - 4.0;
            if (i == active_idx) {
                mistbar::glass::append_rounded_rect(cr, sx, sy + 0.8, seg_w, sh, 6.5);
                cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.14);
                cairo_fill(cr);

                mistbar::glass::append_rounded_rect(cr, sx, sy, seg_w, sh, 6.5);
                if (pal.dark_mode) {
                    cairo_set_source_rgba(cr, 0.28, 0.30, 0.36, 0.96);
                } else {
                    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.99);
                }
                cairo_fill_preserve(cr);
                cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, pal.dark_mode ? 0.14 : 0.85);
                cairo_set_line_width(cr, 0.8);
                cairo_stroke(cr);
            }
            mistbar::window::draw_pango_text_centered(
                cr, labels[static_cast<std::size_t>(i)], sx, sy, seg_w, sh,
                8.5, i == active_idx, pal.text_primary, true, false);

            if (sy + sh > body_top && sy < body_bottom) {
                clicks_.push_back({sx, std::max(body_top, sy), seg_w, sh, [on_pick, i]() { on_pick(i); }});
            }
        }
    }

    void draw_pill_button(cairo_t* cr, double x, double y, double w, double h,
                          const std::string& label, bool primary,
                          std::function<void()> on_click,
                          double body_top, double body_bottom) {
        const auto& pal = win_.palette();
        mistbar::glass::append_rounded_rect(cr, x, y, w, h, 7.5);
        if (primary) {
            cairo_set_source_rgba(cr, pal.accent.r, pal.accent.g, pal.accent.b, 0.95);
        } else {
            cairo_set_source_rgba(cr, 0.5, 0.53, 0.58, pal.dark_mode ? 0.26 : 0.18);
        }
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, primary ? 0.20 : 0.10);
        cairo_set_line_width(cr, 0.8);
        cairo_stroke(cr);

        mistbar::window::draw_pango_text_centered(
            cr, label, x, y, w, h, 8.5, primary,
            primary ? mistbar::themes::Color{1, 1, 1, 1} : pal.text_primary, true, false);
        if (y + h > body_top && y < body_bottom) {
            clicks_.push_back({x, std::max(body_top, y), w, h, std::move(on_click)});
        }
    }

    void draw(cairo_t* cr, int w, int h) {
        clicks_.clear();
        sliders_.clear();
        const auto& pal = win_.palette();

        // macOS 26 Tahoe floating glass sidebar
        const double sb_m = 10.0;
        const double sb_w = 216.0;
        const double sb_h = static_cast<double>(h) - sb_m * 2.0;
        mistbar::window::draw_window_shadow(cr, sb_m, sb_m, sb_w, sb_h, 16.0, 12, true);
        mistbar::glass::draw_glass_panel(cr, &win_.backdrop(), sb_m, sb_m, sb_m, sb_m,
                                        sb_w, sb_h, 16.0, pal, &pal.sidebar_bg);

        // App icon + title below integrated traffic lights
        const std::string logo_path = mistbar::icons::resolve_icon("mistbar-logo.svg");
        if (!logo_path.empty()) {
            win_.icon_cache().draw(cr, logo_path, sb_m + 14.0, sb_m + 38.0, 30.0, 30.0, 0);
        }
        mistbar::window::draw_pango_text_centered(cr, "Mistbar", sb_m + 52.0, sb_m + 37.0, 130.0, 18.0,
                                                 10.8, true, pal.text_primary);
        mistbar::window::draw_pango_text_centered(cr, "Liquid Glass Bar", sb_m + 52.0, sb_m + 53.0, 140.0, 14.0,
                                                 7.8, false, pal.text_secondary);

        // macOS Search Pill in Sidebar
        const double search_x = sb_m + 12.0;
        const double search_y = sb_m + 78.0;
        const double search_w = sb_w - 24.0;
        const double search_h = 28.0;
        mistbar::glass::append_rounded_rect(cr, search_x, search_y, search_w, search_h, 9.0);
        cairo_set_source_rgba(cr, pal.dark_mode ? 1.0 : 0.0, pal.dark_mode ? 1.0 : 0.0,
                              pal.dark_mode ? 1.0 : 0.0, pal.dark_mode ? 0.08 : 0.06);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, pal.accent.r, pal.accent.g, pal.accent.b, search_focused_ ? 0.85 : 0.0);
        cairo_set_line_width(cr, 1.4);
        cairo_stroke(cr);

        const std::string srch_svg = mistbar::icons::resolve_icon("search.svg");
        if (!srch_svg.empty()) {
            win_.icon_cache().draw(cr, srch_svg, search_x + 9.0, search_y + 7.5, 13.0, 13.0,
                                   pal.dark_mode ? 0xFFAAB4C4U : 0xFF6A707CU);
        }
        const std::string search_lbl = search_query_.empty() ? "Search" : search_query_;
        mistbar::window::draw_pango_text_centered(cr, search_lbl, search_x + 28.0, search_y, search_w - 36.0, search_h,
                                                 8.8, false, search_query_.empty() ? pal.text_secondary : pal.text_primary);
        clicks_.push_back({search_x, search_y, search_w, search_h, [this]() {
            search_focused_ = true;
            win_.request_redraw();
        }});

        // Sidebar category rows
        double sy = search_y + 38.0;
        const std::string q_low = to_lower_copy(search_query_);
        for (int i = 0; i < static_cast<int>(categories_.size()); ++i) {
            if (!q_low.empty() && to_lower_copy(categories_[static_cast<std::size_t>(i)].name).find(q_low) == std::string::npos) {
                continue;
            }
            const bool sel = (i == active_cat_);
            const double rx = sb_m + 10.0;
            const double rw = sb_w - 20.0;
            if (sel) {
                mistbar::glass::append_rounded_rect(cr, rx, sy, rw, 33.0, 9.0);
                cairo_set_source_rgba(cr, pal.accent.r, pal.accent.g, pal.accent.b, 0.92);
                cairo_fill(cr);
            }
            const std::string ic = mistbar::icons::resolve_icon(categories_[static_cast<std::size_t>(i)].icon);
            if (!ic.empty()) {
                win_.icon_cache().draw(cr, ic, rx + 8.0, sy + 5.5, 22.0, 22.0, 0);
            }
            const mistbar::themes::Color tc = sel ? mistbar::themes::Color{1, 1, 1, 1} : pal.text_primary;
            mistbar::window::draw_pango_text_centered(cr, categories_[static_cast<std::size_t>(i)].name,
                                                     rx + 38.0, sy, rw - 46.0, 33.0, 9.4, sel, tc);
            clicks_.push_back({rx, sy, rw, 33.0, [this, i]() {
                select_category(i);
            }});
            sy += 36.0;
        }

        // Right content pane
        const double cx = sb_m + sb_w + 22.0;
        const double cw = static_cast<double>(w) - cx - 24.0;
        const double header_h = 54.0;

        // macOS Tahoe Back / Forward header navigation pill
        auto draw_nav_btn = [&](double bx, bool is_back, bool enabled) {
            mistbar::glass::append_rounded_rect(cr, bx, 14.0, 26.0, 26.0, 13.0);
            cairo_set_source_rgba(cr, pal.surface_elevated.r, pal.surface_elevated.g, pal.surface_elevated.b,
                                  pal.dark_mode ? (enabled ? 0.85 : 0.40) : (enabled ? 0.96 : 0.55));
            cairo_fill_preserve(cr);
            cairo_set_source_rgba(cr, pal.border_outer.r, pal.border_outer.g, pal.border_outer.b,
                                  pal.dark_mode ? 0.14 : 0.45);
            cairo_set_line_width(cr, 0.8);
            cairo_stroke(cr);

            cairo_new_path(cr);
            const double mx = bx + 13.0 + (is_back ? -1.5 : 1.5);
            const double my = 27.0;
            const double dir = is_back ? 4.2 : -4.2;
            cairo_move_to(cr, mx + dir, my - 4.5);
            cairo_line_to(cr, mx - dir * 0.3, my);
            cairo_line_to(cr, mx + dir, my + 4.5);
            cairo_set_source_rgba(cr, pal.text_primary.r, pal.text_primary.g, pal.text_primary.b,
                                  enabled ? 0.88 : 0.32);
            cairo_set_line_width(cr, 1.55);
            cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
            cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
            cairo_stroke(cr);

            if (enabled) {
                clicks_.push_back({bx, 14.0, 26.0, 26.0, [this, is_back]() {
                    if (is_back && !back_hist_.empty()) {
                        fwd_hist_.push_back(active_cat_);
                        active_cat_ = back_hist_.back();
                        back_hist_.pop_back();
                    } else if (!is_back && !fwd_hist_.empty()) {
                        back_hist_.push_back(active_cat_);
                        active_cat_ = fwd_hist_.back();
                        fwd_hist_.pop_back();
                    }
                    scroll_y_ = 0.0;
                    refresh_hw();
                    win_.request_redraw();
                }});
            }
        };
        draw_nav_btn(cx, true, !back_hist_.empty());
        draw_nav_btn(cx + 32.0, false, !fwd_hist_.empty());

        mistbar::window::draw_pango_text_centered(
            cr, categories_[static_cast<std::size_t>(active_cat_)].name,
            cx + 70.0, 8.0, cw - 70.0, 40.0, 14.2, true, pal.text_primary);

        const double body_top = header_h;
        const double body_bottom = static_cast<double>(h) - 12.0;
        cairo_save(cr);
        cairo_rectangle(cr, sb_m + sb_w + 2.0, body_top, static_cast<double>(w) - (sb_m + sb_w + 2.0),
                        body_bottom - body_top);
        cairo_clip(cr);

        double ry = body_top + 4.0 - scroll_y_;

        auto draw_section_label = [&](const std::string& label) {
            mistbar::window::draw_pango_text_centered(cr, label, cx + 6.0, ry, cw - 12.0, 20.0,
                                                     8.4, true, pal.text_secondary);
            ry += 24.0;
        };

        auto draw_grouped_card = [&](const std::vector<GroupRow>& rows) {
            if (rows.empty()) return;
            double total_h = 0.0;
            std::vector<double> heights;
            heights.reserve(rows.size());
            for (const auto& r : rows) {
                const double rh = r.subtitle.empty() ? 42.0 : 52.0;
                heights.push_back(rh);
                total_h += rh;
            }

            mistbar::glass::append_rounded_rect(cr, cx, ry, cw, total_h, 12.0);
            cairo_set_source_rgba(cr, pal.surface_elevated.r, pal.surface_elevated.g,
                                  pal.surface_elevated.b, pal.dark_mode ? 0.90 : 0.98);
            cairo_fill_preserve(cr);
            cairo_set_source_rgba(cr, pal.border_outer.r, pal.border_outer.g, pal.border_outer.b,
                                  pal.dark_mode ? 0.14 : 0.45);
            cairo_set_line_width(cr, 1.0);
            cairo_stroke(cr);

            double cur_y = ry;
            for (std::size_t i = 0; i < rows.size(); ++i) {
                const auto& r = rows[i];
                const double rh = heights[i];
                if (r.subtitle.empty()) {
                    mistbar::window::draw_pango_text_centered(cr, r.title, cx + 14.0, cur_y, cw - 150.0, rh,
                                                             9.3, false, pal.text_primary);
                } else {
                    mistbar::window::draw_pango_text_centered(cr, r.title, cx + 14.0, cur_y + 6.0, cw - 150.0, 18.0,
                                                             9.3, false, pal.text_primary);
                    mistbar::window::draw_pango_text_centered(cr, r.subtitle, cx + 14.0, cur_y + 25.0, cw - 150.0, 16.0,
                                                             7.9, false, pal.text_secondary);
                }
                if (r.render_right) r.render_right(cr, cx, cur_y, cw, rh);
                if (r.register_hit && cur_y + rh > body_top && cur_y < body_bottom) {
                    r.register_hit(cx, std::max(body_top, cur_y), cw, rh);
                }
                if (i + 1 < rows.size()) {
                    cairo_rectangle(cr, cx + 14.0, cur_y + rh - 0.5, cw - 28.0, 0.8);
                    cairo_set_source_rgba(cr, pal.border_outer.r, pal.border_outer.g, pal.border_outer.b,
                                          pal.dark_mode ? 0.10 : 0.22);
                    cairo_fill(cr);
                }
                cur_y += rh;
            }
            ry += total_h + 14.0;
        };

        auto make_toggle_row = [&](const std::string& title, const std::string& sub, bool val, std::function<void()> cb) -> GroupRow {
            return {
                title, sub,
                [this, val](cairo_t* c, double rx, double rry, double rw, double rh) {
                    draw_macos_switch(c, rx + rw - 52.0, rry + (rh - 22.0) * 0.5, val);
                },
                [this, cb](double rx, double rry, double rw, double rh) {
                    clicks_.push_back({rx, rry, rw, rh, cb});
                }
            };
        };

        auto make_action_row = [&](const std::string& title, const std::string& sub, const std::string& btn,
                                   bool primary, std::function<void()> cb) -> GroupRow {
            return {
                title, sub,
                [this, btn, primary, cb, body_top, body_bottom](cairo_t* c, double rx, double rry, double rw, double rh) {
                    draw_pill_button(c, rx + rw - 108.0, rry + (rh - 26.0) * 0.5, 94.0, 26.0,
                                     btn, primary, cb, body_top, body_bottom);
                },
                nullptr
            };
        };

        auto draw_slider_group = [&](const std::vector<std::tuple<std::string, std::string, double, double, double, std::function<void(double)>>>& items) {
            const double row_h = 52.0;
            const double total_h = static_cast<double>(items.size()) * row_h;
            mistbar::glass::append_rounded_rect(cr, cx, ry, cw, total_h, 12.0);
            cairo_set_source_rgba(cr, pal.surface_elevated.r, pal.surface_elevated.g,
                                  pal.surface_elevated.b, pal.dark_mode ? 0.90 : 0.98);
            cairo_fill_preserve(cr);
            cairo_set_source_rgba(cr, pal.border_outer.r, pal.border_outer.g, pal.border_outer.b,
                                  pal.dark_mode ? 0.14 : 0.45);
            cairo_set_line_width(cr, 1.0);
            cairo_stroke(cr);

            double cur_y = ry;
            for (std::size_t i = 0; i < items.size(); ++i) {
                const auto& [lbl, val_txt, norm, min_v, max_v, on_ch] = items[i];
                mistbar::window::draw_pango_text_centered(cr, lbl, cx + 14.0, cur_y + 5.0, cw - 110.0, 18.0,
                                                         9.1, false, pal.text_primary);
                mistbar::window::draw_pango_text_centered(cr, val_txt, cx + cw - 92.0, cur_y + 5.0, 78.0, 18.0,
                                                         8.6, true, pal.accent, false, true);

                const double tx = cx + 14.0;
                const double ty = cur_y + 32.0;
                const double tw = cw - 28.0;
                const double th = 5.0;
                mistbar::glass::append_rounded_rect(cr, tx, ty, tw, th, 2.5);
                cairo_set_source_rgba(cr, 0.5, 0.52, 0.58, 0.28);
                cairo_fill(cr);

                const double cl = std::clamp(norm, 0.0, 1.0);
                mistbar::glass::append_rounded_rect(cr, tx, ty, std::max(5.0, cl * tw), th, 2.5);
                cairo_set_source_rgba(cr, pal.accent.r, pal.accent.g, pal.accent.b, 0.96);
                cairo_fill(cr);

                const double kx = tx + cl * tw;
                cairo_arc(cr, kx, ty + 3.3, 7.2, 0.0, 6.283185);
                cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.18);
                cairo_fill(cr);

                cairo_arc(cr, kx, ty + 2.5, 7.0, 0.0, 6.283185);
                cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.99);
                cairo_fill(cr);

                if (cur_y + row_h > body_top && cur_y < body_bottom) {
                    sliders_.push_back({tx - 6.0, std::max(body_top, ty - 10.0), tw + 12.0, 24.0, min_v, max_v, on_ch});
                }
                if (i + 1 < items.size()) {
                    cairo_rectangle(cr, cx + 14.0, cur_y + row_h - 0.5, cw - 28.0, 0.8);
                    cairo_set_source_rgba(cr, pal.border_outer.r, pal.border_outer.g, pal.border_outer.b,
                                          pal.dark_mode ? 0.10 : 0.22);
                    cairo_fill(cr);
                }
                cur_y += row_h;
            }
            ry += total_h + 14.0;
        };

        if (active_cat_ == 0) {
            draw_section_label("THEME & LIQUID GLASS STYLE");
            draw_grouped_card({
                {"Appearance", "Choose Dark or Light glass tone",
                 [&](cairo_t* c, double rx, double rry, double rw, double rh) {
                     draw_segmented_control(c, rx + rw - 170.0, rry + (rh - 28.0) * 0.5, 156.0, 28.0,
                                            {"Dark", "Light"}, cfg_.theme == "light" ? 1 : 0,
                                            [this](int idx) {
                                                cfg_.theme = (idx == 1) ? "light" : "dark";
                                                if (cfg_.glass_preset == "dark" && cfg_.theme == "light") cfg_.glass_preset = "regular";
                                                save_and_reload();
                                            }, body_top, body_bottom);
                 }, nullptr},
                {"Glass Material", "Optical glass preset",
                 [&](cairo_t* c, double rx, double rry, double rw, double rh) {
                     const auto presets = mistbar::themes::available_presets();
                     int act = 0;
                     for (int i = 0; i < static_cast<int>(presets.size()); ++i) {
                         if (presets[static_cast<std::size_t>(i)] == cfg_.glass_preset) act = i;
                     }
                     draw_segmented_control(c, rx + rw - 294.0, rry + (rh - 28.0) * 0.5, 280.0, 28.0,
                                            {"Regular", "Frosted", "Dark", "Clear", "Solid"}, act,
                                            [this, presets](int idx) {
                                                mistbar::config::apply_glass_preset(cfg_, presets[static_cast<std::size_t>(idx)]);
                                                save_and_reload();
                                            }, body_top, body_bottom);
                 }, nullptr},
                {"Bar Shape", "Pill capsule or edge-to-edge strip",
                 [&](cairo_t* c, double rx, double rry, double rw, double rh) {
                     draw_segmented_control(c, rx + rw - 190.0, rry + (rh - 28.0) * 0.5, 176.0, 28.0,
                                            {"Pill", "Attached"}, cfg_.look == "attached" ? 1 : 0,
                                            [this](int idx) {
                                                cfg_.look = (idx == 1) ? "attached" : "pill";
                                                save_and_reload();
                                            }, body_top, body_bottom);
                 }, nullptr},
                {"Label Contrast", "Override top bar foreground tint",
                 [&](cairo_t* c, double rx, double rry, double rw, double rh) {
                     int idx = (cfg_.glassy_text_color == "white") ? 1 : (cfg_.glassy_text_color == "dark" ? 2 : 0);
                     draw_segmented_control(c, rx + rw - 190.0, rry + (rh - 28.0) * 0.5, 176.0, 28.0,
                                            {"Auto", "White", "Dark"}, idx,
                                            [this](int p) {
                                                cfg_.glassy_text_color = (p == 1) ? "white" : (p == 2 ? "dark" : "auto");
                                                save_and_reload();
                                            }, body_top, body_bottom);
                 }, nullptr}
            });

            draw_section_label("ACCENT COLOR");
            draw_grouped_card({
                {"System Accent", "Active switches, sliders, and highlights",
                 [&](cairo_t* c, double rx, double rry, double rw, double rh) {
                     const char* swatches[6] = {"#0A84FF", "#5E5CE6", "#30D158", "#FF9F0A", "#FF375F", "#64D2FF"};
                     for (int i = 0; i < 6; ++i) {
                         const double sx = rx + rw - 176.0 + static_cast<double>(i) * 27.0;
                         const double sy_c = rry + rh * 0.5;
                         const auto col = mistbar::themes::Color::from_hex(swatches[i]);
                         cairo_arc(c, sx, sy_c, 8.5, 0.0, 6.283185);
                         cairo_set_source_rgba(c, col.r, col.g, col.b, 1.0);
                         cairo_fill(c);
                         if (cfg_.accent_color == swatches[i]) {
                             cairo_arc(c, sx, sy_c, 11.0, 0.0, 6.283185);
                             cairo_set_source_rgba(c, pal.text_primary.r, pal.text_primary.g, pal.text_primary.b, 0.9);
                             cairo_set_line_width(c, 1.6);
                             cairo_stroke(c);
                         }
                         const std::string hex = swatches[i];
                         if (rry + rh > body_top && rry < body_bottom) {
                             clicks_.push_back({sx - 11.0, sy_c - 11.0, 22.0, 22.0, [this, hex]() {
                                 cfg_.accent_color = hex;
                                 save_and_reload();
                             }});
                         }
                     }
                 }, nullptr}
            });
        } else if (active_cat_ == 1) {
            // Interactive Liquid Glass Studio Preview Card
            preview_x_ = cx;
            preview_y_ = ry;
            preview_w_ = cw;
            preview_h_ = 128.0;
            const int ipw = static_cast<int>(cw);
            const int iph = 128;
            ensure_preview_backdrop(ipw, iph, cfg_.blur_radius);

            cairo_save(cr);
            mistbar::glass::append_rounded_rect(cr, cx, ry, cw, 128.0, 16.0);
            cairo_clip(cr);

            if (preview_raw_) {
                cairo_set_source_surface(cr, preview_raw_, cx, ry);
                cairo_paint(cr);
            }

            const double bar_w = (cfg_.look == "pill") ? (cw - 24.0) : cw;
            const double bar_h = std::clamp(static_cast<double>(cfg_.bar_height), 22.0, 44.0);
            const double bar_x = cx + (cw - bar_w) * 0.5;
            const double bar_y = ry + 12.0;
            const double bar_r = (cfg_.look == "pill") ? static_cast<double>(cfg_.border_radius) : 0.0;

            auto draw_preview_glass = [&](double gx, double gy, double gw, double gh, double gr,
                                          const mistbar::themes::Color& fill_col) {
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
                    lp.cursor_reactive = pal.cursor_reactive;
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
            };

            draw_preview_glass(bar_x, bar_y, bar_w, bar_h, bar_r, pal.bar_bg);

            mistbar::window::draw_pango_text_centered(cr, "Finder", bar_x + 16.0, bar_y, 60.0, bar_h,
                                                     9.2, true, pal.text_primary);
            mistbar::window::draw_pango_text_centered(cr, "File   Edit   View   Go", bar_x + 74.0, bar_y, 190.0, bar_h,
                                                     8.8, false, pal.text_primary);
            mistbar::window::draw_pango_text_centered(cr, "9:41 AM", bar_x + bar_w - 72.0, bar_y, 58.0, bar_h,
                                                     8.8, false, pal.text_primary, false, true);

            const double dock_w = 276.0;
            const double dock_h = 46.0;
            const double dock_x = cx + (cw - dock_w) * 0.5;
            const double dock_y = ry + 68.0;
            draw_preview_glass(dock_x, dock_y, dock_w, dock_h, 20.0, pal.panel_bg);
            // Crisp specular rim on preview dock pill + interactive label
            mistbar::glass::append_rounded_rect(cr, dock_x + 0.5, dock_y + 0.5, dock_w - 1.0, dock_h - 1.0, 19.5);
            cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, pal.dark_mode ? 0.34 : 0.65);
            cairo_set_line_width(cr, 1.0);
            cairo_stroke(cr);

            const char* dock_icons[5] = {"mistbar-logo.svg", "wifi.svg", "bluetooth.svg", "volume.svg", "control-center.svg"};
            const char* dock_cols[5] = {"#0A84FF", "#30D158", "#5E5CE6", "#FF9F0A", "#64D2FF"};
            for (int i = 0; i < 5; ++i) {
                const double bx = dock_x + 14.0 + static_cast<double>(i) * 34.0;
                const double by = dock_y + 9.0;
                const std::string p = mistbar::icons::resolve_icon(dock_icons[i]);
                if (i == 0) {
                    if (!p.empty()) win_.icon_cache().draw(cr, p, bx, by, 28.0, 28.0, 0);
                } else {
                    const auto bc = mistbar::themes::Color::from_hex(dock_cols[i]);
                    mistbar::glass::append_rounded_rect(cr, bx, by, 28.0, 28.0, 8.0);
                    cairo_set_source_rgba(cr, bc.r, bc.g, bc.b, 0.92);
                    cairo_fill(cr);
                    if (!p.empty()) win_.icon_cache().draw(cr, p, bx + 6.0, by + 6.0, 16.0, 16.0, 0xFFFFFFFFU);
                }
            }
            mistbar::window::draw_pango_text_centered(cr, "Liquid Glass",
                                                     dock_x + 186.0, dock_y, 82.0, dock_h, 8.2, true,
                                                     pal.text_primary, false, false);
            cairo_restore(cr);

            mistbar::glass::append_rounded_rect(cr, cx + 0.5, ry + 0.5, cw - 1.0, 127.0, 16.0);
            cairo_set_source_rgba(cr, pal.border_outer.r, pal.border_outer.g, pal.border_outer.b, 0.32);
            cairo_set_line_width(cr, 1.0);
            cairo_stroke(cr);
            ry += 140.0;

            draw_section_label("OPTICAL SHADER CONTROLS");
            draw_slider_group({
                {"Blur / Frost Radius", std::to_string(cfg_.blur_radius) + " px",
                 static_cast<double>(cfg_.blur_radius) / 48.0, 0.0, 48.0,
                 [this](double v) { cfg_.blur_radius = static_cast<int>(std::round(v)); save_and_reload(); }},
                {"Refraction Strength", std::to_string(static_cast<int>(std::round(cfg_.refraction_strength * 100.0f))) + "%",
                 cfg_.refraction_strength, 0.0, 1.0,
                 [this](double v) { cfg_.refraction_strength = static_cast<float>(v); save_and_reload(); }},
                {"Bezel Dome Thickness", std::to_string(static_cast<int>(std::round(cfg_.bezel_thickness * 100.0f))) + "%",
                 cfg_.bezel_thickness, 0.0, 1.0,
                 [this](double v) { cfg_.bezel_thickness = static_cast<float>(v); save_and_reload(); }},
                {"Chromatic Aberration", std::to_string(static_cast<int>(std::round(cfg_.chromatic_aberration * 100.0f))) + "%",
                 cfg_.chromatic_aberration, 0.0, 1.0,
                 [this](double v) { cfg_.chromatic_aberration = static_cast<float>(v); save_and_reload(); }},
                {"Specular & Fresnel", std::to_string(static_cast<int>(std::round(cfg_.specular_intensity * 100.0f))) + "%",
                 cfg_.specular_intensity, 0.0, 1.0,
                 [this](double v) { cfg_.specular_intensity = static_cast<float>(v); save_and_reload(); }},
                {"Tint Opacity", std::to_string(static_cast<int>(std::round(cfg_.tint_opacity * 100.0f))) + "%",
                 cfg_.tint_opacity, 0.0, 1.0,
                 [this](double v) { cfg_.tint_opacity = static_cast<float>(v); save_and_reload(); }},
                {"Glass Opacity", std::to_string(static_cast<int>(std::round(cfg_.glass_opacity * 100.0f))) + "%",
                 (cfg_.glass_opacity - 0.05) / 0.95, 0.05, 1.0,
                 [this](double v) { cfg_.glass_opacity = static_cast<float>(v); save_and_reload(); }}
            });

            draw_section_label("DIMENSIONS & INTERACTION");
            draw_slider_group({
                {"Corner Radius", std::to_string(cfg_.border_radius) + " px",
                 static_cast<double>(cfg_.border_radius) / 24.0, 0.0, 24.0,
                 [this](double v) { cfg_.border_radius = static_cast<int>(std::round(v)); save_and_reload(); }},
                {"Bar Height", std::to_string(cfg_.bar_height) + " px",
                 static_cast<double>(cfg_.bar_height - 22) / 22.0, 22.0, 44.0,
                 [this](double v) { cfg_.bar_height = static_cast<int>(std::round(v)); save_and_reload(); }},
                {"Top Margin", std::to_string(cfg_.bar_margin) + " px",
                 static_cast<double>(cfg_.bar_margin) / 24.0, 0.0, 24.0,
                 [this](double v) { cfg_.bar_margin = static_cast<int>(std::round(v)); save_and_reload(); }}
            });

            draw_grouped_card({
                make_toggle_row("Cursor-Reactive Specular Lighting", "Specular glow tracks cursor across glass",
                                cfg_.cursor_reactive, [this]() { cfg_.cursor_reactive = !cfg_.cursor_reactive; save_and_reload(); }),
                make_action_row("Restore Default Glass Optics", "", "Reset", false, [this]() {
                    mistbar::config::apply_glass_preset(cfg_, "regular");
                    cfg_.bar_height = 28;
                    cfg_.bar_margin = 6;
                    cfg_.border_radius = 16;
                    cfg_.cursor_reactive = true;
                    save_and_reload();
                })
            });
        } else if (active_cat_ == 2) {
            draw_section_label("BEHAVIOR & CLOCK");
            draw_grouped_card({
                make_toggle_row("Intelligent Auto-Hide", "Hide bar when active workspace has windows",
                                cfg_.auto_hide, [this]() { cfg_.auto_hide = !cfg_.auto_hide; save_and_reload(); }),
                {"Clock Format", "12-hour or 24-hour menu bar clock",
                 [&](cairo_t* c, double rx, double rry, double rw, double rh) {
                     draw_segmented_control(c, rx + rw - 140.0, rry + (rh - 26.0) * 0.5, 126.0, 26.0,
                                            {"12h", "24h"}, cfg_.clock_format == "24h" ? 1 : 0,
                                            [this](int idx) {
                                                cfg_.clock_format = (idx == 1) ? "24h" : "12h";
                                                save_and_reload();
                                            }, body_top, body_bottom);
                 }, nullptr},
                make_toggle_row("Show Seconds", "", cfg_.show_seconds,
                                [this]() { cfg_.show_seconds = !cfg_.show_seconds; save_and_reload(); })
            });

            draw_section_label("MENU BAR ITEMS");
            draw_grouped_card({
                make_toggle_row("Application Menus", "File, Edit, View, Go, Window, Help",
                                cfg_.show_app_menu, [this]() { cfg_.show_app_menu = !cfg_.show_app_menu; save_and_reload(); }),
                make_toggle_row("Workspace Indicators", "Niri workspace dots in center",
                                cfg_.show_workspaces, [this]() { cfg_.show_workspaces = !cfg_.show_workspaces; save_and_reload(); }),
                make_toggle_row("Privacy Mic Indicator", "",
                                cfg_.show_privacy, [this]() { cfg_.show_privacy = !cfg_.show_privacy; save_and_reload(); }),
                make_toggle_row("System Tray", "StatusNotifier D-Bus icons",
                                cfg_.show_tray, [this]() { cfg_.show_tray = !cfg_.show_tray; save_and_reload(); }),
                make_toggle_row("Wi-Fi", "", cfg_.show_network,
                                [this]() { cfg_.show_network = !cfg_.show_network; save_and_reload(); }),
                make_toggle_row("Bluetooth", "", cfg_.show_bluetooth,
                                [this]() { cfg_.show_bluetooth = !cfg_.show_bluetooth; save_and_reload(); }),
                make_toggle_row("Sound", "", cfg_.show_volume,
                                [this]() { cfg_.show_volume = !cfg_.show_volume; save_and_reload(); }),
                make_toggle_row("Display Brightness", "", cfg_.show_brightness,
                                [this]() { cfg_.show_brightness = !cfg_.show_brightness; save_and_reload(); }),
                make_toggle_row("Battery", "", cfg_.show_battery,
                                [this]() { cfg_.show_battery = !cfg_.show_battery; save_and_reload(); }),
                make_toggle_row("Show Battery Percentage", "", cfg_.show_battery_pct,
                                [this]() { cfg_.show_battery_pct = !cfg_.show_battery_pct; save_and_reload(); }),
                make_toggle_row("Spotlight Search", "", cfg_.show_spotlight,
                                [this]() { cfg_.show_spotlight = !cfg_.show_spotlight; save_and_reload(); }),
                make_toggle_row("Control Center", "", cfg_.show_control_center,
                                [this]() { cfg_.show_control_center = !cfg_.show_control_center; save_and_reload(); }),
                make_toggle_row("Power Menu Icon", "Power actions also live in the top-left M menu", cfg_.show_power_menu,
                                [this]() { cfg_.show_power_menu = !cfg_.show_power_menu; save_and_reload(); })
            });
        } else if (active_cat_ == 3) {
            draw_section_label("WI-FI CONNECTION");
            std::vector<GroupRow> rows;
            rows.push_back(make_toggle_row("Wi-Fi", wifi_.connected ? ("Connected to " + wifi_.ssid) : (wifi_.enabled ? "Not Connected" : "Off"),
                                           wifi_.enabled, [this]() {
                mistbar::sysinfo::set_wifi_enabled(!wifi_.enabled);
                refresh_hw();
                win_.request_redraw();
            }));
            rows.push_back(make_action_row("Nearby Scan", "Refresh available Wi-Fi networks", "Rescan", false, [this]() {
                mistbar::sysinfo::rescan_wifi();
                refresh_hw();
                win_.request_redraw();
            }));
            if (wifi_.connected) {
                rows.push_back(make_action_row("Active: " + wifi_.ssid, "", "Disconnect", false, [this]() {
                    mistbar::sysinfo::disconnect_wifi(wifi_.ssid);
                    refresh_hw();
                    win_.request_redraw();
                }));
            }
            draw_grouped_card(rows);

            draw_section_label("KNOWN & NEARBY NETWORKS");
            if (!wifi_.networks.empty()) {
                std::vector<GroupRow> net_rows;
                for (const auto& net : wifi_.networks) {
                    net_rows.push_back(make_action_row(
                        net.ssid, "Signal " + std::to_string(net.signal) + "%",
                        net.in_use ? "Connected" : "Connect", net.in_use,
                        [this, net]() {
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
                    {wifi_.enabled ? "No Other Networks Found" : "Wi-Fi is Turned Off",
                     wifi_.enabled ? "Click Rescan above to search nearby access points" : "Turn on Wi-Fi above to view nearby networks",
                     nullptr, nullptr}
                });
            }
        } else if (active_cat_ == 4) {
            draw_section_label("BLUETOOTH");
            draw_grouped_card({
                make_toggle_row("Bluetooth", bt_.powered ? (bt_.connected_name.empty() ? "On" : bt_.connected_name) : "Off",
                                bt_.powered, [this]() {
                    mistbar::sysinfo::set_bluetooth_enabled(!bt_.powered);
                    refresh_hw();
                    win_.request_redraw();
                })
            });
            draw_section_label("MY DEVICES");
            if (!bt_.devices.empty()) {
                std::vector<GroupRow> dev_rows;
                for (const auto& dev : bt_.devices) {
                    dev_rows.push_back(make_action_row(
                        dev.name, dev.mac, dev.connected ? "Disconnect" : "Connect", dev.connected,
                        [this, dev]() {
                            mistbar::sysinfo::connect_bluetooth_device(dev.mac, !dev.connected);
                            refresh_hw();
                            win_.request_redraw();
                        }));
                }
                draw_grouped_card(dev_rows);
            } else {
                draw_grouped_card({
                    {bt_.powered ? "No Paired Devices Connected" : "Bluetooth is Turned Off",
                     bt_.powered ? "Devices paired via bluetoothctl appear here" : "Turn on Bluetooth above to manage devices",
                     nullptr, nullptr}
                });
            }
        } else if (active_cat_ == 5) {
            draw_section_label("OUTPUT & VOLUME");
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
            rows.push_back(make_toggle_row("Mute Output", "", audio_.muted, [this]() {
                mistbar::sysinfo::toggle_audio_mute();
                refresh_hw();
                win_.request_redraw();
            }));
            for (const auto& s : audio_.sinks) {
                rows.push_back(make_action_row(s.name, "", s.is_default ? "Active" : "Select", s.is_default, [this, s]() {
                    mistbar::sysinfo::set_default_audio_sink(s.id);
                    refresh_hw();
                    win_.request_redraw();
                }));
            }
            draw_grouped_card(rows);
        } else if (active_cat_ == 6) {
            draw_section_label("BRIGHTNESS");
            draw_slider_group({
                {"Display Brightness", std::to_string(brightness_.percent) + "%",
                 static_cast<double>(brightness_.percent - 5) / 95.0, 5.0, 100.0,
                 [this](double v) {
                     brightness_.percent = static_cast<int>(std::round(v));
                     mistbar::sysinfo::set_brightness_percent(brightness_.percent);
                     win_.request_redraw();
                 }}
            });
            draw_section_label("MISTBAR SERVICE");
            std::vector<GroupRow> svc_rows;
            svc_rows.push_back({"Daemon Status", daemon_status_, nullptr, nullptr});
            svc_rows.push_back(make_action_row(
                "Menu Bar Service", "", daemon_running_ ? "Restart" : "Start", true, [this]() {
                    if (daemon_running_) {
                        mistbar::ipc::send_command(mistbar::ipc::default_socket_path(), "quit");
                    }
                    mistbar::sysinfo::spawn_detached({resolve_sibling_binary("mistbar-daemon")});
                    refresh_hw();
                    win_.request_redraw();
                }));
            if (daemon_running_) {
                svc_rows.push_back(make_action_row("Stop Running Bar", "", "Stop", false, [this]() {
                    mistbar::ipc::send_command(mistbar::ipc::default_socket_path(), "quit");
                    refresh_hw();
                    win_.request_redraw();
                }));
            }
            draw_grouped_card(svc_rows);
        } else {
            // About
            mistbar::glass::append_rounded_rect(cr, cx, ry, cw, 126.0, 14.0);
            cairo_set_source_rgba(cr, pal.surface_elevated.r, pal.surface_elevated.g,
                                  pal.surface_elevated.b, pal.dark_mode ? 0.90 : 0.98);
            cairo_fill(cr);
            if (!logo_path.empty()) {
                win_.icon_cache().draw(cr, logo_path, cx + cw * 0.5 - 28.0, ry + 14.0, 56.0, 56.0, 0);
            }
            mistbar::window::draw_pango_text_centered(cr, "Mistbar", cx, ry + 76.0, cw, 20.0,
                                                     12.5, true, pal.text_primary, true, false);
            mistbar::window::draw_pango_text_centered(cr, std::string("Version ") + MISTBAR_VERSION + " - macOS Tahoe Liquid Glass",
                                                     cx, ry + 98.0, cw, 16.0, 8.5, false, pal.text_secondary, true, false);
            ry += 140.0;

            auto make_info = [](const std::string& k, const std::string& v) -> GroupRow {
                return {
                    k, "",
                    [v](cairo_t* c, double rx, double rry, double rw, double rh) {
                        mistbar::window::draw_pango_text_centered(
                            c, v, rx + 130.0, rry, rw - 144.0, rh, 8.6, false,
                            mistbar::themes::Color{0.62, 0.66, 0.74, 1.0}, false, true);
                    },
                    nullptr
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
                make_info("Config", cfg_display),
                make_action_row("Factory Reset", "Restore all Mistbar preferences", "Reset All", false, [this]() {
                    cfg_ = mistbar::config::MistbarConfig{};
                    save_and_reload();
                })
            });
        }

        const double content_end = ry + scroll_y_;
        max_scroll_y_ = std::max(0.0, content_end - body_bottom + 10.0);
        scroll_y_ = std::clamp(scroll_y_, 0.0, max_scroll_y_);

        cairo_restore(cr);

        if (max_scroll_y_ > 2.0) {
            const double view_h = body_bottom - body_top - 12.0;
            const double thumb_h = std::max(28.0, view_h * (view_h / (view_h + max_scroll_y_)));
            const double thumb_y = body_top + 6.0 + (scroll_y_ / max_scroll_y_) * (view_h - thumb_h);
            mistbar::glass::append_rounded_rect(cr, static_cast<double>(w) - 9.0, thumb_y, 3.0, thumb_h, 1.5);
            cairo_set_source_rgba(cr, pal.text_secondary.r, pal.text_secondary.g, pal.text_secondary.b, 0.25);
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
            if (active_cat_ == 1 && ev.x >= preview_x_ && ev.x <= preview_x_ + preview_w_ &&
                ev.y >= preview_y_ && ev.y <= preview_y_ + preview_h_) {
                preview_ptr_x_ = std::clamp((ev.x - preview_x_) / std::max(1.0, preview_w_), 0.0, 1.0);
                preview_ptr_y_ = std::clamp((ev.y - preview_y_) / std::max(1.0, preview_h_), 0.0, 1.0);
                win_.request_redraw();
            }
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
            if (p == "wifi" || p == "wi-fi") return 3;
            if (p == "bluetooth") return 4;
            if (p == "sound") return 5;
            if (p == "display") return 6;
            if (p == "about") return 7;
        }
    }
    return 1;
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
