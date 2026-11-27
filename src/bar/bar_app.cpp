/* Mistbar - MIT License */

#include "bar/bar_app.h"
#include "common/defaults.h"
#include "common/logger.h"
#include "glass/specular/specular.h"
#include "window/shadows/window_shadow.h"
#include "window/titlebar/window_frame.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <fcntl.h>
#include <filesystem>
#include <linux/input-event-codes.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace fs = std::filesystem;

namespace mistbar::bar {

namespace {

constexpr int kPopShadowMargin = 20;

void draw_cc_box_icon(cairo_t* cr, double cx, double cy, bool active, bool double_box, const themes::Color& col) {
    cairo_save(cr);
    cairo_set_source_rgba(cr, col.r, col.g, col.b, active ? 0.98 : 0.85);
    cairo_set_line_width(cr, 1.3);
    glass::append_rounded_rect(cr, cx - 8.0, cy - 5.5, 11.0, 8.5, 2.0);
    cairo_stroke(cr);
    if (double_box) {
        glass::append_rounded_rect(cr, cx - 2.0, cy - 2.0, 11.0, 8.5, 2.0);
        cairo_stroke(cr);
    } else {
        cairo_rectangle(cr, cx - 5.0, cy - 3.0, 2.5, 4.0);
        cairo_fill(cr);
    }
    cairo_restore(cr);
}

void draw_cc_speaker_glyph(cairo_t* cr, double cx, double cy, double sz, const themes::Color& col, bool waves) {
    cairo_save(cr);
    cairo_set_source_rgba(cr, col.r, col.g, col.b, col.a);
    const double s = sz * 0.42;
    cairo_move_to(cr, cx - s * 0.85, cy - s * 0.34);
    cairo_line_to(cr, cx - s * 0.36, cy - s * 0.34);
    cairo_line_to(cr, cx + s * 0.14, cy - s * 0.78);
    cairo_line_to(cr, cx + s * 0.14, cy + s * 0.78);
    cairo_line_to(cr, cx - s * 0.36, cy + s * 0.34);
    cairo_line_to(cr, cx - s * 0.85, cy + s * 0.34);
    cairo_close_path(cr);
    cairo_fill(cr);
    if (waves) {
        cairo_set_line_width(cr, sz * 0.10);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_new_sub_path(cr);
        cairo_arc(cr, cx + s * 0.22, cy, s * 0.44, -0.7, 0.7);
        cairo_stroke(cr);
        cairo_new_sub_path(cr);
        cairo_arc(cr, cx + s * 0.22, cy, s * 0.78, -0.75, 0.75);
        cairo_stroke(cr);
    }
    cairo_restore(cr);
}

void draw_cc_sun_glyph(cairo_t* cr, double cx, double cy, double sz, const themes::Color& col) {
    constexpr double kPi = 3.14159265358979323846;
    cairo_save(cr);
    cairo_set_source_rgba(cr, col.r, col.g, col.b, col.a);
    cairo_arc(cr, cx, cy, sz * 0.22, 0.0, 2.0 * kPi);
    cairo_fill(cr);
    cairo_set_line_width(cr, sz * 0.11);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    for (int i = 0; i < 8; ++i) {
        const double ang = static_cast<double>(i) * (kPi / 4.0);
        cairo_move_to(cr, cx + std::cos(ang) * sz * 0.33, cy + std::sin(ang) * sz * 0.33);
        cairo_line_to(cr, cx + std::cos(ang) * sz * 0.46, cy + std::sin(ang) * sz * 0.46);
    }
    cairo_stroke(cr);
    cairo_restore(cr);
}

void draw_cc_wifi_glyph(cairo_t* cr, double cx, double cy, double sz, const themes::Color& col) {
    constexpr double kPi = 3.14159265358979323846;
    cairo_save(cr);
    cairo_new_path(cr);
    cairo_set_source_rgba(cr, col.r, col.g, col.b, col.a);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_width(cr, sz * 0.13);
    for (int i = 0; i < 3; ++i) {
        const double rad = sz * (0.25 + static_cast<double>(i) * 0.21);
        cairo_new_sub_path(cr);
        cairo_arc(cr, cx, cy + sz * 0.28, rad, -kPi * 0.78, -kPi * 0.22);
        cairo_stroke(cr);
    }
    cairo_new_sub_path(cr);
    cairo_arc(cr, cx, cy + sz * 0.30, sz * 0.085, 0.0, 2.0 * kPi);
    cairo_fill(cr);
    cairo_restore(cr);
}

void draw_cc_bt_glyph(cairo_t* cr, double cx, double cy, double sz, const themes::Color& col) {
    cairo_save(cr);
    cairo_new_path(cr);
    cairo_set_source_rgba(cr, col.r, col.g, col.b, col.a);
    cairo_set_line_width(cr, sz * 0.12);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    const double s = sz * 0.40;
    cairo_move_to(cr, cx - s * 0.52, cy - s * 0.42);
    cairo_line_to(cr, cx + s * 0.52, cy + s * 0.42);
    cairo_line_to(cr, cx, cy + s * 0.92);
    cairo_line_to(cr, cx, cy - s * 0.92);
    cairo_line_to(cr, cx + s * 0.52, cy - s * 0.42);
    cairo_line_to(cr, cx - s * 0.52, cy + s * 0.42);
    cairo_stroke(cr);
    cairo_restore(cr);
}

void draw_cc_moon_glyph(cairo_t* cr, double cx, double cy, double sz, const themes::Color& col) {
    constexpr double kPi = 3.14159265358979323846;
    cairo_save(cr);
    cairo_new_path(cr);
    cairo_set_source_rgba(cr, col.r, col.g, col.b, col.a);
    cairo_arc(cr, cx, cy, sz * 0.36, 0.35, 2.0 * kPi - 0.75);
    cairo_arc_negative(cr, cx + sz * 0.18, cy - sz * 0.12, sz * 0.26, 2.0 * kPi - 0.45, 0.75);
    cairo_close_path(cr);
    cairo_fill(cr);
    cairo_restore(cr);
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

std::string format_clock_str(const config::MistbarConfig& cfg) {
    const std::time_t now = std::time(nullptr);
    std::tm tm_now{};
    localtime_r(&now, &tm_now);
    char prefix[32]{};
    std::strftime(prefix, sizeof(prefix), "%a %b", &tm_now);
    char out[64]{};
    if (cfg.clock_format == "12h") {
        int h12 = tm_now.tm_hour % 12;
        if (h12 == 0) h12 = 12;
        const char* ampm = (tm_now.tm_hour >= 12) ? "PM" : "AM";
        if (cfg.show_seconds) {
            std::snprintf(out, sizeof(out), "%s %d  %d:%02d:%02d %s",
                          prefix, tm_now.tm_mday, h12, tm_now.tm_min, tm_now.tm_sec, ampm);
        } else {
            std::snprintf(out, sizeof(out), "%s %d  %d:%02d %s",
                          prefix, tm_now.tm_mday, h12, tm_now.tm_min, ampm);
        }
    } else {
        if (cfg.show_seconds) {
            std::snprintf(out, sizeof(out), "%s %d  %02d:%02d:%02d",
                          prefix, tm_now.tm_mday, tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
        } else {
            std::snprintf(out, sizeof(out), "%s %d  %02d:%02d",
                          prefix, tm_now.tm_mday, tm_now.tm_hour, tm_now.tm_min);
        }
    }
    return std::string(out);
}

std::string format_full_date_str() {
    const std::time_t now = std::time(nullptr);
    std::tm tm_now{};
    localtime_r(&now, &tm_now);
    char wd[32]{};
    char mo[32]{};
    std::strftime(wd, sizeof(wd), "%A", &tm_now);
    std::strftime(mo, sizeof(mo), "%B", &tm_now);
    char buf[96]{};
    std::snprintf(buf, sizeof(buf), "%s, %s %d, %d", wd, mo, tm_now.tm_mday, tm_now.tm_year + 1900);
    return std::string(buf);
}

std::string prettify_app_id(const std::string& app_id) {
    if (app_id.empty()) return "Finder";
    if (app_id == "mistbar-settings") return "Mistbar Settings";
    const auto dot = app_id.rfind('.');
    std::string base = (dot != std::string::npos) ? app_id.substr(dot + 1) : app_id;
    if (!base.empty()) {
        base[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(base[0])));
    }
    if (base.size() > 24) base = base.substr(0, 23) + "...";
    return base;
}

int days_in_month(int year, int month0) {
    static const int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int d = kDays[std::clamp(month0, 0, 11)];
    if (month0 == 1) {
        const bool leap = ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
        if (leap) d = 29;
    }
    return d;
}

std::string to_lower_ascii(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

void draw_vector_airdrop(cairo_t* cr, double cx, double cy, double sz, const themes::Color& col) {
    constexpr double kPi = 3.14159265358979323846;
    cairo_save(cr);
    cairo_set_source_rgba(cr, col.r, col.g, col.b, col.a);
    cairo_set_line_width(cr, sz * 0.11);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    for (int i = 0; i < 3; ++i) {
        const double r = sz * (0.16 + static_cast<double>(i) * 0.15);
        cairo_new_sub_path(cr);
        cairo_arc(cr, cx, cy + sz * 0.06, r, -kPi * 1.18, kPi * 0.18);
        cairo_stroke(cr);
    }
    cairo_new_sub_path(cr);
    cairo_arc(cr, cx, cy + sz * 0.12, sz * 0.075, 0.0, 2.0 * kPi);
    cairo_fill(cr);
    cairo_restore(cr);
}

void draw_vector_quick_glyph(cairo_t* cr, int idx, double cx, double cy, const themes::Color& col) {
    constexpr double kPi = 3.14159265358979323846;
    cairo_save(cr);
    cairo_set_source_rgba(cr, col.r, col.g, col.b, col.a);
    cairo_set_line_width(cr, 1.35);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

    switch (idx) {
        case 0: { // Contrast / Preset cycle (half-filled circle)
            cairo_arc(cr, cx, cy, 7.0, 0.0, 2.0 * kPi);
            cairo_stroke(cr);
            cairo_arc(cr, cx, cy, 6.6, -kPi * 0.5, kPi * 0.5);
            cairo_fill(cr);
            break;
        }
        case 1: { // Auto-hide bar layout
            glass::append_rounded_rect(cr, cx - 8.0, cy - 6.0, 16.0, 12.0, 2.5);
            cairo_stroke(cr);
            cairo_rectangle(cr, cx - 6.0, cy - 4.0, 12.0, 2.2);
            cairo_fill(cr);
            break;
        }
        case 2: { // Clock 12h/24h
            cairo_arc(cr, cx, cy, 7.0, 0.0, 2.0 * kPi);
            cairo_stroke(cr);
            cairo_move_to(cr, cx, cy);
            cairo_line_to(cr, cx, cy - 4.0);
            cairo_move_to(cr, cx, cy);
            cairo_line_to(cr, cx + 3.2, cy + 1.5);
            cairo_stroke(cr);
            break;
        }
        case 3: { // Bar look (pill vs attached)
            glass::append_rounded_rect(cr, cx - 8.5, cy - 4.0, 17.0, 8.0, 4.0);
            cairo_stroke(cr);
            cairo_arc(cr, cx + 4.0, cy, 2.0, 0.0, 2.0 * kPi);
            cairo_fill(cr);
            break;
        }
        case 4: { // Wi-Fi list (signal arcs)
            for (int i = 0; i < 3; ++i) {
                const double r = 3.2 + static_cast<double>(i) * 2.6;
                cairo_new_sub_path(cr);
                cairo_arc(cr, cx, cy + 4.0, r, -kPi * 0.78, -kPi * 0.22);
                cairo_stroke(cr);
            }
            cairo_arc(cr, cx, cy + 4.5, 1.3, 0.0, 2.0 * kPi);
            cairo_fill(cr);
            break;
        }
        case 5: { // Bluetooth devices
            const double s = 3.6;
            cairo_move_to(cr, cx - s * 0.65, cy - s * 0.55);
            cairo_line_to(cr, cx + s * 0.65, cy + s * 0.55);
            cairo_line_to(cr, cx, cy + s * 1.15);
            cairo_line_to(cr, cx, cy - s * 1.15);
            cairo_line_to(cr, cx + s * 0.65, cy - s * 0.55);
            cairo_line_to(cr, cx - s * 0.65, cy + s * 0.55);
            cairo_stroke(cr);
            break;
        }
        case 6: { // Audio sinks (speaker)
            cairo_move_to(cr, cx - 6.5, cy - 2.2);
            cairo_line_to(cr, cx - 3.0, cy - 2.2);
            cairo_line_to(cr, cx + 0.5, cy - 5.2);
            cairo_line_to(cr, cx + 0.5, cy + 5.2);
            cairo_line_to(cr, cx - 3.0, cy + 2.2);
            cairo_line_to(cr, cx - 6.5, cy + 2.2);
            cairo_close_path(cr);
            cairo_fill(cr);
            cairo_new_sub_path(cr);
            cairo_arc(cr, cx + 1.5, cy, 3.6, -0.75, 0.75);
            cairo_stroke(cr);
            break;
        }
        default: { // Shader Tuner sliders
            for (int row = -1; row <= 1; ++row) {
                const double y = cy + static_cast<double>(row) * 4.5;
                cairo_move_to(cr, cx - 7.0, y);
                cairo_line_to(cr, cx + 7.0, y);
                cairo_stroke(cr);
                const double kx = cx + (row == 0 ? 2.5 : -2.5);
                cairo_arc(cr, kx, y, 1.8, 0.0, 2.0 * kPi);
                cairo_fill(cr);
            }
            break;
        }
    }
    cairo_restore(cr);
}

} // namespace

BarApp::BarApp(wl::Display& display)
    : display_(display),
      bar_surface_(std::make_unique<wl::LayerSurface>(display)),
      trigger_surface_(std::make_unique<wl::LayerSurface>(display)) {}

BarApp::~BarApp() {
    stop_worker();
    if (bar_surface_ && bar_surface_->wl_handle()) {
        mizzle_.unregister_surface(bar_surface_->wl_handle());
    }
    if (popover_surface_ && popover_surface_->wl_handle()) {
        mizzle_.unregister_surface(popover_surface_->wl_handle());
    }
    bar_surface_.reset();
    trigger_surface_.reset();
    popover_surface_.reset();
    mizzle_.shutdown();
    invalidate_glass_caches();
    destroy_ui_overlays();
}

void BarApp::destroy_ui_overlays() {
    if (bar_ui_surf_) {
        cairo_surface_destroy(bar_ui_surf_);
        bar_ui_surf_ = nullptr;
    }
    if (pop_ui_surf_) {
        cairo_surface_destroy(pop_ui_surf_);
        pop_ui_surf_ = nullptr;
    }
    bar_ui_w_ = bar_ui_h_ = 0;
    pop_ui_w_ = pop_ui_h_ = 0;
}

void BarApp::invalidate_glass_caches() {
    ++cfg_gen_;
    if (bar_glass_cache_) {
        cairo_surface_destroy(bar_glass_cache_);
        bar_glass_cache_ = nullptr;
    }
    if (pop_glass_cache_) {
        cairo_surface_destroy(pop_glass_cache_);
        pop_glass_cache_ = nullptr;
    }
    bar_cache_w_ = 0;
    bar_cache_h_ = 0;
    pop_cache_w_ = 0;
    pop_cache_h_ = 0;
}

void BarApp::start_worker() {
    if (wake_pipe_[0] >= 0) return;
    if (::pipe2(wake_pipe_, O_NONBLOCK | O_CLOEXEC) != 0) {
        wake_pipe_[0] = wake_pipe_[1] = -1;
        return;
    }
    worker_stop_ = false;
    worker_thread_ = std::thread([this]() {
        int cycle = 0;
        while (true) {
            bool want_full = false;
            bool show_privacy = false;
            bool show_tray = false;
            PopoverKind cur_pop = PopoverKind::None;
            {
                std::unique_lock<std::mutex> lk(worker_mu_);
                worker_cv_.wait_for(lk, std::chrono::milliseconds(2000), [this]() {
                    return worker_stop_ || worker_req_;
                });
                if (worker_stop_) break;
                want_full = worker_full_req_ || (cycle % 3 == 0);
                worker_req_ = false;
                worker_full_req_ = false;
                show_privacy = cfg_.show_privacy;
                show_tray = cfg_.show_tray;
                cur_pop = open_popover_;
            }
            ++cycle;

            HwSnapshot snap;
            snap.workspaces = sysinfo::get_niri_workspaces();
            snap.has_workspaces = true;
            snap.active_app = sysinfo::get_niri_focused_app();
            snap.audio = sysinfo::get_audio_status(cur_pop == PopoverKind::Volume);
            snap.brightness = sysinfo::get_brightness_status();
            snap.mic_active = show_privacy ? sysinfo::is_mic_active() : false;
            if (want_full || cur_pop == PopoverKind::ControlCenter) {
                snap.media = sysinfo::get_media_status();
            }
            if (want_full) {
                snap.has_full = true;
                snap.wifi = sysinfo::get_wifi_status(cur_pop == PopoverKind::Wifi);
                snap.bt = sysinfo::get_bluetooth_status(cur_pop == PopoverKind::Bluetooth);
                snap.battery = sysinfo::get_battery_status(cur_pop == PopoverKind::Battery);
                snap.tray = show_tray ? sysinfo::get_tray_items() : std::vector<sysinfo::TrayItem>{};
            }

            {
                std::lock_guard<std::mutex> lk(worker_mu_);
                if (worker_stop_) break;
                pending_hw_ = std::move(snap);
                has_pending_hw_ = true;
            }
            if (wake_pipe_[1] >= 0) {
                const char b = '1';
                const ssize_t wr = ::write(wake_pipe_[1], &b, 1);
                (void)wr;
            }
        }
    });
}

void BarApp::stop_worker() {
    {
        std::lock_guard<std::mutex> lk(worker_mu_);
        worker_stop_ = true;
    }
    worker_cv_.notify_all();
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    if (wake_pipe_[0] >= 0) { ::close(wake_pipe_[0]); wake_pipe_[0] = -1; }
    if (wake_pipe_[1] >= 0) { ::close(wake_pipe_[1]); wake_pipe_[1] = -1; }
}

void BarApp::trigger_async_refresh(bool full) {
    if (wake_pipe_[0] < 0) return;
    {
        std::lock_guard<std::mutex> lk(worker_mu_);
        worker_req_ = true;
        if (full) worker_full_req_ = true;
    }
    worker_cv_.notify_one();
}

void BarApp::apply_worker_updates() {
    char drain[64];
    while (wake_pipe_[0] >= 0 && ::read(wake_pipe_[0], drain, sizeof(drain)) > 0) {}

    HwSnapshot snap;
    bool got = false;
    {
        std::lock_guard<std::mutex> lk(worker_mu_);
        if (has_pending_hw_) {
            snap = std::move(pending_hw_);
            has_pending_hw_ = false;
            got = true;
        }
    }
    if (!got) return;

    bool bar_changed = false;
    bool pop_changed = (open_popover_ != PopoverKind::None);

    if (snap.has_workspaces) {
        if (snap.workspaces.size() != cached_workspaces_.size()) {
            bar_changed = true;
        } else {
            for (std::size_t i = 0; i < snap.workspaces.size(); ++i) {
                if (snap.workspaces[i].idx != cached_workspaces_[i].idx ||
                    snap.workspaces[i].focused != cached_workspaces_[i].focused ||
                    snap.workspaces[i].has_windows != cached_workspaces_[i].has_windows) {
                    bar_changed = true;
                    break;
                }
            }
        }
        cached_workspaces_ = std::move(snap.workspaces);
    }

    bool any_windows = false;
    for (const auto& ws : cached_workspaces_) {
        if (ws.focused && ws.has_windows) { any_windows = true; break; }
    }
    if (!any_windows) {
        for (const auto& tl : display_.toplevels()) {
            if (tl.active) { any_windows = true; break; }
        }
    }
    if (any_windows != has_active_windows_) {
        has_active_windows_ = any_windows;
        if (cfg_.auto_hide) update_visibility();
    }

    std::string app = snap.active_app;
    if (app.empty()) {
        for (const auto& tl : display_.toplevels()) {
            if (tl.active) {
                app = prettify_app_id(tl.app_id.empty() ? tl.title : tl.app_id);
                break;
            }
        }
    }
    if (app.empty()) app = "Finder";
    if (app != cached_active_app_) {
        cached_active_app_ = std::move(app);
        bar_changed = true;
    }

    if (!dragging_volume_) {
        if (snap.audio.volume_percent != cached_audio_.volume_percent ||
            snap.audio.muted != cached_audio_.muted) {
            bar_changed = true;
        }
        cached_audio_ = std::move(snap.audio);
    }
    if (!dragging_brightness_) {
        if (snap.brightness.percent != cached_brightness_.percent) {
            bar_changed = true;
        }
        cached_brightness_ = snap.brightness;
    }
    if (snap.mic_active != cached_mic_active_) {
        cached_mic_active_ = snap.mic_active;
        bar_changed = true;
    }
    if (snap.media.active || cached_media_.active) {
        cached_media_ = std::move(snap.media);
    }

    if (snap.has_full) {
        if (snap.wifi.enabled != cached_wifi_.enabled ||
            snap.wifi.connected != cached_wifi_.connected ||
            snap.wifi.ssid != cached_wifi_.ssid) {
            bar_changed = true;
        }
        cached_wifi_ = std::move(snap.wifi);

        if (snap.bt.powered != cached_bt_.powered ||
            snap.bt.connected_name != cached_bt_.connected_name) {
            bar_changed = true;
        }
        cached_bt_ = std::move(snap.bt);

        if (snap.battery.present != cached_battery_.present ||
            snap.battery.percent != cached_battery_.percent ||
            snap.battery.charging != cached_battery_.charging) {
            bar_changed = true;
        }
        cached_battery_ = std::move(snap.battery);

        if (snap.tray.size() != cached_tray_.size()) {
            bar_changed = true;
        }
        cached_tray_ = std::move(snap.tray);
    }

    if (bar_changed) {
        bar_surface_->request_redraw();
    }
    if (pop_changed && popover_surface_) {
        popover_surface_->request_redraw();
    }
}

void BarApp::capture_live_backdrop(bool force) {
    if (!mizzle_.is_available()) return;
    if (popover_surface_ && popover_surface_->is_visible()) return;

    const auto now = std::chrono::steady_clock::now();
    if (!force && last_capture_.time_since_epoch().count() != 0) {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_capture_).count();
        if (ms < 400) return;
    }
    last_capture_ = now;

    if (display_.has_screencopy()) {
        wl::CapturedFrame frame;
        if (display_.capture_output(nullptr, frame, false) && !frame.pixels.empty()) {
            mizzle_.set_backdrop_bgra(frame.pixels.data(), frame.width, frame.height, frame.stride);
            return;
        }
    }
    if (backdrop_.raw_wallpaper()) {
        mizzle_.set_backdrop_cairo(backdrop_.raw_wallpaper());
    }
}

bool BarApp::ensure_popover_surface() {
    if (popover_surface_) {
        if (popover_surface_->wl_handle()) {
            mizzle_.unregister_surface(popover_surface_->wl_handle());
        }
        popover_surface_.reset();
    }
    popover_surface_ = std::make_unique<wl::LayerSurface>(display_);

    popover_surface_->set_draw_callback([this](cairo_t* cr, int w, int h) { draw_popover(cr, w, h); });
    popover_surface_->set_egl_draw_callback([this](wl_surface* s, int w, int h, int scale) {
        return render_popover_egl(s, w, h, scale);
    });
    popover_surface_->set_resize_callback([this](wl_surface* s, int w, int h, int scale) {
        if (mizzle_.has_wayland_egl()) {
            mizzle_.resize_surface(s, w * scale, h * scale);
        }
    });
    popover_surface_->set_pointer_callback([this](const wl::PointerEvent& ev) { handle_popover_pointer(ev); });
    popover_surface_->set_key_callback([this](const wl::KeyEvent& ev) { handle_popover_key(ev); });
    popover_surface_->set_close_callback([this]() {
        open_popover_ = PopoverKind::None;
        active_menu_name_.clear();
        hovered_pop_id_.clear();
        dragging_brightness_ = false;
        dragging_volume_ = false;
        dragging_tuner_idx_ = -1;
        update_visibility();
        if (bar_surface_) bar_surface_->request_redraw();
    });

    const int total_w = popover_w_ + kPopShadowMargin * 2;
    const int total_h = popover_h_ + kPopShadowMargin * 2;
    const auto kb = (open_popover_ == PopoverKind::Spotlight)
        ? wl::LayerSurface::KeyboardMode::Exclusive
        : wl::LayerSurface::KeyboardMode::OnDemand;

    popover_surface_->set_visible(true);
    return popover_surface_->create("mistbar-popover", wl::LayerSurface::Layer::Overlay,
                                    wl::LayerSurface::Top | wl::LayerSurface::Left,
                                    total_w, total_h, -1, kb, nullptr,
                                    popover_top_m_, 0, 0, popover_left_m_);
}

bool BarApp::init() {
    config::ensure_default_config();
    cfg_ = config::load_config();
    palette_ = themes::resolve_palette(cfg_);
    backdrop_.init(cfg_);

    const auto out = display_.primary_output();
    backdrop_.load_wallpaper(palette_.wallpaper_path, out.width, out.height, palette_.blur_radius);

    if (cfg_.gpu_acceleration) {
        if (mizzle_.init(display_.handle())) {
            capture_live_backdrop(true);
        } else {
            log::warn("bar", "MizzleRenderer unavailable; falling back to SHM Cairo");
        }
    }

    bar_surface_->set_draw_callback([this](cairo_t* cr, int w, int h) { draw_bar(cr, w, h); });
    bar_surface_->set_egl_draw_callback([this](wl_surface* s, int w, int h, int scale) {
        return render_bar_egl(s, w, h, scale);
    });
    bar_surface_->set_resize_callback([this](wl_surface* s, int w, int h, int scale) {
        if (mizzle_.has_wayland_egl()) {
            mizzle_.resize_surface(s, w * scale, h * scale);
        }
    });
    bar_surface_->set_pointer_callback([this](const wl::PointerEvent& ev) { handle_bar_pointer(ev); });

    const bool is_pill = (cfg_.look == "pill");
    const std::uint32_t anchors = wl::LayerSurface::Top | wl::LayerSurface::Left | wl::LayerSurface::Right;
    const int top_m = is_pill ? cfg_.bar_margin : 0;
    const int side_m = is_pill ? defaults::BAR_SIDE_MARGIN : 0;
    const int excl = cfg_.auto_hide ? -1 : (cfg_.bar_height + top_m);

    if (!bar_surface_->create("mistbar", wl::LayerSurface::Layer::Top, anchors,
                              0, cfg_.bar_height, excl, wl::LayerSurface::KeyboardMode::None,
                              nullptr, top_m, side_m, 0, side_m)) {
        return false;
    }

    trigger_surface_->set_draw_callback([](cairo_t* cr, int w, int h) {
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
        cairo_rectangle(cr, 0, 0, w, h);
        cairo_fill(cr);
    });
    trigger_surface_->set_pointer_callback([this](const wl::PointerEvent& ev) {
        if (ev.type == wl::PointerEvent::Type::Enter) {
            trigger_hovered_ = true;
            grace_active_ = false;
            update_visibility();
        } else if (ev.type == wl::PointerEvent::Type::Leave) {
            trigger_hovered_ = false;
            update_visibility();
        }
    });
    trigger_surface_->set_visible(false);
    trigger_surface_->create("mistbar-trigger", wl::LayerSurface::Layer::Overlay,
                             anchors, 0, 3, -1, wl::LayerSurface::KeyboardMode::None);

    display_.set_toplevel_change_callback([this]() {
        trigger_async_refresh(false);
    });

    cached_clock_ = format_clock_str(cfg_);
    cached_full_date_ = format_full_date_str();
    cached_active_app_ = "Finder";

    slide_anim_.snap(cfg_.auto_hide ? 0.0 : 1.0);
    if (!cfg_.auto_hide) {
        bar_surface_->set_visible(true);
    }
    start_worker();
    trigger_async_refresh(true);
    return true;
}

void BarApp::apply_geometry() {
    const bool is_pill = (cfg_.look == "pill");
    const int top_m = is_pill ? cfg_.bar_margin : 0;
    const int side_m = is_pill ? defaults::BAR_SIDE_MARGIN : 0;
    const int slide_offset = static_cast<int>((1.0 - slide_anim_.value()) * -(cfg_.bar_height + top_m + 6));
    const int excl = cfg_.auto_hide ? -1 : (cfg_.bar_height + top_m);

    bar_surface_->set_geometry(0, cfg_.bar_height, top_m + slide_offset, side_m, 0, side_m, excl);
}

void BarApp::reload_config() {
    const std::string prev_wp = palette_.wallpaper_path;
    const int prev_blur = palette_.blur_radius;
    {
        std::lock_guard<std::mutex> lk(worker_mu_);
        cfg_ = config::load_config();
    }
    palette_ = themes::resolve_palette(cfg_);
    backdrop_.init(cfg_);
    icons_.clear();
    invalidate_glass_caches();
    if (palette_.wallpaper_path != prev_wp || palette_.blur_radius != prev_blur || !backdrop_.raw_wallpaper()) {
        const auto out = display_.primary_output();
        backdrop_.load_wallpaper(palette_.wallpaper_path, out.width, out.height, palette_.blur_radius);
        if (mizzle_.is_available() && !display_.has_screencopy() && backdrop_.raw_wallpaper()) {
            mizzle_.set_backdrop_cairo(backdrop_.raw_wallpaper());
        }
    }
    cached_clock_ = format_clock_str(cfg_);
    cached_full_date_ = format_full_date_str();
    apply_geometry();
    update_visibility();
    bar_surface_->request_redraw();
    if (open_popover_ != PopoverKind::None && popover_surface_) {
        popover_surface_->request_redraw();
    }
    trigger_async_refresh(true);
}

void BarApp::update_visibility() {
    if (manually_hidden_) {
        close_popover();
        grace_active_ = false;
        bar_surface_->hide();
        trigger_surface_->hide();
        return;
    }

    if (!cfg_.auto_hide) {
        grace_active_ = false;
        trigger_surface_->hide();
        slide_anim_.snap(1.0);
        apply_geometry();
        if (!bar_surface_->is_visible()) {
            bar_surface_->show();
        }
        return;
    }

    const bool should_reveal = !has_active_windows_ || bar_hovered_ || trigger_hovered_ ||
                               (open_popover_ != PopoverKind::None);
    if (should_reveal) {
        grace_active_ = false;
        trigger_surface_->hide();
        if (!bar_surface_->is_visible()) {
            slide_anim_.snap(0.0);
            bar_surface_->show();
        }
        slide_anim_.animate_to(1.0, 0.18);
    } else {
        if (!grace_active_) {
            grace_active_ = true;
            grace_start_ = std::chrono::steady_clock::now();
        }
    }
}

bool BarApp::tick() {
    if (pending_pop_op_ != PendingPopOp::None) {
        const PendingPopOp op = pending_pop_op_;
        const PopoverKind kind = pending_pop_kind_;
        const double ax = pending_pop_anchor_x_;
        const std::string menu = std::move(pending_pop_menu_name_);
        pending_pop_op_ = PendingPopOp::None;
        pending_pop_menu_name_.clear();

        if (op == PendingPopOp::Close) {
            close_popover();
        } else if (op == PendingPopOp::Open) {
            open_popover(kind, ax, menu);
        } else if (op == PendingPopOp::RecaptureAndOpenTuner) {
            close_popover();
            display_.roundtrip();
            capture_live_backdrop(true);
            open_popover(PopoverKind::ShaderTuner, ax);
        }
    }

    if (grace_active_ && cfg_.auto_hide && !manually_hidden_) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - grace_start_).count();
        if (elapsed >= 450) {
            grace_active_ = false;
            slide_anim_.animate_to(0.0, 0.18);
        }
    }

