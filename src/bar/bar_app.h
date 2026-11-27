/* Mistbar - MIT License */

#pragma once

#include "animations/transitions/transition.h"
#include "common/config.h"
#include "common/svg_loader.h"
#include "common/sysinfo.h"
#include "common/wayland_client.h"
#include "glass/translucent/glass_panel.h"
#include "themes/theme.h"

#include <cairo.h>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace mistbar::bar {

class BarApp {
public:
    explicit BarApp(wl::Display& display);
    ~BarApp();

    BarApp(const BarApp&) = delete;
    BarApp& operator=(const BarApp&) = delete;

    bool init();
    void reload_config();
    void refresh_clock_only();
    void refresh_clock_and_niri();
    void refresh_hardware_quick();
    void refresh_hardware_full();
    void trigger_async_refresh(bool full = false);
    void apply_worker_updates();
    int wake_fd() const noexcept { return wake_pipe_[0]; }
    bool tick();

    // IPC command handler returning status/reply string.
    std::string handle_ipc(const std::string& cmd, bool& should_quit);

    // Renders bar and optional Control Center preview directly onto a Cairo context (for visual tests).
    void render_bar_preview(cairo_t* cr, int w, int h);
    void render_control_center_preview(cairo_t* cr, int w, int h);

private:
    enum class PopoverKind {
        None,
        LogoMenu,
        AppMenu,
        Wifi,
        Bluetooth,
        Volume,
        Brightness,
        Battery,
        ControlCenter,
        Spotlight,
        ShaderTuner,
        Calendar,
        PowerMenu
    };

    struct SpotlightItem {
        std::string title;
        std::string subtitle;
        std::string action_id;
        std::string arg;
    };

    struct HitRect {
        double x = 0.0;
        double y = 0.0;
        double w = 0.0;
        double h = 0.0;
        std::string id;
        int arg_int = 0;
    };

    struct PopAction {
        double x = 0.0;
        double y = 0.0;
        double w = 0.0;
        double h = 0.0;
        std::string id;
        std::string arg;
        std::string hover_key;

        PopAction() = default;
        PopAction(double px, double py, double pw, double ph,
                  std::string pid, std::string parg, std::string hk = "")
            : x(px), y(py), w(pw), h(ph),
              id(std::move(pid)), arg(std::move(parg)), hover_key(std::move(hk)) {}
    };

    wl::Display& display_;
    config::MistbarConfig cfg_;
    themes::ThemePalette palette_;
    glass::GlassBackdrop backdrop_;
    icons::IconCache icons_;

    std::unique_ptr<wl::LayerSurface> bar_surface_;
    std::unique_ptr<wl::LayerSurface> trigger_surface_;
    std::unique_ptr<wl::LayerSurface> popover_surface_;

    // Bar state
    bool manually_hidden_ = false;
    bool bar_hovered_ = false;
    bool trigger_hovered_ = false;
    bool has_active_windows_ = false;
    bool grace_active_ = false;
    std::chrono::steady_clock::time_point grace_start_{};
    anim::ValueTransition slide_anim_{1.0, 0.20};

    double pointer_norm_x_ = -1.0;
    double pointer_norm_y_ = -1.0;
    std::string hovered_bar_id_;
    std::string hovered_pop_id_;
    std::chrono::steady_clock::time_point last_reactive_draw_{};

    // Cached glass surfaces to avoid GPU glReadPixels on every hover/slider/clock redraw
    cairo_surface_t* bar_glass_cache_ = nullptr;
    int bar_cache_w_ = 0;
    int bar_cache_h_ = 0;
    int bar_cache_ptr_bucket_ = -2;
    std::uint64_t bar_cache_gen_ = 0;

    cairo_surface_t* pop_glass_cache_ = nullptr;
    int pop_cache_w_ = 0;
    int pop_cache_h_ = 0;
    std::uint64_t pop_cache_gen_ = 0;
    std::uint64_t cfg_gen_ = 1;

