// Decoding of raw TomTom framebuffer captures (what a screenshot of the device screen looks like).
//
// Evidence (docs/CONTROL_CENTER_PLAN.md): the sample `screenshot-v1.raw` from the device is 307,200 bytes. Decoded as
// little-endian RGB565 it is two stacked 320x240 pages - the first holds the picture, the second is black - i.e. a
// 320x480 virtual framebuffer (double buffering). A single-page capture is 153,600 bytes. Which page is on screen at the
// moment of capture depends on the framebuffer's y-offset, so a future device-side SCREENSHOT must report it.
#pragma once
#include <string>

#include "image.h"

namespace tt {

constexpr size_t kCapturePageBytes = 320u * 240u * 2u;  // 153,600

// Number of 320x240 pages in a capture of this size (1 or 2), or 0 if the size is not a known capture size.
int capture_page_count(size_t byte_size);

// Decodes page `page` (0-based) of an RGB565-LE capture to RGB with exact bit replication.
bool decode_tomtom_capture(const Bytes& raw, int page, Image& out, std::string& error);

}  // namespace tt