    const bool animating = slide_anim_.tick();
    if (animating) {
        apply_geometry();
        bar_surface_->request_redraw();
    } else if (cfg_.auto_hide && !manually_hidden_ && slide_anim_.target() < 0.1 &&
               bar_surface_->is_visible() && !bar_hovered_ && !trigger_hovered_ &&
               has_active_windows_ && open_popover_ == PopoverKind::None) {
        bar_surface_->hide();
        trigger_surface_->show();
    }

    bool pulse_active = false;
    const auto now = std::chrono::steady_clock::now();
    if (bar_click_.active) {
        const float age = std::chrono::duration<float>(now - bar_click_start_).count();
        if (age < 0.36f) {
            bar_click_.age_sec = age;
            pulse_active = true;
            if (bar_surface_->is_visible()) bar_surface_->request_redraw();
        } else {
            bar_click_.active = false;
        }
    }
    if (pop_click_.active) {
        const float age = std::chrono::duration<float>(now - pop_click_start_).count();
        if (age < 0.36f) {
            pop_click_.age_sec = age;
            pulse_active = true;
            if (popover_surface_ && popover_surface_->is_visible()) popover_surface_->request_redraw();
        } else {
            pop_click_.active = false;
        }
    }

    return animating || grace_active_ || pulse_active;
}

void BarApp::refresh_clock_only() {
    const std::string next_clock = format_clock_str(cfg_);
    if (next_clock != cached_clock_) {
        cached_clock_ = next_clock;
        cached_full_date_ = format_full_date_str();
        bar_surface_->request_redraw();
    }
}

void BarApp::refresh_clock_and_niri() {
    cached_clock_ = format_clock_str(cfg_);
    cached_full_date_ = format_full_date_str();
    trigger_async_refresh(false);
}

void BarApp::refresh_hardware_quick() {
    trigger_async_refresh(false);
}

void BarApp::refresh_hardware_full() {
    trigger_async_refresh(true);
}

void BarApp::launch_settings(const std::string& page) {
    const std::string bin = resolve_sibling_binary("mistbar-settings");
    if (page.empty()) {
        sysinfo::spawn_detached({bin});
    } else {
        sysinfo::spawn_detached({bin, "--page", page});
    }
}

void BarApp::queue_open_popover(PopoverKind kind, double anchor_x, const std::string& menu_name) {
    pending_pop_op_ = PendingPopOp::Open;
    pending_pop_kind_ = kind;
    pending_pop_anchor_x_ = anchor_x;
    pending_pop_menu_name_ = menu_name;
}

void BarApp::queue_close_popover() {
    pending_pop_op_ = PendingPopOp::Close;
}

void BarApp::queue_recapture_and_open_tuner(double anchor_x) {
    pending_pop_op_ = PendingPopOp::RecaptureAndOpenTuner;
    pending_pop_anchor_x_ = anchor_x;
}

void BarApp::close_popover() {
    if (open_popover_ != PopoverKind::None || popover_surface_) {
        open_popover_ = PopoverKind::None;
        active_menu_name_.clear();
        hovered_pop_id_.clear();
        dragging_brightness_ = false;
        dragging_volume_ = false;
        dragging_tuner_idx_ = -1;
        if (popover_surface_) {
            if (popover_surface_->wl_handle()) {
                mizzle_.unregister_surface(popover_surface_->wl_handle());
            }
            popover_surface_.reset();
        }
        update_visibility();
        bar_surface_->request_redraw();
    }
}

