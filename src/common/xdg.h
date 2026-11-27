/* Mistbar - MIT License */

#pragma once

#include <optional>
#include <string>

namespace mistbar::xdg {

std::string config_dir();
std::string data_dir();
std::string runtime_dir();
std::string home_dir();

bool ensure_dir(const std::string& path);
std::optional<std::string> read_text_file(const std::string& path);
bool write_text_file(const std::string& path, const std::string& content);
std::string resolve_asset_path(const std::string& relative_path);

} // namespace mistbar::xdg
