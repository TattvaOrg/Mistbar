/* Mistbar - MIT License */

#include "glass/blur/gaussian_blur.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace mistbar::glass {

namespace {

constexpr double PI = 3.14159265358979323846;

void downsample_2x(const std::uint8_t* src, int sw, int sh, int sstride,
                   std::vector<std::uint8_t>& dst, int& dw, int& dh) {
    dw = std::max(1, sw / 2);
    dh = std::max(1, sh / 2);
    dst.resize(static_cast<std::size_t>(dw) * static_cast<std::size_t>(dh) * 4U);

    for (int y = 0; y < dh; ++y) {
        const int sy0 = std::min(sh - 1, y * 2);
        const int sy1 = std::min(sh - 1, y * 2 + 1);
        const auto* r0 = src + sy0 * sstride;
        const auto* r1 = src + sy1 * sstride;
        auto* out = dst.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(dw) * 4U;

        for (int x = 0; x < dw; ++x) {
            const int sx0 = std::min(sw - 1, x * 2) * 4;
            const int sx1 = std::min(sw - 1, x * 2 + 1) * 4;
            for (int c = 0; c < 4; ++c) {
                const int sum = r0[sx0 + c] + r0[sx1 + c] + r1[sx0 + c] + r1[sx1 + c];
                out[x * 4 + c] = static_cast<std::uint8_t>((sum + 2) >> 2);
            }
        }
    }
}

void upsample_bilinear(const std::vector<std::uint8_t>& src, int sw, int sh,
                       std::uint8_t* dst, int dw, int dh, int dstride) {
    for (int y = 0; y < dh; ++y) {
        const float fy = (static_cast<float>(y) + 0.5f) * static_cast<float>(sh) / static_cast<float>(dh) - 0.5f;
        const int y0 = std::clamp(static_cast<int>(std::floor(fy)), 0, sh - 1);
        const int y1 = std::min(sh - 1, y0 + 1);
        const float wy1 = std::clamp(fy - static_cast<float>(y0), 0.0f, 1.0f);
        const float wy0 = 1.0f - wy1;

        const auto* row0 = src.data() + static_cast<std::size_t>(y0) * static_cast<std::size_t>(sw) * 4U;
        const auto* row1 = src.data() + static_cast<std::size_t>(y1) * static_cast<std::size_t>(sw) * 4U;
        auto* out = dst + y * dstride;

        for (int x = 0; x < dw; ++x) {
            const float fx = (static_cast<float>(x) + 0.5f) * static_cast<float>(sw) / static_cast<float>(dw) - 0.5f;
            const int x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, sw - 1);
            const int x1 = std::min(sw - 1, x0 + 1);
            const float wx1 = std::clamp(fx - static_cast<float>(x0), 0.0f, 1.0f);
            const float wx0 = 1.0f - wx1;

            const float w00 = wx0 * wy0;
            const float w10 = wx1 * wy0;
            const float w01 = wx0 * wy1;
            const float w11 = wx1 * wy1;

            for (int c = 0; c < 4; ++c) {
                const float val = row0[x0 * 4 + c] * w00 + row0[x1 * 4 + c] * w10 +
                                  row1[x0 * 4 + c] * w01 + row1[x1 * 4 + c] * w11;
                out[x * 4 + c] = static_cast<std::uint8_t>(std::clamp(val + 0.5f, 0.0f, 255.0f));
            }
        }
    }
}