void BarApp::rebuild_spotlight_matches() {
    spotlight_matches_.clear();
    const std::string q = to_lower_ascii(spotlight_query_);

    auto push_if_match = [&](const std::string& title, const std::string& subtitle,
                             const std::string& action_id, const std::string& arg) {
        if (spotlight_matches_.size() >= 7) return;
        if (!q.empty() &&
            to_lower_ascii(title).find(q) == std::string::npos &&
            to_lower_ascii(subtitle).find(q) == std::string::npos) {
            return;
        }
        spotlight_matches_.push_back({title, subtitle, action_id, arg});
    };

    push_if_match("Shader Tuner", "Mistbar Live Optics", "open_tuner", "");
    push_if_match("Control Center", "Quick Controls", "open_cc", "");
    push_if_match("Mistbar Settings: Appearance", "Settings", "settings", "appearance");
    push_if_match("Mistbar Settings: Liquid Glass", "Settings", "settings", "liquid-glass");
    push_if_match("Mistbar Settings: Menu Bar", "Settings", "settings", "menubar");
    push_if_match("Mistbar Settings: Wi-Fi", "Settings", "settings", "wifi");
    push_if_match("Mistbar Settings: Bluetooth", "Settings", "settings", "bluetooth");
    push_if_match("Toggle Auto-Hide", "Mistbar Action", "autohide_toggle", "");
    push_if_match("Switch Bar Geometry", "Mistbar Action", "look_toggle", "");

    for (const auto& app : cached_apps_) {
        if (spotlight_matches_.size() >= 7) break;
        push_if_match(app.name, app.category, "exec", app.exec_cmd);
    }

    if (spotlight_selected_ >= static_cast<int>(spotlight_matches_.size())) {
        spotlight_selected_ = std::max(0, static_cast<int>(spotlight_matches_.size()) - 1);
    }
}

void BarApp::open_popover(PopoverKind kind, double anchor_x, const std::string& menu_name) {
    if (open_popover_ == kind && active_menu_name_ == menu_name) {
        close_popover();
        return;
    }

    if (open_popover_ == PopoverKind::None) {
        capture_live_backdrop(true);
    }

    open_popover_ = kind;
    active_menu_name_ = menu_name;
    hovered_pop_id_.clear();
    dragging_tuner_idx_ = -1;
    pop_cache_w_ = 0;
    pop_cache_h_ = 0;

    if (kind == PopoverKind::Wifi || kind == PopoverKind::Bluetooth ||
        kind == PopoverKind::Volume || kind == PopoverKind::Battery ||
        kind == PopoverKind::Brightness || kind == PopoverKind::ControlCenter) {
        trigger_async_refresh(true);
    } else if (kind == PopoverKind::Spotlight) {
        spotlight_query_.clear();
        spotlight_selected_ = 0;
        if (cached_apps_.empty()) {
            cached_apps_ = sysinfo::list_desktop_apps();
        }
        rebuild_spotlight_matches();
    }

    switch (kind) {
        case PopoverKind::LogoMenu:      popover_w_ = 224; popover_h_ = 258; break;
        case PopoverKind::AppMenu:
            popover_w_ = 228;
            if (menu_name == "Edit") popover_h_ = 204;
            else if (menu_name == "Go") popover_h_ = 242;
            else if (menu_name == "Window") popover_h_ = 152;
            else popover_h_ = 120;
            break;
        case PopoverKind::Wifi:          popover_w_ = 292; popover_h_ = 192 + static_cast<int>(std::min<std::size_t>(cached_wifi_.networks.size(), 6)) * 32; break;
        case PopoverKind::Bluetooth:     popover_w_ = 280; popover_h_ = 182 + static_cast<int>(std::max<std::size_t>(cached_bt_.devices.size(), 1)) * 32; break;
        case PopoverKind::Volume:        popover_w_ = 280; popover_h_ = 158 + static_cast<int>(std::max<std::size_t>(cached_audio_.sinks.size(), 1)) * 32; break;
        case PopoverKind::Brightness:    popover_w_ = 268; popover_h_ = 148; break;
        case PopoverKind::Battery:       popover_w_ = 264; popover_h_ = 170; break;
        case PopoverKind::ControlCenter: popover_w_ = 248; popover_h_ = 408; break;
        case PopoverKind::Spotlight:
            popover_w_ = 480;
            popover_h_ = 54 + static_cast<int>(std::min<std::size_t>(spotlight_matches_.size(), 7)) * 38 + 14;
            break;
        case PopoverKind::ShaderTuner:   popover_w_ = 286; popover_h_ = 310; break;
        case PopoverKind::Calendar:      popover_w_ = 272; popover_h_ = 262; break;
        case PopoverKind::PowerMenu:     popover_w_ = 220; popover_h_ = 180; break;
        case PopoverKind::None:          return;
    }

    const int bar_w = std::max(640, bar_surface_->width());
    const int side_m = (cfg_.look == "pill") ? defaults::BAR_SIDE_MARGIN : 0;
    const int top_m = ((cfg_.look == "pill") ? cfg_.bar_margin : 0) + cfg_.bar_height + 6 - kPopShadowMargin;
    int target_x = static_cast<int>(anchor_x) + side_m - kPopShadowMargin;
    if (kind == PopoverKind::Spotlight) {
        target_x = (bar_w - popover_w_) / 2 + side_m - kPopShadowMargin;
    }
    const int total_w = popover_w_ + kPopShadowMargin * 2;
    const int max_left = std::max(0, bar_w + side_m * 2 - total_w);
    const int left_m = std::clamp(target_x, 0, max_left);

    popover_anchor_x_ = static_cast<double>(left_m + kPopShadowMargin);
    popover_top_m_ = std::max(0, top_m);
    popover_left_m_ = left_m;

    if (!ensure_popover_surface()) {
        open_popover_ = PopoverKind::None;
        return;
    }
    bar_surface_->request_redraw();
}

glass::GlassMaterial BarApp::current_glass_material() const {
    return glass::make_glass_material({
        cfg_.glass_preset,
        cfg_.blur_radius,
        cfg_.refraction_strength,
        cfg_.chromatic_aberration,
        cfg_.specular_intensity,
        cfg_.tint_opacity,
        cfg_.glass_opacity
    });
}

glass::GlassMaterial BarApp::current_bar_material() const {
    glass::GlassMaterial bar_mat = current_glass_material();
    bar_mat.shadowAlpha = 0.0f;
    if (cfg_.glass_preset == "solid") {
        return bar_mat;
    }
    if (cfg_.look == "attached") {
        bar_mat.frost = std::min(bar_mat.frost, 0.30f);
        if (cfg_.glass_preset == "clear" || cfg_.glass_preset == "regular") {
            bar_mat.tintR = 0.20f;
            bar_mat.tintG = 0.28f;
            bar_mat.tintB = 0.44f;
            bar_mat.tintAlpha = std::min(bar_mat.tintAlpha, 0.06f);
            bar_mat.refraction = std::min(bar_mat.refraction, 0.22f);
            bar_mat.specular = std::min(bar_mat.specular, 0.16f);
        }
    }
    return bar_mat;
}

std::vector<glass::GlassPanelSpec> BarApp::collect_bar_panels(int w, int h, int scale) const {
    const float sc = static_cast<float>(std::max(1, scale));
    const float radius = (cfg_.look == "pill") ? static_cast<float>(cfg_.border_radius) : 0.0f;
    glass::GlassPanelSpec p;
    p.x = 0.0f;
    p.y = 0.0f;
    p.w = static_cast<float>(w) * sc;
    p.h = static_cast<float>(h) * sc;
    p.radius = radius * sc;
    p.isPill = (cfg_.look == "pill");
    p.overrideMaterial = true;
    p.customMaterial = current_bar_material();
    p.customOpacity = (cfg_.glass_preset == "solid") ? 1.0f : ((cfg_.look == "pill") ? 0.44f : 0.34f);
    return {p};
}

std::vector<glass::GlassPanelSpec> BarApp::collect_popover_panels(int total_w, int total_h, int scale) const {
    std::vector<glass::GlassPanelSpec> panels;
    const float sc = static_cast<float>(std::max(1, scale));
    const float m = static_cast<float>(kPopShadowMargin);
    const float pw = std::max(1.0f, static_cast<float>(total_w) - 2.0f * m);
    const float ph = std::max(1.0f, static_cast<float>(total_h) - 2.0f * m);

    auto push_panel = [&](float x, float y, float w, float h, float r, bool is_pill, float opacity) {
        glass::GlassPanelSpec spec;
        spec.x = x * sc;
        spec.y = y * sc;
        spec.w = w * sc;
        spec.h = h * sc;
        spec.radius = r * sc;
        spec.isPill = is_pill;
        spec.customOpacity = (cfg_.glass_preset == "solid") ? 1.0f : opacity;
        panels.push_back(spec);
    };

    if (open_popover_ == PopoverKind::ControlCenter) {
        // Slim mizzle Control Center: every pill & circle is an independent floating glass lens
        push_panel(m + 0.0f,   m + 0.0f,   120.0f, 44.0f, 22.0f, true, 0.50f);  // Wi-Fi
        push_panel(m + 0.0f,   m + 52.0f,  56.0f,  44.0f, 22.0f, true, 0.48f);  // Bluetooth
        push_panel(m + 64.0f,  m + 52.0f,  56.0f,  44.0f, 22.0f, true, 0.48f);  // AirDrop
        push_panel(m + 128.0f, m + 0.0f,   120.0f, 96.0f, 20.0f, true, 0.50f);  // Media card
        push_panel(m + 0.0f,   m + 104.0f, 120.0f, 44.0f, 22.0f, true, 0.50f);  // Focus
        push_panel(m + 128.0f, m + 104.0f, 56.0f,  44.0f, 22.0f, true, 0.48f);  // Workspaces
        push_panel(m + 192.0f, m + 104.0f, 56.0f,  44.0f, 22.0f, true, 0.48f);  // Bar Look
        push_panel(m + 0.0f,   m + 156.0f, 248.0f, 50.0f, 22.0f, true, 0.50f);  // Sound
        push_panel(m + 0.0f,   m + 214.0f, 248.0f, 50.0f, 22.0f, true, 0.50f);  // Display
        for (int i = 0; i < 8; ++i) {
            const int col = i % 4;
            const int row = i / 4;
            push_panel(m + static_cast<float>(col) * 64.0f,
                       m + 272.0f + static_cast<float>(row) * 52.0f,
                       56.0f, 44.0f, 22.0f, true, 0.48f);
        }
        push_panel(m + 76.0f, m + 380.0f, 96.0f, 24.0f, 12.0f, true, 0.48f);   // Edit Controls
        return panels;
    }

    const float pop_r = (open_popover_ == PopoverKind::Spotlight) ? 21.0f :
                        ((open_popover_ == PopoverKind::ShaderTuner) ? 20.0f : 18.0f);
    push_panel(m, m, pw, ph, pop_r, open_popover_ == PopoverKind::Spotlight, 0.62f);
    return panels;
}

bool BarApp::render_bar_egl(wl_surface* surf, int w, int h, int scale) {
    if (!mizzle_.is_available() || !mizzle_.has_wayland_egl() || w <= 0 || h <= 0) {
        return false;
    }
    const int sc = std::max(1, scale);
    const int px_w = w * sc;
    const int px_h = h * sc;
    if (!bar_ui_surf_ || bar_ui_w_ != px_w || bar_ui_h_ != px_h) {
        if (bar_ui_surf_) cairo_surface_destroy(bar_ui_surf_);
        bar_ui_surf_ = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, px_w, px_h);
        bar_ui_w_ = px_w;
        bar_ui_h_ = px_h;
    }

    cairo_t* cr = cairo_create(bar_ui_surf_);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    if (sc > 1) cairo_scale(cr, static_cast<double>(sc), static_cast<double>(sc));
    draw_bar_foreground(cr, w, h);
    cairo_destroy(cr);

    const bool is_pill = (cfg_.look == "pill");
    const int top_m = is_pill ? cfg_.bar_margin : 0;
    const int side_m = is_pill ? defaults::BAR_SIDE_MARGIN : 0;
    const int slide_offset = static_cast<int>((1.0 - slide_anim_.value()) * -(cfg_.bar_height + top_m + 6));

    const float time_sec = std::chrono::duration<float>(
                               std::chrono::steady_clock::now() - start_time_).count();
    glass::ClickPulse click = bar_click_;
    click.x *= static_cast<float>(sc);
    click.y *= static_cast<float>(sc);

    const bool translucent = (cfg_.glass_preset != "solid");
    return mizzle_.render_surface(
        surf, px_w, px_h,
        static_cast<float>(side_m * sc),
        static_cast<float>((top_m + slide_offset) * sc),
        time_sec, click,
        current_bar_material(),
        collect_bar_panels(w, h, sc),
        bar_ui_surf_,
        translucent);
}

bool BarApp::render_popover_egl(wl_surface* surf, int w, int h, int scale) {
    if (!mizzle_.is_available() || !mizzle_.has_wayland_egl() || w <= 0 || h <= 0) {
        return false;
    }
    const int sc = std::max(1, scale);
    const int px_w = w * sc;
    const int px_h = h * sc;
    if (!pop_ui_surf_ || pop_ui_w_ != px_w || pop_ui_h_ != px_h) {
        if (pop_ui_surf_) cairo_surface_destroy(pop_ui_surf_);
        pop_ui_surf_ = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, px_w, px_h);
        pop_ui_w_ = px_w;
        pop_ui_h_ = px_h;
    }

    pop_actions_.clear();
    cairo_t* cr = cairo_create(pop_ui_surf_);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    if (sc > 1) cairo_scale(cr, static_cast<double>(sc), static_cast<double>(sc));
    cairo_translate(cr, static_cast<double>(kPopShadowMargin), static_cast<double>(kPopShadowMargin));
    draw_popover_foreground(cr, std::max(1, w - kPopShadowMargin * 2));
    cairo_destroy(cr);

    const float time_sec = std::chrono::duration<float>(
                               std::chrono::steady_clock::now() - start_time_).count();
    glass::ClickPulse click = pop_click_;
    click.x *= static_cast<float>(sc);
    click.y *= static_cast<float>(sc);

    const bool translucent = (cfg_.glass_preset != "solid");
    return mizzle_.render_surface(
        surf, px_w, px_h,
        static_cast<float>(popover_left_m_ * sc),
        static_cast<float>(popover_top_m_ * sc),
        time_sec, click,
        current_glass_material(),
        collect_popover_panels(w, h, sc),
        pop_ui_surf_,
        translucent);
}

void BarApp::render_bar_preview(cairo_t* cr, int w, int h) {
    config::ensure_default_config();
    cfg_ = config::load_config();
    palette_ = themes::resolve_palette(cfg_);
    backdrop_.init(cfg_);
    backdrop_.load_wallpaper(palette_.wallpaper_path, w, 800, palette_.blur_radius);
    cached_clock_ = format_clock_str(cfg_);
    cached_active_app_ = "Finder";
    cached_workspaces_ = {{1, 1, true, true, ""}, {2, 2, false, true, ""}, {3, 3, false, false, ""}};
    cached_wifi_.enabled = true;
    cached_wifi_.connected = true;
    cached_wifi_.ssid = "Tahoe-5G";
    cached_bt_.powered = true;
    cached_battery_.present = true;
    cached_battery_.percent = 88;
    cached_audio_.volume_percent = 72;

    if (!mizzle_.is_available() && mizzle_.init(nullptr)) {
        if (backdrop_.raw_wallpaper()) mizzle_.set_backdrop_cairo(backdrop_.raw_wallpaper());
    }
    if (mizzle_.is_available()) {
        cairo_surface_t* ui = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        cairo_t* ucr = cairo_create(ui);
        draw_bar_foreground(ucr, w, h);
        cairo_destroy(ucr);

        cairo_surface_t* out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        if (mizzle_.render_to_cairo(out, 0.0f, 0.0f, 0.0f, {}, current_bar_material(),
                                    collect_bar_panels(w, h, 1), ui)) {
            cairo_set_source_surface(cr, out, 0.0, 0.0);
            cairo_paint(cr);
            cairo_surface_destroy(out);
            cairo_surface_destroy(ui);
            return;
        }
        cairo_surface_destroy(out);
        cairo_surface_destroy(ui);
    }

    const double radius = (cfg_.look == "pill") ? static_cast<double>(cfg_.border_radius) : 0.0;
    glass::draw_glass_panel(cr, &backdrop_, 0.0, 0.0, 0.0, 0.0,
                            static_cast<double>(w), static_cast<double>(h),
                            radius, palette_, &palette_.bar_bg, false, 0.35, 0.45);
    draw_bar_foreground(cr, w, h);
}

void BarApp::draw_bar(cairo_t* cr, int w, int h) {
    if (mizzle_.is_available()) {
        cairo_surface_t* ui = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, std::max(1, w), std::max(1, h));
        cairo_t* ucr = cairo_create(ui);
        draw_bar_foreground(ucr, w, h);
        cairo_destroy(ucr);

        cairo_surface_t* out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, std::max(1, w), std::max(1, h));
        const bool is_pill = (cfg_.look == "pill");
        const int top_m = is_pill ? cfg_.bar_margin : 0;
        const int side_m = is_pill ? defaults::BAR_SIDE_MARGIN : 0;
        if (mizzle_.render_to_cairo(out, static_cast<float>(side_m), static_cast<float>(top_m),
                                    0.0f, bar_click_, current_bar_material(),
                                    collect_bar_panels(w, h, 1), ui)) {
            cairo_set_source_surface(cr, out, 0.0, 0.0);
            cairo_paint(cr);
            cairo_surface_destroy(out);
            cairo_surface_destroy(ui);
            return;
        }
        cairo_surface_destroy(out);
        cairo_surface_destroy(ui);
    }

    const double radius = (cfg_.look == "pill") ? static_cast<double>(cfg_.border_radius) : 0.0;
    const bool overlay_only = !palette_.solid_mode;
    const int ptr_bucket = (cfg_.cursor_reactive && pointer_norm_x_ >= 0.0)
        ? static_cast<int>(pointer_norm_x_ * 32.0)
        : -1;

    if (!bar_glass_cache_ || bar_cache_w_ != w || bar_cache_h_ != h ||
        bar_cache_gen_ != cfg_gen_ || bar_cache_ptr_bucket_ != ptr_bucket) {
        if (bar_glass_cache_) cairo_surface_destroy(bar_glass_cache_);
        bar_glass_cache_ = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, std::max(1, w), std::max(1, h));
        cairo_t* bcr = cairo_create(bar_glass_cache_);
        cairo_set_operator(bcr, CAIRO_OPERATOR_CLEAR);
        cairo_paint(bcr);
        cairo_set_operator(bcr, CAIRO_OPERATOR_OVER);
        glass::draw_glass_panel(bcr, &backdrop_, 0.0, 0.0,
                                (cfg_.look == "pill") ? defaults::BAR_SIDE_MARGIN : 0.0,
                                (cfg_.look == "pill") ? cfg_.bar_margin : 0.0,
                                static_cast<double>(w), static_cast<double>(h),
                                radius, palette_, &palette_.bar_bg, overlay_only,
                                pointer_norm_x_, pointer_norm_y_);
        cairo_destroy(bcr);
        bar_cache_w_ = w;
        bar_cache_h_ = h;
        bar_cache_gen_ = cfg_gen_;
        bar_cache_ptr_bucket_ = ptr_bucket;
    }

    cairo_save(cr);
    cairo_set_source_surface(cr, bar_glass_cache_, 0.0, 0.0);
    cairo_paint(cr);
    cairo_restore(cr);

    draw_bar_foreground(cr, w, h);
}

