// .ttface package export/inspection and .ttproj editor project files.
// Formats are byte-for-byte the ones produced by the Python Face Studio.
#pragma once
#include <string>
#include <vector>

#include "image.h"
#include "json.h"

namespace tt {

uint16_t color_hex_to_rgb565(const std::string& hex);  // 0xFFFF for malformed input
Bytes image_to_rgb565_bytes(const Image& img);         // nearest-neighbour to 320x240, little endian
Image rgb565_bytes_to_image(const Bytes& raw);         // 153600 bytes -> 320x240 (r<<3, g<<2, b<<3)

struct ExportResult {
    bool ok = false;
    std::vector<std::string> messages;
};

// Validates `project`, then builds the .ttface ZIP in memory. `bg` may be null
// (a solid background_color fill is used).
ExportResult build_ttface_package(const Json& project, const Image* bg, Bytes& zip_out);
ExportResult export_ttface_package(const Json& project, const Image* bg, const std::string& output_path);

struct InspectedFace {
    bool loaded = false;      // manifest was read (Python: manifest is not None)
    Json manifest;
    bool has_background = false;
    Image background;
    std::vector<std::string> errors;
};
InspectedFace inspect_ttface_bytes(const Bytes& data);
InspectedFace inspect_ttface_file(const std::string& path);

// .ttproj: {"file_format": "ttproj", "version": 1, "project": ..., "canvas_png_b64": ...}
bool save_project_file(const std::string& path, const Json& project, const Image& canvas, std::string& error);
bool load_project_file(const std::string& path, Json& project, Image& canvas, std::string& error);

}  // namespace tt
