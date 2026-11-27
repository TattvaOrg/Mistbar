/* Mistbar - MIT License */

#pragma once

#include <string>

namespace mistbar::log {

enum class Level {
    Debug,
    Info,
    Warn,
    Error
};

void init(const std::string& custom_path = "");
void set_min_level(Level level);
void write(Level level, const std::string& component, const std::string& message);

void debug(const std::string& component, const std::string& message);
void info(const std::string& component, const std::string& message);
void warn(const std::string& component, const std::string& message);
void error(const std::string& component, const std::string& message);

} // namespace mistbar::log
