#include "png.h"

#include <cstdlib>
#include <cstring>

#include "deflate.h"

namespace tt {

namespace {
const uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

inline uint32_t be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
inline void put_be32(Bytes& b, uint32_t v) {
    b.push_back(uint8_t(v >> 24)); b.push_back(uint8_t(v >> 16)); b.push_back(uint8_t(v >> 8)); b.push_back(uint8_t(v));
}

void put_chunk(Bytes& out, const char* type, const Bytes& body) {
    put_be32(out, static_cast<uint32_t>(body.size()));
    size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), body.begin(), body.end());
    put_be32(out, crc32(out.data() + start, out.size() - start));
}

int paeth(int a, int b, int c) {
    int p = a + b - c;
    int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return a;
    return pb <= pc ? b : c;
}
}  // namespace

Bytes png_encode(const Image& img) {
    Bytes out(kSignature, kSignature + 8);
    Bytes ihdr;
    put_be32(ihdr, static_cast<uint32_t>(img.w));
    put_be32(ihdr, static_cast<uint32_t>(img.h));
    ihdr.push_back(8);  // bit depth
    ihdr.push_back(2);  // truecolour
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    put_chunk(out, "IHDR", ihdr);
    Bytes raw;
    raw.reserve((static_cast<size_t>(img.w) * 3 + 1) * img.h);
    for (int y = 0; y < img.h; ++y) {
        raw.push_back(0);  // filter: None
        raw.insert(raw.end(), img.px.begin() + static_cast<std::ptrdiff_t>(y) * img.w * 3,
                   img.px.begin() + static_cast<std::ptrdiff_t>(y + 1) * img.w * 3);
    }
    put_chunk(out, "IDAT", zlib_compress(raw.data(), raw.size()));
    put_chunk(out, "IEND", Bytes());
    return out;
}

bool png_dimensions(const Bytes& d, int& width, int& height) {
    if (d.size() < 33 || std::memcmp(d.data(), kSignature, 8) != 0) return false;
    if (be32(&d[8]) != 13 || std::memcmp(&d[12], "IHDR", 4) != 0) return false;
    uint32_t w = be32(&d[16]), h = be32(&d[20]);
    if (w == 0 || h == 0 || w > 0x7FFFFFFF || h > 0x7FFFFFFF) return false;
    width = static_cast<int>(w);
    height = static_cast<int>(h);
    return true;
}