void BarApp::draw_bar_foreground(cairo_t* cr, int w, int h) {
    bar_hits_.clear();

    const std::uint32_t fg_tint =
        (palette_.text_primary.r > 0.5) ? 0xFFF5F7FAU : 0xFF1A1C20U;
    const double icon_sz = 15.0;
    const double icon_y = (static_cast<double>(h) - icon_sz) * 0.5;

    auto draw_hover_pill = [&](double hx, double hy, double hw, double hh, const std::string& id, bool active = false) {
        if (active || hovered_bar_id_ == id) {
            glass::append_rounded_rect(cr, hx, hy, hw, hh, std::min(7.0, hh * 0.42));
            const double a = active ? 0.24 : 0.14;
            if (palette_.text_primary.r > 0.5) {
                cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, a);
            } else {
                cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, a * 0.60);
            }
            cairo_fill(cr);
        }
    };

    double lx = (cfg_.look == "pill") ? 12.0 : 10.0;

    // Liquid Glass M mark
    const bool logo_active = (open_popover_ == PopoverKind::LogoMenu);
    draw_hover_pill(lx - 4.0, 3.0, 26.0, static_cast<double>(h) - 6.0, "logo", logo_active);
    const std::string mark_path = icons::resolve_icon("mistbar-mark.svg");
    if (!mark_path.empty()) {
        icons_.draw(cr, mark_path, lx + 1.5, icon_y, 15.0, 15.0, fg_tint);
    }
    bar_hits_.push_back({lx - 4.0, 0.0, 26.0, static_cast<double>(h), "logo", 0});
    lx += 28.0;

    // Active app name (bold, measured precisely)
    const double app_text_w = window::measure_pango_text_width(cr, cached_active_app_, 9.4, true);
    const double app_w = std::min(168.0, app_text_w + 14.0);
    window::draw_pango_text_centered(cr, cached_active_app_, lx + 6.0, 0.0, app_w - 12.0,
                                     static_cast<double>(h), 9.4, true, palette_.text_primary);
    bar_hits_.push_back({lx, 0.0, app_w, static_cast<double>(h), "app", 0});
    lx += app_w + 2.0;

    // macOS App Menus (always visible, including Finder on desktop)
    if (cfg_.show_app_menu) {
        const char* menus[] = {"File", "Edit", "View", "Go", "Window", "Help"};
        for (const char* m : menus) {
            const std::string label(m);
            const double tw = window::measure_pango_text_width(cr, label, 9.2, false);
            const double mw = tw + 16.0;
            if (lx + mw > static_cast<double>(w) * 0.44) break;
            const std::string hid = "menu:" + label;
            const bool active = (open_popover_ == PopoverKind::AppMenu && active_menu_name_ == label);
            draw_hover_pill(lx, 3.0, mw, static_cast<double>(h) - 6.0, hid, active);
            window::draw_pango_text_centered(cr, label, lx + 8.0, 0.0, tw + 2.0,
                                             static_cast<double>(h), 9.2, false, palette_.text_primary);
            bar_hits_.push_back({lx, 0.0, mw, static_cast<double>(h), hid, 0});
            lx += mw;
        }
    }

    // Center Niri Workspace Dots
    if (cfg_.show_workspaces && cached_workspaces_.size() > 1) {
        const double total_ws_w = static_cast<double>(cached_workspaces_.size()) * 16.0 + 10.0;
        double wx = static_cast<double>(w) * 0.5 - total_ws_w * 0.5;
        if (wx > lx + 10.0) {
            glass::append_rounded_rect(cr, wx - 4.0, 5.0, total_ws_w + 8.0, static_cast<double>(h) - 10.0, 9.0);
            cairo_set_source_rgba(cr, palette_.text_primary.r, palette_.text_primary.g,
                                  palette_.text_primary.b, 0.07);
            cairo_fill(cr);

            const double cy = static_cast<double>(h) * 0.5;
            wx += 5.0;
            for (const auto& ws : cached_workspaces_) {
                const double dot_w = ws.focused ? 14.0 : 6.0;
                const double dot_h = 6.0;
                const double dx = wx + (16.0 - dot_w) * 0.5;
                const double dy = cy - dot_h * 0.5;
                glass::append_rounded_rect(cr, dx, dy, dot_w, dot_h, 3.0);
                if (ws.focused) {
                    cairo_set_source_rgba(cr, palette_.text_primary.r, palette_.text_primary.g,
                                          palette_.text_primary.b, 0.96);
                } else if (ws.has_windows) {
                    cairo_set_source_rgba(cr, palette_.text_primary.r, palette_.text_primary.g,
                                          palette_.text_primary.b, 0.52);
                } else {
                    cairo_set_source_rgba(cr, palette_.text_primary.r, palette_.text_primary.g,
                                          palette_.text_primary.b, 0.24);
                }
                cairo_fill(cr);
                bar_hits_.push_back({wx, 0.0, 16.0, static_cast<double>(h), "ws", ws.idx});
                wx += 16.0;
            }
        }
    }

    // Right section (placed right-to-left in macOS Tahoe order: Clock, Control Center, Spotlight, Wi-Fi, Battery, optional extras)
    double rx = static_cast<double>(w) - ((cfg_.look == "pill") ? 12.0 : 12.0);

    if (cfg_.show_power_menu) {
        rx -= 24.0;
        draw_hover_pill(rx, 3.0, 22.0, static_cast<double>(h) - 6.0, "power",
                        open_popover_ == PopoverKind::PowerMenu);
        const std::string p = icons::resolve_icon("power.svg");
        if (!p.empty()) icons_.draw(cr, p, rx + 3.5, icon_y, icon_sz, icon_sz, fg_tint);
        bar_hits_.push_back({rx, 0.0, 22.0, static_cast<double>(h), "power", 0});
        rx -= 4.0;
    }

    // Clock (far right, measured precisely)
    const double clock_tw = window::measure_pango_text_width(cr, cached_clock_, 9.2, false);
    const double clock_w = clock_tw + 14.0;
    rx -= clock_w;
    draw_hover_pill(rx, 3.0, clock_w, static_cast<double>(h) - 6.0, "clock",
                    open_popover_ == PopoverKind::Calendar);
    window::draw_pango_text_centered(cr, cached_clock_, rx + 7.0, 0.0, clock_tw + 2.0,
                                     static_cast<double>(h), 9.2, false, palette_.text_primary);
    bar_hits_.push_back({rx, 0.0, clock_w, static_cast<double>(h), "clock", 0});
    rx -= 3.0;

    // Control Center
    if (cfg_.show_control_center) {
        rx -= 25.0;
        draw_hover_pill(rx, 3.0, 23.0, static_cast<double>(h) - 6.0, "cc",
                        open_popover_ == PopoverKind::ControlCenter);
        const std::string p = icons::resolve_icon("control-center.svg");
        if (!p.empty()) icons_.draw(cr, p, rx + 4.0, icon_y, icon_sz, icon_sz, fg_tint);
        bar_hits_.push_back({rx, 0.0, 23.0, static_cast<double>(h), "cc", 0});
        rx -= 3.0;
    }

    // Spotlight Search
    if (cfg_.show_spotlight) {
        rx -= 24.0;
        draw_hover_pill(rx, 3.0, 22.0, static_cast<double>(h) - 6.0, "spotlight",
                        open_popover_ == PopoverKind::Spotlight);
        const std::string p = icons::resolve_icon("search.svg");
        if (!p.empty()) icons_.draw(cr, p, rx + 3.5, icon_y, icon_sz, icon_sz, fg_tint);
        bar_hits_.push_back({rx, 0.0, 22.0, static_cast<double>(h), "spotlight", 0});
        rx -= 3.0;
    }

    // Wi-Fi (next to Search, matching macOS Tahoe screenshot)
    if (cfg_.show_network) {
        rx -= 25.0;
        draw_hover_pill(rx, 3.0, 23.0, static_cast<double>(h) - 6.0, "wifi",
                        open_popover_ == PopoverKind::Wifi);
        const std::string p = icons::resolve_icon(cached_wifi_.enabled && cached_wifi_.connected ? "wifi.svg" : "wifi-off.svg");
        if (!p.empty()) icons_.draw(cr, p, rx + 4.0, icon_y, icon_sz, icon_sz, fg_tint);
        bar_hits_.push_back({rx, 0.0, 23.0, static_cast<double>(h), "wifi", 0});
        rx -= 3.0;
    }

    // Battery with dynamic macOS fill level (next to Wi-Fi, matching Tahoe screenshot)
    if (cfg_.show_battery && cached_battery_.present) {
        const std::string pct_str = std::to_string(cached_battery_.percent) + "%";
        const double pct_w = cfg_.show_battery_pct ? window::measure_pango_text_width(cr, pct_str, 8.6, false) + 5.0 : 0.0;
        const double bw = 26.0 + pct_w;
        rx -= bw;
        draw_hover_pill(rx, 3.0, bw, static_cast<double>(h) - 6.0, "battery",
                        open_popover_ == PopoverKind::Battery);
        if (cfg_.show_battery_pct) {
            window::draw_pango_text_centered(cr, pct_str, rx + 4.0, 0.0, pct_w,
                                             static_cast<double>(h), 8.6, false, palette_.text_primary);
        }
        const double bx = rx + pct_w + 3.0;
        const double by = (static_cast<double>(h) - 10.5) * 0.5;
        glass::append_rounded_rect(cr, bx, by, 17.5, 10.5, 2.8);
        cairo_set_source_rgba(cr, palette_.text_primary.r, palette_.text_primary.g,
                              palette_.text_primary.b, 0.72);
        cairo_set_line_width(cr, 1.15);
        cairo_stroke(cr);

        const double fill_frac = std::clamp(static_cast<double>(cached_battery_.percent) / 100.0, 0.08, 1.0);
        glass::append_rounded_rect(cr, bx + 2.0, by + 2.0, 13.5 * fill_frac, 6.5, 1.4);
        if (cached_battery_.charging) {
            cairo_set_source_rgba(cr, 0.19, 0.82, 0.35, 0.96);
        } else if (cached_battery_.percent <= 20) {
            cairo_set_source_rgba(cr, 1.0, 0.27, 0.23, 0.96);
        } else {
            cairo_set_source_rgba(cr, palette_.text_primary.r, palette_.text_primary.g,
                                  palette_.text_primary.b, 0.94);
        }
        cairo_fill(cr);

        glass::append_rounded_rect(cr, bx + 18.5, by + 3.2, 1.6, 4.1, 0.8);
        cairo_set_source_rgba(cr, palette_.text_primary.r, palette_.text_primary.g,
                              palette_.text_primary.b, 0.65);
        cairo_fill(cr);

        bar_hits_.push_back({rx, 0.0, bw, static_cast<double>(h), "battery", 0});
        rx -= 3.0;
    }

    // Bluetooth (optional in Menu Bar settings)
    if (cfg_.show_bluetooth) {
        rx -= 24.0;
        draw_hover_pill(rx, 3.0, 22.0, static_cast<double>(h) - 6.0, "bluetooth",
                        open_popover_ == PopoverKind::Bluetooth);
        const char* bsvg = !cached_bt_.powered
                               ? "bluetooth-off.svg"
                               : (!cached_bt_.connected_name.empty() ? "bluetooth-connected.svg" : "bluetooth.svg");
        const std::string p = icons::resolve_icon(bsvg);
        if (!p.empty()) icons_.draw(cr, p, rx + 3.5, icon_y, icon_sz, icon_sz, fg_tint);
        bar_hits_.push_back({rx, 0.0, 22.0, static_cast<double>(h), "bluetooth", 0});
        rx -= 3.0;
    }

    // Brightness (optional)
    if (cfg_.show_brightness) {
        rx -= 24.0;
        draw_hover_pill(rx, 3.0, 22.0, static_cast<double>(h) - 6.0, "brightness",
                        open_popover_ == PopoverKind::Brightness);
        const std::string p = icons::resolve_icon("sun.svg");
        if (!p.empty()) icons_.draw(cr, p, rx + 3.5, icon_y, icon_sz, icon_sz, fg_tint);
        bar_hits_.push_back({rx, 0.0, 22.0, static_cast<double>(h), "brightness", 0});
        rx -= 3.0;
    }

    // Volume (optional)
    if (cfg_.show_volume) {
        rx -= 24.0;
        draw_hover_pill(rx, 3.0, 22.0, static_cast<double>(h) - 6.0, "volume",
                        open_popover_ == PopoverKind::Volume);
        const char* vsvg = cached_audio_.muted || cached_audio_.volume_percent == 0
                               ? "volume-mute.svg"
                               : (cached_audio_.volume_percent < 45 ? "volume-low.svg" : "volume.svg");
        const std::string p = icons::resolve_icon(vsvg);
        if (!p.empty()) icons_.draw(cr, p, rx + 3.5, icon_y, icon_sz, icon_sz, fg_tint);
        bar_hits_.push_back({rx, 0.0, 22.0, static_cast<double>(h), "volume", 0});
        rx -= 3.0;
    }

    // System Tray items
    if (cfg_.show_tray && !cached_tray_.empty()) {
        for (int i = static_cast<int>(cached_tray_.size()) - 1; i >= 0; --i) {
            rx -= 22.0;
            const std::string tid = "tray:" + std::to_string(i);
            draw_hover_pill(rx, 3.0, 20.0, static_cast<double>(h) - 6.0, tid);
            const std::string icon_path = icons::resolve_desktop_icon(cached_tray_[static_cast<std::size_t>(i)].icon);
            if (!icon_path.empty()) {
                icons_.draw(cr, icon_path, rx + 3.0, icon_y + 0.5, 14.0, 14.0, 0);
            } else {
                const std::string ch(1, cached_tray_[static_cast<std::size_t>(i)].title.empty()
                                          ? 'T'
                                          : static_cast<char>(std::toupper(static_cast<unsigned char>(
                                                cached_tray_[static_cast<std::size_t>(i)].title[0]))));
                window::draw_pango_text_centered(cr, ch, rx, 0.0, 20.0, static_cast<double>(h),
                                                 8.8, true, palette_.text_primary, true, false);
            }
            bar_hits_.push_back({rx, 0.0, 20.0, static_cast<double>(h), "tray", i});
            rx -= 2.0;
        }
    }

    // Privacy Microphone Indicator
    if (cfg_.show_privacy && cached_mic_active_) {
        rx -= 24.0;
        const double py = (static_cast<double>(h) - 16.0) * 0.5;
        glass::append_rounded_rect(cr, rx, py, 20.0, 16.0, 8.0);
        cairo_set_source_rgba(cr, 1.0, 0.58, 0.0, 0.94);
        cairo_fill(cr);
        const std::string p = icons::resolve_icon("mic.svg");
        if (!p.empty()) icons_.draw(cr, p, rx + 4.5, py + 2.5, 11.0, 11.0, 0xFFFFFFFFU);
        rx -= 4.0;
    }
}

