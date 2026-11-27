/* Mistbar - MIT License */

#include "common/sysinfo.h"
#include "common/json.h"
#include "common/xdg.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <sstream>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <unordered_set>

namespace fs = std::filesystem;

namespace mistbar::sysinfo {

namespace {

std::string trim(const std::string& s) {
    const auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

long long read_sysfs_ll(const std::string& path, long long fallback = -1) {
    auto txt = xdg::read_text_file(path);
    if (!txt) return fallback;
    char* endptr = nullptr;
    long long val = std::strtoll(txt->c_str(), &endptr, 10);
    if (endptr == txt->c_str()) return fallback;
    return val;
}

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t pos = 0;
    while (pos < text.size()) {
        const auto nl = text.find('\n', pos);
        if (nl == std::string::npos) {
            lines.push_back(text.substr(pos));
            break;
        }
        lines.push_back(text.substr(pos, nl - pos));
        pos = nl + 1;
    }
    return lines;
}

std::vector<std::string> split_char(const std::string& s, char delim) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= s.size()) {
        const auto pos = s.find(delim, start);
        if (pos == std::string::npos) {
            out.push_back(s.substr(start));
            break;
        }
        out.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    return out;
}

} // namespace

bool run_exec(const std::vector<std::string>& argv, std::string* stdout_out) {
    if (argv.empty()) return false;

    int pipefd[2]{-1, -1};
    if (stdout_out) {
        if (::pipe(pipefd) != 0) return false;
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        if (stdout_out) {
            ::close(pipefd[0]);
            ::close(pipefd[1]);
        }
        return false;
    }

    if (pid == 0) {
        if (stdout_out) {
            ::close(pipefd[0]);
            ::dup2(pipefd[1], STDOUT_FILENO);
            ::close(pipefd[1]);
        }
        int devnull = ::open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            ::dup2(devnull, STDERR_FILENO);
            ::close(devnull);
        }
        std::vector<char*> c_args;
        c_args.reserve(argv.size() + 1);
        for (const auto& a : argv) c_args.push_back(const_cast<char*>(a.c_str()));
        c_args.push_back(nullptr);
        ::execvp(c_args[0], c_args.data());
        ::_exit(127);
    }

    if (stdout_out) {
        ::close(pipefd[1]);
        stdout_out->clear();
        char buf[1024];
        while (true) {
            const ssize_t n = ::read(pipefd[0], buf, sizeof(buf));
            if (n <= 0) break;
            stdout_out->append(buf, static_cast<std::size_t>(n));
        }
        ::close(pipefd[0]);
    }

    int status = 0;
    if (::waitpid(pid, &status, 0) < 0) return false;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool spawn_detached(const std::vector<std::string>& argv) {
    if (argv.empty()) return false;
    const pid_t pid = ::fork();
    if (pid < 0) return false;
    if (pid == 0) {
        ::setsid();
        const pid_t inner = ::fork();
        if (inner == 0) {
            int devnull = ::open("/dev/null", O_RDWR);
            if (devnull >= 0) {
                ::dup2(devnull, STDIN_FILENO);
                ::dup2(devnull, STDOUT_FILENO);
                ::dup2(devnull, STDERR_FILENO);
                if (devnull > 2) ::close(devnull);
            }
            std::vector<char*> c_args;
            c_args.reserve(argv.size() + 1);
            for (const auto& a : argv) c_args.push_back(const_cast<char*>(a.c_str()));
            c_args.push_back(nullptr);
            ::execvp(c_args[0], c_args.data());
            ::_exit(127);
        }
        ::_exit(0);
    }
    int status = 0;
    ::waitpid(pid, &status, 0);
    return true;
}

BatteryStatus get_battery_status(bool include_profile) {
    BatteryStatus res;
    std::error_code ec;
    const fs::path root("/sys/class/power_supply");
    if (fs::exists(root, ec)) {
        for (const auto& entry : fs::directory_iterator(root, ec)) {
            const auto type_txt = xdg::read_text_file((entry.path() / "type").string());
            if (!type_txt || trim(*type_txt) != "Battery") continue;
            res.present = true;
            const long long cap = read_sysfs_ll((entry.path() / "capacity").string(), 100);
            res.percent = static_cast<int>(std::clamp<long long>(cap, 0, 100));
            const auto st = xdg::read_text_file((entry.path() / "status").string());
            res.state_text = st ? trim(*st) : "Unknown";
            res.charging = (res.state_text == "Charging");
            break;
        }
    }
    if (include_profile) {
        std::string out;
        if (run_exec({"powerprofilesctl", "get"}, &out)) {
            const std::string p = trim(out);
            if (!p.empty()) res.power_profile = p;
        }
    }
    return res;
}

