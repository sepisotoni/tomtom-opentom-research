#include "frame_converter.h"

#include <limits>

namespace tt::display {
namespace {

constexpr std::uint32_t kMaxSourceDimension = 16384;
constexpr std::size_t kSourceBytesPerPixel = 4;

}  // namespace

bool convert_bgra8_to_rgb565(const std::uint8_t* source,
                             std::size_t source_bytes,
                             std::uint32_t source_width,
                             std::uint32_t source_height,
                             std::size_t source_stride,
                             std::uint8_t* destination,
                             std::size_t destination_bytes) noexcept {
    if (source == nullptr || destination == nullptr ||
        source_width == 0 || source_height == 0 ||
        source_width > kMaxSourceDimension ||
        source_height > kMaxSourceDimension ||
        destination_bytes < kFrameBytes ||
        source_width > std::numeric_limits<std::size_t>::max() /
                           kSourceBytesPerPixel) {
        return false;
    }

    const auto minimum_stride =
        static_cast<std::size_t>(source_width) * kSourceBytesPerPixel;
    if (source_stride < minimum_stride ||
        source_height - 1 >
            (std::numeric_limits<std::size_t>::max() - minimum_stride) /
                source_stride) {
        return false;
    }

    const auto required_source_bytes =
        static_cast<std::size_t>(source_height - 1) * source_stride +
        minimum_stride;
    if (source_bytes < required_source_bytes) {
        return false;
    }

    for (std::uint32_t y = 0; y < kFrameHeight; ++y) {
        const auto source_y =
            static_cast<std::uint32_t>(
                (static_cast<std::uint64_t>(y) * source_height) /
                kFrameHeight);
        const auto* source_row =
            source + static_cast<std::size_t>(source_y) * source_stride;
        auto* destination_row =
            destination + static_cast<std::size_t>(y) * kFrameWidth * 2;

        for (std::uint32_t x = 0; x < kFrameWidth; ++x) {
            const auto source_x =
                static_cast<std::uint32_t>(
                    (static_cast<std::uint64_t>(x) * source_width) /
                    kFrameWidth);
            const auto* pixel =
                source_row + static_cast<std::size_t>(source_x) *
                                 kSourceBytesPerPixel;
            const auto red = static_cast<std::uint16_t>(pixel[2] >> 3);
            const auto green = static_cast<std::uint16_t>(pixel[1] >> 2);
            const auto blue = static_cast<std::uint16_t>(pixel[0] >> 3);
            const auto rgb565 = static_cast<std::uint16_t>(
                (red << 11) | (green << 5) | blue);
            const auto destination_offset =
                static_cast<std::size_t>(x) * 2;
            destination_row[destination_offset] =
                static_cast<std::uint8_t>(rgb565 & 0xff);
            destination_row[destination_offset + 1] =
                static_cast<std::uint8_t>(rgb565 >> 8);
        }
    }

    return true;
}

}  // namespace tt::display
