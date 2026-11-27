/* Mistbar - MIT License */

#include "common/logger.h"
#include "common/defaults.h"
#include "common/xdg.h"

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>

#if MISTBAR_HAS_SYSTEMD
#include <systemd/sd-journal.h>
#include <syslog.h>
#endif

namespace fs = std::filesystem;

namespace mistbar::log {

namespace {
std::mutex g_log_mutex;
std::string g_log_path;
Level g_min_level = Level::Info;
bool g_initialized = false;

std::string current_timestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm tm_now{};
    localtime_r(&now, &tm_now);
    char buf[64]{};
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_now);
    return std::string(buf);
}

const char* level_tag(Level level) {
    switch (level) {
        case Level::Debug: return "DEBUG";
        case Level::Info:  return "INFO";
        case Level::Warn:  return "WARN";
        case Level::Error: return "ERROR";
    }
    return "INFO";
}

void rotate_if_needed() {
    if (g_log_path.empty()) {
        return;
    }
    std::error_code ec;
    if (fs::exists(g_log_path, ec)) {
        const auto size = fs::file_size(g_log_path, ec);
        if (!ec && size >= defaults::LOG_MAX_BYTES) {
            const std::string backup = g_log_path + ".1";
            fs::remove(backup, ec);
            fs::rename(g_log_path, backup, ec);
        }
    }
}
} // namespace

void init(const std::string& custom_path) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (custom_path.empty()) {
        const std::string dir = xdg::data_dir();
        xdg::ensure_dir(dir);
        g_log_path = dir + "/mistbar.log";
    } else {
        fs::path p(custom_path);
        if (p.has_parent_path()) {
            xdg::ensure_dir(p.parent_path().string());
        }
        g_log_path = custom_path;
    }
    g_initialized = true;
}

void set_min_level(Level level) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    g_min_level = level;
}

void write(Level level, const std::string& component, const std::string& message) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (static_cast<int>(level) < static_cast<int>(g_min_level)) {
        return;
    }
    if (!g_initialized) {
        const std::string dir = xdg::data_dir();
        xdg::ensure_dir(dir);
        g_log_path = dir + "/mistbar.log";
        g_initialized = true;
    }

    rotate_if_needed();

    const std::string line = "[" + current_timestamp() + "] [" + level_tag(level) + "] " +
                             component + ": " + message + "\n";

    std::ofstream out(g_log_path, std::ios::out | std::ios::app);
    if (out.is_open()) {
        out.write(line.data(), static_cast<std::streamsize>(line.size()));
    }

#if MISTBAR_HAS_SYSTEMD
    int prio = LOG_INFO;
    if (level == Level::Debug) prio = LOG_DEBUG;
    else if (level == Level::Warn) prio = LOG_WARNING;
    else if (level == Level::Error) prio = LOG_ERR;
    sd_journal_print(prio, "[mistbar:%s] %s", component.c_str(), message.c_str());
#endif
}

void debug(const std::string& component, const std::string& message) {
    write(Level::Debug, component, message);
}

void info(const std::string& component, const std::string& message) {
    write(Level::Info, component, message);
}

void warn(const std::string& component, const std::string& message) {
    write(Level::Warn, component, message);
}

void error(const std::string& component, const std::string& message) {
    write(Level::Error, component, message);
}

} // namespace mistbar::log