void BarApp::handle_bar_pointer(const wl::PointerEvent& ev) {
    if (ev.type == wl::PointerEvent::Type::Enter) {
        bar_hovered_ = true;
        grace_active_ = false;
        update_visibility();
    } else if (ev.type == wl::PointerEvent::Type::Leave) {
        bar_hovered_ = false;
        pointer_norm_x_ = -1.0;
        pointer_norm_y_ = -1.0;
        hovered_bar_id_.clear();
        update_visibility();
        bar_surface_->request_redraw();
        return;
    } else if (ev.type == wl::PointerEvent::Type::Motion) {
        const double w = std::max(1, bar_surface_->width());
        const double h = std::max(1, bar_surface_->height());
        pointer_norm_x_ = std::clamp(ev.x / w, 0.0, 1.0);
        pointer_norm_y_ = std::clamp(ev.y / h, 0.0, 1.0);
        std::string next_hover;
        for (const auto& r : bar_hits_) {
            if (ev.x >= r.x && ev.x <= r.x + r.w && ev.y >= r.y && ev.y <= r.y + r.h) {
                next_hover = (r.id == "tray") ? ("tray:" + std::to_string(r.arg_int)) : r.id;
                break;
            }
        }
        bool need_redraw = (next_hover != hovered_bar_id_);
        if (!need_redraw && cfg_.cursor_reactive) {
            const int bucket = static_cast<int>(pointer_norm_x_ * 32.0);
            const auto now = std::chrono::steady_clock::now();
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_reactive_draw_).count();
            if (bucket != bar_cache_ptr_bucket_ && ms >= 40) {
                last_reactive_draw_ = now;
                need_redraw = true;
            }
        }
        if (need_redraw) {
            hovered_bar_id_ = next_hover;
            bar_surface_->request_redraw();
        }
        return;
    } else if (ev.type == wl::PointerEvent::Type::Axis) {
        for (const auto& r : bar_hits_) {
            if (ev.x >= r.x && ev.x <= r.x + r.w) {
                const int delta = (ev.axis_dy < 0.0) ? 5 : -5;
                if (r.id == "volume") {
                    cached_audio_.volume_percent = std::clamp(cached_audio_.volume_percent + delta, 0, 100);
                    sysinfo::spawn_detached({"wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@",
                                             std::to_string(cached_audio_.volume_percent) + "%"});
                    bar_surface_->request_redraw();
                } else if (r.id == "brightness") {
                    cached_brightness_.percent = std::clamp(cached_brightness_.percent + delta, 5, 100);
                    sysinfo::spawn_detached({"brightnessctl", "set",
                                             std::to_string(cached_brightness_.percent) + "%"});
                    bar_surface_->request_redraw();
                }
                break;
            }
        }
        return;
    }

    if (ev.type != wl::PointerEvent::Type::ButtonPress || ev.button != BTN_LEFT) {
        return;
    }

    bar_click_.x = static_cast<float>(ev.x);
    bar_click_.y = static_cast<float>(ev.y);
    bar_click_.age_sec = 0.0f;
    bar_click_.active = true;
    bar_click_start_ = std::chrono::steady_clock::now();
    bar_surface_->request_redraw();

    for (const auto& r : bar_hits_) {
        if (ev.x >= r.x && ev.x <= r.x + r.w && ev.y >= r.y && ev.y <= r.y + r.h) {
            if (r.id == "logo") {
                queue_open_popover(PopoverKind::LogoMenu, r.x);
            } else if (r.id.rfind("menu:", 0) == 0) {
                queue_open_popover(PopoverKind::AppMenu, r.x, r.id.substr(5));
            } else if (r.id == "ws") {
                sysinfo::focus_niri_workspace(r.arg_int);
                refresh_clock_and_niri();
                bar_surface_->request_redraw();
            } else if (r.id == "wifi") {
                queue_open_popover(PopoverKind::Wifi, r.x - 180.0);
            } else if (r.id == "bluetooth") {
                queue_open_popover(PopoverKind::Bluetooth, r.x - 180.0);
            } else if (r.id == "volume") {
                queue_open_popover(PopoverKind::Volume, r.x - 180.0);
            } else if (r.id == "brightness") {
                queue_open_popover(PopoverKind::Brightness, r.x - 160.0);
            } else if (r.id == "battery") {
                queue_open_popover(PopoverKind::Battery, r.x - 160.0);
            } else if (r.id == "spotlight") {
                queue_open_popover(PopoverKind::Spotlight, r.x);
            } else if (r.id == "cc") {
                queue_open_popover(PopoverKind::ControlCenter, r.x - 220.0);
            } else if (r.id == "clock") {
                queue_open_popover(PopoverKind::Calendar, r.x - 120.0);
            } else if (r.id == "power") {
                queue_open_popover(PopoverKind::PowerMenu, r.x - 170.0);
            } else if (r.id == "tray") {
                const auto idx = static_cast<std::size_t>(r.arg_int);
                if (idx < cached_tray_.size()) {
                    sysinfo::activate_tray_item(cached_tray_[idx]);
                }
            }
            return;
        }
    }
    queue_close_popover();
}

void BarApp::draw_separator(cairo_t* cr, double x, double y, double w) {
    cairo_rectangle(cr, x, y, w, 1.0);
    cairo_set_source_rgba(cr, palette_.border_outer.r, palette_.border_outer.g,
                          palette_.border_outer.b, 0.18);
    cairo_fill(cr);
}

void BarApp::draw_toggle_pill(cairo_t* cr, double x, double y, bool on) {
    glass::append_rounded_rect(cr, x, y, 36.0, 20.0, 10.0);
    if (on) {
        cairo_set_source_rgba(cr, palette_.accent.r, palette_.accent.g, palette_.accent.b, 0.95);
    } else {
        cairo_set_source_rgba(cr, 0.5, 0.53, 0.58, 0.36);
    }
    cairo_fill(cr);
    cairo_arc(cr, on ? (x + 26.0) : (x + 10.0), y + 10.0, 7.5, 0.0, 6.283185);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.98);
    cairo_fill(cr);
}

void BarApp::draw_capsule_slider(cairo_t* cr, double x, double y, double w, double h, int pct, const std::string& icon_svg) {
    glass::append_rounded_rect(cr, x, y, w, h, h * 0.5);
    cairo_set_source_rgba(cr, 0.5, 0.52, 0.56, 0.28);
    cairo_fill(cr);

    const double fill_w = std::clamp(static_cast<double>(pct) / 100.0, 0.09, 1.0) * w;
    glass::append_rounded_rect(cr, x, y, fill_w, h, h * 0.5);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, palette_.dark_mode ? 0.92 : 0.98);
    cairo_fill(cr);

    const std::string p = icons::resolve_icon(icon_svg);
    if (!p.empty()) {
        icons_.draw(cr, p, x + 6.0, y + (h - 13.0) * 0.5, 13.0, 13.0, 0xFF1C2026U);
    }
}

void BarApp::draw_popover_row(cairo_t* cr, double x, double y, double w, const std::string& label,
                              const std::string& shortcut, const std::string& action_id,
                              const std::string& arg, bool bold, bool active) {
    const std::string key = action_id + ":" + arg + ":" + label;
    const bool hovered = (!hovered_pop_id_.empty() && hovered_pop_id_ == key);
    if (active || hovered) {
        glass::append_rounded_rect(cr, x, y, w, 26.0, 7.0);
        if (active) {
            cairo_set_source_rgba(cr, palette_.accent.r, palette_.accent.g, palette_.accent.b, 0.28);
        } else {
            cairo_set_source_rgba(cr, palette_.accent.r, palette_.accent.g, palette_.accent.b, 0.85);
        }
        cairo_fill(cr);
    }
    const themes::Color fg = hovered ? themes::Color{1, 1, 1, 1} : palette_.text_primary;
    const themes::Color sc_fg = hovered ? themes::Color{1, 1, 1, 0.85} : palette_.text_secondary;
    const double sc_w = shortcut.empty() ? 0.0 : 72.0;
    window::draw_pango_text_centered(cr, label, x + 10.0, y, w - 20.0 - sc_w, 26.0, 9.2, bold, fg);
    if (!shortcut.empty()) {
        window::draw_pango_text_centered(cr, shortcut, x + w - sc_w - 8.0, y, sc_w, 26.0,
                                         8.2, false, sc_fg, false, true);
    }
    pop_actions_.push_back({x, y, w, 26.0, action_id, arg, key});
}

void BarApp::render_control_center_preview(cairo_t* cr, int w, int h) {
    config::ensure_default_config();
    cfg_ = config::load_config();
    palette_ = themes::resolve_palette(cfg_);
    backdrop_.init(cfg_);
    backdrop_.load_wallpaper(palette_.wallpaper_path, 1280, 800, palette_.blur_radius);
    cached_wifi_ = {true, true, "Tahoe-5G", {}};
    cached_bt_ = {true, true, "AirPods Pro", {}};
    cached_brightness_ = {true, 80};
    cached_audio_ = {68, false, {}};
    cached_media_ = {false, false, ""};
    pop_actions_.clear();
    open_popover_ = PopoverKind::ControlCenter;
    popover_w_ = 248;
    popover_h_ = 408;

    if (!mizzle_.is_available() && mizzle_.init(nullptr)) {
        if (backdrop_.raw_wallpaper()) mizzle_.set_backdrop_cairo(backdrop_.raw_wallpaper());
    }
    if (mizzle_.is_available()) {
        cairo_surface_t* ui = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, std::max(1, w), std::max(1, h));
        cairo_t* ucr = cairo_create(ui);
        cairo_translate(ucr, static_cast<double>(kPopShadowMargin), static_cast<double>(kPopShadowMargin));
        draw_control_center(ucr, 248);
        cairo_destroy(ucr);

        cairo_surface_t* out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, std::max(1, w), std::max(1, h));
        if (mizzle_.render_to_cairo(out, 964.0f, 26.0f, 0.0f, {}, current_glass_material(),
                                    collect_popover_panels(w, h, 1), ui, cfg_.glass_preset != "solid")) {
            cairo_set_source_surface(cr, out, 0.0, 0.0);
            cairo_paint(cr);
            cairo_surface_destroy(out);
            cairo_surface_destroy(ui);
            open_popover_ = PopoverKind::None;
            return;
        }
        cairo_surface_destroy(out);
        cairo_surface_destroy(ui);
    }

    cairo_save(cr);
    cairo_translate(cr, static_cast<double>(kPopShadowMargin), static_cast<double>(kPopShadowMargin));
    draw_control_center(cr, w - kPopShadowMargin * 2);
    cairo_restore(cr);
    open_popover_ = PopoverKind::None;
}

void BarApp::draw_popover_foreground(cairo_t* cr, int pw) {
    switch (open_popover_) {
        case PopoverKind::LogoMenu:      draw_logo_menu(cr, pw); break;
        case PopoverKind::AppMenu:       draw_app_menu(cr, pw); break;
        case PopoverKind::Wifi:          draw_wifi_popover(cr, pw); break;
        case PopoverKind::Bluetooth:     draw_bluetooth_popover(cr, pw); break;
        case PopoverKind::Volume:        draw_volume_popover(cr, pw); break;
        case PopoverKind::Brightness:    draw_brightness_popover(cr, pw); break;
        case PopoverKind::Battery:       draw_battery_popover(cr, pw); break;
        case PopoverKind::ControlCenter: draw_control_center(cr, pw); break;
        case PopoverKind::Spotlight:     draw_spotlight_popover(cr, pw); break;
        case PopoverKind::ShaderTuner:   draw_shader_tuner_popover(cr, pw); break;
        case PopoverKind::Calendar:      draw_calendar_popover(cr, pw); break;
        case PopoverKind::PowerMenu:     draw_power_popover(cr, pw); break;
        case PopoverKind::None:          break;
    }
}

void BarApp::draw_popover(cairo_t* cr, int total_w, int total_h) {
    pop_actions_.clear();
    const double m = static_cast<double>(kPopShadowMargin);
    const double w = std::max(1.0, static_cast<double>(total_w) - 2.0 * m);
    const double h = std::max(1.0, static_cast<double>(total_h) - 2.0 * m);

    if (mizzle_.is_available()) {
        cairo_surface_t* ui = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, std::max(1, total_w), std::max(1, total_h));
        cairo_t* ucr = cairo_create(ui);
        cairo_translate(ucr, m, m);
        draw_popover_foreground(ucr, static_cast<int>(w));
        cairo_destroy(ucr);

        cairo_surface_t* out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, std::max(1, total_w), std::max(1, total_h));
        if (mizzle_.render_to_cairo(out, static_cast<float>(popover_left_m_), static_cast<float>(popover_top_m_),
                                    0.0f, pop_click_, current_glass_material(),
                                    collect_popover_panels(total_w, total_h, 1), ui)) {
            cairo_set_source_surface(cr, out, 0.0, 0.0);
            cairo_paint(cr);
            cairo_surface_destroy(out);
            cairo_surface_destroy(ui);
            return;
        }
        cairo_surface_destroy(out);
        cairo_surface_destroy(ui);
    }

    if (open_popover_ == PopoverKind::ControlCenter) {
        // Fallback path when GPU is unavailable: draw floating pills without an outer box
        cairo_save(cr);
        cairo_translate(cr, m, m);
        draw_popover_foreground(cr, static_cast<int>(w));
        cairo_restore(cr);
        return;
    }

    const double pop_r = (open_popover_ == PopoverKind::Spotlight) ? 20.0 : 18.0;
    const bool overlay_only = !palette_.solid_mode;

    if (!pop_glass_cache_ || pop_cache_w_ != total_w || pop_cache_h_ != total_h || pop_cache_gen_ != cfg_gen_) {
        if (pop_glass_cache_) cairo_surface_destroy(pop_glass_cache_);
        pop_glass_cache_ = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, std::max(1, total_w), std::max(1, total_h));
        cairo_t* pcr = cairo_create(pop_glass_cache_);
        cairo_set_operator(pcr, CAIRO_OPERATOR_CLEAR);
        cairo_paint(pcr);
        cairo_set_operator(pcr, CAIRO_OPERATOR_OVER);

        // Clip OUT the glass interior so drop shadow never darkens translucent Liquid Glass
        cairo_save(pcr);
        cairo_new_path(pcr);
        cairo_rectangle(pcr, 0.0, 0.0, static_cast<double>(total_w), static_cast<double>(total_h));
        glass::append_rounded_rect(pcr, m, m, w, h, pop_r);
        cairo_set_fill_rule(pcr, CAIRO_FILL_RULE_EVEN_ODD);
        cairo_clip(pcr);
        window::draw_window_shadow(pcr, m, m, w, h, pop_r, 16, true);
        cairo_restore(pcr);

        cairo_save(pcr);
        cairo_translate(pcr, m, m);
        glass::draw_glass_panel(pcr, &backdrop_, 0.0, 0.0, popover_anchor_x_,
                                static_cast<double>(cfg_.bar_height + 10),
                                w, h, pop_r, palette_, &palette_.panel_bg, overlay_only);
        cairo_restore(pcr);
        cairo_destroy(pcr);

        pop_cache_w_ = total_w;
        pop_cache_h_ = total_h;
        pop_cache_gen_ = cfg_gen_;
    }

    cairo_save(cr);
    cairo_set_source_surface(cr, pop_glass_cache_, 0.0, 0.0);
    cairo_paint(cr);
    cairo_restore(cr);

    cairo_save(cr);
    cairo_translate(cr, m, m);
    draw_popover_foreground(cr, static_cast<int>(w));
    cairo_restore(cr);
}

void BarApp::draw_logo_menu(cairo_t* cr, int w) {
    const double row_w = static_cast<double>(w) - 16.0;
    double y = 10.0;
    draw_popover_row(cr, 8.0, y, row_w, "About Mistbar", "", "settings", "about", true); y += 28.0;
    draw_popover_row(cr, 8.0, y, row_w, "Mistbar Settings...", "", "settings", "appearance"); y += 30.0;
    draw_separator(cr, 12.0, y, static_cast<double>(w) - 24.0); y += 7.0;
    draw_popover_row(cr, 8.0, y, row_w, "Sleep", "", "exec", "systemctl suspend"); y += 28.0;
    draw_popover_row(cr, 8.0, y, row_w, "Restart...", "", "exec", "systemctl reboot"); y += 28.0;
    draw_popover_row(cr, 8.0, y, row_w, "Shut Down...", "", "exec", "systemctl poweroff"); y += 30.0;
    draw_separator(cr, 12.0, y, static_cast<double>(w) - 24.0); y += 7.0;
    draw_popover_row(cr, 8.0, y, row_w, "Lock Screen", "", "exec", "loginctl lock-session 2>/dev/null || niri msg action power-off-monitors"); y += 28.0;
    draw_popover_row(cr, 8.0, y, row_w, "Log Out...", "", "exec", "niri msg action quit -s 2>/dev/null || loginctl terminate-session ${XDG_SESSION_ID:-self}");
}

void BarApp::draw_app_menu(cairo_t* cr, int w) {
    const double row_w = static_cast<double>(w) - 16.0;
    double y = 10.0;
    if (active_menu_name_ == "File") {
        draw_popover_row(cr, 8.0, y, row_w, "New Window", "Ctrl+N", "exec", "wtype -M ctrl -k n -m ctrl 2>/dev/null || true"); y += 28.0;
        draw_popover_row(cr, 8.0, y, row_w, "Close Window", "Ctrl+W", "niri", "close-window"); y += 30.0;
        draw_separator(cr, 12.0, y, static_cast<double>(w) - 24.0); y += 7.0;
        draw_popover_row(cr, 8.0, y, row_w, "Quit", "Ctrl+Q", "niri", "close-window");
    } else if (active_menu_name_ == "Edit") {
        draw_popover_row(cr, 8.0, y, row_w, "Undo", "Ctrl+Z", "exec", "wtype -M ctrl -k z -m ctrl 2>/dev/null || true"); y += 28.0;
        draw_popover_row(cr, 8.0, y, row_w, "Redo", "Ctrl+Shift+Z", "exec", "wtype -M ctrl -M shift -k z -m shift -m ctrl 2>/dev/null || true"); y += 30.0;
        draw_separator(cr, 12.0, y, static_cast<double>(w) - 24.0); y += 7.0;
        draw_popover_row(cr, 8.0, y, row_w, "Cut", "Ctrl+X", "exec", "wtype -M ctrl -k x -m ctrl 2>/dev/null || true"); y += 28.0;
        draw_popover_row(cr, 8.0, y, row_w, "Copy", "Ctrl+C", "exec", "wtype -M ctrl -k c -m ctrl 2>/dev/null || true"); y += 28.0;
        draw_popover_row(cr, 8.0, y, row_w, "Paste", "Ctrl+V", "exec", "wtype -M ctrl -k v -m ctrl 2>/dev/null || true"); y += 28.0;
        draw_popover_row(cr, 8.0, y, row_w, "Select All", "Ctrl+A", "exec", "wtype -M ctrl -k a -m ctrl 2>/dev/null || true");
    } else if (active_menu_name_ == "View") {
        draw_popover_row(cr, 8.0, y, row_w, "Toggle Fullscreen", "F11", "niri", "fullscreen-window"); y += 28.0;
        draw_popover_row(cr, 8.0, y, row_w, "Center Column", "", "niri", "center-column"); y += 28.0;
        draw_popover_row(cr, 8.0, y, row_w, "Expand Column", "", "niri", "expand-column-to-available-width");
    } else if (active_menu_name_ == "Go") {
        draw_popover_row(cr, 8.0, y, row_w, "Home", "", "exec", "xdg-open ~"); y += 28.0;
        draw_popover_row(cr, 8.0, y, row_w, "Documents", "", "exec", "xdg-open \"$(xdg-user-dir DOCUMENTS 2>/dev/null || echo $HOME/Documents)\""); y += 28.0;
        draw_popover_row(cr, 8.0, y, row_w, "Downloads", "", "exec", "xdg-open \"$(xdg-user-dir DOWNLOAD 2>/dev/null || echo $HOME/Downloads)\""); y += 28.0;
        draw_popover_row(cr, 8.0, y, row_w, "Pictures", "", "exec", "xdg-open \"$(xdg-user-dir PICTURES 2>/dev/null || echo $HOME/Pictures)\""); y += 30.0;
        draw_separator(cr, 12.0, y, static_cast<double>(w) - 24.0); y += 7.0;
        draw_popover_row(cr, 8.0, y, row_w, "Applications", "", "open_spotlight", ""); y += 28.0;
        draw_popover_row(cr, 8.0, y, row_w, "Terminal", "", "exec", "alacritty 2>/dev/null || kitty 2>/dev/null || foot 2>/dev/null || x-terminal-emulator"); y += 28.0;
        draw_popover_row(cr, 8.0, y, row_w, "Computer", "", "exec", "xdg-open /");
    } else if (active_menu_name_ == "Window") {
        draw_popover_row(cr, 8.0, y, row_w, "Next Workspace", "", "niri", "focus-workspace-down"); y += 28.0;
        draw_popover_row(cr, 8.0, y, row_w, "Previous Workspace", "", "niri", "focus-workspace-up"); y += 30.0;
        draw_separator(cr, 12.0, y, static_cast<double>(w) - 24.0); y += 7.0;
        draw_popover_row(cr, 8.0, y, row_w, "Move to Next Workspace", "", "niri", "move-column-to-workspace-down"); y += 28.0;
        draw_popover_row(cr, 8.0, y, row_w, "Move to Prev Workspace", "", "niri", "move-column-to-workspace-up");
    } else {
        draw_popover_row(cr, 8.0, y, row_w, "Spotlight Search...", "", "open_spotlight", ""); y += 28.0;
        draw_popover_row(cr, 8.0, y, row_w, "Shader Tuner...", "", "open_tuner", ""); y += 28.0;
        draw_popover_row(cr, 8.0, y, row_w, "Mistbar Settings...", "", "settings", "appearance");
    }
}

