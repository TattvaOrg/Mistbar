/* Mistbar - MIT License */

#pragma once

#include <cstddef>
#include <cstdint>

namespace mistbar::defaults {

constexpr int BAR_HEIGHT           = 28;
constexpr int BAR_MARGIN           = 6;
constexpr int BAR_SIDE_MARGIN      = 12;
constexpr int BAR_CORNER_RADIUS    = 16;

constexpr int TITLEBAR_HEIGHT      = 36;
constexpr int TRAFFIC_LIGHT_SIZE   = 12;
constexpr int TRAFFIC_LIGHT_GAP    = 8;
constexpr int WINDOW_CORNER_RADIUS = 14;
constexpr int WINDOW_SHADOW_RADIUS = 24;

constexpr int   BLUR_RADIUS            = 20;
constexpr float GLASS_OPACITY          = 0.85f;
constexpr float TINT_OPACITY           = 0.18f;
constexpr float SPECULAR_INTENSITY     = 0.55f;
constexpr float REFRACTION_STRENGTH    = 0.45f;
constexpr float BEZEL_THICKNESS        = 0.35f;
constexpr float CHROMATIC_ABERRATION   = 0.25f;
constexpr float INNER_SHADOW_OPACITY   = 0.18f;

constexpr const char* THEME_DEFAULT    = "dark";
constexpr const char* PRESET_DEFAULT   = "regular";
constexpr const char* LOOK_DEFAULT     = "pill";
constexpr const char* ACCENT_DEFAULT   = "#0A84FF";

constexpr std::size_t LOG_MAX_BYTES    = 5U * 1024U * 1024U;

constexpr std::uint32_t COLOR_CLOSE    = 0xFFFF5F57U;
constexpr std::uint32_t COLOR_MINIMIZE = 0xFFFEBC2EU;
constexpr std::uint32_t COLOR_MAXIMIZE = 0xFF28C840U;
constexpr std::uint32_t COLOR_INACTIVE = 0xFF5A5A5EU;

} // namespace mistbar::defaults