    // Popover state
    PopoverKind open_popover_ = PopoverKind::None;
    std::string active_menu_name_;
    double popover_anchor_x_ = 0.0;
    int popover_w_ = 240;
    int popover_h_ = 200;
    bool dragging_brightness_ = false;
    bool dragging_volume_ = false;
    int dragging_tuner_idx_ = -1;
    std::chrono::steady_clock::time_point last_hw_cmd_{};

    // Background worker for non-blocking subprocess polling
    struct HwSnapshot {
        bool has_workspaces = false;
        std::vector<sysinfo::WorkspaceInfo> workspaces;
        std::string active_app;
        sysinfo::WifiStatus wifi;
        sysinfo::BluetoothStatus bt;
        sysinfo::AudioStatus audio;
        sysinfo::BrightnessStatus brightness;
        sysinfo::BatteryStatus battery;
        sysinfo::MediaStatus media;
        std::vector<sysinfo::TrayItem> tray;
        bool mic_active = false;
        bool has_full = false;
    };

    int wake_pipe_[2]{-1, -1};
    std::thread worker_thread_;
    std::mutex worker_mu_;
    std::condition_variable worker_cv_;
    bool worker_stop_ = false;
    bool worker_req_ = false;
    bool worker_full_req_ = false;
    bool has_pending_hw_ = false;
    HwSnapshot pending_hw_;

    void start_worker();
    void stop_worker();
    void invalidate_glass_caches();

    // Spotlight state
    std::string spotlight_query_;
    int spotlight_selected_ = 0;
    std::vector<sysinfo::DesktopAppEntry> cached_apps_;
    std::vector<SpotlightItem> spotlight_matches_;

    // Cached system state
    std::string cached_clock_;
    std::string cached_full_date_;
    std::string cached_active_app_;
    std::vector<sysinfo::WorkspaceInfo> cached_workspaces_;
    sysinfo::WifiStatus cached_wifi_;
    sysinfo::BluetoothStatus cached_bt_;
    sysinfo::AudioStatus cached_audio_;
    sysinfo::BrightnessStatus cached_brightness_;
    sysinfo::BatteryStatus cached_battery_;
    sysinfo::MediaStatus cached_media_;
    std::vector<sysinfo::TrayItem> cached_tray_;
    bool cached_mic_active_ = false;

    std::vector<HitRect> bar_hits_;
    std::vector<PopAction> pop_actions_;

    void apply_geometry();
    void update_visibility();
    void close_popover();
    void open_popover(PopoverKind kind, double anchor_x, const std::string& menu_name = "");
    void launch_settings(const std::string& page = "");
    void rebuild_spotlight_matches();

    void draw_bar(cairo_t* cr, int w, int h);
    void draw_bar_foreground(cairo_t* cr, int w, int h);
    void handle_bar_pointer(const wl::PointerEvent& ev);

    void draw_popover(cairo_t* cr, int w, int h);
    void handle_popover_pointer(const wl::PointerEvent& ev);
    void handle_popover_key(const wl::KeyEvent& ev);

    void draw_logo_menu(cairo_t* cr, int w);
    void draw_app_menu(cairo_t* cr, int w);
    void draw_wifi_popover(cairo_t* cr, int w);
    void draw_bluetooth_popover(cairo_t* cr, int w);
    void draw_volume_popover(cairo_t* cr, int w);
    void draw_brightness_popover(cairo_t* cr, int w);
    void draw_battery_popover(cairo_t* cr, int w);
    void draw_control_center(cairo_t* cr, int w);
    void draw_spotlight_popover(cairo_t* cr, int w);
    void draw_shader_tuner_popover(cairo_t* cr, int w);
    void draw_calendar_popover(cairo_t* cr, int w);
    void draw_power_popover(cairo_t* cr, int w);

    void draw_capsule_slider(cairo_t* cr, double x, double y, double w, double h, int pct, const std::string& icon_svg);
    void draw_popover_row(cairo_t* cr, double x, double y, double w, const std::string& label,
                          const std::string& shortcut, const std::string& action_id,
                          const std::string& arg = "", bool bold = false, bool active = false);
    void draw_separator(cairo_t* cr, double x, double y, double w);
    void draw_toggle_pill(cairo_t* cr, double x, double y, bool on);
};

} // namespace mistbar::bar
