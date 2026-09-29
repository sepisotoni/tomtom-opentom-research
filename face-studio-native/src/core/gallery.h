// .ttgallery collection creation, inspection and change planning
// (port of studio/core/gallery.py; same manifest, limits and error text).
#pragma once
#include <string>
#include <vector>

#include "codec.h"
#include "json.h"

namespace tt {

constexpr size_t kMaxGalleryFaces = 32;
constexpr size_t kMaxFacePackageBytes = 2u * 1024 * 1024;
constexpr size_t kMaxGalleryBytes = 64u * 1024 * 1024;
constexpr size_t kMaxPreviewBytes = 512u * 1024;

struct GalleryResult {
    bool ok = false;
    std::vector<std::string> errors;
};

// Builds the gallery ZIP from validated .ttface package bytes.
GalleryResult build_ttgallery(const std::vector<Bytes>& face_packages, const std::string& gallery_name,
                              const std::string& version, Bytes& zip_out);
GalleryResult export_ttgallery(const std::vector<std::string>& face_package_paths, const std::string& output_path,
                               const std::string& gallery_name, const std::string& version = "1.0.0");

struct InspectedGallery {
    bool ok = false;  // Python: manifest is not None
    Json manifest;
    std::vector<std::string> errors;
};
InspectedGallery inspect_ttgallery_bytes(const Bytes& data);
InspectedGallery inspect_ttgallery_file(const std::string& path);

struct GalleryPlan {
    std::vector<std::string> add, update, remove;
};
bool plan_gallery_changes(const Json& installed, const Json& desired, GalleryPlan& plan, std::string& error);

}  // namespace tt
