// Simple RGB / RGBA raster helpers used by the canvas, previews and importer.
#pragma once
#include <string>
#include <vector>

#include "codec.h"

namespace tt {

struct Rgb {
    uint8_t r = 0, g = 0, b = 0;
    bool operator==(const Rgb& o) const { return r == o.r && g == o.g && b == o.b; }
    bool operator!=(const Rgb& o) const { return !(*this == o); }
};

// 8-bit RGB raster, row-major, 3 bytes per pixel.
struct Image {
    int w = 0, h = 0;
    Bytes px;

    Image() = default;
    Image(int width, int height, Rgb fill = Rgb{});
    bool in_bounds(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h; }
    Rgb get(int x, int y) const {
        const uint8_t* p = &px[(static_cast<size_t>(y) * w + x) * 3];
        return Rgb{p[0], p[1], p[2]};
    }
    void set(int x, int y, Rgb c) {
        uint8_t* p = &px[(static_cast<size_t>(y) * w + x) * 3];
        p[0] = c.r; p[1] = c.g; p[2] = c.b;
    }
    void fill_rect(int x0, int y0, int x1, int y1, Rgb c);  // inclusive, clipped
    bool operator==(const Image& o) const { return w == o.w && h == o.h && px == o.px; }
};

// Straight-alpha RGBA raster (source for imports).
struct RgbaImage {
    int w = 0, h = 0;
    Bytes px;  // 4 bytes per pixel
};

// Alpha-premultiplied float raster (result of resampling), 4 floats per pixel in 0..255.
struct PremulImage {
    int w = 0, h = 0;
    std::vector<float> px;
};

// "#RRGGBB" (leading '#' characters stripped like Python lstrip("#")); otherwise `fallback`.
Rgb hex_to_rgb(const std::string& hex, Rgb fallback);
std::string rgb_to_hex(Rgb c);  // upper-case "#RRGGBB"

Image resize_nearest(const Image& src, int w, int h);
PremulImage premultiply(const RgbaImage& src);
// Lanczos-3 resampling with Pillow-style support scaling on premultiplied data.
PremulImage resize_lanczos(const RgbaImage& src, int w, int h);
// Composites `src` over `dst` with its top-left corner at (ox, oy); clipped.
void paste_over(Image& dst, const PremulImage& src, int ox, int oy);

}  // namespace tt
