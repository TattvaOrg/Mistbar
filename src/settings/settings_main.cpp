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

void draw_chevron_pair(cairo_t* cr, double cx, double cy, const mistbar::themes::Color& col) {
    cairo_save(cr);
    cairo_set_source_rgba(cr, col.r, col.g, col.b, 0.72);
    cairo_set_line_width(cr, 1.25);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

    cairo_move_to(cr, cx - 2.6, cy - 1.8);
    cairo_line_to(cr, cx, cy - 4.4);
    cairo_line_to(cr, cx + 2.6, cy - 1.8);

    cairo_move_to(cr, cx - 2.6, cy + 1.8);
    cairo_line_to(cr, cx, cy + 4.4);
    cairo_line_to(cr, cx + 2.6, cy + 1.8);
    cairo_stroke(cr);
    cairo_restore(cr);
}

void draw_glass_tiles_glyph(cairo_t* cr, double cx, double cy, double sz, const mistbar::themes::Color& col) {
    cairo_save(cr);
    cairo_set_source_rgba(cr, col.r, col.g, col.b, col.a);
    cairo_set_line_width(cr, 1.15);
    mistbar::glass::append_rounded_rect(cr, cx - sz * 0.48, cy - sz * 0.48, sz * 0.62, sz * 0.62, sz * 0.16);
    cairo_stroke(cr);
    mistbar::glass::append_rounded_rect(cr, cx - sz * 0.14, cy - sz * 0.14, sz * 0.62, sz * 0.62, sz * 0.16);
    cairo_stroke(cr);
    cairo_restore(cr);
}