void BarApp::draw_wifi_popover(cairo_t* cr, int w) {
    const std::uint32_t fg_tint = palette_.dark_mode ? 0xFFF5F7FAU : 0xFF1A1C20U;
    window::draw_pango_text_centered(cr, "Wi-Fi", 14.0, 10.0, 120.0, 24.0, 10.2, true, palette_.text_primary);
    const std::string ref_icon = icons::resolve_icon("refresh.svg");
    if (!ref_icon.empty()) icons_.draw(cr, ref_icon, static_cast<double>(w) - 82.0, 14.0, 15.0, 15.0, fg_tint);
    pop_actions_.push_back({static_cast<double>(w) - 88.0, 8.0, 26.0, 26.0, "wifi_rescan", ""});
    draw_toggle_pill(cr, static_cast<double>(w) - 50.0, 12.0, cached_wifi_.enabled);
    pop_actions_.push_back({static_cast<double>(w) - 52.0, 10.0, 40.0, 24.0, "wifi_toggle", ""});

    draw_separator(cr, 12.0, 42.0, static_cast<double>(w) - 24.0);
    window::draw_pango_text_centered(cr, cached_wifi_.connected ? ("Connected: " + cached_wifi_.ssid) : "Not Connected",
                                     14.0, 48.0, static_cast<double>(w) - 104.0, 26.0, 8.8, false, palette_.text_secondary);
    if (cached_wifi_.connected) {
        draw_popover_row(cr, static_cast<double>(w) - 92.0, 48.0, 80.0, "Disconnect", "", "wifi_disconnect", cached_wifi_.ssid);
    }

    draw_separator(cr, 12.0, 80.0, static_cast<double>(w) - 24.0);
    window::draw_pango_text_centered(cr, "Nearby Networks", 14.0, 84.0, 180.0, 22.0, 8.4, true, palette_.text_secondary);

    double y = 110.0;
    if (cached_wifi_.networks.empty()) {
        window::draw_pango_text_centered(cr, "No networks found", 14.0, y, 200.0, 24.0, 8.8, false, palette_.text_secondary);
    } else {
        const std::size_t max_n = std::min<std::size_t>(cached_wifi_.networks.size(), 6);
        for (std::size_t i = 0; i < max_n; ++i) {
            const auto& net = cached_wifi_.networks[i];
            draw_popover_row(cr, 8.0, y, static_cast<double>(w) - 16.0, net.ssid,
                             net.in_use ? "Connected" : (std::to_string(net.signal) + "%"),
                             "wifi_connect", net.ssid, net.in_use, net.in_use);
            y += 32.0;
        }
    }

    draw_separator(cr, 12.0, static_cast<double>(popover_h_) - 40.0, static_cast<double>(w) - 24.0);
    draw_popover_row(cr, 8.0, static_cast<double>(popover_h_) - 34.0, static_cast<double>(w) - 16.0,
                     "Wi-Fi Settings...", "", "settings", "wifi");
}

void BarApp::draw_bluetooth_popover(cairo_t* cr, int w) {
    window::draw_pango_text_centered(cr, "Bluetooth", 14.0, 10.0, 140.0, 24.0, 10.2, true, palette_.text_primary);
    draw_toggle_pill(cr, static_cast<double>(w) - 50.0, 12.0, cached_bt_.powered);
    pop_actions_.push_back({static_cast<double>(w) - 52.0, 10.0, 40.0, 24.0, "bt_toggle", ""});

    draw_separator(cr, 12.0, 42.0, static_cast<double>(w) - 24.0);
    window::draw_pango_text_centered(cr, cached_bt_.powered
                                             ? (cached_bt_.connected_name.empty() ? "On (No active device)" : cached_bt_.connected_name)
                                             : "Bluetooth Off",
                                     14.0, 48.0, static_cast<double>(w) - 28.0, 24.0, 8.8, false, palette_.text_secondary);

    draw_separator(cr, 12.0, 78.0, static_cast<double>(w) - 24.0);
    window::draw_pango_text_centered(cr, "Paired Devices", 14.0, 82.0, 180.0, 22.0, 8.4, true, palette_.text_secondary);

    double y = 108.0;
    if (cached_bt_.devices.empty()) {
        window::draw_pango_text_centered(cr, "No paired devices", 14.0, y, 200.0, 24.0, 8.8, false, palette_.text_secondary);
    } else {
        for (const auto& dev : cached_bt_.devices) {
            draw_popover_row(cr, 8.0, y, static_cast<double>(w) - 16.0, dev.name,
                             dev.connected ? "Connected" : "Connect",
                             dev.connected ? "bt_disconnect" : "bt_connect", dev.mac,
                             dev.connected, dev.connected);
            y += 32.0;
        }
    }

    draw_separator(cr, 12.0, static_cast<double>(popover_h_) - 40.0, static_cast<double>(w) - 24.0);
    draw_popover_row(cr, 8.0, static_cast<double>(popover_h_) - 34.0, static_cast<double>(w) - 16.0,
                     "Bluetooth Settings...", "", "settings", "bluetooth");
}

void BarApp::draw_volume_popover(cairo_t* cr, int w) {
    window::draw_pango_text_centered(cr, "Sound", 14.0, 10.0, 120.0, 24.0, 10.2, true, palette_.text_primary);
    draw_popover_row(cr, static_cast<double>(w) - 86.0, 10.0, 76.0,
                     cached_audio_.muted ? "Unmute" : "Mute", "", "mute_toggle", "");

    draw_capsule_slider(cr, 14.0, 44.0, static_cast<double>(w) - 28.0, 22.0,
                        cached_audio_.volume_percent, cached_audio_.muted ? "volume-mute.svg" : "volume.svg");
    draw_separator(cr, 12.0, 78.0, static_cast<double>(w) - 24.0);
    window::draw_pango_text_centered(cr, "Output Devices", 14.0, 82.0, 180.0, 22.0, 8.4, true, palette_.text_secondary);

    double y = 108.0;
    if (cached_audio_.sinks.empty()) {
        window::draw_pango_text_centered(cr, "Default Audio Output", 14.0, y, 220.0, 24.0, 8.8, false, palette_.text_secondary);
    } else {
        for (const auto& s : cached_audio_.sinks) {
            draw_popover_row(cr, 8.0, y, static_cast<double>(w) - 16.0, s.name,
                             s.is_default ? "Active" : "", "sink_set", s.id, s.is_default, s.is_default);
            y += 32.0;
        }
    }
}

void BarApp::draw_brightness_popover(cairo_t* cr, int w) {
    window::draw_pango_text_centered(cr, "Display", 14.0, 10.0, 140.0, 24.0, 10.2, true, palette_.text_primary);
    window::draw_pango_text_centered(cr, std::to_string(cached_brightness_.percent) + "%",
                                     static_cast<double>(w) - 68.0, 10.0, 54.0, 24.0, 9.0, false,
                                     palette_.text_secondary, false, true);
    draw_capsule_slider(cr, 14.0, 46.0, static_cast<double>(w) - 28.0, 22.0,
                        cached_brightness_.percent, "sun.svg");
    draw_separator(cr, 12.0, 82.0, static_cast<double>(w) - 24.0);

    const int presets[4] = {25, 50, 75, 100};
    const double btn_w = (static_cast<double>(w) - 46.0) * 0.25;
    for (int i = 0; i < 4; ++i) {
        const double bx = 14.0 + static_cast<double>(i) * (btn_w + 6.0);
        glass::append_rounded_rect(cr, bx, 96.0, btn_w, 28.0, 8.0);
        cairo_set_source_rgba(cr, 0.5, 0.52, 0.58, 0.25);
        cairo_fill(cr);
        window::draw_pango_text_centered(cr, std::to_string(presets[i]) + "%", bx, 96.0, btn_w, 28.0,
                                         8.6, true, palette_.text_primary, true, false);
        pop_actions_.push_back({bx, 96.0, btn_w, 28.0, "bright_set", std::to_string(presets[i])});
    }
}

void BarApp::draw_battery_popover(cairo_t* cr, int w) {
    window::draw_pango_text_centered(cr, "Battery", 14.0, 10.0, 140.0, 24.0, 10.2, true, palette_.text_primary);
    window::draw_pango_text_centered(cr, std::to_string(cached_battery_.percent) + "%",
                                     static_cast<double>(w) - 68.0, 10.0, 54.0, 24.0, 9.5, true,
                                     palette_.text_primary, false, true);
    window::draw_pango_text_centered(cr, "State: " + cached_battery_.state_text,
                                     14.0, 38.0, static_cast<double>(w) - 28.0, 22.0, 8.8, false, palette_.text_secondary);
    draw_separator(cr, 12.0, 70.0, static_cast<double>(w) - 24.0);
    window::draw_pango_text_centered(cr, "Power Mode", 14.0, 76.0, 160.0, 22.0, 8.6, true, palette_.text_secondary);

    const char* profiles[3] = {"power-saver", "balanced", "performance"};
    const char* labels[3]   = {"Saver", "Balanced", "Perf"};
    const double bw = (static_cast<double>(w) - 40.0) / 3.0;
    for (int i = 0; i < 3; ++i) {
        const double bx = 14.0 + static_cast<double>(i) * (bw + 6.0);
        const bool act = (cached_battery_.power_profile == profiles[i]);
        glass::append_rounded_rect(cr, bx, 108.0, bw, 32.0, 8.0);
        if (act) cairo_set_source_rgba(cr, palette_.accent.r, palette_.accent.g, palette_.accent.b, 0.92);
        else cairo_set_source_rgba(cr, 0.5, 0.52, 0.58, 0.25);
        cairo_fill(cr);
        window::draw_pango_text_centered(cr, labels[i], bx, 108.0, bw, 32.0, 8.6, true,
                                         act ? themes::Color{1, 1, 1, 1} : palette_.text_primary, true, false);
        pop_actions_.push_back({bx, 108.0, bw, 32.0, "power_profile", profiles[i]});
    }
}

// Slim mizzle Control Center (248x408) with exact Mizzle Cairo vector glyphs & Liquid Glass pills
void BarApp::draw_control_center(cairo_t* cr, int w) {
    (void)w;
    const bool dark_ink = (cfg_.glassy_text_color == "dark");
    const themes::Color ink = dark_ink
        ? themes::Color{0.08, 0.10, 0.14, 0.94}
        : themes::Color{0.98, 0.99, 1.00, 0.96};
    const themes::Color sub = dark_ink
        ? themes::Color{0.20, 0.24, 0.30, 0.72}
        : themes::Color{0.84, 0.89, 0.96, 0.72};
    const bool use_gpu_glass = mizzle_.is_available();

    auto draw_fallback_pill = [&](double x, double y, double cw, double ch, double r,
                                  const std::string& hkey, bool active) {
        const bool hov = (!hkey.empty() && hovered_pop_id_ == hkey);
        if (use_gpu_glass) {
            if (hov) {
                glass::append_rounded_rect(cr, x, y, cw, ch, r);
                cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.08);
                cairo_fill(cr);
            }
            return;
        }
        glass::append_rounded_rect(cr, x, y, cw, ch, r);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, active ? (hov ? 0.16 : 0.11) : (hov ? 0.12 : 0.06));
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, hov ? 0.28 : 0.15);
        cairo_set_line_width(cr, 0.9);
        cairo_stroke(cr);
    };

    // 1. Top-left Wi-Fi slim pill (0, 0, 120x44) — exact Mizzle ring + vector Wi-Fi glyph
    draw_fallback_pill(0.0, 0.0, 120.0, 44.0, 22.0, "cc:wifi", cached_wifi_.enabled);
    cairo_save(cr);
    cairo_new_path(cr);
    cairo_arc(cr, 22.0, 22.0, 14.0, 0.0, 6.283185);
    cairo_set_source_rgba(cr, ink.r, ink.g, ink.b, cached_wifi_.enabled ? 0.26 : 0.10);
    cairo_fill(cr);
    cairo_restore(cr);
    draw_cc_wifi_glyph(cr, 22.0, 22.0, 14.0,
                       cached_wifi_.enabled ? themes::Color{0.35, 0.72, 1.0, 1.0} : ink);
    window::draw_pango_text_centered(cr, "Wi-Fi", 42.0, 8.0, 70.0, 15.0, 8.0, true, ink);
    window::draw_pango_text_centered(cr,
                                     cached_wifi_.connected ? cached_wifi_.ssid : (cached_wifi_.enabled ? "On" : "Off"),
                                     42.0, 22.0, 70.0, 14.0, 7.0, false, sub);
    pop_actions_.push_back({0.0, 0.0, 120.0, 44.0, "wifi_toggle", "", "cc:wifi"});

    // 2. Bluetooth + AirDrop circle droplets (56x44 each) — exact Mizzle vector glyphs
    draw_fallback_pill(0.0, 52.0, 56.0, 44.0, 22.0, "cc:bt", cached_bt_.powered);
    draw_cc_bt_glyph(cr, 28.0, 74.0, 16.0,
                     cached_bt_.powered ? themes::Color{0.38, 0.74, 1.0, 1.0} : ink);
    pop_actions_.push_back({0.0, 52.0, 56.0, 44.0, "bt_toggle", "", "cc:bt"});

    draw_fallback_pill(64.0, 52.0, 56.0, 44.0, 22.0, "cc:air", cfg_.cc_airdrop_on);
    draw_vector_airdrop(cr, 92.0, 74.0, 16.0,
                        cfg_.cc_airdrop_on ? themes::Color{0.38, 0.74, 1.0, 1.0} : ink);
    pop_actions_.push_back({64.0, 52.0, 56.0, 44.0, "cc_airdrop", "", "cc:air"});

    // 3. Top-right Media card (128, 0, 120x96) — exact Mizzle tile + vector transport controls
    draw_fallback_pill(128.0, 0.0, 120.0, 96.0, 20.0, "cc:media", cached_media_.playing);
    glass::append_rounded_rect(cr, 140.0, 10.0, 34.0, 34.0, 8.0);
    if (cached_media_.playing) {
        cairo_pattern_t* pat = cairo_pattern_create_linear(140.0, 10.0, 174.0, 44.0);
        cairo_pattern_add_color_stop_rgba(pat, 0.0, 0.25, 0.65, 1.0, 0.95);
        cairo_pattern_add_color_stop_rgba(pat, 1.0, 0.68, 0.28, 0.95, 0.95);
        cairo_set_source(cr, pat);
        cairo_fill(cr);
        cairo_pattern_destroy(pat);
    } else {
        cairo_set_source_rgba(cr, ink.r, ink.g, ink.b, 0.14);
        cairo_fill(cr);
    }
    window::draw_pango_text_centered(cr, cached_media_.active ? cached_media_.title : "Not Playing",
                                     140.0, 49.0, 96.0, 16.0, 8.0, true, ink);

    cairo_save(cr);
    cairo_new_path(cr);
    cairo_set_source_rgba(cr, ink.r, ink.g, ink.b, 0.88);
    const double ty = 78.0;
    cairo_move_to(cr, 158.0, ty - 5.0); cairo_line_to(cr, 150.0, ty); cairo_line_to(cr, 158.0, ty + 5.0);
    cairo_close_path(cr); cairo_fill(cr);
    if (cached_media_.playing) {
        cairo_rectangle(cr, 182.0, ty - 5.5, 3.5, 11.0);
        cairo_rectangle(cr, 189.0, ty - 5.5, 3.5, 11.0);
        cairo_fill(cr);
    } else {
        cairo_move_to(cr, 183.0, ty - 6.0); cairo_line_to(cr, 194.0, ty); cairo_line_to(cr, 183.0, ty + 6.0);
        cairo_close_path(cr); cairo_fill(cr);
    }
    cairo_move_to(cr, 216.0, ty - 5.0); cairo_line_to(cr, 224.0, ty); cairo_line_to(cr, 216.0, ty + 5.0);
    cairo_close_path(cr); cairo_fill(cr);
    cairo_restore(cr);

    pop_actions_.push_back({136.0, 66.0, 32.0, 26.0, "exec_keep", "playerctl previous", "cc:m0"});
    pop_actions_.push_back({172.0, 66.0, 32.0, 26.0, "exec_keep", "playerctl play-pause", "cc:m1"});
    pop_actions_.push_back({208.0, 66.0, 32.0, 26.0, "exec_keep", "playerctl next", "cc:m2"});

    // 4. Focus pill + Workspaces + Bar Look circles (y = 104)
    draw_fallback_pill(0.0, 104.0, 120.0, 44.0, 22.0, "cc:focus", cfg_.cc_focus_on);
    draw_cc_moon_glyph(cr, 22.0, 126.0, 14.0,
                       cfg_.cc_focus_on ? themes::Color{0.62, 0.50, 1.0, 1.0} : ink);
    window::draw_pango_text_centered(cr, "Focus", 42.0, 104.0, 70.0, 44.0, 8.0, true, ink);
    pop_actions_.push_back({0.0, 104.0, 120.0, 44.0, "cc_focus", "", "cc:focus"});

    draw_fallback_pill(128.0, 104.0, 56.0, 44.0, 22.0, "cc:ws", cfg_.show_workspaces);
    draw_cc_box_icon(cr, 156.0, 126.0, cfg_.show_workspaces, false,
                     cfg_.show_workspaces ? themes::Color{0.38, 0.74, 1.0, 1.0} : ink);
    pop_actions_.push_back({128.0, 104.0, 56.0, 44.0, "ws_toggle", "", "cc:ws"});

    draw_fallback_pill(192.0, 104.0, 56.0, 44.0, 22.0, "cc:look", cfg_.look == "pill");
    draw_cc_box_icon(cr, 220.0, 126.0, cfg_.look == "pill", true,
                     (cfg_.look == "pill") ? themes::Color{0.38, 0.74, 1.0, 1.0} : ink);
    pop_actions_.push_back({192.0, 104.0, 56.0, 44.0, "look_toggle", "", "cc:look"});

    // 5. Slim mizzle Sound & Display Slider Pills (248x50)
    auto draw_cc_slider = [&](double sy, const char* title, int pct, bool is_sound) {
        draw_fallback_pill(0.0, sy, 248.0, 50.0, 22.0, "", false);
        window::draw_pango_text_centered(cr, title, 14.0, sy + 6.0, 100.0, 16.0, 8.0, true, ink);

        const double track_x = 30.0;
        const double track_y = sy + 31.0;
        const double track_w = is_sound ? 166.0 : 190.0;

        if (is_sound) {
            draw_cc_speaker_glyph(cr, 16.0, track_y + 1.5, 11.0, ink, false);
            draw_cc_speaker_glyph(cr, 210.0, track_y + 1.5, 11.0, ink, true);
            cairo_save(cr);
            cairo_new_path(cr);
            cairo_arc(cr, 231.0, track_y + 1.5, 9.5, 0.0, 6.283185);
            cairo_set_source_rgba(cr, ink.r, ink.g, ink.b, 0.14);
            cairo_fill(cr);
            cairo_restore(cr);
            draw_vector_airdrop(cr, 231.0, track_y + 1.5, 10.0, ink);
            pop_actions_.push_back({219.0, sy + 16.0, 26.0, 28.0, "open_vol", "", "cc:volbtn"});
        } else {
            draw_cc_sun_glyph(cr, 16.0, track_y + 1.5, 11.0, sub);
            draw_cc_sun_glyph(cr, 232.0, track_y + 1.5, 13.0, ink);
        }

        glass::append_rounded_rect(cr, track_x, track_y, track_w, 3.0, 1.5);
        cairo_set_source_rgba(cr, 0.04, 0.06, 0.10, 0.45);
        cairo_fill(cr);

        const double frac = std::clamp(static_cast<double>(pct) / 100.0, 0.0, 1.0);
        const double fill_w = std::max(3.0, track_w * frac);
        glass::append_rounded_rect(cr, track_x, track_y, fill_w, 3.0, 1.5);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.94);
        cairo_fill(cr);

        cairo_save(cr);
        cairo_new_path(cr);
        cairo_arc(cr, track_x + fill_w, track_y + 1.5, 6.5, 0.0, 6.283185);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.98);
        cairo_fill(cr);
        cairo_restore(cr);
    };

    draw_cc_slider(156.0, "Sound", cached_audio_.muted ? 0 : cached_audio_.volume_percent, true);
    draw_cc_slider(214.0, "Display", cached_brightness_.percent, false);

    // 6. 4x2 circular quick-action droplets (56x44 each, pure vector geometry)
    const char* q_actions[8] = {
        "preset_cycle", "autohide_toggle", "clock_toggle", "iconstyle_cycle",
        "open_wifi", "open_bt", "open_vol", "open_tuner"
    };
    const bool q_active[8] = {
        cfg_.glass_preset != "clear",
        cfg_.auto_hide,
        cfg_.clock_format == "24h",
        cfg_.icon_style != "default",
        false, false, false, false
    };
    for (int i = 0; i < 8; ++i) {
        const int col = i % 4;
        const int row = i / 4;
        const double qx = static_cast<double>(col) * 64.0;
        const double qy = 272.0 + static_cast<double>(row) * 52.0;
        const std::string hk = "cc:q" + std::to_string(i);
        draw_fallback_pill(qx, qy, 56.0, 44.0, 22.0, hk, q_active[i]);
        draw_vector_quick_glyph(cr, i, qx + 28.0, qy + 22.0,
                                q_active[i] ? themes::Color{0.38, 0.74, 1.0, 1.0} : ink);
        pop_actions_.push_back({qx, qy, 56.0, 44.0, q_actions[i], "", hk});
    }

    // 7. Edit Controls pill -> opens Live Shader Lab
    draw_fallback_pill(76.0, 380.0, 96.0, 24.0, 12.0, "cc:edit", false);
    window::draw_pango_text_centered(cr, "Edit Controls", 76.0, 380.0, 96.0, 24.0, 7.5, true,
                                     ink, true, false);
    pop_actions_.push_back({76.0, 380.0, 96.0, 24.0, "open_tuner", "", "cc:edit"});
}

