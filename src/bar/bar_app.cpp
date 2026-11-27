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
#include <filesystem>
#include <linux/input-event-codes.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace fs = std::filesystem;

namespace mistbar::bar {

namespace {

constexpr int kPopShadowMargin = 16;

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

} // namespace

BarApp::BarApp(wl::Display& display)
    : display_(display),
      bar_surface_(std::make_unique<wl::LayerSurface>(display)),
      trigger_surface_(std::make_unique<wl::LayerSurface>(display)),
      popover_surface_(std::make_unique<wl::LayerSurface>(display)) {}

bool BarApp::init() {
    config::ensure_default_config();
    cfg_ = config::load_config();
    palette_ = themes::resolve_palette(cfg_);
    backdrop_.init(cfg_);

    const auto out = display_.primary_output();
    backdrop_.load_wallpaper(palette_.wallpaper_path, out.width, out.height, palette_.blur_radius);

    bar_surface_->set_draw_callback([this](cairo_t* cr, int w, int h) { draw_bar(cr, w, h); });
    bar_surface_->set_pointer_callback([this](const wl::PointerEvent& ev) { handle_bar_pointer(ev); });

    const bool is_pill = (cfg_.look == "pill");
    const std::uint32_t anchors = wl::LayerSurface::Top | wl::LayerSurface::Left | wl::LayerSurface::Right;
    const int excl = cfg_.auto_hide ? -1 : (cfg_.bar_height + (is_pill ? cfg_.bar_margin : 0));

    if (!bar_surface_->create("mistbar", wl::LayerSurface::Layer::Top, anchors,
                              0, cfg_.bar_height, excl, wl::LayerSurface::KeyboardMode::None)) {
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
    trigger_surface_->hide();

    popover_surface_->set_draw_callback([this](cairo_t* cr, int w, int h) { draw_popover(cr, w, h); });
    popover_surface_->set_pointer_callback([this](const wl::PointerEvent& ev) { handle_popover_pointer(ev); });
    popover_surface_->set_key_callback([this](const wl::KeyEvent& ev) { handle_popover_key(ev); });
    popover_surface_->set_visible(false);
    popover_surface_->create("mistbar-popover", wl::LayerSurface::Layer::Overlay,
                             wl::LayerSurface::Top | wl::LayerSurface::Left,
                             240 + kPopShadowMargin * 2, 200 + kPopShadowMargin * 2,
                             -1, wl::LayerSurface::KeyboardMode::OnDemand);
    popover_surface_->hide();

    display_.set_toplevel_change_callback([this]() {
        refresh_clock_and_niri();
        bar_surface_->request_redraw();
    });

    apply_geometry();
    refresh_clock_and_niri();
    refresh_hardware_full();
    update_visibility();
    return true;
}

void BarApp::apply_geometry() {
    const bool is_pill = (cfg_.look == "pill");
    const int top_m = is_pill ? cfg_.bar_margin : 0;
    const int side_m = is_pill ? defaults::BAR_SIDE_MARGIN : 0;
    const int slide_offset = static_cast<int>((1.0 - slide_anim_.value()) * -(cfg_.bar_height + top_m + 6));

    bar_surface_->set_size(0, cfg_.bar_height);
    bar_surface_->set_margin(top_m + slide_offset, side_m, 0, side_m);
    bar_surface_->set_exclusive_zone(cfg_.auto_hide ? -1 : (cfg_.bar_height + top_m));
}

void BarApp::reload_config() {
    cfg_ = config::load_config();
    palette_ = themes::resolve_palette(cfg_);
    backdrop_.init(cfg_);
    icons_.clear();
    const auto out = display_.primary_output();
    backdrop_.load_wallpaper(palette_.wallpaper_path, out.width, out.height, palette_.blur_radius);
    apply_geometry();
    update_visibility();
    bar_surface_->request_redraw();
    if (open_popover_ != PopoverKind::None) {
        popover_surface_->request_redraw();
    }
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
    return animating || grace_active_;
}

void BarApp::refresh_clock_and_niri() {
    cached_clock_ = format_clock_str(cfg_);
    cached_full_date_ = format_full_date_str();
    cached_workspaces_ = sysinfo::get_niri_workspaces();

    bool any_windows = false;
    for (const auto& ws : cached_workspaces_) {
        if (ws.focused && ws.has_windows) {
            any_windows = true;
            break;
        }
    }
    if (!any_windows) {
        for (const auto& tl : display_.toplevels()) {
            if (tl.active) {
                any_windows = true;
                break;
            }
        }
    }
    has_active_windows_ = any_windows;

    std::string app = sysinfo::get_niri_focused_app();
    if (app.empty()) {
        for (const auto& tl : display_.toplevels()) {
            if (tl.active) {
                app = prettify_app_id(tl.app_id.empty() ? tl.title : tl.app_id);
                break;
            }
        }
    }
    cached_active_app_ = app.empty() ? "Finder" : app;

    if (cfg_.auto_hide) {
        update_visibility();
    }
}

void BarApp::refresh_hardware_quick() {
    cached_audio_ = sysinfo::get_audio_status(open_popover_ == PopoverKind::Volume);
    cached_brightness_ = sysinfo::get_brightness_status();
    cached_mic_active_ = cfg_.show_privacy ? sysinfo::is_mic_active() : false;
    if (open_popover_ == PopoverKind::ControlCenter) {
        cached_media_ = sysinfo::get_media_status();
    }
    bar_surface_->request_redraw();
    if (open_popover_ != PopoverKind::None) {
        popover_surface_->request_redraw();
    }
}

void BarApp::refresh_hardware_full() {
    cached_wifi_ = sysinfo::get_wifi_status(open_popover_ == PopoverKind::Wifi);
    cached_bt_ = sysinfo::get_bluetooth_status(open_popover_ == PopoverKind::Bluetooth);
    cached_battery_ = sysinfo::get_battery_status(open_popover_ == PopoverKind::Battery);
    cached_audio_ = sysinfo::get_audio_status(open_popover_ == PopoverKind::Volume);
    cached_brightness_ = sysinfo::get_brightness_status();
    cached_media_ = sysinfo::get_media_status();
    cached_tray_ = cfg_.show_tray ? sysinfo::get_tray_items() : std::vector<sysinfo::TrayItem>{};
    cached_mic_active_ = cfg_.show_privacy ? sysinfo::is_mic_active() : false;
    bar_surface_->request_redraw();
    if (open_popover_ != PopoverKind::None) {
        popover_surface_->request_redraw();
    }
}

void BarApp::launch_settings(const std::string& page) {
    const std::string bin = resolve_sibling_binary("mistbar-settings");
    if (page.empty()) {
        sysinfo::spawn_detached({bin});
    } else {
        sysinfo::spawn_detached({bin, "--page", page});
    }
}

void BarApp::close_popover() {
    if (open_popover_ != PopoverKind::None) {
        open_popover_ = PopoverKind::None;
        active_menu_name_.clear();
        hovered_pop_id_.clear();
        dragging_brightness_ = false;
        dragging_volume_ = false;
        popover_surface_->hide();
        update_visibility();
        bar_surface_->request_redraw();
    }
}

void BarApp::open_popover(PopoverKind kind, double anchor_x, const std::string& menu_name) {
    if (open_popover_ == kind && active_menu_name_ == menu_name) {
        close_popover();
        return;
    }

    open_popover_ = kind;
    active_menu_name_ = menu_name;
    hovered_pop_id_.clear();

    if (kind == PopoverKind::Wifi) cached_wifi_ = sysinfo::get_wifi_status(true);
    else if (kind == PopoverKind::Bluetooth) cached_bt_ = sysinfo::get_bluetooth_status(true);
    else if (kind == PopoverKind::Volume) cached_audio_ = sysinfo::get_audio_status(true);
    else if (kind == PopoverKind::Battery) cached_battery_ = sysinfo::get_battery_status(true);
    else if (kind == PopoverKind::Brightness) cached_brightness_ = sysinfo::get_brightness_status();
    else if (kind == PopoverKind::ControlCenter) {
        cached_wifi_ = sysinfo::get_wifi_status(false);
        cached_bt_ = sysinfo::get_bluetooth_status(false);
        cached_audio_ = sysinfo::get_audio_status(false);
        cached_brightness_ = sysinfo::get_brightness_status();
        cached_media_ = sysinfo::get_media_status();
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
        case PopoverKind::ControlCenter: popover_w_ = 336; popover_h_ = cached_media_.active ? 410 : 334; break;
        case PopoverKind::Calendar:      popover_w_ = 272; popover_h_ = 262; break;
        case PopoverKind::PowerMenu:     popover_w_ = 220; popover_h_ = 180; break;
        case PopoverKind::None:          return;
    }

    const int bar_w = std::max(640, bar_surface_->width());
    const int side_m = (cfg_.look == "pill") ? defaults::BAR_SIDE_MARGIN : 0;
    const int top_m = ((cfg_.look == "pill") ? cfg_.bar_margin : 0) + cfg_.bar_height + 6 - kPopShadowMargin;
    const int left_m = std::clamp(static_cast<int>(anchor_x) + side_m - kPopShadowMargin,
                                  0, std::max(0, bar_w + side_m - popover_w_ - kPopShadowMargin));

    popover_anchor_x_ = static_cast<double>(left_m + kPopShadowMargin);
    popover_surface_->set_size(popover_w_ + kPopShadowMargin * 2, popover_h_ + kPopShadowMargin * 2);
    popover_surface_->set_margin(std::max(0, top_m), 0, 0, left_m);
    popover_surface_->show();
    popover_surface_->request_redraw();
    bar_surface_->request_redraw();
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

    const double radius = (cfg_.look == "pill") ? static_cast<double>(cfg_.border_radius) : 0.0;
    glass::draw_glass_panel(cr, &backdrop_, 0.0, 0.0, 0.0, 0.0,
                            static_cast<double>(w), static_cast<double>(h),
                            radius, palette_, &palette_.bar_bg, false, 0.35, 0.45);
    draw_bar_foreground(cr, w, h);
}

void BarApp::draw_bar(cairo_t* cr, int w, int h) {
    const double radius = (cfg_.look == "pill") ? static_cast<double>(cfg_.border_radius) : 0.0;
    const bool overlay_only = !palette_.solid_mode;

    glass::draw_glass_panel(cr, &backdrop_, 0.0, 0.0,
                            (cfg_.look == "pill") ? defaults::BAR_SIDE_MARGIN : 0.0,
                            (cfg_.look == "pill") ? cfg_.bar_margin : 0.0,
                            static_cast<double>(w), static_cast<double>(h),
                            radius, palette_, &palette_.bar_bg, overlay_only,
                            pointer_norm_x_, pointer_norm_y_);
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

    // Right section (placed right-to-left in macOS order)
    double rx = static_cast<double>(w) - ((cfg_.look == "pill") ? 12.0 : 10.0);

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
        rx -= 2.0;
    }

    // Spotlight Search
    if (cfg_.show_spotlight) {
        rx -= 24.0;
        draw_hover_pill(rx, 3.0, 22.0, static_cast<double>(h) - 6.0, "spotlight");
        const std::string p = icons::resolve_icon("search.svg");
        if (!p.empty()) icons_.draw(cr, p, rx + 3.5, icon_y, icon_sz, icon_sz, fg_tint);
        bar_hits_.push_back({rx, 0.0, 22.0, static_cast<double>(h), "spotlight", 0});
        rx -= 2.0;
    }

    // Volume
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
        rx -= 2.0;
    }