bool set_power_profile(const std::string& profile) {
    return run_exec({"powerprofilesctl", "set", profile});
}

BrightnessStatus get_brightness_status() {
    BrightnessStatus res;
    std::error_code ec;
    const fs::path root("/sys/class/backlight");
    if (!fs::exists(root, ec)) return res;

    for (const auto& entry : fs::directory_iterator(root, ec)) {
        const long long cur = read_sysfs_ll((entry.path() / "brightness").string(), -1);
        const long long maxv = read_sysfs_ll((entry.path() / "max_brightness").string(), -1);
        if (cur >= 0 && maxv > 0) {
            res.supported = true;
            res.percent = static_cast<int>(std::clamp<long long>((cur * 100LL) / maxv, 1, 100));
            break;
        }
    }
    return res;
}

bool set_brightness_percent(int percent) {
    const int clamped = std::clamp(percent, 5, 100);
    return run_exec({"brightnessctl", "set", std::to_string(clamped) + "%"});
}

bool adjust_brightness_percent(int delta) {
    const auto cur = get_brightness_status();
    return set_brightness_percent(cur.percent + delta);
}

AudioStatus get_audio_status(bool include_sinks) {
    AudioStatus res;
    std::string out;
    if (run_exec({"wpctl", "get-volume", "@DEFAULT_AUDIO_SINK@"}, &out)) {
        res.muted = (out.find("MUTED") != std::string::npos);
        const auto pos = out.find(':');
        if (pos != std::string::npos) {
            const double v = std::strtod(out.c_str() + pos + 1, nullptr);
            res.volume_percent = static_cast<int>(std::clamp(v * 100.0 + 0.5, 0.0, 150.0));
        }
    } else if (run_exec({"pactl", "get-sink-volume", "@DEFAULT_SINK@"}, &out)) {
        const auto pct_pos = out.find('%');
        if (pct_pos != std::string::npos) {
            auto start = pct_pos;
            while (start > 0 && std::isdigit(static_cast<unsigned char>(out[start - 1]))) {
                --start;
            }
            res.volume_percent = std::atoi(out.substr(start, pct_pos - start).c_str());
        }
    }

    if (include_sinks) {
        std::string status_out;
        if (run_exec({"wpctl", "status"}, &status_out)) {
            bool in_sinks = false;
            for (const auto& line : split_lines(status_out)) {
                if (line.find("Sinks:") != std::string::npos) {
                    in_sinks = true;
                    continue;
                }
                if (in_sinks) {
                    const std::string t = trim(line);
                    if (t.empty() || t.find("Sources:") != std::string::npos ||
                        t.find("Filters:") != std::string::npos ||
                        t.find("Streams:") != std::string::npos) {
                        break;
                    }
                    const bool is_def = (line.find('*') != std::string::npos);
                    const auto dot = line.find('.');
                    if (dot == std::string::npos) continue;
                    std::size_t id_start = dot;
                    while (id_start > 0 && std::isdigit(static_cast<unsigned char>(line[id_start - 1]))) {
                        --id_start;
                    }
                    if (id_start == dot) continue;
                    const std::string id = line.substr(id_start, dot - id_start);
                    std::string name = trim(line.substr(dot + 1));
                    const auto br = name.find('[');
                    if (br != std::string::npos) {
                        name = trim(name.substr(0, br));
                    }
                    if (!id.empty() && !name.empty()) {
                        res.sinks.push_back({id, name, is_def});
                    }
                }
            }
        }
    }

    return res;
}

bool set_audio_volume(int percent) {
    const int clamped = std::clamp(percent, 0, 100);
    if (run_exec({"wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", std::to_string(clamped) + "%"})) {
        return true;
    }
    return run_exec({"pactl", "set-sink-volume", "@DEFAULT_SINK@", std::to_string(clamped) + "%"});
}

bool adjust_audio_volume(int delta) {
    const auto cur = get_audio_status(false);
    return set_audio_volume(cur.volume_percent + delta);
}

bool toggle_audio_mute() {
    if (run_exec({"wpctl", "set-mute", "@DEFAULT_AUDIO_SINK@", "toggle"})) {
        return true;
    }
    return run_exec({"pactl", "set-sink-mute", "@DEFAULT_SINK@", "toggle"});
}