bool png_decode(const Bytes& d, Image& out, std::string& error) {
    int w = 0, h = 0;
    if (!png_dimensions(d, w, h)) { error = "Not a valid PNG image"; return false; }
    if (static_cast<uint64_t>(w) * h > (1u << 26)) { error = "PNG image is too large"; return false; }
    int depth = d[24], ctype = d[25], interlace = d[28];
    if (d[26] != 0 || d[27] != 0) { error = "Unsupported PNG compression/filter method"; return false; }
    if (interlace != 0) { error = "Interlaced PNG images are not supported"; return false; }
    int channels;
    switch (ctype) {
        case 0: channels = 1; break;
        case 2: channels = 3; break;
        case 3: channels = 1; break;
        case 4: channels = 2; break;
        case 6: channels = 4; break;
        default: error = "Unsupported PNG colour type"; return false;
    }
    bool depth_ok = (ctype == 0 && (depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16)) ||
                    (ctype == 3 && (depth == 1 || depth == 2 || depth == 4 || depth == 8)) ||
                    ((ctype == 2 || ctype == 4 || ctype == 6) && (depth == 8 || depth == 16));
    if (!depth_ok) { error = "Unsupported PNG bit depth"; return false; }

    Bytes idat, palette;
    size_t p = 8;
    bool seen_iend = false;
    while (p + 12 <= d.size()) {
        uint32_t len = be32(&d[p]);
        if (len > d.size() || p + 12 + len > d.size()) { error = "Truncated PNG chunk"; return false; }
        const uint8_t* type = &d[p + 4];
        const uint8_t* body = &d[p + 8];
        if (crc32(type, 4u + len) != be32(body + len)) { error = "PNG chunk CRC mismatch"; return false; }
        if (std::memcmp(type, "IDAT", 4) == 0) idat.insert(idat.end(), body, body + len);
        else if (std::memcmp(type, "PLTE", 4) == 0) palette.assign(body, body + len);
        else if (std::memcmp(type, "IEND", 4) == 0) { seen_iend = true; break; }
        p += 12u + len;
    }
    if (!seen_iend || idat.empty()) { error = "PNG is missing image data"; return false; }
    if (ctype == 3 && (palette.empty() || palette.size() % 3 != 0)) { error = "PNG palette is invalid"; return false; }

    const size_t bits_per_pixel = static_cast<size_t>(channels) * depth;
    const size_t stride = (static_cast<size_t>(w) * bits_per_pixel + 7) / 8;
    const size_t bpp = (bits_per_pixel + 7) / 8;
    const size_t raw_size = (stride + 1) * static_cast<size_t>(h);
    Bytes raw;
    if (!zlib_decompress(idat.data(), idat.size(), raw, raw_size) || raw.size() != raw_size) {
        error = "PNG image data is corrupt";
        return false;
    }
    // Undo scanline filters in place.
    for (int y = 0; y < h; ++y) {
        uint8_t* row = &raw[static_cast<size_t>(y) * (stride + 1)];
        int ft = row[0];
        uint8_t* cur = row + 1;
        const uint8_t* up = y ? row - (stride + 1) + 1 : nullptr;
        for (size_t i = 0; i < stride; ++i) {
            int a = i >= bpp ? cur[i - bpp] : 0;
            int b = up ? up[i] : 0;
            int c = (up && i >= bpp) ? up[i - bpp] : 0;
            int add;
            switch (ft) {
                case 0: add = 0; break;
                case 1: add = a; break;
                case 2: add = b; break;
                case 3: add = (a + b) / 2; break;
                case 4: add = paeth(a, b, c); break;
                default: error = "Bad PNG filter type"; return false;
            }
            cur[i] = static_cast<uint8_t>(cur[i] + add);
        }
    }

    out = Image(w, h);
    auto sample = [&](const uint8_t* cur, size_t index) -> unsigned {  // index-th sample in the row
        if (depth == 8) return cur[index];
        if (depth == 16) return cur[index * 2];  // high byte
        size_t bit = index * static_cast<size_t>(depth);
        unsigned v = (cur[bit / 8] >> (8 - depth - (bit % 8))) & ((1u << depth) - 1u);
        return v;
    };
    for (int y = 0; y < h; ++y) {
        const uint8_t* cur = &raw[static_cast<size_t>(y) * (stride + 1) + 1];
        for (int x = 0; x < w; ++x) {
            size_t base = static_cast<size_t>(x) * channels;
            Rgb c;
            if (ctype == 3) {
                unsigned idx = sample(cur, base);
                if (idx * 3 + 2 >= palette.size()) { error = "PNG palette index out of range"; return false; }
                c = Rgb{palette[idx * 3], palette[idx * 3 + 1], palette[idx * 3 + 2]};
            } else if (ctype == 0 || ctype == 4) {
                unsigned g = sample(cur, base);
                if (depth < 8) g = g * 255u / ((1u << depth) - 1u);
                c = Rgb{static_cast<uint8_t>(g), static_cast<uint8_t>(g), static_cast<uint8_t>(g)};
            } else {
                c = Rgb{static_cast<uint8_t>(sample(cur, base)), static_cast<uint8_t>(sample(cur, base + 1)),
                        static_cast<uint8_t>(sample(cur, base + 2))};
            }
            out.set(x, y, c);
        }
    }
    return true;
}

}  // namespace tt
