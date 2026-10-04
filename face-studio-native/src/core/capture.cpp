#include "capture.h"

namespace tt {

int capture_page_count(size_t n) {
    if (n == kCapturePageBytes) return 1;
    if (n == 2 * kCapturePageBytes) return 2;
    return 0;
}

bool decode_tomtom_capture(const Bytes& raw, int page, Image& out, std::string& error) {
    int pages = capture_page_count(raw.size());
    if (pages == 0) {
        error = "Not a TomTom capture: expected 153,600 bytes (one 320x240 RGB565 page) or 307,200 bytes (two pages), got " +
                std::to_string(raw.size()) + ".";
        return false;
    }
    if (page < 0 || page >= pages) {
        error = "That page does not exist in this capture.";
        return false;
    }
    out = Image(320, 240);
    const uint8_t* src = raw.data() + static_cast<size_t>(page) * kCapturePageBytes;
    for (int i = 0; i < 320 * 240; ++i) {
        unsigned v = src[2 * i] | (src[2 * i + 1] << 8);
        unsigned r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
        out.set(i % 320, i / 320,
                Rgb{static_cast<uint8_t>((r << 3) | (r >> 2)), static_cast<uint8_t>((g << 2) | (g >> 4)), static_cast<uint8_t>((b << 3) | (b >> 2))});
    }
    return true;
}

}  // namespace tt
