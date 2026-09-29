#include "gallery.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>

#include "fileio.h"
#include "font.h"
#include "model.h"
#include "png.h"
#include "render.h"
#include "zip.h"

namespace tt {

namespace {

bool is_safe_id(const std::string& s) {
    if (s.empty() || s.size() > 48) return false;
    auto alnum = [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); };
    if (!alnum(s[0])) return false;
    for (char c : s)
        if (!alnum(c) && c != '_' && c != '-') return false;
    return true;
}

// Mirrors _safe_archive_names(); returns false with the ValueError text.
bool safe_archive_names(const ZipReader& zip, std::vector<std::string>& names, std::string& error) {
    names.clear();
    for (const ZipEntry& e : zip.entries()) {
        const std::string& name = e.name;
        bool unsafe = name.empty() || name[0] == '/' || name.find('\\') != std::string::npos || e.is_symlink();
        if (!unsafe) {
            size_t start = 0;
            while (true) {
                size_t slash = name.find('/', start);
                std::string part = name.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
                if (part.empty() || part == "." || part == "..") { unsafe = true; break; }
                if (slash == std::string::npos) break;
                start = slash + 1;
            }
        }
        if (unsafe) { error = "Archive contains an unsafe path."; return false; }
        if (e.is_dir()) { error = "Directory entries are not allowed."; return false; }
        if (e.size > kMaxFacePackageBytes) { error = "Archive entry exceeds the permitted size."; return false; }
        names.push_back(name);
    }
    std::set<std::string> unique(names.begin(), names.end());
    if (unique.size() != names.size()) { error = "Archive contains duplicate paths."; return false; }
    return true;
}

// Mirrors _validate_face_bytes(); `error` is the ValueError text on failure.
bool validate_face_bytes(const Bytes& package, Json& manifest, Bytes& background, std::string& error) {
    if (package.size() > kMaxFacePackageBytes) { error = "Face package exceeds the 2 MiB limit."; return false; }
    ZipReader zip;
    if (!zip.open(package, error)) return false;
    std::vector<std::string> names;
    if (!safe_archive_names(zip, names, error)) return false;
    std::set<std::string> have(names.begin(), names.end());
    if (have != std::set<std::string>{"manifest.json", kBackgroundEntry}) {
        error = "Face package must contain only manifest.json and assets/bg.rgb565.";
        return false;
    }
    Bytes manifest_bytes;
    if (!zip.read_named("manifest.json", manifest_bytes, error)) return false;
    if (!Json::parse(to_string(manifest_bytes), manifest, error)) return false;
    std::vector<std::string> errors = validate_face_project(manifest);
    if (!errors.empty()) {
        error.clear();
        for (size_t k = 0; k < errors.size(); ++k) error += (k ? "; " : "") + errors[k];
        return false;
    }
    if (!zip.read_named(kBackgroundEntry, background, error)) return false;
    if (background.size() != kBackgroundBytes) { error = "Face background must be exactly 153600 bytes."; return false; }
    return true;
}

Image lanczos_rgb(const Image& src, int w, int h) {
    RgbaImage rgba;
    rgba.w = src.w;
    rgba.h = src.h;
    rgba.px.resize(static_cast<size_t>(src.w) * src.h * 4);
    for (size_t i = 0; i < static_cast<size_t>(src.w) * src.h; ++i) {
        rgba.px[i * 4] = src.px[i * 3];
        rgba.px[i * 4 + 1] = src.px[i * 3 + 1];
        rgba.px[i * 4 + 2] = src.px[i * 3 + 2];
        rgba.px[i * 4 + 3] = 255;
    }
    PremulImage p = resize_lanczos(rgba, w, h);
    Image out(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const float* q = &p.px[(static_cast<size_t>(y) * w + x) * 4];
            auto to8 = [](float v) { return static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, v + 0.5f))); };
            out.set(x, y, Rgb{to8(q[0]), to8(q[1]), to8(q[2])});
        }
    return out;
}

