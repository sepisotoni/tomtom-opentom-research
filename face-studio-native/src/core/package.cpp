#include "package.h"

#include <algorithm>
#include <cctype>

#include "fileio.h"
#include "model.h"
#include "png.h"
#include "zip.h"

namespace tt {

namespace {
constexpr size_t kMaxInspectEntry = 8u * 1024 * 1024;
constexpr size_t kMaxProjectFile = 64u * 1024 * 1024;
}  // namespace

uint16_t color_hex_to_rgb565(const std::string& hex) {
    size_t start = 0;
    while (start < hex.size() && hex[start] == '#') ++start;
    if (hex.size() - start == 6) {
        Rgb c = hex_to_rgb(hex.substr(start), Rgb{0, 0, 0});
        // hex_to_rgb falls back to black for malformed input; detect that case explicitly.
        bool valid = true;
        for (size_t k = start; k < hex.size(); ++k) valid = valid && std::isxdigit(static_cast<unsigned char>(hex[k]));
        if (valid) return static_cast<uint16_t>(((c.r >> 3) << 11) | ((c.g >> 2) << 5) | (c.b >> 3));
    }
    return 0xFFFF;
}

Bytes image_to_rgb565_bytes(const Image& img) {
    Image scaled = resize_nearest(img, kCanvasWidth, kCanvasHeight);
    Bytes out;
    out.reserve(kBackgroundBytes);
    for (int y = 0; y < kCanvasHeight; ++y)
        for (int x = 0; x < kCanvasWidth; ++x) {
            Rgb c = scaled.get(x, y);
            uint16_t v = static_cast<uint16_t>(((c.r >> 3) << 11) | ((c.g >> 2) << 5) | (c.b >> 3));
            out.push_back(static_cast<uint8_t>(v & 0xFF));
            out.push_back(static_cast<uint8_t>(v >> 8));
        }
    return out;
}

Image rgb565_bytes_to_image(const Bytes& raw) {
    Image img(kCanvasWidth, kCanvasHeight);
    for (size_t i = 0; i + 1 < raw.size() && i / 2 < static_cast<size_t>(kCanvasWidth) * kCanvasHeight; i += 2) {
        unsigned v = raw[i] | (raw[i + 1] << 8);
        int idx = static_cast<int>(i / 2);
        img.set(idx % kCanvasWidth, idx / kCanvasWidth,
                Rgb{static_cast<uint8_t>(((v >> 11) & 0x1F) << 3), static_cast<uint8_t>(((v >> 5) & 0x3F) << 2),
                    static_cast<uint8_t>((v & 0x1F) << 3)});
    }
    return img;
}

ExportResult build_ttface_package(const Json& project, const Image* bg, Bytes& zip_out) {
    ExportResult result;
    result.messages = validate_face_project(project);
    for (const std::string& m : result.messages)
        if (m.rfind("Validation Error:", 0) == 0) return result;  // blocking

    const Json& meta_ref = project.get("metadata");
    if (project.has("metadata") && !meta_ref.is_object()) {
        result.messages.push_back("Export Failed with Exception: metadata must be an object");
        return result;
    }
    static const Json kEmptyObject = Json::object();
    const Json& meta = project.has("metadata") ? meta_ref : kEmptyObject;

    Json face_name = meta.get("name");
    if (!face_name.truthy()) face_name = project.has("face_name") ? project.get("face_name") : Json::string("Untitled Face");
    auto member_or = [](const Json& obj, const char* key, const char* fallback) {
        return obj.has(key) ? obj.get(key) : Json::string(fallback);
    };

    Json manifest = Json::object();
    manifest.set("manifest_version", Json::integer(1));
    manifest.set("face_name", face_name);
    manifest.set("version", member_or(meta, "version", "1.0.0"));
    manifest.set("author", member_or(meta, "author", "Unknown"));
    manifest.set("target_device", Json::string(kTargetDevice));
    manifest.set("renderer_id", member_or(project, "renderer_id", kDefaultRendererId));
    Json canvas = Json::object();
    canvas.set("width", Json::integer(kCanvasWidth));
    canvas.set("height", Json::integer(kCanvasHeight));
    manifest.set("canvas", canvas);
    manifest.set("display", Json::object());
    manifest.set("data_requirements", project.has("data_requirements") ? project.get("data_requirements") : Json::array());
    Json background = Json::object();
    background.set("type", Json::string("raw_rgb565"));
    background.set("file", Json::string(kBackgroundEntry));
    Json color = member_or(project, "background_color", "#000000");
    background.set("color", color);
    manifest.set("background", background);
    manifest.set("elements", project.has("elements") ? project.get("elements") : Json::array());
    manifest.set("rules", project.has("rules") ? project.get("rules") : Json::array());

    ProjectFormats pf = get_project_formats(project);
    if (!pf.formats.items().empty()) {
        Json display = Json::object();
        display.set("formats", pf.formats);
        display.set("default_format", Json::string(pf.default_format));
        manifest.set("display", display);
    }

    Bytes bg_bytes;
    if (bg) {
        bg_bytes = image_to_rgb565_bytes(*bg);
    } else {
        uint16_t v = color_hex_to_rgb565(color.py_str());
        bg_bytes.reserve(kBackgroundBytes);
        for (size_t i = 0; i < kBackgroundBytes / 2; ++i) {
            bg_bytes.push_back(static_cast<uint8_t>(v & 0xFF));
            bg_bytes.push_back(static_cast<uint8_t>(v >> 8));
        }
    }
    ZipWriter zw;
    zw.add("manifest.json", to_bytes(manifest.dump(2)));
    zw.add(kBackgroundEntry, bg_bytes);
    zip_out = zw.finish();
    result.ok = true;
    return result;
}

ExportResult export_ttface_package(const Json& project, const Image* bg, const std::string& output_path) {
    Bytes zip;
    ExportResult r = build_ttface_package(project, bg, zip);
    if (!r.ok) return r;
    std::string error;
    if (!write_file(output_path, zip, error)) {
        r.ok = false;
        r.messages.push_back("Export Failed with Exception: " + error);
    }
    return r;
}

InspectedFace inspect_ttface_bytes(const Bytes& data) {
    InspectedFace out;
    auto fail = [&](const std::string& m) {
        InspectedFace f;
        f.errors.push_back(m);
        return f;
    };
    ZipReader zip;
    std::string error;
    if (!zip.open(data, error)) return fail("Error inspecting .ttface package: " + error);
    const ZipEntry* manifest_entry = zip.find("manifest.json");
    if (!manifest_entry) return fail("Error: Invalid .ttface package. Missing 'manifest.json'.");
    if (manifest_entry->size > kMaxInspectEntry) return fail("Error inspecting .ttface package: manifest.json is too large");
    Bytes manifest_bytes;
    if (!zip.read(*manifest_entry, manifest_bytes, error)) return fail("Error inspecting .ttface package: " + error);
    Json manifest;
    if (!Json::parse(to_string(manifest_bytes), manifest, error))
        return fail("Error inspecting .ttface package: " + error);
    if (!manifest.is_object()) return fail("Error inspecting .ttface package: manifest must be a JSON object");

    out.loaded = true;
    out.manifest = manifest;
    out.errors = validate_face_project(manifest);

    const Json& bg = manifest.get("background");
    if (bg.is_object() && bg.get("file").truthy() && bg.get("file").is_string()) {
        const std::string bg_path = bg.get("file").as_string();
        const ZipEntry* entry = zip.find(bg_path);
        if (!entry) {
            out.errors.push_back("Validation Error: Background file '" + bg_path +
                                 "' listed in manifest is missing from ZIP entries.");
        } else if (entry->size == kBackgroundBytes) {
            Bytes raw;
            if (!zip.read(*entry, raw, error)) return fail("Error inspecting .ttface package: " + error);
            out.background = rgb565_bytes_to_image(raw);
            out.has_background = true;
        }
    }
    return out;
}

InspectedFace inspect_ttface_file(const std::string& path) {
    if (!file_exists(path)) {
        InspectedFace f;
        f.errors.push_back("Error: File '" + path + "' does not exist.");
        return f;
    }
    Bytes data;
    std::string error;
    if (!read_file(path, data, 16u * 1024 * 1024, error)) {
        InspectedFace f;
        f.errors.push_back("Error inspecting .ttface package: " + error);
        return f;
    }
    return inspect_ttface_bytes(data);
}

bool save_project_file(const std::string& path, const Json& project, const Image& canvas, std::string& error) {
    Json payload = Json::object();
    payload.set("file_format", Json::string("ttproj"));
    payload.set("version", Json::integer(1));
    payload.set("project", project);
    payload.set("canvas_png_b64", Json::string(base64_encode(png_encode(canvas))));
    return write_file(path, to_bytes(payload.dump(2)), error);
}

bool load_project_file(const std::string& path, Json& project, Image& canvas, std::string& error) {
    Bytes data;
    if (!read_file(path, data, kMaxProjectFile, error)) return false;
    Json payload;
    if (!Json::parse(to_string(data), payload, error)) return false;
    if (!payload.is_object()) {
        error = "Project file must contain a JSON object.";
        return false;
    }
    project = payload.has("project") ? payload.get("project") : Json::object();
    if (!project.is_object()) {
        error = "Project data must be a JSON object.";
        return false;
    }
    const Json& b64 = payload.get("canvas_png_b64");
    if (b64.truthy() && b64.is_string()) {
        Bytes png;
        Image decoded;
        if (!base64_decode(b64.as_string(), png)) {
            error = "Project canvas image is not valid Base64.";
            return false;
        }
        if (!png_decode(png, decoded, error)) return false;
        if (decoded.w != kCanvasWidth || decoded.h != kCanvasHeight) {
            error = "Project canvas image must be 320x240.";
            return false;
        }
        canvas = std::move(decoded);
    } else {
        const Json& bg = project.get("background_color");
        canvas = Image(kCanvasWidth, kCanvasHeight, hex_to_rgb(bg.is_string() ? bg.as_string() : "#000000", Rgb{0, 0, 0}));
    }
    return true;
}

}  // namespace tt