class MistbarSettingsApp {
public:
    MistbarSettingsApp(mistbar::wl::Display& dpy, mistbar::config::MistbarConfig cfg, int initial_cat)
        : dpy_(dpy), win_(dpy), cfg_(std::move(cfg)), active_cat_(initial_cat) {
        categories_ = {
            {"Wi-Fi",          "categories/wifi.svg",         "#0A84FF", 0},
            {"Bluetooth",      "categories/bluetooth.svg",    "#0A84FF", 0},
            {"Energy",         "battery-charging.svg",        "#30D158", 0},
            {"Appearance",     "categories/appearance.svg",   "#2C2C30", 1},
            {"Liquid Glass",   "categories/liquid-glass.svg", "#5E5CE6", 1},
            {"Menu Bar",       "categories/menubar.svg",      "#48484D", 1},
            {"Control Center", "control-center.svg",          "#48484D", 1},
            {"Sound",          "categories/sound.svg",        "#FF375F", 2},
            {"Display",        "categories/display.svg",      "#0A84FF", 2},
            {"About",          "categories/about.svg",        "#48484D", 2}
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
        return win_.create("Mistbar Settings", "mistbar-settings", 840, 648, cfg_, 18, true);
    }

    bool is_open() const noexcept { return win_.is_open(); }

    void refresh_hw() {
        if (active_cat_ == 0) wifi_ = mistbar::sysinfo::get_wifi_status(true);
        else if (active_cat_ == 1) bt_ = mistbar::sysinfo::get_bluetooth_status(true);
        else if (active_cat_ == 2) battery_ = mistbar::sysinfo::get_battery_status(true);
        else if (active_cat_ == 7) audio_ = mistbar::sysinfo::get_audio_status(true);
        else if (active_cat_ == 8) brightness_ = mistbar::sysinfo::get_brightness_status();
        std::string st;
        daemon_running_ = mistbar::ipc::send_command(mistbar::ipc::default_socket_path(), "status", &st);
        daemon_status_ = daemon_running_ ? st : "Not running";
    }

    void request_redraw() { win_.request_redraw(); }

    void render_offscreen_png(const std::string& out_path) {
        const int w = 840;
        const int h = 648;
        const int m = 18;
        win_.apply_config(cfg_);
        wifi_ = {true, true, "Tahoe-5G", {}};
        bt_ = {true, true, "AirPods Pro", {}};
        battery_ = {true, true, 92, "Charging", "balanced"};
        audio_ = {72, false, {}};
        brightness_ = {true, 85};
        daemon_status_ = "running (theme: dark, preset: regular, look: attached, gpu_glass: on)";

        cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w + m * 2, h + m * 2);
        cairo_t* cr = cairo_create(surf);

        const auto& pal = win_.palette();
        mistbar::window::draw_window_shadow(cr, m, m, w, h, pal.window_radius, 24, true);
        cairo_save(cr);
        mistbar::glass::append_rounded_rect(cr, m, m, w, h, pal.window_radius);
        cairo_clip(cr);
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 1.0);
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
        std::string badge_hex;
        int group = 0;
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
        double fixed_height = 0.0;
    };

    mistbar::wl::Display& dpy_;
    mistbar::window::MistbarWindow win_;
    mistbar::config::MistbarConfig cfg_;
    std::vector<CategoryItem> categories_;
    int active_cat_ = 3; // Appearance by default
    std::vector<int> back_hist_;
    std::vector<int> fwd_hist_;
    double scroll_y_ = 0.0;
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
    int preview_h_cached_ = 0;

    mistbar::sysinfo::WifiStatus wifi_;
    mistbar::sysinfo::BluetoothStatus bt_;
    mistbar::sysinfo::BatteryStatus battery_;
    mistbar::sysinfo::AudioStatus audio_;
    mistbar::sysinfo::BrightnessStatus brightness_;
    bool daemon_running_ = false;
    std::string daemon_status_ = "Checking...";

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

    // Pure black monochrome geometric backdrop for Liquid Glass preview (zero photos/auroras)
    void ensure_preview_backdrop(int pw, int ph, int blur_r) {
        if (pw <= 0 || ph <= 0) return;
        if (!preview_raw_ || preview_w_cached_ != pw || preview_h_cached_ != ph) {
            if (preview_raw_) cairo_surface_destroy(preview_raw_);
            if (preview_blurred_) cairo_surface_destroy(preview_blurred_);
            preview_raw_ = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pw, ph);
            preview_blurred_ = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pw, ph);
            preview_w_cached_ = pw;
            preview_h_cached_ = ph;
            preview_blur_cached_ = -1;

            cairo_t* pcr = cairo_create(preview_raw_);
            cairo_set_source_rgb(pcr, 0.0, 0.0, 0.0);
            cairo_paint(pcr);

            // Crisp monochrome architectural lines + gentle grayscale radial depth
            cairo_pattern_t* sheen = cairo_pattern_create_radial(
                pw * 0.55, ph * 0.38, 6.0, pw * 0.55, ph * 0.38, pw * 0.62);
            cairo_pattern_add_color_stop_rgba(sheen, 0.0, 0.24, 0.24, 0.27, 0.52);
            cairo_pattern_add_color_stop_rgba(sheen, 0.6, 0.08, 0.08, 0.09, 0.25);
            cairo_pattern_add_color_stop_rgba(sheen, 1.0, 0.00, 0.00, 0.00, 0.0);
            cairo_set_source(pcr, sheen);
            cairo_paint(pcr);
            cairo_pattern_destroy(sheen);

            cairo_set_source_rgba(pcr, 1.0, 1.0, 1.0, 0.07);
            cairo_set_line_width(pcr, 1.0);
            for (int x = 18; x < pw; x += 28) {
                cairo_move_to(pcr, static_cast<double>(x), 0.0);
                cairo_line_to(pcr, static_cast<double>(x), static_cast<double>(ph));
            }
            for (int y = 18; y < ph; y += 28) {
                cairo_move_to(pcr, 0.0, static_cast<double>(y));
                cairo_line_to(pcr, static_cast<double>(pw), static_cast<double>(y));
            }
            cairo_stroke(pcr);

            // Subtle monochrome horizon silhouettes so glass refraction bends visible shapes on pure black
            cairo_set_source_rgba(pcr, 1.0, 1.0, 1.0, 0.08);
            cairo_move_to(pcr, 0.0, ph * 0.68);
            cairo_line_to(pcr, pw * 0.28, ph * 0.42);
            cairo_line_to(pcr, pw * 0.54, ph * 0.62);
            cairo_line_to(pcr, pw * 0.80, ph * 0.36);
            cairo_line_to(pcr, static_cast<double>(pw), ph * 0.58);
            cairo_line_to(pcr, static_cast<double>(pw), static_cast<double>(ph));
            cairo_line_to(pcr, 0.0, static_cast<double>(ph));
            cairo_close_path(pcr);
            cairo_fill(pcr);

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

    void draw_macos_switch(cairo_t* cr, double x, double y, bool on) {
        const auto& pal = win_.palette();
        mistbar::glass::append_rounded_rect(cr, x, y, 38.0, 22.0, 11.0);
        if (on) {
            cairo_set_source_rgba(cr, pal.accent.r, pal.accent.g, pal.accent.b, 0.98);
        } else {
            cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.16);
        }
        cairo_fill(cr);

        const double kx = on ? (x + 27.0) : (x + 11.0);
        const double ky = y + 11.0;
        cairo_arc(cr, kx, ky + 1.0, 9.0, 0.0, 6.283185);
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.28);
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
        cairo_set_source_rgba(cr, 0.04, 0.04, 0.05, 0.95);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.10);
        cairo_set_line_width(cr, 0.8);
        cairo_stroke(cr);

        const int n = static_cast<int>(labels.size());
        if (n <= 0) return;
        const double seg_w = (w - 4.0) / static_cast<double>(n);
        for (int i = 0; i < n; ++i) {
            const double sx = x + 2.0 + static_cast<double>(i) * seg_w;
            const double sy = y + 2.0;
            const double sh = h - 4.0;
            if (i == active_idx) {
                mistbar::glass::append_rounded_rect(cr, sx, sy, seg_w, sh, 6.5);
                cairo_set_source_rgba(cr, 0.18, 0.18, 0.21, 0.98);
                cairo_fill_preserve(cr);
                cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.16);
                cairo_set_line_width(cr, 0.8);
                cairo_stroke(cr);
            }
            mistbar::window::draw_pango_text_centered(
                cr, labels[static_cast<std::size_t>(i)], sx, sy, seg_w, sh,
                8.4, i == active_idx, pal.text_primary, true, false);

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
        mistbar::glass::append_rounded_rect(cr, x, y, w, h, 8.0);
        if (primary) {
            cairo_set_source_rgba(cr, pal.accent.r, pal.accent.g, pal.accent.b, 0.95);
        } else {
            cairo_set_source_rgba(cr, 0.14, 0.14, 0.16, 0.96);
        }
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, primary ? 0.22 : 0.12);
        cairo_set_line_width(cr, 0.8);
        cairo_stroke(cr);

        mistbar::window::draw_pango_text_centered(
            cr, label, x, y, w, h, 8.5, primary,
            primary ? mistbar::themes::Color{1, 1, 1, 1} : pal.text_primary, true, false);
        if (y + h > body_top && y < body_bottom) {
            clicks_.push_back({x, std::max(body_top, y), w, h, std::move(on_click)});
        }
    }

    void draw_dropdown_pill(cairo_t* cr, double x, double y, double w, double h,
                            const std::string& label, bool has_dot, const mistbar::themes::Color& dot_col,
                            std::function<void()> on_click, double body_top, double body_bottom) {
        const auto& pal = win_.palette();
        mistbar::glass::append_rounded_rect(cr, x, y, w, h, 7.5);
        cairo_set_source_rgba(cr, 0.14, 0.14, 0.16, 0.96);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.11);
        cairo_set_line_width(cr, 0.8);
        cairo_stroke(cr);

        double tx = x + 10.0;
        if (has_dot) {
            cairo_arc(cr, x + 14.0, y + h * 0.5, 5.5, 0.0, 6.283185);
            cairo_set_source_rgba(cr, dot_col.r, dot_col.g, dot_col.b, 0.98);
            cairo_fill(cr);
            tx += 14.0;
        }
        mistbar::window::draw_pango_text_centered(cr, label, tx, y, w - (tx - x) - 22.0, h,
                                                 8.5, false, pal.text_primary);
        draw_chevron_pair(cr, x + w - 12.0, y + h * 0.5, pal.text_secondary);

        if (y + h > body_top && y < body_bottom) {
            clicks_.push_back({x, std::max(body_top, y), w, h, std::move(on_click)});
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
            lp.cursor_reactive = pal.cursor_reactive;
            lp.dark_mode = true;
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

    void draw(cairo_t* cr, int w, int h) {
        clicks_.clear();
        sliders_.clear();
        const auto& pal = win_.palette();

        // Pure black main window canvas
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 1.0);
        cairo_paint(cr);

        // Left Sidebar matching Golden Gate System Settings (dark obsidian panel with crisp 1px separator)
        const double sb_w = 224.0;
        cairo_rectangle(cr, 0.0, 0.0, sb_w, static_cast<double>(h));
        cairo_set_source_rgba(cr, 0.05, 0.05, 0.06, 1.0);
        cairo_fill(cr);

        cairo_rectangle(cr, sb_w, 0.0, 1.0, static_cast<double>(h));
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.08);
        cairo_fill(cr);

        // Sidebar Search Pill (below traffic lights at y=22)
        const double search_x = 14.0;
        const double search_y = 52.0;
        const double search_w = sb_w - 28.0;
        const double search_h = 28.0;
        mistbar::glass::append_rounded_rect(cr, search_x, search_y, search_w, search_h, 8.0);
        cairo_set_source_rgba(cr, 0.11, 0.11, 0.13, 1.0);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, pal.accent.r, pal.accent.g, pal.accent.b, search_focused_ ? 0.90 : 0.0);
        cairo_set_line_width(cr, 1.4);
        cairo_stroke(cr);

        const std::string srch_svg = mistbar::icons::resolve_icon("search.svg");
        if (!srch_svg.empty()) {
            win_.icon_cache().draw(cr, srch_svg, search_x + 9.0, search_y + 7.5, 13.0, 13.0, 0xFF8E8E93U);
        }
        const std::string search_lbl = search_query_.empty() ? "Search" : search_query_;
        mistbar::window::draw_pango_text_centered(cr, search_lbl, search_x + 28.0, search_y, search_w - 36.0, search_h,
                                                 8.8, false, search_query_.empty() ? pal.text_secondary : pal.text_primary);
        clicks_.push_back({search_x, search_y, search_w, search_h, [this]() {
            search_focused_ = true;
            win_.request_redraw();
        }});

        // Compact Mistbar identity row below Search (matching profile row spacing in screenshot)
        const double id_y = 92.0;
        mistbar::glass::append_rounded_rect(cr, 16.0, id_y + 2.0, 32.0, 32.0, 16.0);
        cairo_set_source_rgba(cr, 0.13, 0.13, 0.15, 1.0);
        cairo_fill(cr);
        const std::string mark_svg = mistbar::icons::resolve_icon("mistbar-mark.svg");
        if (!mark_svg.empty()) {
            win_.icon_cache().draw(cr, mark_svg, 23.0, id_y + 9.0, 18.0, 18.0, 0xFFF5F5F7U);
        }
        mistbar::window::draw_pango_text_centered(cr, "Mistbar Shell", 56.0, id_y + 2.0, 150.0, 16.0,
                                                 9.2, true, pal.text_primary);
        mistbar::window::draw_pango_text_centered(cr, daemon_running_ ? "Daemon Active" : "Liquid Glass",
                                                 56.0, id_y + 18.0, 150.0, 14.0, 7.6, false, pal.text_secondary);

        // Grouped Sidebar Categories
        double sy = 142.0;
        int prev_group = 0;
        const std::string q_low = to_lower_copy(search_query_);
        for (int i = 0; i < static_cast<int>(categories_.size()); ++i) {
            const auto& cat = categories_[static_cast<std::size_t>(i)];
            if (!q_low.empty() && to_lower_copy(cat.name).find(q_low) == std::string::npos) {
                continue;
            }
            if (cat.group != prev_group) {
                sy += 10.0;
                prev_group = cat.group;
            }
            const bool sel = (i == active_cat_);
            const double rx = 12.0;
            const double rw = sb_w - 24.0;
            if (sel) {
                mistbar::glass::append_rounded_rect(cr, rx, sy, rw, 30.0, 8.0);
                cairo_set_source_rgba(cr, pal.accent.r, pal.accent.g, pal.accent.b, 0.96);
                cairo_fill(cr);
            }
            const std::string ic = mistbar::icons::resolve_icon(cat.icon);
            if (cat.icon.rfind("categories/", 0) == 0) {
                if (!ic.empty()) {
                    win_.icon_cache().draw(cr, ic, rx + 7.0, sy + 4.5, 21.0, 21.0, 0);
                }
            } else {
                const auto badge_col = mistbar::themes::Color::from_hex(cat.badge_hex);
                mistbar::glass::append_rounded_rect(cr, rx + 7.0, sy + 4.5, 21.0, 21.0, 5.5);
                cairo_set_source_rgba(cr, badge_col.r, badge_col.g, badge_col.b, 0.98);
                cairo_fill(cr);
                if (!ic.empty()) {
                    win_.icon_cache().draw(cr, ic, rx + 11.0, sy + 8.5, 13.0, 13.0, 0xFFFFFFFFU);
                }
            }
            const mistbar::themes::Color tc = sel ? mistbar::themes::Color{1, 1, 1, 1} : pal.text_primary;
            mistbar::window::draw_pango_text_centered(cr, cat.name, rx + 35.0, sy, rw - 42.0, 30.0,
                                                     9.1, sel, tc);
            clicks_.push_back({rx, sy, rw, 30.0, [this, i]() {
                select_category(i);
            }});
            sy += 32.0;
        }

        // Right Content Pane on pure #000000 black
        const double cx = sb_w + 24.0;
        const double cw = static_cast<double>(w) - cx - 24.0;
        const double header_h = 54.0;

        // Unified < | > header pill matching Golden Gate System Settings screenshot
        const double pill_x = cx;
        const double pill_y = 13.0;
        const double pill_w = 64.0;
        const double pill_h = 28.0;
        mistbar::glass::append_rounded_rect(cr, pill_x, pill_y, pill_w, pill_h, 14.0);
        cairo_set_source_rgba(cr, 0.11, 0.11, 0.13, 1.0);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.12);
        cairo_set_line_width(cr, 0.9);
        cairo_stroke(cr);

        // Center divider inside < | > pill
        cairo_rectangle(cr, pill_x + pill_w * 0.5 - 0.5, pill_y + 6.0, 1.0, pill_h - 12.0);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.12);
        cairo_fill(cr);

        auto draw_nav_chevron = [&](double bx, bool is_back, bool enabled) {
            cairo_new_path(cr);
            const double mx = bx + (is_back ? -1.0 : 1.0);
            const double my = pill_y + pill_h * 0.5;
            const double dir = is_back ? 3.8 : -3.8;
            cairo_move_to(cr, mx + dir, my - 4.4);
            cairo_line_to(cr, mx - dir * 0.3, my);
            cairo_line_to(cr, mx + dir, my + 4.4);
            cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, enabled ? 0.90 : 0.28);
            cairo_set_line_width(cr, 1.5);
            cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
            cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
            cairo_stroke(cr);
        };
        draw_nav_chevron(pill_x + 16.0, true, !back_hist_.empty());
        draw_nav_chevron(pill_x + 48.0, false, !fwd_hist_.empty());

        if (!back_hist_.empty()) {
            clicks_.push_back({pill_x, pill_y, pill_w * 0.5, pill_h, [this]() {
                fwd_hist_.push_back(active_cat_);
                active_cat_ = back_hist_.back();
                back_hist_.pop_back();
                scroll_y_ = 0.0;
                refresh_hw();
                win_.request_redraw();
            }});
        }
        if (!fwd_hist_.empty()) {
            clicks_.push_back({pill_x + pill_w * 0.5, pill_y, pill_w * 0.5, pill_h, [this]() {
                back_hist_.push_back(active_cat_);
                active_cat_ = fwd_hist_.back();
                fwd_hist_.pop_back();
                scroll_y_ = 0.0;
                refresh_hw();
                win_.request_redraw();
            }});
        }

        mistbar::window::draw_pango_text_centered(
            cr, categories_[static_cast<std::size_t>(active_cat_)].name,
            cx + 78.0, 9.0, cw - 78.0, 36.0, 13.6, true, pal.text_primary);

        const double body_top = header_h;
        const double body_bottom = static_cast<double>(h) - 12.0;
        cairo_save(cr);
        cairo_rectangle(cr, sb_w + 2.0, body_top, static_cast<double>(w) - (sb_w + 2.0),
                        body_bottom - body_top);
        cairo_clip(cr);

        double ry = body_top + 4.0 - scroll_y_;

        auto draw_section_label = [&](const std::string& label, bool bold_title = false) {
            mistbar::window::draw_pango_text_centered(cr, label, cx + 6.0, ry, cw - 12.0, 22.0,
                                                     bold_title ? 10.2 : 8.2, true,
                                                     bold_title ? pal.text_primary : pal.text_secondary);
            ry += 26.0;
        };

        auto draw_grouped_card = [&](const std::vector<GroupRow>& rows) {
            if (rows.empty()) return;
            double total_h = 0.0;
            std::vector<double> heights;
            heights.reserve(rows.size());
            for (const auto& r : rows) {
                const double rh = (r.fixed_height > 0.0) ? r.fixed_height : (r.subtitle.empty() ? 42.0 : 52.0);
                heights.push_back(rh);
                total_h += rh;
            }

            mistbar::glass::append_rounded_rect(cr, cx, ry, cw, total_h, 12.0);
            cairo_set_source_rgba(cr, 0.065, 0.065, 0.075, 1.0);
            cairo_fill_preserve(cr);
            cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.09);
            cairo_set_line_width(cr, 1.0);
            cairo_stroke(cr);

            double cur_y = ry;
            for (std::size_t i = 0; i < rows.size(); ++i) {
                const auto& r = rows[i];
                const double rh = heights[i];
                if (!r.title.empty()) {
                    if (r.fixed_height > 60.0) {
                        mistbar::window::draw_pango_text_centered(cr, r.title, cx + 16.0, cur_y + 12.0, 170.0, 20.0,
                                                                 9.4, false, pal.text_primary);
                    } else if (r.subtitle.empty()) {
                        mistbar::window::draw_pango_text_centered(cr, r.title, cx + 16.0, cur_y, cw - 160.0, rh,
                                                                 9.3, false, pal.text_primary);
                    } else {
                        mistbar::window::draw_pango_text_centered(cr, r.title, cx + 16.0, cur_y + 6.0, cw - 160.0, 18.0,
                                                                 9.3, false, pal.text_primary);
                        mistbar::window::draw_pango_text_centered(cr, r.subtitle, cx + 16.0, cur_y + 25.0, cw - 160.0, 16.0,
                                                                 7.9, false, pal.text_secondary);
                    }
                }
                if (r.render_right) r.render_right(cr, cx, cur_y, cw, rh);
                if (r.register_hit && cur_y + rh > body_top && cur_y < body_bottom) {
                    r.register_hit(cx, std::max(body_top, cur_y), cw, rh);
                }
                if (i + 1 < rows.size()) {
                    cairo_rectangle(cr, cx + 16.0, cur_y + rh - 0.5, cw - 32.0, 0.8);
                    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.08);
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
                    draw_macos_switch(c, rx + rw - 54.0, rry + (rh - 22.0) * 0.5, val);
                },
                [this, cb](double rx, double rry, double rw, double rh) {
                    clicks_.push_back({rx, rry, rw, rh, cb});
                },
                0.0
            };
        };

        auto make_action_row = [&](const std::string& title, const std::string& sub, const std::string& btn,
                                   bool primary, std::function<void()> cb) -> GroupRow {
            return {
                title, sub,
                [this, btn, primary, cb, body_top, body_bottom](cairo_t* c, double rx, double rry, double rw, double rh) {
                    draw_pill_button(c, rx + rw - 110.0, rry + (rh - 26.0) * 0.5, 94.0, 26.0,
                                     btn, primary, cb, body_top, body_bottom);
                },
                nullptr,
                0.0
            };
        };

        auto draw_slider_group = [&](const std::vector<std::tuple<std::string, std::string, double, double, double, std::function<void(double)>>>& items) {
            const double row_h = 52.0;
            const double total_h = static_cast<double>(items.size()) * row_h;
            mistbar::glass::append_rounded_rect(cr, cx, ry, cw, total_h, 12.0);
            cairo_set_source_rgba(cr, 0.065, 0.065, 0.075, 1.0);
            cairo_fill_preserve(cr);
            cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.09);
            cairo_set_line_width(cr, 1.0);
            cairo_stroke(cr);

            double cur_y = ry;
            for (std::size_t i = 0; i < items.size(); ++i) {
                const auto& [lbl, val_txt, norm, min_v, max_v, on_ch] = items[i];
                mistbar::window::draw_pango_text_centered(cr, lbl, cx + 16.0, cur_y + 5.0, cw - 110.0, 18.0,
                                                         9.1, false, pal.text_primary);
                mistbar::window::draw_pango_text_centered(cr, val_txt, cx + cw - 94.0, cur_y + 5.0, 78.0, 18.0,
                                                         8.6, true, pal.accent, false, true);

                const double tx = cx + 16.0;
                const double ty = cur_y + 32.0;
                const double tw = cw - 32.0;
                const double th = 5.0;
                mistbar::glass::append_rounded_rect(cr, tx, ty, tw, th, 2.5);
                cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.14);
                cairo_fill(cr);

                const double cl = std::clamp(norm, 0.0, 1.0);
                mistbar::glass::append_rounded_rect(cr, tx, ty, std::max(5.0, cl * tw), th, 2.5);
                cairo_set_source_rgba(cr, pal.accent.r, pal.accent.g, pal.accent.b, 0.96);
                cairo_fill(cr);

                const double kx = tx + cl * tw;
                cairo_arc(cr, kx, ty + 2.5, 7.0, 0.0, 6.283185);
                cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.99);
                cairo_fill(cr);

                if (cur_y + row_h > body_top && cur_y < body_bottom) {
                    sliders_.push_back({tx - 6.0, std::max(body_top, ty - 10.0), tw + 12.0, 24.0, min_v, max_v, on_ch});
                }
                if (i + 1 < items.size()) {
                    cairo_rectangle(cr, cx + 16.0, cur_y + row_h - 0.5, cw - 32.0, 0.8);
                    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.08);
                    cairo_fill(cr);
                }
                cur_y += row_h;
            }
            ry += total_h + 14.0;
        };

        if (active_cat_ == 0) {
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
                     nullptr, nullptr, 0.0}
                });
            }
        } else if (active_cat_ == 1) {
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
                     nullptr, nullptr, 0.0}
                });
            }
        } else if (active_cat_ == 2) {
            draw_section_label("BATTERY & ENERGY");
            draw_grouped_card({
                {"Battery Level", battery_.present ? (battery_.state_text + " (" + std::to_string(battery_.percent) + "%)") : "AC Power",
                 [this](cairo_t* c, double rx, double rry, double rw, double rh) {
                     const std::string pct = battery_.present ? (std::to_string(battery_.percent) + "%") : "AC";
                     mistbar::window::draw_pango_text_centered(c, pct, rx + rw - 80.0, rry, 64.0, rh, 10.0, true,
                                                              win_.palette().text_primary, false, true);
                 }, nullptr, 0.0},
                {"Energy Mode", "System power profile",
                 [&](cairo_t* c, double rx, double rry, double rw, double rh) {
                     const char* profs[3] = {"power-saver", "balanced", "performance"};
                     int act = 1;
                     for (int i = 0; i < 3; ++i) {
                         if (battery_.power_profile == profs[i]) act = i;
                     }
                     draw_segmented_control(c, rx + rw - 252.0, rry + (rh - 26.0) * 0.5, 236.0, 26.0,
                                            {"Low Power", "Automatic", "High"}, act,
                                            [this, profs](int idx) {
                                                mistbar::sysinfo::set_power_profile(profs[idx]);
                                                refresh_hw();
                                                win_.request_redraw();
                                            }, body_top, body_bottom);
                 }, nullptr, 0.0},
                make_toggle_row("Show Percentage in Menu Bar", "Display battery percentage next to icon",
                                cfg_.show_battery_pct, [this]() { cfg_.show_battery_pct = !cfg_.show_battery_pct; save_and_reload(); })
            });
        } else if (active_cat_ == 3) {
            // Exact Golden Gate System Settings layout on pure black
            draw_grouped_card({
                {"Appearance", "",
                 [&](cairo_t* c, double rx, double rry, double rw, double rh) {
                     (void)rh;
                     struct ThumbSpec { const char* label; const char* preset; double bg; double card; double rim; };
                     const ThumbSpec thumbs[3] = {
                         {"Solid Black", "solid", 0.02, 0.09, 0.14},
                         {"Liquid Dark", "dark",  0.04, 0.14, 0.34},
                         {"Clear Glass", "clear", 0.01, 0.06, 0.55}
                     };
                     int sel_idx = 1;
                     if (cfg_.glass_preset == "solid") sel_idx = 0;
                     else if (cfg_.glass_preset == "clear") sel_idx = 2;

                     const double tw = 74.0;
                     const double th = 46.0;
                     const double gap = 12.0;
                     const double start_x = rx + rw - 16.0 - (3.0 * tw + 2.0 * gap);
                     const double ty = rry + 12.0;

                     for (int i = 0; i < 3; ++i) {
                         const double bx = start_x + static_cast<double>(i) * (tw + gap);
                         const bool sel = (i == sel_idx);

                         mistbar::glass::append_rounded_rect(c, bx, ty, tw, th, 8.0);
                         cairo_set_source_rgba(c, thumbs[i].bg, thumbs[i].bg, thumbs[i].bg + 0.01, 1.0);
                         cairo_fill(c);

                         // Mini window frame inside thumbnail
                         mistbar::glass::append_rounded_rect(c, bx + 10.0, ty + 8.0, tw - 10.0, th - 8.0, 5.0);
                         cairo_set_source_rgba(c, thumbs[i].card, thumbs[i].card, thumbs[i].card + 0.02, 0.96);
                         cairo_fill(c);

                         // Mini traffic dots
                         const double dots[3][3] = {{1.0, 0.37, 0.34}, {1.0, 0.74, 0.18}, {0.16, 0.78, 0.25}};
                         for (int d = 0; d < 3; ++d) {
                             cairo_arc(c, bx + 16.0 + d * 5.0, ty + 14.0, 1.5, 0.0, 6.283185);
                             cairo_set_source_rgba(c, dots[d][0], dots[d][1], dots[d][2], 0.95);
                             cairo_fill(c);
                         }
                         // Mini accent pill bar
                         mistbar::glass::append_rounded_rect(c, bx + 15.0, ty + 21.0, 26.0, 6.0, 3.0);
                         cairo_set_source_rgba(c, pal.accent.r, pal.accent.g, pal.accent.b, 0.92);
                         cairo_fill(c);

                         mistbar::glass::append_rounded_rect(c, bx, ty, tw, th, 8.0);
                         if (sel) {
                             cairo_set_source_rgba(c, pal.accent.r, pal.accent.g, pal.accent.b, 0.98);
                             cairo_set_line_width(c, 2.2);
                         } else {
                             cairo_set_source_rgba(c, 1.0, 1.0, 1.0, thumbs[i].rim * 0.45);
                             cairo_set_line_width(c, 1.0);
                         }
                         cairo_stroke(c);

                         mistbar::window::draw_pango_text_centered(
                             c, thumbs[i].label, bx - 4.0, ty + th + 4.0, tw + 8.0, 16.0,
                             7.8, sel, sel ? pal.text_primary : pal.text_secondary, true, false);

                         if (ty + th + 20.0 > body_top && ty < body_bottom) {
                             const std::string preset_name = thumbs[i].preset;
                             clicks_.push_back({bx, std::max(body_top, ty), tw, th + 20.0, [this, preset_name]() {
                                 mistbar::config::apply_glass_preset(cfg_, preset_name);
                                 save_and_reload();
                             }});
                         }
                     }
                 }, nullptr, 84.0},
                {"Liquid Glass", "",
                 [&](cairo_t* c, double rx, double rry, double rw, double rh) {
                     (void)rh;
                     const double pw = 290.0;
                     const double ph = 132.0;
                     const double px = rx + rw - 16.0 - pw;
                     const double py = rry + 12.0;
                     preview_x_ = px;
                     preview_y_ = py;
                     preview_w_ = pw;
                     preview_h_ = ph;

                     ensure_preview_backdrop(static_cast<int>(pw), static_cast<int>(ph), cfg_.blur_radius);

                     cairo_save(c);
                     mistbar::glass::append_rounded_rect(c, px, py, pw, ph, 12.0);
                     cairo_clip(c);

                     if (preview_raw_) {
                         cairo_set_source_surface(c, preview_raw_, px, py);
                         cairo_paint(c);
                     }

                     // Floating glass pill controls inside pure black preview (matching screenshot toolbar pills)
                     const double bar_y = py + 10.0;
                     draw_preview_glass(c, px, py, px + 10.0, bar_y, 70.0, 28.0, 14.0, pal.bar_bg);
                     draw_preview_glass(c, px, py, px + 88.0, bar_y, 28.0, 28.0, 14.0, pal.bar_bg);
                     draw_preview_glass(c, px, py, px + 124.0, bar_y, 156.0, 28.0, 14.0, pal.bar_bg);

                     // Crisp vector glyphs inside glass pills (zero emojis)
                     const std::string srch_p = mistbar::icons::resolve_icon("search.svg");
                     if (!srch_p.empty()) {
                         win_.icon_cache().draw(c, srch_p, px + 135.0, bar_y + 7.5, 13.0, 13.0, 0xFFE5E5EAU);
                     }
                     mistbar::window::draw_pango_text_centered(c, "Search", px + 153.0, bar_y, 90.0, 28.0,
                                                              8.6, false, pal.text_primary);
                     draw_glass_tiles_glyph(c, px + 102.0, bar_y + 14.0, 13.0, pal.text_primary);
                     draw_chevron_pair(c, px + 28.0, bar_y + 14.0, pal.text_primary);
                     for (int d = 0; d < 3; ++d) {
                         cairo_arc(c, px + 50.0 + d * 6.0, bar_y + 14.0, 1.3, 0.0, 6.283185);
                         cairo_set_source_rgba(c, 0.96, 0.96, 0.97, 0.85);
                         cairo_fill(c);
                     }

                     // Subtle dark footer inside preview card (replacing "Golden Gate Bridge" caption)
                     cairo_rectangle(c, px, py + ph - 28.0, pw, 28.0);
                     cairo_set_source_rgba(c, 0.02, 0.02, 0.03, 0.82);
                     cairo_fill(c);
                     mistbar::window::draw_pango_text_centered(c, "Mistbar Liquid Glass", px + 14.0, py + ph - 26.0,
                                                              pw - 28.0, 24.0, 9.2, true, pal.text_primary);
                     cairo_restore(c);

                     mistbar::glass::append_rounded_rect(c, px + 0.5, py + 0.5, pw - 1.0, ph - 1.0, 12.0);
                     cairo_set_source_rgba(c, 1.0, 1.0, 1.0, 0.14);
                     cairo_set_line_width(c, 1.0);
                     cairo_stroke(c);

                     // Slider underneath preview card (matching screenshot's icon-slider-icon row)
                     const double sy = py + ph + 16.0;
                     draw_glass_tiles_glyph(c, px + 8.0, sy + 2.5, 13.0, pal.text_secondary);
                     draw_glass_tiles_glyph(c, px + pw - 8.0, sy + 2.5, 14.0, pal.text_primary);

                     const double tx = px + 24.0;
                     const double tw = pw - 48.0;
                     mistbar::glass::append_rounded_rect(c, tx, sy, tw, 4.0, 2.0);
                     cairo_set_source_rgba(c, 1.0, 1.0, 1.0, 0.16);
                     cairo_fill(c);

                     const double cl = std::clamp(static_cast<double>(cfg_.refraction_strength), 0.0, 1.0);
                     mistbar::glass::append_rounded_rect(c, tx, sy, std::max(4.0, cl * tw), 4.0, 2.0);
                     cairo_set_source_rgba(c, 1.0, 1.0, 1.0, 0.85);
                     cairo_fill(c);

                     cairo_arc(c, tx + cl * tw, sy + 2.0, 7.5, 0.0, 6.283185);
                     cairo_set_source_rgba(c, 1.0, 1.0, 1.0, 0.99);
                     cairo_fill(c);

                     if (sy + 14.0 > body_top && sy - 10.0 < body_bottom) {
                         sliders_.push_back({tx - 6.0, std::max(body_top, sy - 10.0), tw + 12.0, 24.0, 0.0, 1.0,
                             [this](double v) {
                                 cfg_.refraction_strength = static_cast<float>(v);
                                 cfg_.specular_intensity = static_cast<float>(std::clamp(0.20 + v * 0.65, 0.0, 1.0));
                                 save_and_reload();
                             }});
                     }
                 }, nullptr, 182.0}
            });

            draw_section_label("Theme", true);
            struct SwatchInfo { const char* hex; const char* name; };
            const SwatchInfo swatches[9] = {
                {"#0A84FF", "Blue"},
                {"#BF5AF2", "Purple"},
                {"#FF375F", "Pink"},
                {"#FF453A", "Red"},
                {"#FF9F0A", "Orange"},
                {"#FFD60A", "Yellow"},
                {"#30D158", "Green"},
                {"#8E8E93", "Graphite"},
                {"#AEAEB2", "Silver"}
            };
            draw_grouped_card({
                {"Color", "",
                 [&](cairo_t* c, double rx, double rry, double rw, double rh) {
                     (void)rh;
                     const double start_x = rx + rw - 16.0 - 9.0 * 26.0;
                     const double sy_c = rry + 22.0;
                     for (int i = 0; i < 9; ++i) {
                         const double sx = start_x + static_cast<double>(i) * 26.0 + 10.0;
                         const auto col = mistbar::themes::Color::from_hex(swatches[i].hex);
                         const bool sel = (cfg_.accent_color == swatches[i].hex);
                         cairo_arc(c, sx, sy_c, 9.0, 0.0, 6.283185);
                         cairo_set_source_rgba(c, col.r, col.g, col.b, 1.0);
                         cairo_fill(c);
                         if (sel) {
                             cairo_arc(c, sx, sy_c, 12.0, 0.0, 6.283185);
                             cairo_set_source_rgba(c, pal.accent.r, pal.accent.g, pal.accent.b, 0.96);
                             cairo_set_line_width(c, 1.8);
                             cairo_stroke(c);
                             mistbar::window::draw_pango_text_centered(
                                 c, swatches[i].name, sx - 24.0, sy_c + 13.0, 48.0, 14.0,
                                 7.4, false, pal.text_secondary, true, false);
                         }
                         const std::string hex = swatches[i].hex;
                         if (rry + 44.0 > body_top && rry < body_bottom) {
                             clicks_.push_back({sx - 12.0, sy_c - 12.0, 24.0, 24.0, [this, hex]() {
                                 cfg_.accent_color = hex;
                                 save_and_reload();
                             }});
                         }
                     }
                 }, nullptr, 56.0},
                {"Label contrast", "",
                 [&](cairo_t* c, double rx, double rry, double rw, double rh) {
                     const std::string lbl = (cfg_.glassy_text_color == "white") ? "White"
                                           : (cfg_.glassy_text_color == "dark" ? "Dark" : "Automatic");
                     draw_dropdown_pill(c, rx + rw - 154.0, rry + (rh - 26.0) * 0.5, 138.0, 26.0,
                                        lbl, true, pal.accent,
                                        [this]() {
                                            if (cfg_.glassy_text_color == "auto") cfg_.glassy_text_color = "white";
                                            else if (cfg_.glassy_text_color == "white") cfg_.glassy_text_color = "dark";
                                            else cfg_.glassy_text_color = "auto";
                                            save_and_reload();
                                        }, body_top, body_bottom);
                 }, nullptr, 44.0}
            });

            draw_grouped_card({
                {"Icon & bar style", "",
                 [&](cairo_t* c, double rx, double rry, double rw, double rh) {
                     (void)rh;
                     const char* styles[4] = {"default", "dark", "clear", "tinted"};
                     const char* labels[4] = {"Default", "Dark", "Clear", "Tinted"};
                     const double tw = 48.0;
                     const double gap = 12.0;
                     const double start_x = rx + rw - 16.0 - (4.0 * tw + 3.0 * gap);
                     const double ty = rry + 12.0;

                     for (int i = 0; i < 4; ++i) {
                         const double bx = start_x + static_cast<double>(i) * (tw + gap);
                         const bool sel = (cfg_.icon_style == styles[i]);

                         mistbar::glass::append_rounded_rect(c, bx, ty, tw, 44.0, 10.0);
                         if (i == 0) cairo_set_source_rgba(c, pal.accent.r, pal.accent.g, pal.accent.b, 0.92);
                         else if (i == 1) cairo_set_source_rgba(c, 0.08, 0.08, 0.10, 1.0);
                         else if (i == 2) cairo_set_source_rgba(c, 0.22, 0.22, 0.25, 0.65);
                         else cairo_set_source_rgba(c, pal.accent.r * 0.45, pal.accent.g * 0.45, pal.accent.b * 0.45, 0.85);
                         cairo_fill(c);

                         const std::string ic = mistbar::icons::resolve_icon("mistbar-mark.svg");
                         if (!ic.empty()) {
                             win_.icon_cache().draw(c, ic, bx + 15.0, ty + 13.0, 18.0, 18.0, 0xFFFFFFFFU);
                         }

                         mistbar::glass::append_rounded_rect(c, bx, ty, tw, 44.0, 10.0);
                         if (sel) {
                             cairo_set_source_rgba(c, pal.accent.r, pal.accent.g, pal.accent.b, 0.98);
                             cairo_set_line_width(c, 2.2);
                         } else {
                             cairo_set_source_rgba(c, 1.0, 1.0, 1.0, 0.14);
                             cairo_set_line_width(c, 1.0);
                         }
                         cairo_stroke(c);

                         mistbar::window::draw_pango_text_centered(
                             c, labels[i], bx - 4.0, ty + 47.0, tw + 8.0, 15.0,
                             7.8, sel, sel ? pal.text_primary : pal.text_secondary, true, false);

                         if (ty + 62.0 > body_top && ty < body_bottom) {
                             const std::string st_name = styles[i];
                             clicks_.push_back({bx, std::max(body_top, ty), tw, 62.0, [this, st_name]() {
                                 cfg_.icon_style = st_name;
                                 save_and_reload();
                             }});
                         }
                     }
                 }, nullptr, 82.0},
                {"Bar geometry", "",
                 [&](cairo_t* c, double rx, double rry, double rw, double rh) {
                     draw_dropdown_pill(c, rx + rw - 164.0, rry + (rh - 26.0) * 0.5, 148.0, 26.0,
                                        cfg_.look == "pill" ? "Floating Pill" : "Attached Edge", true, pal.accent,
                                        [this]() {
                                            cfg_.look = (cfg_.look == "attached") ? "pill" : "attached";
                                            save_and_reload();
                                        }, body_top, body_bottom);
                 }, nullptr, 44.0}
            });
        } else if (active_cat_ == 4) {
            // Liquid Glass full studio controls
            preview_x_ = cx;
            preview_y_ = ry;
            preview_w_ = cw;
            preview_h_ = 124.0;
            ensure_preview_backdrop(static_cast<int>(cw), 124, cfg_.blur_radius);

            cairo_save(cr);
            mistbar::glass::append_rounded_rect(cr, cx, ry, cw, 124.0, 14.0);
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
            draw_preview_glass(cr, cx, ry, bar_x, bar_y, bar_w, bar_h, bar_r, pal.bar_bg);

            mistbar::window::draw_pango_text_centered(cr, "Finder", bar_x + 16.0, bar_y, 60.0, bar_h,
                                                     9.2, true, pal.text_primary);
            mistbar::window::draw_pango_text_centered(cr, "File   Edit   View   Go", bar_x + 74.0, bar_y, 190.0, bar_h,
                                                     8.8, false, pal.text_primary);
            mistbar::window::draw_pango_text_centered(cr, "9:41 AM", bar_x + bar_w - 72.0, bar_y, 58.0, bar_h,
                                                     8.8, false, pal.text_primary, false, true);

            const double dock_w = 240.0;
            const double dock_h = 44.0;
            const double dock_x = cx + (cw - dock_w) * 0.5;
            const double dock_y = ry + 64.0;
            draw_preview_glass(cr, cx, ry, dock_x, dock_y, dock_w, dock_h, 22.0, pal.panel_bg);
            mistbar::window::draw_pango_text_centered(cr, "Live Liquid Glass Optics",
                                                     dock_x, dock_y, dock_w, dock_h, 8.6, true,
                                                     pal.text_primary, true, false);
            cairo_restore(cr);

            mistbar::glass::append_rounded_rect(cr, cx + 0.5, ry + 0.5, cw - 1.0, 123.0, 14.0);
            cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.14);
            cairo_set_line_width(cr, 1.0);
            cairo_stroke(cr);
            ry += 138.0;

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
                    cfg_.bar_height = 26;
                    cfg_.bar_margin = 6;
                    cfg_.border_radius = 14;
                    cfg_.look = "attached";
                    cfg_.cursor_reactive = true;
                    save_and_reload();
                })
            });
        } else if (active_cat_ == 5) {
            draw_section_label("BEHAVIOR & CLOCK");
            draw_grouped_card({
                make_toggle_row("Intelligent Auto-Hide", "Hide bar when active workspace has windows",
                                cfg_.auto_hide, [this]() { cfg_.auto_hide = !cfg_.auto_hide; save_and_reload(); }),
                {"Clock Format", "12-hour or 24-hour menu bar clock",
                 [&](cairo_t* c, double rx, double rry, double rw, double rh) {
                     draw_segmented_control(c, rx + rw - 142.0, rry + (rh - 26.0) * 0.5, 126.0, 26.0,
                                            {"12h", "24h"}, cfg_.clock_format == "24h" ? 1 : 0,
                                            [this](int idx) {
                                                cfg_.clock_format = (idx == 1) ? "24h" : "12h";
                                                save_and_reload();
                                            }, body_top, body_bottom);
                 }, nullptr, 0.0},
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
                make_toggle_row("Battery", "", cfg_.show_battery,
                                [this]() { cfg_.show_battery = !cfg_.show_battery; save_and_reload(); }),
                make_toggle_row("Wi-Fi", "", cfg_.show_network,
                                [this]() { cfg_.show_network = !cfg_.show_network; save_and_reload(); }),
                make_toggle_row("Bluetooth", "", cfg_.show_bluetooth,
                                [this]() { cfg_.show_bluetooth = !cfg_.show_bluetooth; save_and_reload(); }),
                make_toggle_row("Sound", "", cfg_.show_volume,
                                [this]() { cfg_.show_volume = !cfg_.show_volume; save_and_reload(); }),
                make_toggle_row("Display Brightness", "", cfg_.show_brightness,
                                [this]() { cfg_.show_brightness = !cfg_.show_brightness; save_and_reload(); }),
                make_toggle_row("Spotlight Search", "", cfg_.show_spotlight,
                                [this]() { cfg_.show_spotlight = !cfg_.show_spotlight; save_and_reload(); }),
                make_toggle_row("Control Center", "", cfg_.show_control_center,
                                [this]() { cfg_.show_control_center = !cfg_.show_control_center; save_and_reload(); }),
                make_toggle_row("Power Menu Icon", "Power actions also live in the top-left M menu", cfg_.show_power_menu,
                                [this]() { cfg_.show_power_menu = !cfg_.show_power_menu; save_and_reload(); })
            });
        } else if (active_cat_ == 6) {
            draw_section_label("CONTROL CENTER & SHADER TUNER");
            draw_grouped_card({
                make_toggle_row("AirDrop Quick Tile", "Show AirDrop toggle inside Control Center",
                                cfg_.cc_airdrop_on, [this]() { cfg_.cc_airdrop_on = !cfg_.cc_airdrop_on; save_and_reload(); }),
                make_toggle_row("Focus Mode Tile", "Show Focus toggle inside Control Center",
                                cfg_.cc_focus_on, [this]() { cfg_.cc_focus_on = !cfg_.cc_focus_on; save_and_reload(); }),
                make_action_row("Live Shader Tuner", "Open real-time GLSL shader editor popover in bar", "Open Tuner", true, [this]() {
                    mistbar::ipc::send_command(mistbar::ipc::default_socket_path(), "tuner");
                }),
                make_action_row("Spotlight Search Pill", "Open built-in Liquid Glass Spotlight search", "Open", false, [this]() {
                    mistbar::ipc::send_command(mistbar::ipc::default_socket_path(), "spotlight");
                })
            });
        } else if (active_cat_ == 7) {
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
        } else if (active_cat_ == 8) {
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
            svc_rows.push_back({"Daemon Status", daemon_status_, nullptr, nullptr, 0.0});
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
            mistbar::glass::append_rounded_rect(cr, cx, ry, cw, 126.0, 12.0);
            cairo_set_source_rgba(cr, 0.065, 0.065, 0.075, 1.0);
            cairo_fill_preserve(cr);
            cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.09);
            cairo_set_line_width(cr, 1.0);
            cairo_stroke(cr);

            if (!mark_svg.empty()) {
                win_.icon_cache().draw(cr, mark_svg, cx + cw * 0.5 - 22.0, ry + 18.0, 44.0, 44.0, 0xFFF5F5F7U);
            }
            mistbar::window::draw_pango_text_centered(cr, "Mistbar", cx, ry + 72.0, cw, 20.0,
                                                     12.5, true, pal.text_primary, true, false);
            mistbar::window::draw_pango_text_centered(cr, std::string("Version ") + MISTBAR_VERSION + " - Liquid Glass Wayland Shell",
                                                     cx, ry + 94.0, cw, 16.0, 8.5, false, pal.text_secondary, true, false);
            ry += 140.0;

            auto make_info = [](const std::string& k, const std::string& v) -> GroupRow {
                return {
                    k, "",
                    [v](cairo_t* c, double rx, double rry, double rw, double rh) {
                        mistbar::window::draw_pango_text_centered(
                            c, v, rx + 130.0, rry, rw - 146.0, rh, 8.6, false,
                            mistbar::themes::Color{0.58, 0.58, 0.62, 1.0}, false, true);
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
            cairo_set_source_rgba(cr, pal.text_secondary.r, pal.text_secondary.g, pal.text_secondary.b, 0.30);
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
            if ((active_cat_ == 3 || active_cat_ == 4) &&
                ev.x >= preview_x_ && ev.x <= preview_x_ + preview_w_ &&
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
            if (p == "wifi" || p == "wi-fi") return 0;
            if (p == "bluetooth") return 1;
            if (p == "energy" || p == "battery") return 2;
            if (p == "appearance") return 3;
            if (p == "liquid-glass" || p == "glass") return 4;
            if (p == "menubar" || p == "menu-bar") return 5;
            if (p == "control-center" || p == "cc") return 6;
            if (p == "sound") return 7;
            if (p == "display") return 8;
            if (p == "about") return 9;
        }
    }
    return 3; // Default to Appearance (Golden Gate System Settings view)
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
