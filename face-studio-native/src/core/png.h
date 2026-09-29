// Small PNG encoder/decoder (8-bit RGB output, non-interlaced input).
#pragma once
#include <string>

#include "image.h"

namespace tt {

Bytes png_encode(const Image& image);
// Accepts grayscale/RGB/palette/gray+alpha/RGBA, 1-16 bit depths, non-interlaced.
// Alpha is discarded (matching PIL's convert("RGB")).
bool png_decode(const Bytes& data, Image& out, std::string& error);
// Signature + IHDR only (cheap format check used when inspecting galleries).
bool png_dimensions(const Bytes& data, int& width, int& height);

}  // namespace tt
