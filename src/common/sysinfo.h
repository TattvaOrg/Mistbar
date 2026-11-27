/* Mistbar - MIT License */

#pragma once

#include <string>
#include <vector>

namespace mistbar::sysinfo {

struct BatteryStatus {
    bool present = false;
    bool charging = false;
    int percent = 100;
    std::string state_text = "Full";
    std::string power_profile = "balanced";
};

struct WifiNetwork {
    bool in_use = false;
    std::string ssid;
    int signal = 0;
    std::string security;
};

struct WifiStatus {
    bool enabled = false;
    bool connected = false;
    std::string ssid;
    std::vector<WifiNetwork> networks;
};

struct BluetoothDevice {
    std::string mac;
    std::string name;
    bool connected = false;
};

struct BluetoothStatus {
    bool available = false;
    bool powered = false;
    std::string connected_name;
    std::vector<BluetoothDevice> devices;
};

struct AudioSink {
    std::string id;
    std::string name;
    bool is_default = false;
};

struct AudioStatus {
    int volume_percent = 50;
    bool muted = false;
    std::vector<AudioSink> sinks;
};

struct BrightnessStatus {
    bool supported = false;
    int percent = 100;
};

struct WorkspaceInfo {
    int id = 0;
    int idx = 1;
    bool focused = false;
    bool has_windows = false;
    std::string output;
};

struct MediaStatus {
    bool active = false;
    bool playing = false;
    std::string title;
};

struct TrayItem {
    std::string service;
    std::string path;
    std::string title;
    std::string icon;
};

struct DesktopAppEntry {
    std::string name;
    std::string category;
    std::string exec_cmd;
};

bool run_exec(const std::vector<std::string>& argv, std::string* stdout_out = nullptr);
bool spawn_detached(const std::vector<std::string>& argv);
std::vector<DesktopAppEntry> list_desktop_apps();

BatteryStatus get_battery_status(bool include_profile = false);
bool set_power_profile(const std::string& profile);

BrightnessStatus get_brightness_status();
bool set_brightness_percent(int percent);
bool adjust_brightness_percent(int delta);

AudioStatus get_audio_status(bool include_sinks = false);
bool set_audio_volume(int percent);
bool adjust_audio_volume(int delta);
bool toggle_audio_mute();
bool set_default_audio_sink(const std::string& id);

WifiStatus get_wifi_status(bool include_networks = false);
bool set_wifi_enabled(bool enabled);
bool rescan_wifi();
bool connect_wifi(const std::string& ssid);
bool disconnect_wifi(const std::string& ssid);

BluetoothStatus get_bluetooth_status(bool include_devices = false);
bool set_bluetooth_enabled(bool enabled);
bool connect_bluetooth_device(const std::string& mac, bool connect);

std::vector<WorkspaceInfo> get_niri_workspaces();
std::string get_niri_focused_app();
bool focus_niri_workspace(int idx);
bool niri_action(const std::string& action);

bool is_mic_active();
MediaStatus get_media_status();
std::vector<TrayItem> get_tray_items();
bool activate_tray_item(const TrayItem& item);

std::string get_cpu_model();
std::string get_ram_total();
std::string get_active_wallpaper();

} // namespace mistbar::sysinfo
