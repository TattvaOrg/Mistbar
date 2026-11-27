/* Mistbar - MIT License */

#include "common/config.h"
#include "common/defaults.h"
#include "common/json.h"
#include "common/xdg.h"

#include <algorithm>

namespace mistbar::config {

std::string config_file_path() {
    return xdg::config_dir() + "/config.json";
}

void apply_glass_preset(MistbarConfig& cfg, const std::string& preset_name) {
    if (preset_name == "frosted") {
        cfg.glass_preset         = "frosted";
        cfg.blur_radius          = 30;
        cfg.refraction_strength  = 0.45f;
        cfg.bezel_thickness      = 0.35f;
        cfg.chromatic_aberration = 0.34f;
        cfg.specular_intensity   = 0.45f;
        cfg.tint_opacity         = 0.20f;
        cfg.glass_opacity        = 0.90f;
    } else if (preset_name == "smoked") {
        cfg.glass_preset         = "smoked";
        cfg.theme                = "dark";
        cfg.blur_radius          = 20;
        cfg.refraction_strength  = 0.52f;
        cfg.bezel_thickness      = 0.38f;
        cfg.chromatic_aberration = 0.44f;
        cfg.specular_intensity   = 0.46f;
        cfg.tint_opacity         = 0.30f;
        cfg.glass_opacity        = 0.88f;
    } else if (preset_name == "aqua") {
        cfg.glass_preset         = "aqua";
        cfg.blur_radius          = 14;
        cfg.refraction_strength  = 0.62f;
        cfg.bezel_thickness      = 0.42f;
        cfg.chromatic_aberration = 0.54f;
        cfg.specular_intensity   = 0.54f;
        cfg.tint_opacity         = 0.14f;
        cfg.glass_opacity        = 0.84f;
    } else if (preset_name == "dark") {
        cfg.glass_preset         = "dark";
        cfg.theme                = "dark";
        cfg.blur_radius          = 22;
        cfg.refraction_strength  = 0.48f;
        cfg.bezel_thickness      = 0.38f;
        cfg.chromatic_aberration = 0.28f;
        cfg.specular_intensity   = 0.62f;
        cfg.tint_opacity         = 0.26f;
        cfg.glass_opacity        = 0.88f;
    } else if (preset_name == "clear" || preset_name == "transparent") {
        cfg.glass_preset         = "clear";
        cfg.blur_radius          = 0;
        cfg.refraction_strength  = 0.65f;
        cfg.bezel_thickness      = 0.42f;
        cfg.chromatic_aberration = 0.46f;
        cfg.specular_intensity   = 0.50f;
        cfg.tint_opacity         = 0.04f;
        cfg.glass_opacity        = 0.82f;
    } else if (preset_name == "solid") {
        cfg.glass_preset         = "solid";
        cfg.blur_radius          = 0;
        cfg.refraction_strength  = 0.0f;
        cfg.bezel_thickness      = 0.15f;
        cfg.chromatic_aberration = 0.0f;
        cfg.specular_intensity   = 0.18f;
        cfg.tint_opacity         = 1.0f;
        cfg.glass_opacity        = 1.0f;
    } else {
        cfg.glass_preset         = "regular";
        cfg.blur_radius          = defaults::BLUR_RADIUS;
        cfg.refraction_strength  = defaults::REFRACTION_STRENGTH;
        cfg.bezel_thickness      = defaults::BEZEL_THICKNESS;
        cfg.chromatic_aberration = defaults::CHROMATIC_ABERRATION;
        cfg.specular_intensity   = defaults::SPECULAR_INTENSITY;
        cfg.tint_opacity         = defaults::TINT_OPACITY;
        cfg.glass_opacity        = defaults::GLASS_OPACITY;
    }
}

void ensure_default_config() {
    const std::string path = config_file_path();
    if (!xdg::read_text_file(path).has_value()) {
        save_config(MistbarConfig{});
    }
}

MistbarConfig load_config() {
    MistbarConfig cfg;
    const auto txt = xdg::read_text_file(config_file_path());
    if (!txt) {
        return cfg;
    }
    const auto parsed = json::parse(*txt);
    if (!parsed || !parsed->is_object()) {
        return cfg;
    }

    const auto& j = *parsed;
    cfg.theme = j.get_string("theme", cfg.theme);
    if (cfg.theme != "light" && cfg.theme != "dark") {
        cfg.theme = "dark";
    }

    // Backwards-compatible mapping from legacy "style" key
    std::string preset = j.get_string("glassPreset", "");
    if (preset.empty()) {
        const std::string legacy_style = j.get_string("style", "glassy");
        if (legacy_style == "transparent") preset = "clear";
        else if (legacy_style == "solid") preset = "solid";
        else preset = "regular";
    }
    cfg.glass_preset = preset;

    std::string sh = j.get_string("shaderStyle", cfg.shader_style);
    if (sh == "overshifted" || sh == "liquidglass" || sh == "liquid-glass") {
        cfg.shader_style = "overshifted";
    } else {
        cfg.shader_style = "mizzle";
    }

    cfg.look = j.get_string("look", cfg.look);
    if (cfg.look != "pill" && cfg.look != "attached") {
        cfg.look = "attached";
    }

    cfg.icon_style = j.get_string("iconStyle", cfg.icon_style);
    if (cfg.icon_style != "default" && cfg.icon_style != "dark" &&
        cfg.icon_style != "clear" && cfg.icon_style != "tinted") {
        cfg.icon_style = "default";
    }

    cfg.accent_color      = j.get_string("accentColor", cfg.accent_color);
    cfg.glassy_text_color = j.get_string("glassyTextColor", cfg.glassy_text_color);
    cfg.wallpaper         = j.get_string("wallpaper", cfg.wallpaper);
    cfg.auto_hide         = j.get_bool("autoHide", cfg.auto_hide);

    cfg.bar_height    = std::clamp(j.get_int("barHeight", cfg.bar_height), 22, 44);
    cfg.bar_margin    = std::clamp(j.get_int("barMargin", cfg.bar_margin), 0, 24);
    cfg.border_radius = std::clamp(j.get_int("borderRadius", cfg.border_radius), 0, 24);

    cfg.blur_radius          = std::clamp(j.get_int("blurRadius", cfg.blur_radius), 0, 48);
    cfg.refraction_strength  = static_cast<float>(std::clamp(j.get_double("refractionStrength", cfg.refraction_strength), 0.0, 1.0));
    cfg.bezel_thickness      = static_cast<float>(std::clamp(j.get_double("bezelThickness", cfg.bezel_thickness), 0.0, 1.0));
    cfg.chromatic_aberration = static_cast<float>(std::clamp(j.get_double("chromaticAberration", cfg.chromatic_aberration), 0.0, 1.0));
    cfg.specular_intensity   = static_cast<float>(std::clamp(j.get_double("specularIntensity", cfg.specular_intensity), 0.0, 1.0));
    cfg.tint_opacity         = static_cast<float>(std::clamp(j.get_double("tintOpacity", cfg.tint_opacity), 0.0, 1.0));
    cfg.glass_opacity        = static_cast<float>(std::clamp(j.get_double("glassOpacity", cfg.glass_opacity), 0.05, 1.0));
    cfg.cursor_reactive      = j.get_bool("cursorReactive", cfg.cursor_reactive);
    cfg.gpu_acceleration     = j.get_bool("gpuAcceleration", cfg.gpu_acceleration);

    cfg.show_app_menu       = j.get_bool("showAppMenu", cfg.show_app_menu);
    cfg.show_workspaces     = j.get_bool("showWorkspaces", cfg.show_workspaces);
    cfg.show_privacy        = j.get_bool("showPrivacy", cfg.show_privacy);
    cfg.show_tray           = j.get_bool("showTray", cfg.show_tray);
    cfg.show_battery        = j.get_bool("showBattery", cfg.show_battery);
    cfg.show_battery_pct    = j.get_bool("showBatteryPercent", cfg.show_battery_pct);
    cfg.show_brightness     = j.get_bool("showBrightness", cfg.show_brightness);
    cfg.show_volume         = j.get_bool("showVolume", cfg.show_volume);
    cfg.show_network        = j.get_bool("showNetwork", cfg.show_network);
    cfg.show_bluetooth      = j.get_bool("showBluetooth", cfg.show_bluetooth);
    cfg.show_spotlight      = j.get_bool("showSpotlight", cfg.show_spotlight);
    cfg.show_control_center = j.get_bool("showControlCenter", cfg.show_control_center);
    cfg.show_power_menu     = j.get_bool("showPowerMenu", cfg.show_power_menu);
    cfg.cc_airdrop_on       = j.get_bool("ccAirdropOn", cfg.cc_airdrop_on);
    cfg.cc_focus_on         = j.get_bool("ccFocusOn", cfg.cc_focus_on);

    cfg.clock_format = j.get_string("clockFormat", cfg.clock_format);
    if (cfg.clock_format != "12h" && cfg.clock_format != "24h") {
        cfg.clock_format = "12h";
    }
    cfg.show_seconds = j.get_bool("showSeconds", cfg.show_seconds);

    return cfg;
}

bool save_config(const MistbarConfig& cfg) {
    json::Object obj;
    obj["theme"]               = cfg.theme;
    obj["glassPreset"]         = cfg.glass_preset;
    obj["shaderStyle"]         = cfg.shader_style;
    obj["look"]                = cfg.look;
    obj["iconStyle"]           = cfg.icon_style;
    obj["accentColor"]         = cfg.accent_color;
    obj["glassyTextColor"]     = cfg.glassy_text_color;
    obj["wallpaper"]           = cfg.wallpaper;
    obj["autoHide"]            = cfg.auto_hide;
    obj["barHeight"]           = cfg.bar_height;
    obj["barMargin"]           = cfg.bar_margin;
    obj["borderRadius"]        = cfg.border_radius;
    obj["blurRadius"]          = cfg.blur_radius;
    obj["refractionStrength"]  = static_cast<double>(cfg.refraction_strength);
    obj["bezelThickness"]      = static_cast<double>(cfg.bezel_thickness);
    obj["chromaticAberration"] = static_cast<double>(cfg.chromatic_aberration);
    obj["specularIntensity"]   = static_cast<double>(cfg.specular_intensity);
    obj["tintOpacity"]         = static_cast<double>(cfg.tint_opacity);
    obj["glassOpacity"]        = static_cast<double>(cfg.glass_opacity);
    obj["cursorReactive"]      = cfg.cursor_reactive;
    obj["gpuAcceleration"]     = cfg.gpu_acceleration;
    obj["showAppMenu"]         = cfg.show_app_menu;
    obj["showWorkspaces"]      = cfg.show_workspaces;
    obj["showPrivacy"]         = cfg.show_privacy;
    obj["showTray"]            = cfg.show_tray;
    obj["showBattery"]         = cfg.show_battery;
    obj["showBatteryPercent"]  = cfg.show_battery_pct;
    obj["showBrightness"]      = cfg.show_brightness;
    obj["showVolume"]          = cfg.show_volume;
    obj["showNetwork"]         = cfg.show_network;
    obj["showBluetooth"]       = cfg.show_bluetooth;
    obj["showSpotlight"]       = cfg.show_spotlight;
    obj["showControlCenter"]   = cfg.show_control_center;
    obj["showPowerMenu"]       = cfg.show_power_menu;
    obj["ccAirdropOn"]         = cfg.cc_airdrop_on;
    obj["ccFocusOn"]           = cfg.cc_focus_on;
    obj["clockFormat"]         = cfg.clock_format;
    obj["showSeconds"]         = cfg.show_seconds;
    return xdg::write_text_file(config_file_path(), json::Value(std::move(obj)).dump(2));
}

} // namespace mistbar::config