// Gallery previews expand RGB565 with (v * 255 / max) like the reference (_preview_png).
Image rgb565_bytes_to_image_exact(const Bytes& raw) {
    Image img(kCanvasWidth, kCanvasHeight);
    for (size_t i = 0; i + 1 < raw.size() && i / 2 < static_cast<size_t>(kCanvasWidth) * kCanvasHeight; i += 2) {
        unsigned v = raw[i] | (raw[i + 1] << 8);
        int idx = static_cast<int>(i / 2);
        img.set(idx % kCanvasWidth, idx / kCanvasWidth,
                Rgb{static_cast<uint8_t>(((v >> 11) & 0x1F) * 255 / 31), static_cast<uint8_t>(((v >> 5) & 0x3F) * 255 / 63),
                    static_cast<uint8_t>((v & 0x1F) * 255 / 31)});
    }
    return img;
}

Bytes preview_png(const Bytes& background, const Json& manifest) {
    Image image = rgb565_bytes_to_image_exact(background);
    const Json* rid = manifest.find("renderer_id");
    std::string renderer_id = rid && rid->is_string() ? rid->as_string() : kDefaultRendererId;
    const RendererDef* renderer = find_renderer(renderer_id);
    if (renderer && renderer->native_face_index >= 0) {
        static const Rgb colors[5] = {{130, 235, 255}, {0, 245, 255}, {232, 218, 242}, {255, 115, 65}, {96, 165, 250}};
        int idx = renderer->native_face_index;
        Rgb color = colors[idx];
        if (idx == 4) {
            draw_text(image, 36, 45, "LIVE SYSTEM", color, 24);
            draw_text(image, 36, 70, "TELEMETRY", Rgb{220, 228, 240}, 24);
            draw_text(image, 36, 105, "10:08  UTC", Rgb{110, 240, 180}, 24);
        } else {
            draw_text(image, 36, 58, "10", color, 64);
            draw_text(image, 174, 58, "08", color, 64);
            if (idx == 1)
                for (int x = 24; x < 296; ++x) {
                    int y = 174 + static_cast<int>(4 * std::sin(x / 15.0));
                    image.fill_rect(x, y, x, y + 1, Rgb{20, 180, 255});
                }
        }
    } else {
        SimTime t;  // 2026-01-01 10:08:00
        Json state = Json::object();
        state.set("battery_percent", Json::integer(80));
        state.set("gps_status", Json::integer(0));
        state.set("weather_status", Json::integer(1));
        state.set("charging", Json::boolean(false));
        ProjectFormats pf = get_project_formats(manifest);
        std::string layout = pf.default_format.empty() ? "horizontal" : pf.default_format;
        const Json& rules = manifest.get("rules");
        const Json& elements = manifest.get("elements");
        if (elements.is_array())
            for (const Json& el : elements.items()) {
                if (!RuleEngine::is_element_visible(el, rules, state)) continue;
                Json copy = el;
                if (copy.get("type") == Json::string("digital_time")) copy.set("layout", Json::string(layout));
                render_element(image, copy, t);
            }
    }
    return png_encode(lanczos_rgb(image, 160, 120));
}