bool set_default_audio_sink(const std::string& id) {
    return run_exec({"wpctl", "set-default", id});
}

WifiStatus get_wifi_status(bool include_networks) {
    WifiStatus res;
    std::string out;
    if (run_exec({"nmcli", "radio", "wifi"}, &out)) {
        res.enabled = (trim(out) == "enabled");
    }
    if (res.enabled && run_exec({"nmcli", "-t", "-f", "ACTIVE,SSID", "dev", "wifi"}, &out)) {
        for (const auto& line : split_lines(out)) {
            if (line.rfind("yes:", 0) == 0) {
                res.connected = true;
                res.ssid = line.substr(4);
                break;
            }
        }
    }
    if (res.enabled && include_networks &&
        run_exec({"nmcli", "-t", "-f", "IN-USE,SSID,SIGNAL,SECURITY", "dev", "wifi", "list"}, &out)) {
        std::unordered_set<std::string> seen;
        for (const auto& line : split_lines(out)) {
            if (trim(line).empty()) continue;
            const auto parts = split_char(line, ':');
            if (parts.size() < 2) continue;
            const bool in_use = (trim(parts[0]) == "*");
            const std::string ssid = trim(parts[1]);
            if (ssid.empty() || ssid == "--" || seen.count(ssid)) continue;
            seen.insert(ssid);
            const int signal = (parts.size() > 2 && !trim(parts[2]).empty()) ? std::atoi(parts[2].c_str()) : 0;
            const std::string sec = (parts.size() > 3) ? trim(parts[3]) : "";
            res.networks.push_back({in_use, ssid, signal, sec});
            if (res.networks.size() >= 7) break;
        }
    }
    return res;
}

bool set_wifi_enabled(bool enabled) {
    return run_exec({"nmcli", "radio", "wifi", enabled ? "on" : "off"});
}

bool rescan_wifi() {
    return spawn_detached({"nmcli", "dev", "wifi", "rescan"});
}

bool connect_wifi(const std::string& ssid) {
    if (ssid.empty()) return false;
    std::string conns;
    if (run_exec({"nmcli", "-t", "-f", "NAME", "connection", "show"}, &conns)) {
        for (const auto& line : split_lines(conns)) {
            if (trim(line) == ssid) {
                return spawn_detached({"nmcli", "connection", "up", ssid});
            }
        }
    }
    return spawn_detached({"nmcli", "dev", "wifi", "connect", ssid});
}

bool disconnect_wifi(const std::string& ssid) {
    if (!ssid.empty()) {
        return run_exec({"nmcli", "connection", "down", ssid});
    }
    return false;
}

BluetoothStatus get_bluetooth_status(bool include_devices) {
    BluetoothStatus res;
    std::string out;
    if (run_exec({"bluetoothctl", "show"}, &out)) {
        res.available = (out.find("Controller ") != std::string::npos);
        res.powered = (out.find("Powered: yes") != std::string::npos);
    }
    if (res.powered) {
        std::string conn_out;
        std::unordered_set<std::string> conn_macs;
        if (run_exec({"bluetoothctl", "devices", "Connected"}, &conn_out)) {
            for (const auto& line : split_lines(conn_out)) {
                if (line.rfind("Device ", 0) == 0) {
                    const auto sp = line.find(' ', 7);
                    if (sp != std::string::npos) {
                        const std::string mac = line.substr(7, sp - 7);
                        const std::string name = trim(line.substr(sp + 1));
                        conn_macs.insert(mac);
                        if (res.connected_name.empty()) res.connected_name = name;
                    }
                }
            }
        }
        if (include_devices) {
            std::string dev_out;
            if (run_exec({"bluetoothctl", "devices"}, &dev_out)) {
                for (const auto& line : split_lines(dev_out)) {
                    if (line.rfind("Device ", 0) == 0) {
                        const auto sp = line.find(' ', 7);
                        if (sp != std::string::npos) {
                            const std::string mac = line.substr(7, sp - 7);
                            const std::string name = trim(line.substr(sp + 1));
                            res.devices.push_back({mac, name, conn_macs.count(mac) > 0});
                        }
                    }
                }
            }
        }
    }
    return res;
}

bool set_bluetooth_enabled(bool enabled) {
    return run_exec({"bluetoothctl", "power", enabled ? "on" : "off"});
}

bool connect_bluetooth_device(const std::string& mac, bool connect) {
    return spawn_detached({"bluetoothctl", connect ? "connect" : "disconnect", mac});
}