    // Brightness (optional)
    if (cfg_.show_brightness) {
        rx -= 24.0;
        draw_hover_pill(rx, 3.0, 22.0, static_cast<double>(h) - 6.0, "brightness",
                        open_popover_ == PopoverKind::Brightness);
        const std::string p = icons::resolve_icon("sun.svg");
        if (!p.empty()) icons_.draw(cr, p, rx + 3.5, icon_y, icon_sz, icon_sz, fg_tint);
        bar_hits_.push_back({rx, 0.0, 22.0, static_cast<double>(h), "brightness", 0});
        rx -= 2.0;
    }

    // Bluetooth
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
        rx -= 2.0;
    }

    // Wi-Fi
    if (cfg_.show_network) {
        rx -= 25.0;
        draw_hover_pill(rx, 3.0, 23.0, static_cast<double>(h) - 6.0, "wifi",
                        open_popover_ == PopoverKind::Wifi);
        const std::string p = icons::resolve_icon(cached_wifi_.enabled && cached_wifi_.connected ? "wifi.svg" : "wifi-off.svg");
        if (!p.empty()) icons_.draw(cr, p, rx + 4.0, icon_y, icon_sz, icon_sz, fg_tint);
        bar_hits_.push_back({rx, 0.0, 23.0, static_cast<double>(h), "wifi", 0});
        rx -= 2.0;
    }

    // Battery with dynamic macOS fill level
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
        if (next_hover != hovered_bar_id_ || cfg_.cursor_reactive) {
            hovered_bar_id_ = next_hover;
            bar_surface_->request_redraw();
        }
        return;
    } else if (ev.type == wl::PointerEvent::Type::Axis) {
        for (const auto& r : bar_hits_) {
            if (ev.x >= r.x && ev.x <= r.x + r.w) {
                const int delta = (ev.axis_dy < 0.0) ? 5 : -5;
                if (r.id == "volume") {
                    sysinfo::adjust_audio_volume(delta);
                    refresh_hardware_quick();
                } else if (r.id == "brightness") {
                    sysinfo::adjust_brightness_percent(delta);
                    refresh_hardware_quick();
                }
                break;
            }
        }
        return;
    }

    if (ev.type != wl::PointerEvent::Type::ButtonPress || ev.button != BTN_LEFT) {
        return;
    }

    for (const auto& r : bar_hits_) {
        if (ev.x >= r.x && ev.x <= r.x + r.w && ev.y >= r.y && ev.y <= r.y + r.h) {
            if (r.id == "logo") {
                open_popover(PopoverKind::LogoMenu, r.x);
            } else if (r.id.rfind("menu:", 0) == 0) {
                open_popover(PopoverKind::AppMenu, r.x, r.id.substr(5));
            } else if (r.id == "ws") {
                sysinfo::focus_niri_workspace(r.arg_int);
                refresh_clock_and_niri();
                bar_surface_->request_redraw();
            } else if (r.id == "wifi") {
                open_popover(PopoverKind::Wifi, r.x - 180.0);
            } else if (r.id == "bluetooth") {
                open_popover(PopoverKind::Bluetooth, r.x - 180.0);
            } else if (r.id == "volume") {
                open_popover(PopoverKind::Volume, r.x - 180.0);
            } else if (r.id == "brightness") {
                open_popover(PopoverKind::Brightness, r.x - 160.0);
            } else if (r.id == "battery") {
                open_popover(PopoverKind::Battery, r.x - 160.0);
            } else if (r.id == "spotlight") {
                close_popover();
                sysinfo::spawn_detached({"bash", "-c", "fuzzel 2>/dev/null || rofi -show drun 2>/dev/null || true"});
            } else if (r.id == "cc") {
                open_popover(PopoverKind::ControlCenter, r.x - 260.0);
            } else if (r.id == "clock") {
                open_popover(PopoverKind::Calendar, r.x - 120.0);
            } else if (r.id == "power") {
                open_popover(PopoverKind::PowerMenu, r.x - 170.0);
            } else if (r.id == "tray") {
                const auto idx = static_cast<std::size_t>(r.arg_int);
                if (idx < cached_tray_.size()) {
                    sysinfo::activate_tray_item(cached_tray_[idx]);
                }
            }
            return;
        }
    }
    close_popover();
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

    window::draw_window_shadow(cr, 16.0, 16.0, static_cast<double>(w - 32), static_cast<double>(h - 32), 16.0, 18, true);
    cairo_save(cr);
    cairo_translate(cr, 16.0, 16.0);
    glass::draw_glass_panel(cr, &backdrop_, 0.0, 0.0, 900.0, 44.0,
                            static_cast<double>(w - 32), static_cast<double>(h - 32),
                            16.0, palette_, &palette_.panel_bg, false);
    draw_control_center(cr, w - 32);
    cairo_restore(cr);
}