std::string derive_face_id(const std::string& name) {
    std::string lowered;
    size_t a = 0, b = name.size();
    while (a < b && std::isspace(static_cast<unsigned char>(name[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(name[b - 1]))) --b;
    bool in_run = false;
    for (size_t i = a; i < b; ++i) {
        unsigned char c = static_cast<unsigned char>(name[i]);
        char l = (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : static_cast<char>(c);
        bool ok = (l >= 'a' && l <= 'z') || (l >= '0' && l <= '9') || l == '_' || l == '-';
        if (c >= 0x80) ok = false;
        if (ok) { lowered.push_back(l); in_run = false; }
        else if (!in_run) { lowered.push_back('-'); in_run = true; }
    }
    size_t s = lowered.find_first_not_of("-_");
    if (s == std::string::npos) return "face";
    size_t e = lowered.find_last_not_of("-_");
    std::string id = lowered.substr(s, e - s + 1);
    if (id.size() > 48) id.resize(48);
    return id.empty() ? "face" : id;
}

std::string strip(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

}  // namespace

GalleryResult build_ttgallery(const std::vector<Bytes>& packages, const std::string& gallery_name,
                              const std::string& version, Bytes& zip_out) {
    GalleryResult r;
    std::string name = strip(gallery_name);
    if (name.empty()) { r.errors.push_back("Gallery name must not be empty."); return r; }
    if (packages.empty() || packages.size() > kMaxGalleryFaces) {
        r.errors.push_back("Gallery must contain 1 to " + std::to_string(kMaxGalleryFaces) + " faces.");
        return r;
    }
    std::map<std::string, Bytes> entries;
    Json faces = Json::array();
    std::set<std::string> used_ids;
    std::vector<Json> all_reqs;
    std::vector<std::string> all_formats;
    size_t total = 0;

    for (const Bytes& package : packages) {
        Json manifest;
        Bytes background;
        std::string error;
        if (!validate_face_bytes(package, manifest, background, error)) {
            r.errors.push_back("Gallery export failed: " + error);
            return r;
        }
        const Json& fname = manifest.get("face_name");
        if (!fname.is_string()) {
            r.errors.push_back("Gallery export failed: 'face_name'");
            return r;
        }
        std::string face_id = derive_face_id(fname.as_string());
        if (!is_safe_id(face_id) || used_ids.count(face_id)) {
            r.errors.push_back("Face names must produce unique safe IDs: '" + face_id + "'.");
            return r;
        }
        used_ids.insert(face_id);
        const std::string package_entry = "faces/" + face_id + ".ttface";
        const std::string preview_entry = "previews/" + face_id + ".png";
        Bytes preview = preview_png(background, manifest);
        ProjectFormats pf = get_project_formats(manifest);
        for (const Json& f : pf.formats.items())
            if (f.is_string()) all_formats.push_back(f.as_string());
        const Json* rid = manifest.find("renderer_id");
        Json renderer_id = rid ? *rid : Json::string(kDefaultRendererId);
        const Json& reqs = manifest.get("data_requirements");
        Json face_reqs = reqs.is_array() ? reqs : Json::array();
        all_reqs.push_back(face_reqs);

        entries[package_entry] = package;
        entries[preview_entry] = preview;
        Json face = Json::object();
        face.set("id", Json::string(face_id));
        face.set("name", fname);
        face.set("version", manifest.has("version") ? manifest.get("version") : Json::string("1.0.0"));
        face.set("formats", pf.formats);
        face.set("default_format", Json::string(pf.default_format));
        face.set("renderer_id", renderer_id);
        face.set("data_requirements", face_reqs);
        face.set("package", Json::string(package_entry));
        face.set("preview", Json::string(preview_entry));
        face.set("sha256", Json::string(sha256_hex(package)));
        face.set("preview_sha256", Json::string(sha256_hex(preview)));
        faces.push(std::move(face));
        total += package.size() + preview.size();
        if (total > kMaxGalleryBytes) {
            r.errors.push_back("Gallery exceeds the 64 MiB package limit.");
            return r;
        }
    }

    std::vector<std::string> union_reqs = get_union_data_requirements(all_reqs);
    Json union_json = Json::array();
    for (const std::string& u : union_reqs) union_json.push(Json::string(u));

    Json gm = Json::object();
    gm.set("gallery_version", Json::integer(1));
    gm.set("runtime_api", Json::string("ttface-manifest-v2"));
    gm.set("name", Json::string(name));
    gm.set("version", Json::string(version));
    gm.set("target_device", Json::string(kTargetDevice));
    gm.set("face_count", Json::integer(static_cast<int64_t>(faces.items().size())));
    gm.set("formats", make_format_catalog(all_formats));
    gm.set("data_requirements", union_json);
    gm.set("provider_capabilities", get_provider_capabilities(union_json));
    gm.set("faces", faces);

    ZipWriter zw;
    zw.add("gallery.json", to_bytes(gm.dump(2, true)));
    for (const auto& kv : entries) zw.add(kv.first, kv.second);
    zip_out = zw.finish();
    r.ok = true;
    return r;
}

GalleryResult export_ttgallery(const std::vector<std::string>& paths, const std::string& output_path,
                               const std::string& gallery_name, const std::string& version) {
    GalleryResult r;
    std::vector<Bytes> packages;
    for (const std::string& path : paths) {
        Bytes data;
        std::string error;
        if (!read_file(path, data, kMaxFacePackageBytes + 1, error)) {
            r.errors.push_back("Gallery export failed: " + error);
            return r;
        }
        packages.push_back(std::move(data));
    }
    Bytes zip;
    r = build_ttgallery(packages, gallery_name, version, zip);
    if (!r.ok) return r;
    std::string error;
    if (!write_file(output_path, zip, error)) {
        r.ok = false;
        r.errors.push_back("Gallery export failed: " + error);
    }
    return r;
}

InspectedGallery inspect_ttgallery_bytes(const Bytes& data) {
    InspectedGallery out;
    auto fail = [&](const std::string& m) {
        InspectedGallery g;
        g.errors.push_back(m);
        return g;
    };
    auto internal = [&](const std::string& m) { return fail("Gallery inspection failed: " + m); };

    ZipReader zip;
    std::string error;
    if (!zip.open(data, error)) return internal(error);
    std::vector<std::string> names;
    if (!safe_archive_names(zip, names, error)) return internal(error);
    if (!zip.find("gallery.json")) return fail("Gallery package is missing gallery.json.");
    if (zip.entries().size() > kMaxGalleryFaces * 2 + 1) return fail("Gallery contains too many entries.");
    uint64_t total = 0;
    for (const ZipEntry& e : zip.entries()) total += e.size;
    if (total > kMaxGalleryBytes) return fail("Gallery exceeds the 64 MiB package limit.");

    Bytes manifest_bytes;
    if (!zip.read_named("gallery.json", manifest_bytes, error)) return internal(error);
    Json manifest;
    if (!Json::parse(to_string(manifest_bytes), manifest, error)) return internal(error);
    if (!manifest.is_object() || !manifest.get("gallery_version").is_int() || manifest.get("gallery_version").as_int() != 1)
        return fail("Unsupported gallery manifest version.");
    if (!(manifest.get("runtime_api") == Json::string("ttface-manifest-v2"))) return fail("Unsupported gallery runtime API.");
    if (!manifest.get("name").is_string() || strip(manifest.get("name").as_string()).empty() ||
        !manifest.get("version").is_string())
        return fail("Gallery name or version is invalid.");
    const Json& faces = manifest.get("faces");
    if (!faces.is_array() || faces.items().empty() || faces.items().size() > kMaxGalleryFaces ||
        !manifest.get("face_count").is_int() ||
        manifest.get("face_count").as_int() != static_cast<int64_t>(faces.items().size()))
        return fail("Gallery face list/count is invalid.");

    std::set<std::string> expected = {"gallery.json"};
    std::set<std::string> seen_ids;
    std::set<std::string> declared_formats;
    for (const Json& face : faces.items()) {
        if (!face.is_object()) return fail("Gallery face descriptor must be an object.");
        const Json& id = face.get("id");
        const Json& package_path = face.get("package");
        const Json& preview_path = face.get("preview");
        const Json* rid = face.find("renderer_id");
        Json renderer_id = rid ? *rid : Json::string(kDefaultRendererId);
        const Json& face_formats = face.get("formats");
        bool bad = !id.is_string() || !is_safe_id(id.as_string()) || seen_ids.count(id.as_string()) ||
                   !(package_path == Json::string("faces/" + id.as_string() + ".ttface")) ||
                   !(preview_path == Json::string("previews/" + id.as_string() + ".png")) || !face_formats.is_array() ||
                   !renderer_id.is_string() || !find_renderer(renderer_id.as_string());
        if (bad) return fail("Gallery contains an invalid or duplicate face descriptor.");
        const std::string face_id = id.as_string();
        const Json& default_format = face.get("default_format");

        bool formats_bad = false;
        for (const Json& f : face_formats.items())
            if (!f.is_string() || !find_format(f.as_string())) formats_bad = true;
        for (size_t a = 0; a < face_formats.items().size() && !formats_bad; ++a)
            for (size_t b = a + 1; b < face_formats.items().size(); ++b)
                if (face_formats.items()[a] == face_formats.items()[b]) formats_bad = true;
        if (!formats_bad) {
            if (!face_formats.items().empty()) {
                bool listed = false;
                for (const Json& f : face_formats.items())
                    if (f == default_format) listed = true;
                formats_bad = !listed;
            } else {
                formats_bad = !(default_format.is_null() || default_format == Json::string(""));
            }
        }
        if (formats_bad) return fail("Face format declaration is invalid: " + face_id + ".");
        seen_ids.insert(face_id);
        for (const Json& f : face_formats.items()) declared_formats.insert(f.as_string());
        expected.insert(package_path.as_string());
        expected.insert(preview_path.as_string());

        Bytes package_bytes, preview_bytes;
        if (!zip.find(package_path.as_string())) return internal("'" + package_path.as_string() + "'");
        if (!zip.find(preview_path.as_string())) return internal("'" + preview_path.as_string() + "'");
        if (!zip.read_named(package_path.as_string(), package_bytes, error)) return internal(error);
        if (!zip.read_named(preview_path.as_string(), preview_bytes, error)) return internal(error);
        if (preview_bytes.size() > kMaxPreviewBytes) return fail("Face preview exceeds the size limit: " + face_id + ".");
        if (!(Json::string(sha256_hex(package_bytes)) == face.get("sha256")))
            return fail("Face package checksum failed: " + face_id + ".");
        if (!(Json::string(sha256_hex(preview_bytes)) == face.get("preview_sha256")))
            return fail("Face preview checksum failed: " + face_id + ".");

        Json nested;
        Bytes nested_bg;
        if (!validate_face_bytes(package_bytes, nested, nested_bg, error)) return internal(error);
        if (!(nested.get("face_name") == face.get("name"))) return fail("Face name does not match package: " + face_id + ".");
        const Json* nrid = nested.find("renderer_id");
        Json nested_renderer = nrid ? *nrid : Json::string(kDefaultRendererId);
        if (!(nested_renderer == renderer_id)) return fail("Face renderer does not match package: " + face_id + ".");
        ProjectFormats npf = get_project_formats(nested);
        if (!(npf.formats == face_formats) || !(Json::string(npf.default_format) == default_format))
            return fail("Face formats do not match package: " + face_id + ".");
        int pw = 0, ph = 0;
        if (!png_dimensions(preview_bytes, pw, ph) || pw != 160 || ph != 120)
            return fail("Invalid face preview image: " + face_id + ".");
    }

    if (manifest.has("data_requirements")) {
        std::vector<std::string> req_errs;
        if (!validate_data_requirements(manifest.get("data_requirements"), req_errs))
            return fail("Gallery data requirements invalid: " + req_errs[0]);
    }
    if (std::set<std::string>(names.begin(), names.end()) != expected) return fail("Gallery contains unlisted or missing files.");
    std::vector<std::string> fmts(declared_formats.begin(), declared_formats.end());
    if (!(manifest.get("formats") == make_format_catalog(fmts))) return fail("Gallery format catalog does not match its faces.");

    out.ok = true;
    out.manifest = std::move(manifest);
    return out;
}

InspectedGallery inspect_ttgallery_file(const std::string& path) {
    Bytes data;
    std::string error;
    if (!read_file(path, data, kMaxGalleryBytes + 4096, error)) {
        InspectedGallery g;
        g.errors.push_back("Gallery inspection failed: " + error);
        return g;
    }
    return inspect_ttgallery_bytes(data);
}

bool plan_gallery_changes(const Json& installed, const Json& desired, GalleryPlan& plan, std::string& error) {
    auto hashes = [&](const Json& manifest, std::map<std::string, std::string>& out) {
        const Json& faces = manifest.get("faces");
        if (!faces.is_array()) { error = "Gallery manifest faces must be a list."; return false; }
        for (const Json& face : faces.items()) {
            const Json& id = face.get("id");
            const Json& sha = face.get("sha256");
            bool sha_ok = sha.is_string() && sha.as_string().size() == 64 &&
                          std::all_of(sha.as_string().begin(), sha.as_string().end(),
                                      [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
            if (!face.is_object() || !id.is_string() || !is_safe_id(id.as_string()) || !sha_ok) {
                error = "Gallery manifest contains an invalid face descriptor.";
                return false;
            }
            if (out.count(id.as_string())) { error = "Gallery manifest contains duplicate face IDs."; return false; }
            out[id.as_string()] = sha.as_string();
        }
        return true;
    };
    std::map<std::string, std::string> old_hashes, new_hashes;
    if (!hashes(installed, old_hashes) || !hashes(desired, new_hashes)) return false;
    plan = GalleryPlan();
    for (const auto& kv : new_hashes) {
        auto it = old_hashes.find(kv.first);
        if (it == old_hashes.end()) plan.add.push_back(kv.first);
        else if (it->second != kv.second) plan.update.push_back(kv.first);
    }
    for (const auto& kv : old_hashes)
        if (!new_hashes.count(kv.first)) plan.remove.push_back(kv.first);
    return true;
}

}  // namespace tt