std::vector<WorkspaceInfo> get_niri_workspaces() {
    std::vector<WorkspaceInfo> list;
    std::string out;
    if (!run_exec({"niri", "msg", "--json", "workspaces"}, &out)) {
        return list;
    }
    const auto parsed = json::parse(out);
    if (!parsed || !parsed->is_array()) {
        return list;
    }
    for (const auto& item : parsed->as_array()) {
        WorkspaceInfo ws;
        ws.id = item.get_int("id", 0);
        ws.idx = item.get_int("idx", 1);
        ws.focused = item.get_bool("is_focused", false);
        ws.output = item.get_string("output", "");
        const auto* act_win = item.find("active_window_id");
        ws.has_windows = (act_win != nullptr && !act_win->is_null());
        list.push_back(std::move(ws));
    }
    std::sort(list.begin(), list.end(), [](const WorkspaceInfo& a, const WorkspaceInfo& b) {
        return a.idx < b.idx;
    });
    return list;
}

std::string get_niri_focused_app() {
    std::string out;
    if (!run_exec({"niri", "msg", "--json", "focused-window"}, &out)) {
        return {};
    }
    const auto parsed = json::parse(out);
    if (!parsed || !parsed->is_object()) {
        return {};
    }
    const std::string app_id = parsed->get_string("app_id", "");
    if (app_id.empty()) return {};
    const auto dot = app_id.rfind('.');
    std::string base = (dot != std::string::npos) ? app_id.substr(dot + 1) : app_id;
    if (!base.empty()) {
        base[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(base[0])));
    }
    return base;
}

bool focus_niri_workspace(int idx) {
    return spawn_detached({"niri", "msg", "action", "focus-workspace", std::to_string(idx)});
}

bool niri_action(const std::string& action) {
    return spawn_detached({"niri", "msg", "action", action});
}

bool is_mic_active() {
    std::string out;
    if (run_exec({"pactl", "list", "source-outputs"}, &out)) {
        return out.find("state: RUNNING") != std::string::npos;
    }
    return false;
}

MediaStatus get_media_status() {
    MediaStatus st;
    std::string status_out;
    if (!run_exec({"playerctl", "status"}, &status_out)) {
        return st;
    }
    const std::string s = trim(status_out);
    if (s == "Playing" || s == "Paused") {
        st.active = true;
        st.playing = (s == "Playing");
        std::string meta;
        if (run_exec({"playerctl", "metadata", "--format", "{{title}} - {{artist}}"}, &meta)) {
            st.title = trim(meta);
        }
        if (st.title.empty()) st.title = "Media Playing";
    }
    return st;
}

std::vector<TrayItem> get_tray_items() {
    std::vector<TrayItem> items;
    std::string out;
    if (!run_exec({"gdbus", "call", "--session",
                   "--dest", "org.kde.StatusNotifierWatcher",
                   "--object-path", "/StatusNotifierWatcher",
                   "--method", "org.freedesktop.DBus.Properties.Get",
                   "org.kde.StatusNotifierWatcher", "RegisteredStatusNotifierItems"}, &out)) {
        return items;
    }
    const auto lb = out.find('[');
    const auto rb = out.find(']');
    if (lb == std::string::npos || rb == std::string::npos || rb <= lb + 1) {
        return items;
    }
    const std::string content = out.substr(lb + 1, rb - lb - 1);
    for (auto entry : split_char(content, ',')) {
        std::string clean;
        for (char c : trim(entry)) {
            if (c != '\'' && c != '"') clean.push_back(c);
        }
        if (clean.empty()) continue;
        std::string service = clean;
        std::string path = "/StatusNotifierItem";
        const auto slash = clean.find('/');
        if (slash != std::string::npos) {
            service = clean.substr(0, slash);
            path = clean.substr(slash);
        }
        auto read_prop = [&](const char* prop) -> std::string {
            std::string pout;
            if (!run_exec({"gdbus", "call", "--session",
                           "--dest", service, "--object-path", path,
                           "--method", "org.freedesktop.DBus.Properties.Get",
                           "org.kde.StatusNotifierItem", prop}, &pout)) {
                return {};
            }
            std::string res;
            for (char c : pout) {
                if (c != '(' && c != ')' && c != '<' && c != '>' && c != '\'' && c != '"' && c != ',') {
                    res.push_back(c);
                }
            }
            return trim(res);
        };
        TrayItem item;
        item.service = service;
        item.path = path;
        item.icon = read_prop("IconName");
        item.title = read_prop("Title");
        if (item.title.empty()) item.title = service;
        items.push_back(std::move(item));
        if (items.size() >= 6) break;
    }
    return items;
}