void BarApp::draw_popover(cairo_t* cr, int total_w, int total_h) {
    pop_actions_.clear();
    const double m = static_cast<double>(kPopShadowMargin);
    const double w = std::max(1.0, static_cast<double>(total_w) - 2.0 * m);
    const double h = std::max(1.0, static_cast<double>(total_h) - 2.0 * m);

    window::draw_window_shadow(cr, m, m, w, h, 16.0, 18, true);

    cairo_save(cr);
    cairo_translate(cr, m, m);

    glass::draw_glass_panel(cr, &backdrop_, 0.0, 0.0, popover_anchor_x_,
                            static_cast<double>(cfg_.bar_height + 10),
                            w, h, 16.0, palette_, &palette_.panel_bg, false);

    const int pw = static_cast<int>(w);
    switch (open_popover_) {
        case PopoverKind::LogoMenu:      draw_logo_menu(cr, pw); break;
        case PopoverKind::AppMenu:       draw_app_menu(cr, pw); break;
        case PopoverKind::Wifi:          draw_wifi_popover(cr, pw); break;
        case PopoverKind::Bluetooth:     draw_bluetooth_popover(cr, pw); break;
        case PopoverKind::Volume:        draw_volume_popover(cr, pw); break;
        case PopoverKind::Brightness:    draw_brightness_popover(cr, pw); break;
        case PopoverKind::Battery:       draw_battery_popover(cr, pw); break;
        case PopoverKind::ControlCenter: draw_control_center(cr, pw); break;
        case PopoverKind::Calendar:      draw_calendar_popover(cr, pw); break;
        case PopoverKind::PowerMenu:     draw_power_popover(cr, pw); break;
        case PopoverKind::None:          break;
    }

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
        draw_popover_row(cr, 8.0, y, row_w, "Applications", "", "exec", "fuzzel"); y += 28.0;
        draw_popover_row(cr, 8.0, y, row_w, "Terminal", "", "exec", "alacritty 2>/dev/null || kitty 2>/dev/null || foot 2>/dev/null || x-terminal-emulator"); y += 28.0;
        draw_popover_row(cr, 8.0, y, row_w, "Computer", "", "exec", "xdg-open /");
    } else if (active_menu_name_ == "Window") {
        draw_popover_row(cr, 8.0, y, row_w, "Next Workspace", "", "niri", "focus-workspace-down"); y += 28.0;
        draw_popover_row(cr, 8.0, y, row_w, "Previous Workspace", "", "niri", "focus-workspace-up"); y += 30.0;
        draw_separator(cr, 12.0, y, static_cast<double>(w) - 24.0); y += 7.0;
        draw_popover_row(cr, 8.0, y, row_w, "Move to Next Workspace", "", "niri", "move-column-to-workspace-down"); y += 28.0;
        draw_popover_row(cr, 8.0, y, row_w, "Move to Prev Workspace", "", "niri", "move-column-to-workspace-up");
    } else {
        draw_popover_row(cr, 8.0, y, row_w, "Search Commands...", "", "exec", "fuzzel"); y += 28.0;
        draw_popover_row(cr, 8.0, y, row_w, "Mistbar Settings...", "", "settings", "appearance"); y += 28.0;
        draw_popover_row(cr, 8.0, y, row_w, "Mistbar Repository", "", "exec", "xdg-open https://github.com/TattvaOrg/Mistbar");
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

void BarApp::draw_control_center(cairo_t* cr, int w) {
    const std::uint32_t fg_tint = palette_.dark_mode ? 0xFFF5F7FAU : 0xFF1A1C20U;
    auto draw_card = [&](double x, double y, double cw, double ch) {
        glass::append_rounded_rect(cr, x, y, cw, ch, 14.0);
        cairo_set_source_rgba(cr, palette_.surface_elevated.r, palette_.surface_elevated.g,
                              palette_.surface_elevated.b, palette_.dark_mode ? 0.44 : 0.72);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, palette_.border_outer.r, palette_.border_outer.g,
                              palette_.border_outer.b, 0.24);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);
    };
    auto draw_circle = [&](double cx, double cy, double r, bool active, const std::string& svg) {
        cairo_arc(cr, cx, cy, r, 0.0, 6.283185);
        if (active) cairo_set_source_rgba(cr, palette_.accent.r, palette_.accent.g, palette_.accent.b, 0.95);
        else cairo_set_source_rgba(cr, 0.5, 0.53, 0.58, 0.30);
        cairo_fill(cr);
        const std::string p = icons::resolve_icon(svg);
        if (!p.empty()) icons_.draw(cr, p, cx - 8.5, cy - 8.5, 17.0, 17.0, active ? 0xFFFFFFFFU : fg_tint);
    };

    draw_card(14.0, 14.0, 152.0, 118.0);
    draw_circle(40.0, 44.0, 16.0, cached_wifi_.enabled, cached_wifi_.enabled ? "wifi.svg" : "wifi-off.svg");
    window::draw_pango_text_centered(cr, "Wi-Fi", 64.0, 26.0, 92.0, 18.0, 9.2, true, palette_.text_primary);
    window::draw_pango_text_centered(cr, cached_wifi_.connected ? cached_wifi_.ssid : (cached_wifi_.enabled ? "On" : "Off"),
                                     64.0, 42.0, 92.0, 16.0, 8.0, false, palette_.text_secondary);
    pop_actions_.push_back({14.0, 14.0, 152.0, 58.0, "wifi_toggle", ""});

    draw_circle(40.0, 98.0, 16.0, cached_bt_.powered, cached_bt_.powered ? "bluetooth.svg" : "bluetooth-off.svg");
    window::draw_pango_text_centered(cr, "Bluetooth", 64.0, 80.0, 92.0, 18.0, 9.2, true, palette_.text_primary);
    window::draw_pango_text_centered(cr, !cached_bt_.connected_name.empty() ? cached_bt_.connected_name : (cached_bt_.powered ? "On" : "Off"),
                                     64.0, 96.0, 92.0, 16.0, 8.0, false, palette_.text_secondary);
    pop_actions_.push_back({14.0, 73.0, 152.0, 58.0, "bt_toggle", ""});

    draw_card(176.0, 14.0, 146.0, 54.0);
    draw_circle(202.0, 41.0, 15.0, palette_.dark_mode, palette_.dark_mode ? "moon.svg" : "sun.svg");
    window::draw_pango_text_centered(cr, palette_.dark_mode ? "Dark Mode" : "Light Mode",
                                     224.0, 14.0, 90.0, 54.0, 9.0, true, palette_.text_primary);
    pop_actions_.push_back({176.0, 14.0, 146.0, 54.0, "theme_toggle", ""});

    draw_card(176.0, 78.0, 70.0, 54.0);
    draw_circle(211.0, 96.0, 11.0, cfg_.auto_hide, "control-center.svg");
    window::draw_pango_text_centered(cr, "Auto-Hide", 176.0, 110.0, 70.0, 18.0, 7.8, true, palette_.text_primary, true, false);
    pop_actions_.push_back({176.0, 78.0, 70.0, 54.0, "autohide_toggle", ""});

    draw_card(252.0, 78.0, 70.0, 54.0);
    draw_circle(287.0, 96.0, 11.0, false, "lock.svg");
    window::draw_pango_text_centered(cr, "Lock", 252.0, 110.0, 70.0, 18.0, 7.8, true, palette_.text_primary, true, false);
    pop_actions_.push_back({252.0, 78.0, 70.0, 54.0, "exec", "loginctl lock-session 2>/dev/null || niri msg action power-off-monitors"});

    draw_card(14.0, 144.0, 308.0, 62.0);
    window::draw_pango_text_centered(cr, "Display", 26.0, 148.0, 160.0, 20.0, 9.0, true, palette_.text_primary);
    window::draw_pango_text_centered(cr, std::to_string(cached_brightness_.percent) + "%", 250.0, 148.0, 58.0, 20.0,
                                     8.6, false, palette_.text_secondary, false, true);
    draw_capsule_slider(cr, 26.0, 174.0, 284.0, 20.0, cached_brightness_.percent, "sun.svg");

    draw_card(14.0, 216.0, 308.0, 62.0);
    window::draw_pango_text_centered(cr, "Sound", 26.0, 220.0, 160.0, 20.0, 9.0, true, palette_.text_primary);
    window::draw_pango_text_centered(cr, std::to_string(cached_audio_.volume_percent) + "%", 250.0, 220.0, 58.0, 20.0,
                                     8.6, false, palette_.text_secondary, false, true);
    draw_capsule_slider(cr, 26.0, 246.0, 284.0, 20.0, cached_audio_.volume_percent,
                        cached_audio_.muted ? "volume-mute.svg" : "volume.svg");

    double footer_y = 288.0;
    if (cached_media_.active) {
        draw_card(14.0, 288.0, 308.0, 64.0);
        window::draw_pango_text_centered(cr, cached_media_.title, 26.0, 292.0, 284.0, 22.0, 8.8, true, palette_.text_primary);
        const char* media_icons[3] = {"prev.svg", cached_media_.playing ? "pause.svg" : "play.svg", "next.svg"};
        const char* media_cmds[3]  = {"playerctl previous", "playerctl play-pause", "playerctl next"};
        for (int i = 0; i < 3; ++i) {
            const double bx = 114.0 + static_cast<double>(i) * 42.0;
            const std::string p = icons::resolve_icon(media_icons[i]);
            if (!p.empty()) icons_.draw(cr, p, bx, 322.0, 18.0, 18.0, fg_tint);
            pop_actions_.push_back({bx - 6.0, 318.0, 30.0, 26.0, "exec_keep", media_cmds[i]});
        }
        footer_y = 362.0;
    }

    draw_card(14.0, footer_y, 308.0, 32.0);
    window::draw_pango_text_centered(cr, "Open Mistbar Settings...", 14.0, footer_y, 308.0, 32.0, 9.0, true,
                                     palette_.accent, true, false);
    pop_actions_.push_back({14.0, footer_y, 308.0, 32.0, "settings", "liquid-glass"});
    (void)w;
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
        dragging_brightness_ = false;
        dragging_volume_ = false;
        return;
    }

    auto slider_pct = [](double x, double sx, double sw) -> int {
        return std::clamp(static_cast<int>(((x - sx) / sw) * 100.0 + 0.5), 1, 100);
    };

    if (ev.type == wl::PointerEvent::Type::Motion) {
        if (dragging_brightness_) {
            const double sx = (open_popover_ == PopoverKind::ControlCenter) ? 26.0 : 14.0;
            const double sw = (open_popover_ == PopoverKind::ControlCenter) ? 284.0 : (popover_w_ - 28.0);
            cached_brightness_.percent = slider_pct(lx, sx, sw);
            sysinfo::set_brightness_percent(cached_brightness_.percent);
            popover_surface_->request_redraw();
            return;
        }
        if (dragging_volume_) {
            const double sx = (open_popover_ == PopoverKind::ControlCenter) ? 26.0 : 14.0;
            const double sw = (open_popover_ == PopoverKind::ControlCenter) ? 284.0 : (popover_w_ - 28.0);
            cached_audio_.volume_percent = slider_pct(lx, sx, sw);
            sysinfo::set_audio_volume(cached_audio_.volume_percent);
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
        close_popover();
        return;
    }

    if (open_popover_ == PopoverKind::Brightness && ly >= 42.0 && ly <= 72.0 && lx >= 14.0 && lx <= popover_w_ - 14.0) {
        dragging_brightness_ = true;
        cached_brightness_.percent = slider_pct(lx, 14.0, popover_w_ - 28.0);
        sysinfo::set_brightness_percent(cached_brightness_.percent);
        popover_surface_->request_redraw();
        return;
    }
    if (open_popover_ == PopoverKind::Volume && ly >= 40.0 && ly <= 70.0 && lx >= 14.0 && lx <= popover_w_ - 14.0) {
        dragging_volume_ = true;
        cached_audio_.volume_percent = slider_pct(lx, 14.0, popover_w_ - 28.0);
        sysinfo::set_audio_volume(cached_audio_.volume_percent);
        popover_surface_->request_redraw();
        bar_surface_->request_redraw();
        return;
    }
    if (open_popover_ == PopoverKind::ControlCenter) {
        if (lx >= 26.0 && lx <= 310.0 && ly >= 170.0 && ly <= 200.0) {
            dragging_brightness_ = true;
            cached_brightness_.percent = slider_pct(lx, 26.0, 284.0);
            sysinfo::set_brightness_percent(cached_brightness_.percent);
            popover_surface_->request_redraw();
            return;
        }
        if (lx >= 26.0 && lx <= 310.0 && ly >= 242.0 && ly <= 272.0) {
            dragging_volume_ = true;
            cached_audio_.volume_percent = slider_pct(lx, 26.0, 284.0);
            sysinfo::set_audio_volume(cached_audio_.volume_percent);
            popover_surface_->request_redraw();
            bar_surface_->request_redraw();
            return;
        }
    }

    for (const auto& a : pop_actions_) {
        if (lx >= a.x && lx <= a.x + a.w && ly >= a.y && ly <= a.y + a.h) {
            if (a.id == "settings") {
                launch_settings(a.arg);
                close_popover();
            } else if (a.id == "exec") {
                sysinfo::spawn_detached({"bash", "-c", a.arg});
                close_popover();
            } else if (a.id == "exec_keep") {
                sysinfo::spawn_detached({"bash", "-c", a.arg});
                refresh_hardware_quick();
            } else if (a.id == "niri") {
                sysinfo::niri_action(a.arg);
                close_popover();
            } else if (a.id == "wifi_toggle") {
                sysinfo::set_wifi_enabled(!cached_wifi_.enabled);
                refresh_hardware_full();
            } else if (a.id == "wifi_rescan") {
                sysinfo::rescan_wifi();
                refresh_hardware_full();
            } else if (a.id == "wifi_connect") {
                sysinfo::connect_wifi(a.arg);
                close_popover();
            } else if (a.id == "wifi_disconnect") {
                sysinfo::disconnect_wifi(a.arg);
                refresh_hardware_full();
            } else if (a.id == "bt_toggle") {
                sysinfo::set_bluetooth_enabled(!cached_bt_.powered);
                refresh_hardware_full();
            } else if (a.id == "bt_connect") {
                sysinfo::connect_bluetooth_device(a.arg, true);
                close_popover();
            } else if (a.id == "bt_disconnect") {
                sysinfo::connect_bluetooth_device(a.arg, false);
                close_popover();
            } else if (a.id == "mute_toggle") {
                sysinfo::toggle_audio_mute();
                refresh_hardware_quick();
            } else if (a.id == "sink_set") {
                sysinfo::set_default_audio_sink(a.arg);
                refresh_hardware_quick();
            } else if (a.id == "bright_set") {
                sysinfo::set_brightness_percent(std::atoi(a.arg.c_str()));
                refresh_hardware_quick();
            } else if (a.id == "power_profile") {
                sysinfo::set_power_profile(a.arg);
                refresh_hardware_full();
            } else if (a.id == "theme_toggle") {
                cfg_.theme = (cfg_.theme == "light") ? "dark" : "light";
                if (cfg_.glass_preset == "dark" && cfg_.theme == "light") cfg_.glass_preset = "regular";
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
    if (ev.pressed && ev.keysym == XKB_KEY_Escape) {
        close_popover();
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
        return "running (theme: " + cfg_.theme + ", preset: " + cfg_.glass_preset +
               ", look: " + cfg_.look + ", autohide: " + (cfg_.auto_hide ? "on" : "off") +
               ", gpu_glass: " + (backdrop_.using_gpu() ? "on" : "cpu") + ")";
    }
    return "unknown";
}

} // namespace mistbar::bar
