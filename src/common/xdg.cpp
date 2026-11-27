/* Mistbar - MIT License */

#include "common/xdg.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>

namespace fs = std::filesystem;

namespace mistbar::xdg {

static std::string get_env_str(const char* name) {
    const char* val = std::getenv(name);
    return val ? std::string(val) : std::string();
}

std::string home_dir() {
    std::string home = get_env_str("HOME");
    if (!home.empty()) {
        return home;
    }
    return "/tmp";
}

std::string config_dir() {
    std::string base = get_env_str("XDG_CONFIG_HOME");
    if (base.empty()) {
        base = home_dir() + "/.config";
    }
    return base + "/mistbar";
}

std::string data_dir() {
    std::string base = get_env_str("XDG_DATA_HOME");
    if (base.empty()) {
        base = home_dir() + "/.local/share";
    }
    return base + "/mistbar";
}

std::string runtime_dir() {
    std::string rt = get_env_str("MISTBAR_RUNTIME_DIR");
    if (!rt.empty()) {
        return rt;
    }
    std::string base = get_env_str("XDG_RUNTIME_DIR");
    if (!base.empty()) {
        return base + "/mistbar";
    }
    return "/tmp/mistbar-" + std::to_string(static_cast<unsigned long>(::getuid()));
}

bool ensure_dir(const std::string& path) {
    std::error_code ec;
    if (fs::exists(path, ec)) {
        return fs::is_directory(path, ec);
    }
    return fs::create_directories(path, ec);
}

std::optional<std::string> read_text_file(const std::string& path) {
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in.is_open()) {
        return std::nullopt;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool write_text_file(const std::string& path, const std::string& content) {
    std::error_code ec;
    fs::path target(path);
    if (target.has_parent_path() && !ensure_dir(target.parent_path().string())) {
        return false;
    }
    const std::string tmp_path = path + ".tmp." + std::to_string(::getpid());
    {
        std::ofstream out(tmp_path, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            return false;
        }
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.flush();
        if (!out.good()) {
            fs::remove(tmp_path, ec);
            return false;
        }
    }
    fs::rename(tmp_path, target, ec);
    if (ec) {
        fs::remove(tmp_path, ec);
        return false;
    }
    return true;
}

std::string resolve_asset_path(const std::string& relative_path) {
    std::error_code ec;
    const std::string candidates[] = {
        data_dir() + "/" + relative_path,
        home_dir() + "/.mistbar/" + relative_path,
        std::string(MISTBAR_INSTALL_PREFIX) + "/share/mistbar/" + relative_path,
        std::string(MISTBAR_SOURCE_DIR) + "/" + relative_path
    };
    for (const auto& candidate : candidates) {
        if (fs::exists(candidate, ec)) {
            return candidate;
        }
    }
    return std::string(MISTBAR_SOURCE_DIR) + "/" + relative_path;
}

} // namespace mistbar::xdg