bool activate_tray_item(const TrayItem& item) {
    return spawn_detached({"gdbus", "call", "--session",
                           "--dest", item.service,
                           "--object-path", item.path,
                           "--method", "org.kde.StatusNotifierItem.Activate",
                           "0", "0"});
}

std::string get_cpu_model() {
    auto txt = xdg::read_text_file("/proc/cpuinfo");
    if (!txt) return "Linux CPU";
    for (const auto& line : split_lines(*txt)) {
        if (line.rfind("model name", 0) == 0) {
            const auto colon = line.find(':');
            if (colon != std::string::npos) return trim(line.substr(colon + 1));
        }
    }
    return "Linux CPU";
}

std::string get_ram_total() {
    auto txt = xdg::read_text_file("/proc/meminfo");
    if (!txt) return "Unknown";
    std::istringstream iss(*txt);
    std::string key;
    long kb = 0;
    while (iss >> key >> kb) {
        if (key == "MemTotal:") {
            const double gb = static_cast<double>(kb) / (1024.0 * 1024.0);
            char buf[32]{};
            std::snprintf(buf, sizeof(buf), "%.1f GB", gb);
            return std::string(buf);
        }
    }
    return "Unknown";
}

std::string get_active_wallpaper() {
    std::string out;
    if (run_exec({"swww", "query"}, &out)) {
        const auto pos = out.find("currently displaying: image: ");
        if (pos != std::string::npos) {
            std::string path = trim(out.substr(pos + 29));
            if (fs::exists(path)) return path;
        }
    }
    return {};
}

namespace {

std::string strip_desktop_exec_codes(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '%' && i + 1 < raw.size()) {
            ++i;
            continue;
        }
        out.push_back(raw[i]);
    }
    return trim(out);
}

} // namespace

std::vector<DesktopAppEntry> list_desktop_apps() {
    std::vector<DesktopAppEntry> apps;
    std::unordered_set<std::string> seen;
    std::vector<fs::path> dirs;
    if (const char* home = std::getenv("HOME")) {
        dirs.push_back(fs::path(home) / ".local/share/applications");
    }
    dirs.emplace_back("/usr/local/share/applications");
    dirs.emplace_back("/usr/share/applications");

    std::error_code ec;
    for (const auto& dir : dirs) {
        if (!fs::exists(dir, ec)) continue;
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            if (entry.path().extension() != ".desktop") continue;
            const auto txt = xdg::read_text_file(entry.path().string());
            if (!txt) continue;

            bool in_desktop_entry = false;
            bool no_display = false;
            bool hidden = false;
            std::string name;
            std::string exec_cmd;
            std::string category = "Application";

            for (const auto& line : split_lines(*txt)) {
                const std::string t = trim(line);
                if (t.empty() || t[0] == '#') continue;
                if (t.front() == '[' && t.back() == ']') {
                    in_desktop_entry = (t == "[Desktop Entry]");
                    continue;
                }
                if (!in_desktop_entry) continue;
                const auto eq = t.find('=');
                if (eq == std::string::npos) continue;
                const std::string key = trim(t.substr(0, eq));
                const std::string val = trim(t.substr(eq + 1));
                if (key == "Name" && name.empty()) name = val;
                else if (key == "Exec" && exec_cmd.empty()) exec_cmd = strip_desktop_exec_codes(val);
                else if (key == "Categories") {
                    const auto semi = val.find(';');
                    const std::string first_cat = trim(semi == std::string::npos ? val : val.substr(0, semi));
                    if (!first_cat.empty()) category = first_cat;
                } else if (key == "NoDisplay" && (val == "true" || val == "1")) {
                    no_display = true;
                } else if (key == "Hidden" && (val == "true" || val == "1")) {
                    hidden = true;
                }
            }

            if (no_display || hidden || name.empty() || exec_cmd.empty()) continue;
            if (seen.count(name)) continue;
            seen.insert(name);
            apps.push_back({std::move(name), std::move(category), std::move(exec_cmd)});
        }
    }

    std::sort(apps.begin(), apps.end(), [](const DesktopAppEntry& a, const DesktopAppEntry& b) {
        return a.name < b.name;
    });
    return apps;
}

} // namespace mistbar::sysinfo