void separable_convolve(const std::uint8_t* src, std::uint8_t* dst,
                        int w, int h, int src_stride, int dst_stride,
                        const std::vector<float>& kernel, int radius) {
    std::vector<float> tmp(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4U, 0.0f);

    for (int y = 0; y < h; ++y) {
        const auto* srow = src + y * src_stride;
        float* trow = tmp.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(w) * 4U;
        for (int x = 0; x < w; ++x) {
            float b = 0.0f, g = 0.0f, r = 0.0f, a = 0.0f;
            for (int k = -radius; k <= radius; ++k) {
                const int sx = std::clamp(x + k, 0, w - 1);
                const float wgt = kernel[static_cast<std::size_t>(k + radius)];
                const auto* px = srow + sx * 4;
                b += static_cast<float>(px[0]) * wgt;
                g += static_cast<float>(px[1]) * wgt;
                r += static_cast<float>(px[2]) * wgt;
                a += static_cast<float>(px[3]) * wgt;
            }
            float* out = trow + x * 4;
            out[0] = b;
            out[1] = g;
            out[2] = r;
            out[3] = a;
        }
    }

    for (int y = 0; y < h; ++y) {
        auto* drow = dst + y * dst_stride;
        for (int x = 0; x < w; ++x) {
            float b = 0.0f, g = 0.0f, r = 0.0f, a = 0.0f;
            for (int k = -radius; k <= radius; ++k) {
                const int sy = std::clamp(y + k, 0, h - 1);
                const float wgt = kernel[static_cast<std::size_t>(k + radius)];
                const float* px = tmp.data() + (static_cast<std::size_t>(sy) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 4U;
                b += px[0] * wgt;
                g += px[1] * wgt;
                r += px[2] * wgt;
                a += px[3] * wgt;
            }
            auto* out = drow + x * 4;
            out[0] = static_cast<std::uint8_t>(std::clamp(b + 0.5f, 0.0f, 255.0f));
            out[1] = static_cast<std::uint8_t>(std::clamp(g + 0.5f, 0.0f, 255.0f));
            out[2] = static_cast<std::uint8_t>(std::clamp(r + 0.5f, 0.0f, 255.0f));
            out[3] = static_cast<std::uint8_t>(std::clamp(a + 0.5f, 0.0f, 255.0f));
        }
    }
}

} // namespace

std::vector<float> build_gaussian_kernel_1d(int radius, double sigma) {
    const int r = std::max(1, radius);
    const double s = std::max(0.5, sigma);
    const int len = 2 * r + 1;
    std::vector<float> kernel(static_cast<std::size_t>(len), 0.0f);

    const double norm_coeff = 1.0 / (std::sqrt(2.0 * PI) * s);
    const double two_sigma_sq = 2.0 * s * s;
    double sum = 0.0;

    for (int i = -r; i <= r; ++i) {
        const double g = norm_coeff * std::exp(-(static_cast<double>(i * i)) / two_sigma_sq);
        kernel[static_cast<std::size_t>(i + r)] = static_cast<float>(g);
        sum += g;
    }

    if (sum > 0.0) {
        for (float& v : kernel) {
            v = static_cast<float>(static_cast<double>(v) / sum);
        }
    }
    return kernel;
}

bool cpu_gaussian_blur(cairo_surface_t* src, cairo_surface_t* dst, int radius) {
    if (!src || !dst) return false;
    if (cairo_surface_get_type(src) != CAIRO_SURFACE_TYPE_IMAGE ||
        cairo_surface_get_type(dst) != CAIRO_SURFACE_TYPE_IMAGE) {
        return false;
    }

    const int w = cairo_image_surface_get_width(src);
    const int h = cairo_image_surface_get_height(src);
    if (w <= 0 || h <= 0 ||
        cairo_image_surface_get_width(dst) != w ||
        cairo_image_surface_get_height(dst) != h) {
        return false;
    }

    cairo_surface_flush(src);
    cairo_surface_flush(dst);

    const std::uint8_t* src_data = cairo_image_surface_get_data(src);
    std::uint8_t* dst_data = cairo_image_surface_get_data(dst);
    const int src_stride = cairo_image_surface_get_stride(src);
    const int dst_stride = cairo_image_surface_get_stride(dst);

    if (radius <= 0) {
        for (int y = 0; y < h; ++y) {
            std::memcpy(dst_data + y * dst_stride, src_data + y * src_stride,
                        static_cast<std::size_t>(w) * 4U);
        }
        cairo_surface_mark_dirty(dst);
        return true;
    }

    if (radius >= 8 && w >= 16 && h >= 16) {
        std::vector<std::uint8_t> small_src;
        int sw = 0, sh = 0;
        downsample_2x(src_data, w, h, src_stride, small_src, sw, sh);

        const int half_r = std::max(1, radius / 2);
        const double sigma = std::max(1.0, static_cast<double>(half_r) * 0.5);
        const auto kernel = build_gaussian_kernel_1d(half_r, sigma);

        std::vector<std::uint8_t> small_dst(small_src.size(), 0);
        separable_convolve(small_src.data(), small_dst.data(), sw, sh, sw * 4, sw * 4, kernel, half_r);
        upsample_bilinear(small_dst, sw, sh, dst_data, w, h, dst_stride);
    } else {
        const double sigma = std::max(0.8, static_cast<double>(radius) * 0.5);
        const auto kernel = build_gaussian_kernel_1d(radius, sigma);
        separable_convolve(src_data, dst_data, w, h, src_stride, dst_stride, kernel, radius);
    }

    cairo_surface_mark_dirty(dst);
    return true;
}

} // namespace mistbar::glass