void BarApp::draw_spotlight_popover(cairo_t* cr, int w) {
    const double pw = static_cast<double>(w);
    const std::string srch = icons::resolve_icon("search.svg");
    if (!srch.empty()) {
        icons_.draw(cr, srch, 18.0, 17.0, 18.0, 18.0, 0xFF9EA4B0U);
    }
    const std::string display = spotlight_query_.empty()
        ? "Spotlight Search..."
        : (spotlight_query_ + "|");
    window::draw_pango_text_centered(cr, display, 46.0, 8.0, pw - 64.0, 36.0, 11.4, false,
                                     spotlight_query_.empty() ? palette_.text_secondary : palette_.text_primary);

    draw_separator(cr, 14.0, 50.0, pw - 28.0);

    double y = 58.0;
    if (spotlight_matches_.empty()) {
        window::draw_pango_text_centered(cr, "No matching apps or actions", 20.0, y, pw - 40.0, 32.0, 9.0, false, palette_.text_secondary);
        return;
    }

    for (int i = 0; i < static_cast<int>(spotlight_matches_.size()); ++i) {
        const auto& m = spotlight_matches_[static_cast<std::size_t>(i)];
        const std::string hk = "spot:" + std::to_string(i);
        const bool active = (i == spotlight_selected_) || (hovered_pop_id_ == hk);
        if (active) {
            glass::append_rounded_rect(cr, 10.0, y, pw - 20.0, 34.0, 10.0);
            cairo_set_source_rgba(cr, palette_.accent.r, palette_.accent.g, palette_.accent.b, 0.30);
            cairo_fill(cr);
        }
        window::draw_pango_text_centered(cr, m.title, 22.0, y, pw - 170.0, 34.0, 9.4, active, palette_.text_primary);
        window::draw_pango_text_centered(cr, m.subtitle, pw - 150.0, y, 130.0, 34.0, 8.0, false,
                                         palette_.text_secondary, false, true);
        pop_actions_.push_back({10.0, y, pw - 20.0, 34.0, m.action_id, m.arg, hk});
        y += 38.0;
    }
}

void BarApp::draw_shader_tuner_popover(cairo_t* cr, int w) {
    const double pw = static_cast<double>(w);
    const themes::Color ink = (cfg_.glassy_text_color == "dark")
        ? themes::Color{0.08, 0.10, 0.14, 0.94}
        : themes::Color{0.98, 0.99, 1.00, 0.96};
    const themes::Color sub = (cfg_.glassy_text_color == "dark")
        ? themes::Color{0.20, 0.24, 0.30, 0.72}
        : themes::Color{0.84, 0.89, 0.96, 0.76};

    window::draw_pango_text_centered(cr, "Liquid Glass Shader Lab", 16.0, 12.0, pw - 32.0, 18.0,
                                     9.5, true, ink);
    window::draw_pango_text_centered(cr, "Drag sliders to tune bar and Control Center glass live",
                                     16.0, 29.0, pw - 32.0, 15.0, 7.0, false, sub);

    const glass::GlassMaterial mat = current_glass_material();
    struct TunerRow {
        const char* name;
        double norm;
        std::string val;
    };
    auto fmt_f = [](float v) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%.2f", v);
        return std::string(buf);
    };
    const TunerRow rows[5] = {
        {"Refraction", static_cast<double>(cfg_.refraction_strength), fmt_f(mat.refraction)},
        {"Dispersion", static_cast<double>(cfg_.chromatic_aberration), fmt_f(mat.dispersion)},
        {"Frost / Blur", static_cast<double>(cfg_.blur_radius) / 48.0, fmt_f(mat.frost)},
        {"Rim Shine", static_cast<double>(cfg_.specular_intensity), fmt_f(mat.specular)},
        {"Tint Opacity", static_cast<double>(cfg_.tint_opacity) / 0.45, fmt_f(mat.tintAlpha)}
    };

    for (int i = 0; i < 5; ++i) {
        const double sy = 50.0 + static_cast<double>(i) * 36.0;
        const double sx = 16.0;
        const double sw = pw - 32.0;
        window::draw_pango_text_centered(cr, rows[i].name, sx, sy - 2.0, 150.0, 16.0, 8.0, false, ink);
        window::draw_pango_text_centered(cr, rows[i].val, sx + sw - 54.0, sy - 2.0, 54.0, 16.0, 7.5, true,
                                         sub, false, true);

        const double ty = sy + 16.0;
        glass::append_rounded_rect(cr, sx, ty, sw, 4.0, 2.0);
        cairo_set_source_rgba(cr, ink.r, ink.g, ink.b, 0.18);
        cairo_fill(cr);

        const double n = std::clamp(rows[i].norm, 0.0, 1.0);
        glass::append_rounded_rect(cr, sx, ty, std::max(4.0, sw * n), 4.0, 2.0);
        cairo_set_source_rgba(cr, 0.28, 0.68, 1.0, 0.94);
        cairo_fill(cr);

        cairo_arc(cr, sx + n * sw, ty + 2.0, 5.5, 0.0, 6.283185);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.98);
        cairo_fill(cr);
    }

    auto draw_tuner_btn = [&](double bx, double by, double bw, const std::string& label,
                              const std::string& aid, const std::string& arg, const std::string& hk) {
        const bool hov = (hovered_pop_id_ == hk);
        glass::append_rounded_rect(cr, bx, by, bw, 22.0, 11.0);
        cairo_set_source_rgba(cr, ink.r, ink.g, ink.b, hov ? 0.22 : 0.14);
        cairo_fill(cr);
        window::draw_pango_text_centered(cr, label, bx, by, bw, 22.0, 7.5, true, ink, true, false);
        pop_actions_.push_back({bx, by, bw, 22.0, aid, arg, hk});
    };

    const double bw = (pw - 42.0) * 0.5;
    draw_tuner_btn(16.0, 242.0, bw, "Preset: " + cfg_.glass_preset, "preset_cycle", "", "tn:preset");
    draw_tuner_btn(26.0 + bw, 242.0, bw, "Look: " + cfg_.look, "look_toggle", "", "tn:look");
    draw_tuner_btn(16.0, 274.0, bw, "Recapture BG", "recapture_bg", "", "tn:recap");
    draw_tuner_btn(26.0 + bw, 274.0, bw, "All Settings...", "settings", "liquid-glass", "tn:settings");
}

void BarApp::draw_calendar_popover(cairo_t* cr, int w) {
    window::draw_pango_text_centered(cr, cached_full_date_, 16.0, 10.0, static_cast<double>(w) - 32.0,
                                     24.0, 9.5, true, palette_.text_primary);
    draw_separator(cr, 12.0, 40.0, static_cast<double>(w) - 24.0);

    const std::time_t now = std::time(nullptr);
    std::tm tm_now{};
    localtime_r(&now, &tm_now);

    std::tm first = tm_now;
    first.tm_mday = 1;
    std::mktime(&first);
    const int start_wd = first.tm_wday;
    const int total_days = days_in_month(tm_now.tm_year + 1900, tm_now.tm_mon);

    const char* wd_labels[7] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
    const double cell_w = (static_cast<double>(w) - 24.0) / 7.0;
    for (int i = 0; i < 7; ++i) {
        window::draw_pango_text_centered(cr, wd_labels[i], 12.0 + static_cast<double>(i) * cell_w, 46.0,
                                         cell_w, 20.0, 8.2, true, palette_.text_secondary, true, false);
    }

    int row = 0;
    int col = start_wd;
    for (int d = 1; d <= total_days; ++d) {
        const double cx = 12.0 + static_cast<double>(col) * cell_w;
        const double cy = 72.0 + static_cast<double>(row) * 28.0;
        const bool is_today = (d == tm_now.tm_mday);
        if (is_today) {
            glass::append_rounded_rect(cr, cx + 4.0, cy + 1.0, cell_w - 8.0, 24.0, 12.0);
            cairo_set_source_rgba(cr, palette_.accent.r, palette_.accent.g, palette_.accent.b, 0.92);
            cairo_fill(cr);
        }
        window::draw_pango_text_centered(cr, std::to_string(d), cx, cy, cell_w, 26.0, 8.8, is_today,
                                         is_today ? themes::Color{1, 1, 1, 1} : palette_.text_primary, true, false);
        ++col;
        if (col >= 7) {
            col = 0;
            ++row;
        }
    }
}

void BarApp::draw_power_popover(cairo_t* cr, int w) {
    const double row_w = static_cast<double>(w) - 16.0;
    double y = 10.0;
    draw_popover_row(cr, 8.0, y, row_w, "Lock Screen", "", "exec", "loginctl lock-session 2>/dev/null || niri msg action power-off-monitors"); y += 28.0;
    draw_popover_row(cr, 8.0, y, row_w, "Sleep", "", "exec", "systemctl suspend"); y += 28.0;
    draw_popover_row(cr, 8.0, y, row_w, "Restart...", "", "exec", "systemctl reboot"); y += 28.0;
    draw_popover_row(cr, 8.0, y, row_w, "Shut Down...", "", "exec", "systemctl poweroff"); y += 30.0;
    draw_separator(cr, 12.0, y, static_cast<double>(w) - 24.0); y += 7.0;
    draw_popover_row(cr, 8.0, y, row_w, "Log Out...", "", "exec", "niri msg action quit -s 2>/dev/null || loginctl terminate-session ${XDG_SESSION_ID:-self}");
}

