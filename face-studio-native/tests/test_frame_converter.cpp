#include "../src/display/frame_converter.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

namespace {

int g_checks = 0;

void check(bool condition, const char* name) {
    ++g_checks;
    if (!condition) {
        std::cerr << "FAIL: " << name << '\n';
        std::exit(1);
    }
}

std::uint16_t pixel_at(const std::vector<std::uint8_t>& frame,
                       std::uint32_t x, std::uint32_t y) {
    const auto offset =
        (static_cast<std::size_t>(y) * tt::display::kFrameWidth + x) * 2;
    return static_cast<std::uint16_t>(frame[offset]) |
           static_cast<std::uint16_t>(frame[offset + 1] << 8);
}

}  // namespace

int main() {
    using tt::display::convert_bgra8_to_rgb565;
    using tt::display::kFrameBytes;
    using tt::display::kFrameHeight;
    using tt::display::kFrameWidth;

    std::vector<std::uint8_t> output(kFrameBytes, 0);
    const std::array<std::uint8_t, 4> red = {0, 0, 255, 255};
    check(convert_bgra8_to_rgb565(red.data(), red.size(), 1, 1, 4,
                                  output.data(), output.size()),
          "convert a 1x1 source");
    check(pixel_at(output, 0, 0) == 0xf800, "RGB565 red and LE byte order");
    check(pixel_at(output, kFrameWidth - 1, kFrameHeight - 1) == 0xf800,
          "upscale source over the fixed frame");

    const std::array<std::uint8_t, 16> quadrants = {
        0, 0, 255, 255,  // red
        0, 255, 0, 255,  // green
        255, 0, 0, 255,  // blue
        255, 255, 255, 255
    };
    check(convert_bgra8_to_rgb565(quadrants.data(), quadrants.size(), 2, 2, 8,
                                  output.data(), output.size()),
          "convert and scale a 2x2 source");
    check(pixel_at(output, 0, 0) == 0xf800, "top-left scaling sample");
    check(pixel_at(output, 159, 119) == 0xf800, "top-left quadrant edge");
    check(pixel_at(output, 160, 0) == 0x07e0, "top-right scaling sample");
    check(pixel_at(output, 0, 120) == 0x001f, "bottom-left scaling sample");
    check(pixel_at(output, 319, 239) == 0xffff,
          "bottom-right scaling sample");

    const std::array<std::uint8_t, 12> padded_rows = {
        0, 0, 255, 255, 99, 99, 99, 99,
        255, 0, 0, 255
    };
    check(convert_bgra8_to_rgb565(padded_rows.data(), padded_rows.size(),
                                  1, 2, 8, output.data(), output.size()),
          "accept padded source rows");
    check(pixel_at(output, 0, 0) == 0xf800, "read first padded row");
    check(pixel_at(output, 0, 239) == 0x001f, "read final padded row");

    check(!convert_bgra8_to_rgb565(nullptr, 0, 1, 1, 4, output.data(),
                                   output.size()),
          "reject null source");
    check(!convert_bgra8_to_rgb565(red.data(), red.size(), 1, 1, 4, nullptr,
                                   output.size()),
          "reject null destination");
    check(!convert_bgra8_to_rgb565(red.data(), red.size(), 0, 1, 4,
                                   output.data(), output.size()),
          "reject zero width");
    check(!convert_bgra8_to_rgb565(red.data(), red.size(), 1, 0, 4,
                                   output.data(), output.size()),
          "reject zero height");
    check(!convert_bgra8_to_rgb565(red.data(), red.size(), 1, 1, 3,
                                   output.data(), output.size()),
          "reject short source stride");
    check(!convert_bgra8_to_rgb565(red.data(), red.size() - 1, 1, 1, 4,
                                   output.data(), output.size()),
          "reject truncated source");
    check(!convert_bgra8_to_rgb565(red.data(), red.size(), 1, 1, 4,
                                   output.data(), output.size() - 1),
          "reject short output");
    check(!convert_bgra8_to_rgb565(red.data(), red.size(), 16385, 1, 65540,
                                   output.data(), output.size()),
          "reject excessive dimensions");
    check(!convert_bgra8_to_rgb565(
              red.data(), red.size(), 1, 2,
              std::numeric_limits<std::size_t>::max(), output.data(),
              output.size()),
          "reject overflowing source span");

    std::cout << "frame converter: " << g_checks << " checks passed\n";
    return 0;
}
