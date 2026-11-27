/* Mistbar - MIT License */

#include "common/svg_loader.h"
#include "common/logger.h"
#include "common/xdg.h"

#include <filesystem>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <librsvg/rsvg.h>
#include <sstream>

namespace fs = std::filesystem;

namespace mistbar::icons {

namespace {

std::string make_key(const std::string& id, int w, int h, std::uint32_t tint) {
    std::ostringstream ss;
    ss << id << '|' << w << 'x' << h << '|' << std::hex << tint;
    return ss.str();
}

cairo_surface_t* apply_tint(cairo_surface_t* src, int w, int h, std::uint32_t tint_argb) {
    if (!src || tint_argb == 0) {
        return src;
    }
    cairo_surface_t* tinted = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t* cr = cairo_create(tinted);

    const double a = ((tint_argb >> 24) & 0xFF) / 255.0;
    const double r = ((tint_argb >> 16) & 0xFF) / 255.0;
    const double g = ((tint_argb >> 8) & 0xFF) / 255.0;
    const double b = (tint_argb & 0xFF) / 255.0;

    cairo_set_source_rgba(cr, r, g, b, a);
    cairo_mask_surface(cr, src, 0.0, 0.0);
    cairo_destroy(cr);
    cairo_surface_destroy(src);
    return tinted;
}

cairo_surface_t* render_rsvg(RsvgHandle* handle, int width, int height) {
    if (!handle || width <= 0 || height <= 0) {
        return nullptr;
    }
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cairo_t* cr = cairo_create(surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    RsvgRectangle viewport{0.0, 0.0, static_cast<double>(width), static_cast<double>(height)};
    GError* err = nullptr;
    const gboolean ok = rsvg_handle_render_document(handle, cr, &viewport, &err);
    cairo_destroy(cr);

    if (!ok) {
        if (err) {
            log::warn("icons", std::string("librsvg render failed: ") + err->message);
            g_error_free(err);
        }
        cairo_surface_destroy(surface);
        return nullptr;
    }
    return surface;
}

cairo_surface_t* pixbuf_to_cairo(GdkPixbuf* pb) {
    if (!pb) return nullptr;
    const int w = gdk_pixbuf_get_width(pb);
    const int h = gdk_pixbuf_get_height(pb);
    const int n_channels = gdk_pixbuf_get_n_channels(pb);
    const int src_stride = gdk_pixbuf_get_rowstride(pb);
    const guchar* pixels = gdk_pixbuf_get_pixels(pb);

    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_surface_flush(surface);
    unsigned char* dst = cairo_image_surface_get_data(surface);
    const int dst_stride = cairo_image_surface_get_stride(surface);

    for (int y = 0; y < h; ++y) {
        const guchar* srow = pixels + y * src_stride;
        auto* drow = reinterpret_cast<std::uint32_t*>(dst + y * dst_stride);
        for (int x = 0; x < w; ++x) {
            const guchar r = srow[x * n_channels + 0];
            const guchar g = srow[x * n_channels + 1];
            const guchar b = srow[x * n_channels + 2];
            const guchar a = (n_channels >= 4) ? srow[x * n_channels + 3] : 255;
            const std::uint32_t pr = (static_cast<std::uint32_t>(r) * a + 127U) / 255U;
            const std::uint32_t pg = (static_cast<std::uint32_t>(g) * a + 127U) / 255U;
            const std::uint32_t pb_ch = (static_cast<std::uint32_t>(b) * a + 127U) / 255U;
            drow[x] = (static_cast<std::uint32_t>(a) << 24) | (pr << 16) | (pg << 8) | pb_ch;
        }
    }
    cairo_surface_mark_dirty(surface);
    return surface;
}

} // namespace

IconCache::~IconCache() {
    clear();
}

void IconCache::clear() {
    for (auto& [k, surf] : cache_) {
        if (surf) cairo_surface_destroy(surf);
    }
    cache_.clear();
}

cairo_surface_t* IconCache::get(const std::string& file_path,
                                int width,
                                int height,
                                std::uint32_t tint_argb) {
    if (file_path.empty() || width <= 0 || height <= 0) {
        return nullptr;
    }
    const std::string key = make_key(file_path, width, height, tint_argb);
    if (auto it = cache_.find(key); it != cache_.end()) {
        return it->second;
    }

    cairo_surface_t* result = nullptr;
    const bool is_svg = file_path.size() >= 4 &&
                        (file_path.rfind(".svg") == file_path.size() - 4 ||
                         file_path.rfind(".SVG") == file_path.size() - 4);

    if (is_svg) {
        GError* err = nullptr;
        RsvgHandle* handle = rsvg_handle_new_from_file(file_path.c_str(), &err);
        if (handle) {
            result = render_rsvg(handle, width, height);
            g_object_unref(handle);
        } else if (err) {
            log::warn("icons", "Failed loading SVG " + file_path + ": " + err->message);
            g_error_free(err);
        }
    } else {
        GError* err = nullptr;
        GdkPixbuf* pb = gdk_pixbuf_new_from_file_at_scale(file_path.c_str(), width, height, TRUE, &err);
        if (pb) {
            result = pixbuf_to_cairo(pb);
            g_object_unref(pb);
        } else if (err) {
            log::warn("icons", "Failed loading image " + file_path + ": " + err->message);
            g_error_free(err);
        }
    }

    if (result && tint_argb != 0) {
        result = apply_tint(result, width, height, tint_argb);
    }

    if (result) {
        cache_[key] = result;
    }
    return result;
}

cairo_surface_t* IconCache::get_inline_svg(const std::string& cache_key,
                                           const std::string& svg_xml,
                                           int width,
                                           int height,
                                           std::uint32_t tint_argb) {
    if (svg_xml.empty() || width <= 0 || height <= 0) {
        return nullptr;
    }
    const std::string key = make_key("inline:" + cache_key, width, height, tint_argb);
    if (auto it = cache_.find(key); it != cache_.end()) {
        return it->second;
    }

    GError* err = nullptr;
    RsvgHandle* handle = rsvg_handle_new_from_data(
        reinterpret_cast<const guint8*>(svg_xml.data()), svg_xml.size(), &err);
    if (!handle) {
        if (err) {
            log::warn("icons", std::string("Inline SVG parse error: ") + err->message);
            g_error_free(err);
        }
        return nullptr;
    }

    cairo_surface_t* result = render_rsvg(handle, width, height);
    g_object_unref(handle);
    if (result && tint_argb != 0) {
        result = apply_tint(result, width, height, tint_argb);
    }
    if (result) {
        cache_[key] = result;
    }
    return result;
}

bool IconCache::draw(cairo_t* cr,
                     const std::string& file_path,
                     double x,
                     double y,
                     double width,
                     double height,
                     std::uint32_t tint_argb) {
    const int pw = static_cast<int>(width + 0.5);
    const int ph = static_cast<int>(height + 0.5);
    cairo_surface_t* surf = get(file_path, pw, ph, tint_argb);
    if (!surf) {
        return false;
    }
    const int sw = cairo_image_surface_get_width(surf);
    const int sh = cairo_image_surface_get_height(surf);
    const double ox = x + (width - static_cast<double>(sw)) * 0.5;
    const double oy = y + (height - static_cast<double>(sh)) * 0.5;
    cairo_save(cr);
    cairo_set_source_surface(cr, surf, ox, oy);
    cairo_paint(cr);
    cairo_restore(cr);
    return true;
}

std::string resolve_icon(const std::string& icon_rel_path) {
    std::error_code ec;
    std::string p = xdg::resolve_asset_path("icons/" + icon_rel_path);
    if (fs::exists(p, ec)) {
        return p;
    }
    return {};
}

std::string resolve_desktop_icon(const std::string& icon_name) {
    if (icon_name.empty()) {
        return {};
    }
    std::error_code ec;
    if (icon_name.front() == '/' && fs::exists(icon_name, ec)) {
        return icon_name;
    }

    const std::string search_roots[] = {
        xdg::home_dir() + "/.local/share/icons/hicolor/scalable/apps",
        xdg::home_dir() + "/.local/share/icons/hicolor/48x48/apps",
        "/usr/share/icons/hicolor/scalable/apps",
        "/usr/share/icons/hicolor/128x128/apps",
        "/usr/share/icons/hicolor/48x48/apps",
        "/usr/share/pixmaps"
    };
    const char* exts[] = {".svg", ".png", ""};
    for (const auto& root : search_roots) {
        for (const char* ext : exts) {
            const std::string candidate = root + "/" + icon_name + ext;
            if (fs::exists(candidate, ec)) {
                return candidate;
            }
        }
    }
    return {};
}

} // namespace mistbar::icons