void BarApp::handle_popover_pointer(const wl::PointerEvent& ev) {
    const double lx = ev.x - static_cast<double>(kPopShadowMargin);
    const double ly = ev.y - static_cast<double>(kPopShadowMargin);

    if (ev.type == wl::PointerEvent::Type::Leave) {
        if (!hovered_pop_id_.empty()) {
            hovered_pop_id_.clear();
            popover_surface_->request_redraw();
        }
        return;
    }

    if (ev.type == wl::PointerEvent::Type::ButtonRelease) {
        if (dragging_brightness_) {
            dragging_brightness_ = false;
            sysinfo::spawn_detached({"brightnessctl", "set", std::to_string(cached_brightness_.percent) + "%"});
        }
        if (dragging_volume_) {
            dragging_volume_ = false;
            sysinfo::spawn_detached({"wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@",
                                     std::to_string(cached_audio_.volume_percent) + "%"});
        }
        if (dragging_tuner_idx_ >= 0) {
            dragging_tuner_idx_ = -1;
            config::save_config(cfg_);
        }
        return;
    }

    auto slider_pct = [](double x, double sx, double sw) -> int {
        return std::clamp(static_cast<int>(((x - sx) / sw) * 100.0 + 0.5), 1, 100);
    };

    auto apply_hw_throttled = [&](bool is_vol, int pct, bool force_now) {
        const auto now = std::chrono::steady_clock::now();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_hw_cmd_).count();
        if (!force_now && ms < 45) return;
        last_hw_cmd_ = now;
        if (is_vol) {
            sysinfo::spawn_detached({"wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", std::to_string(pct) + "%"});
        } else {
            sysinfo::spawn_detached({"brightnessctl", "set", std::to_string(pct) + "%"});
        }
    };

    auto apply_tuner_slider = [&](int idx, double x) {
        const double sx = 16.0;
        const double sw = std::max(1.0, static_cast<double>(popover_w_) - 32.0);
        const double n = std::clamp((x - sx) / sw, 0.0, 1.0);
        if (idx == 0) cfg_.refraction_strength = static_cast<float>(n);
        else if (idx == 1) cfg_.chromatic_aberration = static_cast<float>(n);
        else if (idx == 2) cfg_.blur_radius = static_cast<int>(std::round(n * 48.0));
        else if (idx == 3) cfg_.specular_intensity = static_cast<float>(n);
        else if (idx == 4) cfg_.tint_opacity = static_cast<float>(n * 0.45);
        palette_ = themes::resolve_palette(cfg_);
        invalidate_glass_caches();
        if (idx == 2 && !mizzle_.is_available()) {
            const auto out = display_.primary_output();
            backdrop_.load_wallpaper(palette_.wallpaper_path, out.width, out.height, palette_.blur_radius);
        }
        bar_surface_->request_redraw();
        popover_surface_->request_redraw();
    };

    if (ev.type == wl::PointerEvent::Type::Motion) {
        if (dragging_tuner_idx_ >= 0) {
            apply_tuner_slider(dragging_tuner_idx_, lx);
            return;
        }
        if (dragging_brightness_) {
            const double sx = (open_popover_ == PopoverKind::ControlCenter) ? 30.0 : 14.0;
            const double sw = (open_popover_ == PopoverKind::ControlCenter) ? 190.0 : (popover_w_ - 28.0);
            cached_brightness_.percent = slider_pct(lx, sx, sw);
            apply_hw_throttled(false, cached_brightness_.percent, false);
            popover_surface_->request_redraw();
            return;
        }
        if (dragging_volume_) {
            const double sx = (open_popover_ == PopoverKind::ControlCenter) ? 30.0 : 14.0;
            const double sw = (open_popover_ == PopoverKind::ControlCenter) ? 166.0 : (popover_w_ - 28.0);
            cached_audio_.volume_percent = slider_pct(lx, sx, sw);
            cached_audio_.muted = false;
            apply_hw_throttled(true, cached_audio_.volume_percent, false);
            popover_surface_->request_redraw();
            bar_surface_->request_redraw();
            return;
        }
        std::string next_h;
        for (const auto& a : pop_actions_) {
            if (!a.hover_key.empty() && lx >= a.x && lx <= a.x + a.w && ly >= a.y && ly <= a.y + a.h) {
                next_h = a.hover_key;
                break;
            }
        }
        if (next_h != hovered_pop_id_) {
            hovered_pop_id_ = next_h;
            popover_surface_->request_redraw();
        }
        return;
    }

    if (ev.type != wl::PointerEvent::Type::ButtonPress || ev.button != BTN_LEFT) {
        return;
    }

    if (lx < 0.0 || ly < 0.0 || lx > popover_w_ || ly > popover_h_) {
        queue_close_popover();
        return;
    }

    pop_click_.x = static_cast<float>(ev.x);
    pop_click_.y = static_cast<float>(ev.y);
    pop_click_.age_sec = 0.0f;
    pop_click_.active = true;
    pop_click_start_ = std::chrono::steady_clock::now();
    popover_surface_->request_redraw();

    if (open_popover_ == PopoverKind::ShaderTuner) {
        for (int i = 0; i < 5; ++i) {
            const double sy = 50.0 + static_cast<double>(i) * 36.0;
            if (lx >= 10.0 && lx <= popover_w_ - 10.0 && ly >= sy + 4.0 && ly <= sy + 32.0) {
                dragging_tuner_idx_ = i;
                apply_tuner_slider(i, lx);
                return;
            }
        }
    }
    if (open_popover_ == PopoverKind::Brightness && ly >= 42.0 && ly <= 72.0 && lx >= 14.0 && lx <= popover_w_ - 14.0) {
        dragging_brightness_ = true;
        cached_brightness_.percent = slider_pct(lx, 14.0, popover_w_ - 28.0);
        apply_hw_throttled(false, cached_brightness_.percent, true);
        popover_surface_->request_redraw();
        return;
    }
    if (open_popover_ == PopoverKind::Volume && ly >= 40.0 && ly <= 70.0 && lx >= 14.0 && lx <= popover_w_ - 14.0) {
        dragging_volume_ = true;
        cached_audio_.volume_percent = slider_pct(lx, 14.0, popover_w_ - 28.0);
        apply_hw_throttled(true, cached_audio_.volume_percent, true);
        popover_surface_->request_redraw();
        bar_surface_->request_redraw();
        return;
    }
    if (open_popover_ == PopoverKind::ControlCenter) {
        if (lx >= 24.0 && lx <= 204.0 && ly >= 156.0 && ly <= 206.0) {
            dragging_volume_ = true;
            cached_audio_.volume_percent = slider_pct(lx, 30.0, 166.0);
            cached_audio_.muted = false;
            apply_hw_throttled(true, cached_audio_.volume_percent, true);
            popover_surface_->request_redraw();
            bar_surface_->request_redraw();
            return;
        }
        if (lx >= 24.0 && lx <= 240.0 && ly >= 214.0 && ly <= 264.0) {
            dragging_brightness_ = true;
            cached_brightness_.percent = slider_pct(lx, 30.0, 190.0);
            apply_hw_throttled(false, cached_brightness_.percent, true);
            popover_surface_->request_redraw();
            return;
        }
    }

    const auto actions_copy = pop_actions_;
    for (const auto& a : actions_copy) {
        if (lx >= a.x && lx <= a.x + a.w && ly >= a.y && ly <= a.y + a.h) {
            if (a.id == "settings") {
                launch_settings(a.arg);
                queue_close_popover();
            } else if (a.id == "open_tuner") {
                queue_open_popover(PopoverKind::ShaderTuner, popover_anchor_x_);
            } else if (a.id == "open_cc") {
                queue_open_popover(PopoverKind::ControlCenter, popover_anchor_x_);
            } else if (a.id == "open_spotlight") {
                queue_open_popover(PopoverKind::Spotlight, popover_anchor_x_);
            } else if (a.id == "open_wifi") {
                queue_open_popover(PopoverKind::Wifi, popover_anchor_x_);
            } else if (a.id == "open_bt") {
                queue_open_popover(PopoverKind::Bluetooth, popover_anchor_x_);
            } else if (a.id == "open_vol") {
                queue_open_popover(PopoverKind::Volume, popover_anchor_x_);
            } else if (a.id == "exec") {
                sysinfo::spawn_detached({"bash", "-c", a.arg});
                queue_close_popover();
            } else if (a.id == "exec_keep") {
                sysinfo::spawn_detached({"bash", "-c", a.arg});
                refresh_hardware_quick();
            } else if (a.id == "niri") {
                sysinfo::niri_action(a.arg);
                queue_close_popover();
            } else if (a.id == "wifi_toggle") {
                cached_wifi_.enabled = !cached_wifi_.enabled;
                if (!cached_wifi_.enabled) { cached_wifi_.connected = false; cached_wifi_.ssid.clear(); }
                sysinfo::spawn_detached({"nmcli", "radio", "wifi", cached_wifi_.enabled ? "on" : "off"});
                if (popover_surface_) popover_surface_->request_redraw();
                bar_surface_->request_redraw();
                trigger_async_refresh(true);
            } else if (a.id == "wifi_rescan") {
                sysinfo::rescan_wifi();
                trigger_async_refresh(true);
            } else if (a.id == "wifi_connect") {
                sysinfo::connect_wifi(a.arg);
                queue_close_popover();
                trigger_async_refresh(true);
            } else if (a.id == "wifi_disconnect") {
                sysinfo::spawn_detached({"nmcli", "connection", "down", a.arg});
                cached_wifi_.connected = false;
                cached_wifi_.ssid.clear();
                if (popover_surface_) popover_surface_->request_redraw();
                bar_surface_->request_redraw();
                trigger_async_refresh(true);
            } else if (a.id == "bt_toggle") {
                cached_bt_.powered = !cached_bt_.powered;
                if (!cached_bt_.powered) cached_bt_.connected_name.clear();
                sysinfo::spawn_detached({"bluetoothctl", "power", cached_bt_.powered ? "on" : "off"});
                if (popover_surface_) popover_surface_->request_redraw();
                bar_surface_->request_redraw();
                trigger_async_refresh(true);
            } else if (a.id == "bt_connect") {
                sysinfo::connect_bluetooth_device(a.arg, true);
                queue_close_popover();
                trigger_async_refresh(true);
            } else if (a.id == "bt_disconnect") {
                sysinfo::connect_bluetooth_device(a.arg, false);
                queue_close_popover();
                trigger_async_refresh(true);
            } else if (a.id == "mute_toggle") {
                cached_audio_.muted = !cached_audio_.muted;
                sysinfo::spawn_detached({"wpctl", "set-mute", "@DEFAULT_AUDIO_SINK@", "toggle"});
                if (popover_surface_) popover_surface_->request_redraw();
                bar_surface_->request_redraw();
                trigger_async_refresh(false);
            } else if (a.id == "sink_set") {
                sysinfo::spawn_detached({"wpctl", "set-default", a.arg});
                trigger_async_refresh(true);
            } else if (a.id == "bright_set") {
                cached_brightness_.percent = std::clamp(std::atoi(a.arg.c_str()), 5, 100);
                sysinfo::spawn_detached({"brightnessctl", "set", std::to_string(cached_brightness_.percent) + "%"});
                if (popover_surface_) popover_surface_->request_redraw();
            } else if (a.id == "power_profile") {
                cached_battery_.power_profile = a.arg;
                sysinfo::spawn_detached({"powerprofilesctl", "set", a.arg});
                if (popover_surface_) popover_surface_->request_redraw();
                trigger_async_refresh(true);
            } else if (a.id == "cc_airdrop") {
                cfg_.cc_airdrop_on = !cfg_.cc_airdrop_on;
                config::save_config(cfg_);
                if (popover_surface_) popover_surface_->request_redraw();
            } else if (a.id == "cc_focus") {
                cfg_.cc_focus_on = !cfg_.cc_focus_on;
                config::save_config(cfg_);
                if (popover_surface_) popover_surface_->request_redraw();
            } else if (a.id == "ws_toggle") {
                cfg_.show_workspaces = !cfg_.show_workspaces;
                config::save_config(cfg_);
                reload_config();
            } else if (a.id == "clock_toggle") {
                cfg_.clock_format = (cfg_.clock_format == "24h") ? "12h" : "24h";
                config::save_config(cfg_);
                refresh_clock_and_niri();
                reload_config();
            } else if (a.id == "look_toggle") {
                cfg_.look = (cfg_.look == "attached") ? "pill" : "attached";
                config::save_config(cfg_);
                reload_config();
            } else if (a.id == "preset_cycle") {
                static const char* kCycle[5] = {"clear", "frosted", "smoked", "aqua", "solid"};
                std::size_t idx = 0;
                for (std::size_t pi = 0; pi < 5; ++pi) {
                    if (cfg_.glass_preset == kCycle[pi]) { idx = (pi + 1) % 5; break; }
                }
                config::apply_glass_preset(cfg_, kCycle[idx]);
                config::save_config(cfg_);
                reload_config();
            } else if (a.id == "recapture_bg") {
                queue_recapture_and_open_tuner(popover_anchor_x_);
            } else if (a.id == "iconstyle_cycle") {
                static const char* kStyles[4] = {"default", "dark", "clear", "tinted"};
                int next = 0;
                for (int si = 0; si < 4; ++si) {
                    if (cfg_.icon_style == kStyles[si]) { next = (si + 1) % 4; break; }
                }
                cfg_.icon_style = kStyles[next];
                config::save_config(cfg_);
                reload_config();
            } else if (a.id == "theme_toggle") {
                cfg_.theme = "dark";
                config::save_config(cfg_);
                reload_config();
            } else if (a.id == "autohide_toggle") {
                cfg_.auto_hide = !cfg_.auto_hide;
                config::save_config(cfg_);
                reload_config();
            }
            return;
        }
    }
}

void BarApp::handle_popover_key(const wl::KeyEvent& ev) {
    if (!ev.pressed) return;
    if (open_popover_ == PopoverKind::Spotlight) {
        if (ev.keysym == XKB_KEY_Escape) {
            if (!spotlight_query_.empty()) {
                spotlight_query_.clear();
                spotlight_selected_ = 0;
                rebuild_spotlight_matches();
                if (popover_surface_) popover_surface_->request_redraw();
            } else {
                queue_close_popover();
            }
            return;
        }
        if (ev.keysym == XKB_KEY_Up) {
            if (!spotlight_matches_.empty()) {
                spotlight_selected_ = (spotlight_selected_ > 0)
                    ? (spotlight_selected_ - 1)
                    : static_cast<int>(spotlight_matches_.size()) - 1;
                if (popover_surface_) popover_surface_->request_redraw();
            }
            return;
        }
        if (ev.keysym == XKB_KEY_Down) {
            if (!spotlight_matches_.empty()) {
                spotlight_selected_ = (spotlight_selected_ + 1) % static_cast<int>(spotlight_matches_.size());
                if (popover_surface_) popover_surface_->request_redraw();
            }
            return;
        }
        if (ev.keysym == XKB_KEY_Return || ev.keysym == XKB_KEY_KP_Enter) {
            if (!spotlight_matches_.empty()) {
                const auto item = spotlight_matches_[static_cast<std::size_t>(spotlight_selected_)];
                if (item.action_id == "settings") {
                    launch_settings(item.arg);
                    queue_close_popover();
                } else if (item.action_id == "open_tuner") {
                    queue_open_popover(PopoverKind::ShaderTuner, std::max(0, bar_surface_->width() - 320));
                } else if (item.action_id == "open_cc") {
                    queue_open_popover(PopoverKind::ControlCenter, std::max(0, bar_surface_->width() - 290));
                } else if (item.action_id == "autohide_toggle") {
                    cfg_.auto_hide = !cfg_.auto_hide;
                    config::save_config(cfg_);
                    reload_config();
                    queue_close_popover();
                } else if (item.action_id == "look_toggle") {
                    cfg_.look = (cfg_.look == "attached") ? "pill" : "attached";
                    config::save_config(cfg_);
                    reload_config();
                    queue_close_popover();
                } else if (item.action_id == "exec" && !item.arg.empty()) {
                    sysinfo::spawn_detached({"bash", "-c", item.arg});
                    queue_close_popover();
                }
            }
            return;
        }
        if (ev.keysym == XKB_KEY_BackSpace) {
            if (!spotlight_query_.empty()) {
                spotlight_query_.pop_back();
                spotlight_selected_ = 0;
                rebuild_spotlight_matches();
                if (popover_surface_) popover_surface_->request_redraw();
            }
            return;
        }
        if (!ev.utf8.empty() && static_cast<unsigned char>(ev.utf8[0]) >= 32 && spotlight_query_.size() < 48) {
            spotlight_query_ += ev.utf8;
            spotlight_selected_ = 0;
            rebuild_spotlight_matches();
            if (popover_surface_) popover_surface_->request_redraw();
            return;
        }
    }
    if (ev.keysym == XKB_KEY_Escape) {
        queue_close_popover();
    }
}

std::string BarApp::handle_ipc(const std::string& cmd, bool& should_quit) {
    if (cmd == "quit") {
        should_quit = true;
        return "ok";
    }
    if (cmd == "reload") {
        reload_config();
        return "ok";
    }
    if (cmd == "tuner") {
        const int bar_w = std::max(640, bar_surface_->width());
        open_popover(PopoverKind::ShaderTuner, static_cast<double>(bar_w - 310));
        return "tuner opened";
    }
    if (cmd == "cc" || cmd == "control-center") {
        const int bar_w = std::max(640, bar_surface_->width());
        open_popover(PopoverKind::ControlCenter, static_cast<double>(bar_w - 280));
        return "control center toggled";
    }
    if (cmd == "spotlight") {
        open_popover(PopoverKind::Spotlight, 0.0);
        return "spotlight opened";
    }
    if (cmd == "hide") {
        manually_hidden_ = true;
        update_visibility();
        return "hidden completely";
    }
    if (cmd == "show") {
        manually_hidden_ = false;
        cfg_.auto_hide = false;
        config::save_config(cfg_);
        reload_config();
        return "shown (always visible)";
    }
    if (cmd == "toggle" || cmd == "autohide:toggle" || cmd == "auto-hide toggle") {
        if (manually_hidden_) {
            manually_hidden_ = false;
            update_visibility();
        } else {
            cfg_.auto_hide = !cfg_.auto_hide;
            config::save_config(cfg_);
            reload_config();
        }
        return cfg_.auto_hide ? "autohide on" : "autohide off";
    }
    if (cmd == "autohide:on" || cmd == "auto-hide on") {
        manually_hidden_ = false;
        cfg_.auto_hide = true;
        config::save_config(cfg_);
        reload_config();
        return "autohide on";
    }
    if (cmd == "autohide:off" || cmd == "auto-hide off") {
        manually_hidden_ = false;
        cfg_.auto_hide = false;
        config::save_config(cfg_);
        reload_config();
        return "autohide off";
    }
    if (cmd.rfind("theme:", 0) == 0) {
        const std::string t = cmd.substr(6);
        cfg_.theme = (t == "light") ? "light" : "dark";
        if (cfg_.glass_preset == "dark" && cfg_.theme == "light") cfg_.glass_preset = "regular";
        config::save_config(cfg_);
        reload_config();
        return "theme set to " + cfg_.theme;
    }
    if (cmd.rfind("look:", 0) == 0) {
        const std::string l = cmd.substr(5);
        cfg_.look = (l == "attached") ? "attached" : "pill";
        config::save_config(cfg_);
        reload_config();
        return "look set to " + cfg_.look;
    }
    if (cmd.rfind("preset:", 0) == 0 || cmd.rfind("style:", 0) == 0) {
        std::string arg = cmd.substr(cmd.find(':') + 1);
        std::string sub;
        const auto col = arg.find(':');
        if (col != std::string::npos) {
            sub = arg.substr(col + 1);
            arg = arg.substr(0, col);
        }
        if (arg == "glassy") arg = "regular";
        if (arg == "transparent") arg = "clear";
        config::apply_glass_preset(cfg_, arg);
        if (sub == "dark" || sub == "white") cfg_.glassy_text_color = sub;
        else cfg_.glassy_text_color = "auto";
        config::save_config(cfg_);
        reload_config();
        return "preset set to " + cfg_.glass_preset;
    }
    if (cmd == "status") {
        const std::string gpu_str = mizzle_.has_wayland_egl()
            ? "on (mizzle-egl)"
            : (mizzle_.is_available() ? "on (mizzle-offscreen)" : (backdrop_.using_gpu() ? "on" : "cpu"));
        return "running (theme: " + cfg_.theme + ", preset: " + cfg_.glass_preset +
               ", look: " + cfg_.look + ", autohide: " + (cfg_.auto_hide ? "on" : "off") +
               ", gpu_glass: " + gpu_str + ")";
    }
    return "unknown";
}

} // namespace mistbar::bar
