#pragma once

#include <cstddef>
#include <cstdint>

namespace tt::display {

constexpr std::uint32_t kFrameWidth = 320;
constexpr std::uint32_t kFrameHeight = 240;
constexpr std::size_t kFrameBytes =
    static_cast<std::size_t>(kFrameWidth) * kFrameHeight * 2;

// Converts a BGRA8 surface into the fixed, tightly packed RGB565-LE wire frame.
// Oversized source surfaces are downscaled with nearest-neighbor sampling.
bool convert_bgra8_to_rgb565(const std::uint8_t* source,
                             std::size_t source_bytes,
                             std::uint32_t source_width,
                             std::uint32_t source_height,
                             std::size_t source_stride,
                             std::uint8_t* destination,
                             std::size_t destination_bytes) noexcept;

}  // namespace tt::display
