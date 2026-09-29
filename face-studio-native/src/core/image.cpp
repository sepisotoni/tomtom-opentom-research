#include "image.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tt {

Image::Image(int width, int height, Rgb fill) : w(width), h(height), px(static_cast<size_t>(width) * height * 3) {
    for (size_t i = 0; i < px.size(); i += 3) { px[i] = fill.r; px[i + 1] = fill.g; px[i + 2] = fill.b; }
}

void Image::fill_rect(int x0, int y0, int x1, int y1, Rgb c) {
    x0 = std::max(x0, 0); y0 = std::max(y0, 0);
    x1 = std::min(x1, w - 1); y1 = std::min(y1, h - 1);
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) set(x, y, c);
}

Rgb hex_to_rgb(const std::string& hex, Rgb fallback) {
    size_t start = 0;
    while (start < hex.size() && hex[start] == '#') ++start;
    if (hex.size() - start != 6) return fallback;
    unsigned v[3];
    for (int k = 0; k < 3; ++k) {
        unsigned n = 0;
        for (int d = 0; d < 2; ++d) {
            char c = hex[start + k * 2 + d];
            n <<= 4;
            if (c >= '0' && c <= '9') n |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') n |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') n |= static_cast<unsigned>(c - 'A' + 10);
            else return fallback;
        }
        v[k] = n;
    }
    return Rgb{static_cast<uint8_t>(v[0]), static_cast<uint8_t>(v[1]), static_cast<uint8_t>(v[2])};
}

std::string rgb_to_hex(Rgb c) {
    char buf[8];
    std::snprintf(buf, sizeof buf, "#%02X%02X%02X", c.r, c.g, c.b);
    return buf;
}

Image resize_nearest(const Image& src, int w, int h) {
    if (src.w == w && src.h == h) return src;
    Image out(w, h);
    for (int y = 0; y < h; ++y) {
        int sy = std::min(src.h - 1, static_cast<int>((static_cast<int64_t>(y) * 2 + 1) * src.h / (2 * static_cast<int64_t>(h))));
        for (int x = 0; x < w; ++x) {
            int sx = std::min(src.w - 1, static_cast<int>((static_cast<int64_t>(x) * 2 + 1) * src.w / (2 * static_cast<int64_t>(w))));
            out.set(x, y, src.get(sx, sy));
        }
    }
    return out;
}

PremulImage premultiply(const RgbaImage& src) {
    PremulImage out;
    out.w = src.w;
    out.h = src.h;
    out.px.resize(static_cast<size_t>(src.w) * src.h * 4);
    for (size_t i = 0; i < static_cast<size_t>(src.w) * src.h; ++i) {
        float a = src.px[i * 4 + 3] / 255.0f;
        out.px[i * 4 + 0] = src.px[i * 4 + 0] * a;
        out.px[i * 4 + 1] = src.px[i * 4 + 1] * a;
        out.px[i * 4 + 2] = src.px[i * 4 + 2] * a;
        out.px[i * 4 + 3] = static_cast<float>(src.px[i * 4 + 3]);
    }
    return out;
}

namespace {

struct Taps {
    int start = 0;
    std::vector<float> weights;
};

float lanczos3(float x) {
    if (x <= -3.0f || x >= 3.0f) return 0.0f;
    if (x == 0.0f) return 1.0f;
    const float pi = 3.14159265358979f;
    float px = pi * x;
    return 3.0f * std::sin(px) * std::sin(px / 3.0f) / (px * px);
}

std::vector<Taps> make_taps(int in_size, int out_size) {
    std::vector<Taps> taps(static_cast<size_t>(out_size));
    double scale = static_cast<double>(in_size) / out_size;
    double filter_scale = scale < 1.0 ? 1.0 : scale;
    double support = 3.0 * filter_scale;
    for (int o = 0; o < out_size; ++o) {
        double center = (o + 0.5) * scale;
        int lo = static_cast<int>(center - support + 0.5);
        int hi = static_cast<int>(center + support + 0.5);
        lo = std::max(lo, 0);
        hi = std::min(hi, in_size);
        Taps& t = taps[static_cast<size_t>(o)];
        t.start = lo;
        double sum = 0;
        for (int k = lo; k < hi; ++k) {
            float wgt = lanczos3(static_cast<float>((k - center + 0.5) / filter_scale));
            t.weights.push_back(wgt);
            sum += wgt;
        }
        if (sum != 0.0)
            for (float& wgt : t.weights) wgt = static_cast<float>(wgt / sum);
        else if (!t.weights.empty())
            t.weights[0] = 1.0f;
    }
    return taps;
}

}  // namespace

PremulImage resize_lanczos(const RgbaImage& src, int dw, int dh) {
    PremulImage in = premultiply(src);
    std::vector<Taps> tx = make_taps(src.w, dw);
    std::vector<Taps> ty = make_taps(src.h, dh);

    std::vector<float> mid(static_cast<size_t>(dw) * src.h * 4);
    for (int y = 0; y < src.h; ++y) {
        for (int x = 0; x < dw; ++x) {
            const Taps& t = tx[static_cast<size_t>(x)];
            float acc[4] = {0, 0, 0, 0};
            for (size_t k = 0; k < t.weights.size(); ++k) {
                const float* p = &in.px[(static_cast<size_t>(y) * src.w + t.start + k) * 4];
                for (int c = 0; c < 4; ++c) acc[c] += p[c] * t.weights[k];
            }
            float* q = &mid[(static_cast<size_t>(y) * dw + x) * 4];
            for (int c = 0; c < 4; ++c) q[c] = acc[c];
        }
    }
    PremulImage out;
    out.w = dw;
    out.h = dh;
    out.px.resize(static_cast<size_t>(dw) * dh * 4);
    for (int y = 0; y < dh; ++y) {
        const Taps& t = ty[static_cast<size_t>(y)];
        for (int x = 0; x < dw; ++x) {
            float acc[4] = {0, 0, 0, 0};
            for (size_t k = 0; k < t.weights.size(); ++k) {
                const float* p = &mid[(static_cast<size_t>(t.start + k) * dw + x) * 4];
                for (int c = 0; c < 4; ++c) acc[c] += p[c] * t.weights[k];
            }
            float a = std::min(255.0f, std::max(0.0f, acc[3]));
            float* q = &out.px[(static_cast<size_t>(y) * dw + x) * 4];
            for (int c = 0; c < 3; ++c) q[c] = std::min(a, std::max(0.0f, acc[c]));
            q[3] = a;
        }
    }
    return out;
}

void paste_over(Image& dst, const PremulImage& src, int ox, int oy) {
    for (int sy = 0; sy < src.h; ++sy) {
        int dy = sy + oy;
        if (dy < 0 || dy >= dst.h) continue;
        for (int sx = 0; sx < src.w; ++sx) {
            int dx = sx + ox;
            if (dx < 0 || dx >= dst.w) continue;
            const float* p = &src.px[(static_cast<size_t>(sy) * src.w + sx) * 4];
            float inv = 1.0f - p[3] / 255.0f;
            Rgb d = dst.get(dx, dy);
            auto mix = [&](float s, uint8_t back) {
                float v = s + back * inv;
                return static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, v + 0.5f)));
            };
            dst.set(dx, dy, Rgb{mix(p[0], d.r), mix(p[1], d.g), mix(p[2], d.b)});
        }
    }
}

}  // namespace tt
