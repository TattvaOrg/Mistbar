/* Mistbar - MIT License */

#pragma once

#include <cairo.h>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace mistbar::icons {

class IconCache {
public:
    IconCache() = default;
    ~IconCache();

    IconCache(const IconCache&) = delete;
    IconCache& operator=(const IconCache&) = delete;

    cairo_surface_t* get(const std::string& file_path,
                         int width,
                         int height,
                         std::uint32_t tint_argb = 0);

    cairo_surface_t* get_inline_svg(const std::string& cache_key,
                                    const std::string& svg_xml,
                                    int width,
                                    int height,
                                    std::uint32_t tint_argb = 0);

    bool draw(cairo_t* cr,
              const std::string& file_path,
              double x,
              double y,
              double width,
              double height,
              std::uint32_t tint_argb = 0);

    void clear();

private:
    std::unordered_map<std::string, cairo_surface_t*> cache_;
};

std::string resolve_icon(const std::string& icon_rel_path);
std::string resolve_desktop_icon(const std::string& icon_name);

} // namespace mistbar::icons
