#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>

#include "testing.h"

#include "../src/core/canvas.h"
#include "../src/core/codec.h"
#include "../src/core/deflate.h"
#include "../src/core/fileio.h"
#include "../src/core/gallery.h"
#include "../src/core/image.h"
#include "../src/core/json.h"
#include "../src/core/model.h"
#include "../src/core/package.h"
#include "../src/core/png.h"
#include "../src/core/render.h"
#include "../src/core/zip.h"

namespace testing {
std::vector<Case>& registry() { static std::vector<Case> r; return r; }
int& failures() { static int f = 0; return f; }
}  // namespace testing

using namespace tt;

static Json parse_or_die(const std::string& text) {
    Json j;
    std::string err;
    if (!Json::parse(text, j, err)) { std::printf("    parse error: %s\n", err.c_str()); ++testing::failures(); }
    return j;
}

static std::string temp_dir() {
    static std::string dir;
    if (dir.empty()) {
        auto p = std::filesystem::temp_directory_path() / ("ttfs-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(p);
        dir = p.string();
    }
    return dir;
}
static std::string tmp(const std::string& name) { return (std::filesystem::path(temp_dir()) / name).string(); }

// A project equivalent to the one used by the Python gallery tests.
static Json make_project(const std::string& name, const std::string& layout) {
    return parse_or_die(R"({
      "metadata": {"name": ")" + name + R"(", "version": "1.0.0", "author": "Tests"},
      "canvas": {"width": 320, "height": 240},
      "display": {"formats": [")" + layout + R"("], "default_format": ")" + layout + R"("},
      "background_color": "#101020",
      "elements": [{"id": "clock", "type": "digital_time", "format": "HH:MM", "x": 60, "y": 60,
                    "width": 200, "height": 100, "color": "#FFFFFF", "font_size": 36, "is_12h": false, "rule": null}],
      "rules": []})");
}

static bool has_error(const std::vector<std::string>& errors, const char* needle) {
    for (const std::string& e : errors)
        if (e.find(needle) != std::string::npos) return true;
    return false;
}

// ------------------------------------------------------------------ codecs
TEST(sha256_crc_adler_base64_vectors) {
    CHECK_EQ(sha256_hex(std::string("abc")), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK_EQ(sha256_hex(std::string("")), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    std::string two_blocks(56, 'a');  // forces the 128-byte padding tail
    CHECK_EQ(sha256_hex(two_blocks), std::string("b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"));
    const char* s = "123456789";
    CHECK_EQ(crc32(reinterpret_cast<const uint8_t*>(s), 9), 0xCBF43926u);
    const char* w = "Wikipedia";
    CHECK_EQ(adler32(reinterpret_cast<const uint8_t*>(w), 9), 0x11E60398u);
    Bytes decoded;
    CHECK(base64_decode("aGVsbG8=", decoded));
    CHECK_EQ(to_string(decoded), std::string("hello"));
    CHECK_EQ(base64_encode(to_bytes("hello!?")), std::string("aGVsbG8hPw=="));
    CHECK(!base64_decode("aGVsbG8", decoded));   // bad padding
    CHECK(!base64_decode("aGV$bG8=", decoded));  // bad alphabet
}

TEST(inflate_matches_python_zlib_and_roundtrips) {
    // zlib.compress(b"hello hello hello hello hello") from CPython
    const uint8_t vec[] = {0x78, 0x9c, 0xcb, 0x48, 0xcd, 0xc9, 0xc9, 0x57, 0xc8, 0xc0, 0x4e, 0x02, 0x00, 0xa3, 0x10, 0x0a, 0xe5};
    Bytes out;
    CHECK(zlib_decompress(vec, sizeof vec, out, 1024));
    CHECK_EQ(to_string(out), std::string("hello hello hello hello hello"));
    // Round trip incompressible + repetitive data through our own encoder.
    Bytes data;
    uint32_t x = 12345;
    for (int i = 0; i < 70000; ++i) { x = x * 1664525u + 1013904223u; data.push_back(i % 5000 < 2500 ? uint8_t(i % 7) : uint8_t(x >> 24)); }
    Bytes packed = zlib_compress(data.data(), data.size());
    Bytes back;
    CHECK(zlib_decompress(packed.data(), packed.size(), back, data.size()));
    CHECK(back == data);
    // Output cap and corruption are rejected.
    CHECK(!zlib_decompress(packed.data(), packed.size(), back, data.size() - 1));
    Bytes corrupt = packed;
    corrupt[corrupt.size() / 2] ^= 0x55;
    Bytes tmp_out;
    CHECK(!zlib_decompress(corrupt.data(), corrupt.size(), tmp_out, data.size()) || tmp_out != data);
}

TEST(png_roundtrip) {
    Image img(37, 19);
    for (int y = 0; y < img.h; ++y)
        for (int x = 0; x < img.w; ++x) img.set(x, y, Rgb{uint8_t(x * 7), uint8_t(y * 13), uint8_t(x ^ y)});
    Bytes png = png_encode(img);
    Image back;
    std::string err;
    CHECK(png_decode(png, back, err));
    CHECK(back == img);
    int w = 0, h = 0;
    CHECK(png_dimensions(png, w, h) && w == 37 && h == 19);
    png[png.size() - 20] ^= 1;  // corrupt inside IDAT/IEND -> CRC mismatch
    CHECK(!png_decode(png, back, err));
}

// -------------------------------------------------------------------- JSON
TEST(json_python_semantics) {
    Json j = parse_or_die(R"({"a": 10, "b": 10.0, "c": true, "d": "é\u00e9\ud83d\ude00", "e": [1, 2.5, null]})");
    CHECK(j.get("a").is_int());
    CHECK(j.get("b").is_float() && !j.get("b").is_int());
    CHECK(j.get("c").is_bool() && !j.get("c").is_int());
    CHECK(j.get("a") == j.get("b"));  // 10 == 10.0 in Python
    CHECK(j.get("c") == Json::integer(1));
    CHECK_EQ(j.dump(-1, true), std::string("{\"a\": 10, \"b\": 10.0, \"c\": true, \"d\": \"\\u00e9\\u00e9\\ud83d\\ude00\", \"e\": [1, 2.5, null]}"));
    Json bad;
    std::string err;
    CHECK(!Json::parse("{\"a\": 1,}", bad, err));
    CHECK(!Json::parse("[1, 2", bad, err));
    CHECK(!Json::parse("\"\\ud800\"", bad, err));    // lone surrogate
    CHECK(!Json::parse("\"\xC0\xAF\"", bad, err));   // overlong UTF-8
    CHECK(!Json::parse("01", bad, err));
    CHECK(!Json::parse("{} x", bad, err));
    std::string deep(200, '[');
    CHECK(!Json::parse(deep, bad, err));             // depth limited
    Json obj = parse_or_die("{\"z\": 1, \"a\": {\"y\": [], \"b\": {}}}");
    CHECK_EQ(obj.dump(2), std::string("{\n  \"z\": 1,\n  \"a\": {\n    \"y\": [],\n    \"b\": {}\n  }\n}"));
}

// ------------------------------------------------------------- validation
TEST(validation_messages_match_reference) {
    Json invalid = parse_or_die(R"({
      "metadata": {"name": "Bad Face"}, "canvas": {"width": 320, "height": 240},
      "background": {"file": "wrong_path.rgb565"},
      "elements": [
        {"type": "digital_time", "x": -10, "y": 0, "width": 100, "height": 40},
        {"type": "text", "x": 250, "y": 200, "width": 100, "height": 50},
        {"type": "date", "x": "10", "y": 10, "width": 100, "height": 40}],
      "rules": [{"name": "r1", "signal": "cpu_temperature", "op": "=="},
                {"name": "r2", "signal": "battery_percent", "op": "~="}]})");
    auto errors = validate_face_project(invalid);
    CHECK(has_error(errors, "extends outside 320x240 screen boundary"));
    CHECK(has_error(errors, "field 'x' must be an integer"));
    CHECK(has_error(errors, "unsupported or malformed signal"));
    CHECK(has_error(errors, "unsupported operator"));
    CHECK(has_error(errors, "must match ZIP entry 'assets/bg.rgb565'"));
    CHECK(has_error(errors, "Element #1 ('unnamed') extends outside"));
    CHECK(has_error(errors, "Supported: ['battery_low', 'battery_percent', 'charging', 'gps_fix', 'gps_status', 'weather_status', 'weather_unavailable']"));
    CHECK(has_error(errors, "Supported: ['!=', '<', '<=', '==', '>', '>=']"));
}

TEST(validation_type_strictness) {
    Json p = make_project("T", "horizontal");
    p.find("elements")->items()[0].set("x", Json::number(60.0));  // float, not int
    CHECK(has_error(validate_face_project(p), "field 'x' must be an integer"));
    p = make_project("T", "horizontal");
    p.find("elements")->items()[0].set("x", Json::boolean(true));
    CHECK(has_error(validate_face_project(p), "field 'x' must be an integer"));
    p = make_project("T", "horizontal");
    p.find("canvas")->set("width", Json::number(320.0));
    CHECK(has_error(validate_face_project(p), "Canvas dimensions must be integer 320x240. Found 320.0x240."));
    p = make_project("T", "horizontal");
    p.find("elements")->items()[0].set("font_size", Json::integer(0));
    CHECK(has_error(validate_face_project(p), "font_size must be a positive integer"));
    CHECK(has_error(validate_face_project(parse_or_die("[]")), "Project data must be a JSON object."));
    p = make_project("", "horizontal");
    p.find("metadata")->set("name", Json::string(""));
    CHECK(has_error(validate_face_project(p), "Project name must be a non-empty string."));
    CHECK(validate_face_project(make_project("Ok", "horizontal")).empty());
}

TEST(validation_formats_renderers_and_data_requirements) {
    Json p = make_project("F", "horizontal");
    p.find("display")->set("formats", parse_or_die("[\"diagonal\"]"));
    p.find("display")->set("default_format", Json::string("diagonal"));
    CHECK(has_error(validate_face_project(p), "unsupported format ID"));
    p = make_project("F", "horizontal");
    p.find("display")->set("default_format", Json::string("stacked"));
    CHECK(has_error(validate_face_project(p), "Default display format must be listed as supported."));
    p = make_project("F", "horizontal");
    p.find("display")->set("formats", parse_or_die("[\"horizontal\", \"horizontal\"]"));
    CHECK(has_error(validate_face_project(p), "must not contain duplicates"));
    p = make_project("F", "horizontal");
    p.set("renderer_id", Json::string("opentom.unavailable.renderer.v1"));
    CHECK(has_error(validate_face_project(p), "Unsupported face renderer"));
    p = make_project("F", "horizontal");
    p.set("data_requirements", parse_or_die("[\"battery.percent\", \"battery.percent\", \"bogus\", 3]"));
    auto e = validate_face_project(p);
    CHECK(has_error(e, "Duplicate data field requirement 'battery.percent'"));
    CHECK(has_error(e, "Unknown data field requirement 'bogus'"));
    CHECK(has_error(e, "data_requirements[3] must be a string."));
    // Face with no digital clock and no formats is fine; a default format alone is not.
    Json no_clock = parse_or_die(R"({"metadata": {"name": "n"}, "canvas": {"width": 320, "height": 240},
                                     "display": {"formats": [], "default_format": "horizontal"}, "elements": []})");
    CHECK(has_error(validate_face_project(no_clock), "cannot set a default format"));
    Json clock = parse_or_die(R"({"metadata": {"name": "n"}, "canvas": {"width": 320, "height": 240},
                                  "display": {"formats": []},
                                  "elements": [{"type": "digital_time", "x": 0, "y": 0, "width": 10, "height": 10}]})");
    CHECK(has_error(validate_face_project(clock), "must support at least one display format"));
}

TEST(rule_engine_fail_closed) {
    Json rules = parse_or_die(R"([{"name": "battery_low", "signal": "battery_percent", "op": "<=", "value": 20},
        {"name": "invalid_op_rule", "signal": "battery_percent", "op": "INVALID_OP", "value": 20},
        {"name": "invalid_sig_rule", "signal": "unsupported_sig", "op": "==", "value": 1},
        {"name": "mid", "signal": "battery_percent", "op": ">", "value": 50}])");
    auto vis = [&](const char* rule, const char* state_json) {
        Json el = Json::object();
        el.set("rule", rule ? Json::string(rule) : Json());
        return RuleEngine::is_element_visible(el, rules, parse_or_die(state_json));
    };
    CHECK(vis("battery_low", R"({"battery_percent": 15})"));
    CHECK(!vis("battery_low", R"({"battery_percent": 80})"));
    CHECK(vis("weather_unavailable", R"({"weather_status": 0})"));
    CHECK(vis("gps_fix", R"({"gps_status": 1})"));
    CHECK(!vis("non_existent_rule", R"({"battery_percent": 10})"));
    CHECK(!vis("invalid_op_rule", R"({"battery_percent": 10})"));
    CHECK(!vis("invalid_sig_rule", R"({"battery_percent": 10})"));
    CHECK(vis("mid", R"({"battery_percent": 70})"));
    CHECK(!vis("mid", R"({"battery_percent": "high"})"));  // incomparable types fail closed
    CHECK(vis(nullptr, R"({})"));                          // no rule bound -> visible
    CHECK(vis("", R"({})"));
}

// ------------------------------------------------------------------ canvas
TEST(canvas_draw_undo_redo_fill) {
    CanvasModel c("#000000");
    CHECK_EQ(c.get_pixel_color(10, 10), std::string("#000000"));
    c.push_history();
    c.draw_pixel(10, 10, "#FF0000", 1);
    CHECK_EQ(c.get_pixel_color(10, 10), std::string("#FF0000"));
    CHECK(c.undo());
    CHECK_EQ(c.get_pixel_color(10, 10), std::string("#000000"));
    CHECK(c.redo());
    CHECK_EQ(c.get_pixel_color(10, 10), std::string("#FF0000"));
    c.draw_pixel(-5, 3, "#00FF00");  // clipped silently
    CHECK_EQ(c.get_pixel_color(-5, 3), std::string("#000000"));
    c.draw_pixel(100, 100, "#0000FF", 4);  // 5x5 brush (half = 2)
    CHECK_EQ(c.get_pixel_color(98, 98), std::string("#0000FF"));
    CHECK_EQ(c.get_pixel_color(102, 102), std::string("#0000FF"));
    CHECK_EQ(c.get_pixel_color(103, 100), std::string("#000000"));
    c.flood_fill(0, 0, "#123456");
    CHECK_EQ(c.get_pixel_color(319, 239), std::string("#123456"));
    CHECK_EQ(c.get_pixel_color(10, 10), std::string("#FF0000"));  // barrier kept
    CHECK(c.undo());
    CHECK_EQ(c.get_pixel_color(319, 239), std::string("#000000"));
    for (int i = 0; i < 40; ++i) c.push_history();
    int undone = 0;
    while (c.undo()) ++undone;
    CHECK(undone <= 29);  // MAX_HISTORY bound
}

TEST(canvas_import_fit_modes) {
    RgbaImage red;
    red.w = 160; red.h = 240;
    red.px.assign(static_cast<size_t>(red.w) * red.h * 4, 0);
    for (size_t i = 0; i < red.px.size(); i += 4) { red.px[i] = 255; red.px[i + 3] = 255; }
    CanvasModel c("#000000");
    std::string err;
    CHECK(c.import_background(red, "contain", 0, 0, err));  // 160x240 -> 160x240, centred
    CHECK_EQ(c.get_pixel_color(160, 120), std::string("#FF0000"));
    CHECK_EQ(c.get_pixel_color(10, 120), std::string("#000000"));
    CHECK(c.import_background(red, "cover", 0, 0, err));    // scaled to 320x480, cropped
    CHECK_EQ(c.get_pixel_color(5, 5), std::string("#FF0000"));
    CHECK(c.import_background(red, "stretch", 0, 0, err));
    CHECK_EQ(c.get_pixel_color(319, 239), std::string("#FF0000"));
    CHECK(c.import_background(red, "custom", 200, 100, err));
    CHECK_EQ(c.get_pixel_color(250, 150), std::string("#FF0000"));
    CHECK_EQ(c.get_pixel_color(100, 150), std::string("#000000"));
    // Half-transparent pixels blend with the background colour.
    RgbaImage half = red;
    for (size_t i = 3; i < half.px.size(); i += 4) half.px[i] = 128;
    CanvasModel d("#000000");
    CHECK(d.import_background(half, "stretch", 0, 0, err));
    std::string mid = d.get_pixel_color(160, 120);
    CHECK(mid == "#800000" || mid == "#7F0000");
    // Degenerate aspect ratios fail cleanly without touching the canvas.
    RgbaImage sliver;
    sliver.w = 1; sliver.h = 20000;
    sliver.px.assign(static_cast<size_t>(sliver.h) * 4, 255);
    CanvasModel e("#000000");
    CHECK(!e.import_background(sliver, "contain", 0, 0, err));
    CHECK_EQ(e.get_pixel_color(0, 0), std::string("#000000"));
}

// ---------------------------------------------------------------- packages
TEST(rgb565_conversion) {
    CHECK_EQ(color_hex_to_rgb565("#FF0000"), uint16_t(0xF800));
    CHECK_EQ(color_hex_to_rgb565("#00FF00"), uint16_t(0x07E0));
    CHECK_EQ(color_hex_to_rgb565("#nothex"), uint16_t(0xFFFF));
    CHECK_EQ(color_hex_to_rgb565("#FFF"), uint16_t(0xFFFF));
    Image red(320, 240, Rgb{255, 0, 0});
    Bytes b = image_to_rgb565_bytes(red);
    CHECK_EQ(b.size(), size_t(320 * 240 * 2));
    CHECK(b[0] == 0x00 && b[1] == 0xF8);  // little endian 0xF800
    Image small(2, 2, Rgb{0, 0, 255});
    small.set(1, 0, Rgb{255, 255, 255});
    Bytes scaled = image_to_rgb565_bytes(small);  // nearest-neighbour upscale
    CHECK(scaled[(0 * 320 + 200) * 2] == 0xFF && scaled[(0 * 320 + 200) * 2 + 1] == 0xFF);
    CHECK(scaled[(0 * 320 + 10) * 2] == 0x1F && scaled[(0 * 320 + 10) * 2 + 1] == 0x00);
}

TEST(ttface_export_and_inspect_roundtrip) {
    Json project = make_project("Cyberpunk Sample", "stacked");
    project.find("display")->set("formats", parse_or_die("[\"horizontal\", \"stacked\"]"));
    project.set("data_requirements", parse_or_die("[\"battery.percent\", \"gps.fix\"]"));
    Image bg(320, 240, Rgb{16, 16, 32});
    std::string path = tmp("sample.ttface");
    ExportResult r = export_ttface_package(project, &bg, path);
    CHECK(r.ok);
    InspectedFace f = inspect_ttface_file(path);
    CHECK(f.loaded);
    CHECK(f.errors.empty());
    CHECK_EQ(f.manifest.get("face_name").as_string(), std::string("Cyberpunk Sample"));
    CHECK_EQ(f.manifest.get("renderer_id").as_string(), std::string("ttface.elements.v1"));
    CHECK_EQ(f.manifest.get("background").get("file").as_string(), std::string("assets/bg.rgb565"));
    CHECK_EQ(f.manifest.get("target_device").as_string(), std::string("TomTom ONE v6 (Model 19)"));
    CHECK_EQ(f.manifest.get("display").get("default_format").as_string(), std::string("stacked"));
    CHECK(f.has_background && f.background.w == 320 && f.background.h == 240);
    // Manifest key order matches the reference.
    std::vector<std::string> keys = f.manifest.keys();
    std::vector<std::string> want = {"manifest_version", "face_name", "version", "author", "target_device", "renderer_id",
                                     "canvas", "display", "data_requirements", "background", "elements", "rules"};
    CHECK(keys == want);
    // Solid background from background_color when no image is supplied.
    Bytes zip;
    CHECK(build_ttface_package(project, nullptr, zip).ok);
    ZipReader zr;
    std::string err;
    CHECK(zr.open(zip, err));
    Bytes raw;
    CHECK(zr.read_named("assets/bg.rgb565", raw, err));
    CHECK_EQ(raw.size(), size_t(153600));
    CHECK(raw[0] == 0x84 && raw[1] == 0x10);  // #101020 -> RGB565 0x1084 (little endian)
    // Invalid projects are blocked and nothing is written.
    Json bad = make_project("Bad", "horizontal");
    bad.find("elements")->items()[0].set("x", Json::integer(-1));
    ExportResult rb = export_ttface_package(bad, &bg, tmp("bad.ttface"));
    CHECK(!rb.ok);
    CHECK(!file_exists(tmp("bad.ttface")));
}

TEST(ttface_inspect_rejects_broken_packages) {
    InspectedFace missing = inspect_ttface_file(tmp("does-not-exist.ttface"));
    CHECK(!missing.loaded && has_error(missing.errors, "does not exist"));
    ZipWriter zw;
    zw.add("readme.txt", to_bytes("x"));
    InspectedFace no_manifest = inspect_ttface_bytes(zw.finish());
    CHECK(!no_manifest.loaded && has_error(no_manifest.errors, "Missing 'manifest.json'"));
    Bytes garbage = to_bytes("this is not a zip file at all, just text padding to exceed 22 bytes");
    CHECK(!inspect_ttface_bytes(garbage).loaded);
    // Manifest references a background entry the archive does not contain.
    Json project = make_project("NoBg", "horizontal");
    Bytes zip;
    CHECK(build_ttface_package(project, nullptr, zip).ok);
    ZipReader zr;
    std::string err;
    CHECK(zr.open(zip, err));
    Bytes manifest_bytes;
    CHECK(zr.read_named("manifest.json", manifest_bytes, err));
    ZipWriter only_manifest;
    only_manifest.add("manifest.json", manifest_bytes);
    InspectedFace nb = inspect_ttface_bytes(only_manifest.finish());
    CHECK(nb.loaded && !nb.has_background);
    CHECK(has_error(nb.errors, "listed in manifest is missing from ZIP entries"));
}

TEST(zip_reader_hardening) {
    ZipWriter zw;
    zw.add("a.txt", to_bytes(std::string(1000, 'x')));
    Bytes zip = zw.finish();
    ZipReader ok;
    std::string err;
    CHECK(ok.open(zip, err));
    Bytes out;
    CHECK(ok.read_named("a.txt", out, err) && out.size() == 1000);
    // Flip a payload byte: CRC / inflate must fail.
    Bytes bad = zip;
    bad[35] ^= 0xFF;
    ZipReader br;
    CHECK(br.open(bad, err));
    CHECK(!br.read_named("a.txt", out, err));
    // Lie about the uncompressed size in the central directory.
    Bytes lie = zip;
    for (size_t i = 0; i + 4 < lie.size(); ++i)
        if (lie[i] == 0x50 && lie[i + 1] == 0x4b && lie[i + 2] == 0x01 && lie[i + 3] == 0x02) { lie[i + 24] = 0x10; break; }
    ZipReader lr;
    CHECK(lr.open(lie, err));
    CHECK(!lr.read_named("a.txt", out, err));
    CHECK(!ok.read_named("missing", out, err));
    Bytes truncated(zip.begin(), zip.begin() + 20);
    ZipReader tr;
    CHECK(!tr.open(truncated, err));
}

// ----------------------------------------------------------------- gallery
static std::string make_face_file(const std::string& name, const std::string& layout, const char* renderer = nullptr) {
    Json p = make_project(name, layout);
    if (renderer) p.set("renderer_id", Json::string(renderer));
    Image bg(320, 240, Rgb{16, 16, 32});
    std::string path = tmp(name + ".ttface");
    ExportResult r = export_ttface_package(p, &bg, path);
    if (!r.ok) std::printf("    export failed for %s\n", name.c_str());
    return path;
}

TEST(gallery_build_inspect_and_manifest) {
    std::string a = make_face_file("Roboto Face", "horizontal");
    std::string b = make_face_file("Ubuntu Face", "stacked");
    std::string out = tmp("collection.ttgallery");
    GalleryResult r = export_ttgallery({a, b}, out, "My Collection");
    CHECK(r.ok);
    InspectedGallery g = inspect_ttgallery_file(out);
    CHECK(g.ok);
    CHECK(g.errors.empty());
    CHECK_EQ(g.manifest.get("face_count").as_int(), int64_t(2));
    CHECK_EQ(g.manifest.get("runtime_api").as_string(), std::string("ttface-manifest-v2"));
    CHECK_EQ(g.manifest.get("faces").items()[0].get("id").as_string(), std::string("roboto-face"));
    CHECK_EQ(g.manifest.get("faces").items()[1].get("default_format").as_string(), std::string("stacked"));
    CHECK_EQ(g.manifest.get("formats").items().size(), size_t(2));
    CHECK_EQ(g.manifest.get("faces").items()[0].get("sha256").as_string().size(), size_t(64));
    // Archive layout
    Bytes data; std::string err;
    CHECK(read_file(out, data, 1u << 26, err));
    ZipReader zr;
    CHECK(zr.open(data, err));
    CHECK(zr.find("gallery.json") && zr.find("faces/roboto-face.ttface") && zr.find("previews/ubuntu-face.png"));
    CHECK_EQ(zr.entries().size(), size_t(5));
    Bytes preview;
    CHECK(zr.read_named("previews/ubuntu-face.png", preview, err));
    Image decoded;
    CHECK(png_decode(preview, decoded, err) && decoded.w == 160 && decoded.h == 120);
    // The preview must actually show the clock (more than one colour).
    bool multi = false;
    for (int y = 0; y < decoded.h && !multi; ++y)
        for (int x = 0; x < decoded.w; ++x)
            if (decoded.get(x, y) != decoded.get(0, 0)) { multi = true; break; }
    CHECK(multi);
    // gallery.json is written with sorted keys.
    Bytes gj;
    CHECK(zr.read_named("gallery.json", gj, err));
    std::string text = to_string(gj);
    CHECK(text.find("\"data_requirements\"") < text.find("\"face_count\""));
    CHECK(text.find("\"face_count\"") < text.find("\"faces\""));
}

TEST(gallery_legacy_renderers_and_rejections) {
    std::string legacy = make_face_file("Hydro Aqua Wave", "horizontal", "opentom.builtin.hydro-aqua.v1");
    std::string out = tmp("legacy.ttgallery");
    CHECK(export_ttgallery({legacy}, out, "Legacy").ok);
    InspectedGallery g = inspect_ttgallery_file(out);
    CHECK(g.ok);
    CHECK_EQ(g.manifest.get("faces").items()[0].get("renderer_id").as_string(), std::string("opentom.builtin.hydro-aqua.v1"));
    // Duplicate IDs
    std::string first = make_face_file("Same Name", "horizontal");
    Json p2 = make_project("Same Name", "stacked");
    Image bg(320, 240, Rgb{1, 2, 3});
    std::string second = tmp("same-name-2.ttface");
    CHECK(export_ttface_package(p2, &bg, second).ok);
    std::string dup_out = tmp("dup.ttgallery");
    GalleryResult dup = export_ttgallery({first, second}, dup_out, "Dup");
    CHECK(!dup.ok);
    CHECK(has_error(dup.errors, "unique safe IDs"));
    CHECK(!file_exists(dup_out));
    CHECK(!export_ttgallery({first}, tmp("x.ttgallery"), "   ").ok);
    CHECK(!export_ttgallery({}, tmp("x.ttgallery"), "Empty").ok);
    // A package with an extra entry is not a valid face package.
    ZipWriter extra;
    extra.add("manifest.json", to_bytes("{}"));
    extra.add("assets/bg.rgb565", Bytes(153600));
    extra.add("evil.bin", to_bytes("x"));
    std::string extra_path = tmp("extra.ttface");
    std::string err;
    CHECK(write_file(extra_path, extra.finish(), err));
    GalleryResult er = export_ttgallery({extra_path}, tmp("e.ttgallery"), "E");
    CHECK(!er.ok && has_error(er.errors, "only manifest.json and assets/bg.rgb565"));
}

TEST(gallery_inspection_detects_tampering) {
    std::string face = make_face_file("Integrity Face", "stacked");
    std::string out = tmp("integrity.ttgallery");
    CHECK(export_ttgallery({face}, out, "Integrity").ok);
    Bytes data; std::string err;
    CHECK(read_file(out, data, 1u << 26, err));
    ZipReader zr;
    CHECK(zr.open(data, err));

    auto rebuild = [&](const std::string& drop, const std::string& add_name, const Bytes& add_data, bool flip_face) {
        ZipWriter zw;
        for (const ZipEntry& e : zr.entries()) {
            Bytes content;
            std::string e2;
            zr.read(e, content, e2);
            if (e.name == drop) continue;
            if (flip_face && e.name == "faces/integrity-face.ttface") content.back() ^= 1;
            zw.add(e.name, content);
        }
        if (!add_name.empty()) zw.add(add_name, add_data);
        return inspect_ttgallery_bytes(zw.finish());
    };
    InspectedGallery clean = rebuild("", "", Bytes(), false);
    CHECK(clean.ok);
    InspectedGallery flipped = rebuild("", "", Bytes(), true);
    CHECK(!flipped.ok && has_error(flipped.errors, "checksum"));
    InspectedGallery traversal = rebuild("", "../outside", to_bytes("x"), false);
    CHECK(!traversal.ok && has_error(traversal.errors, "unsafe path"));
    InspectedGallery unlisted = rebuild("", "extra.bin", to_bytes("x"), false);
    CHECK(!unlisted.ok && has_error(unlisted.errors, "unlisted or missing files"));
    InspectedGallery missing = rebuild("previews/integrity-face.png", "", Bytes(), false);
    CHECK(!missing.ok);
    InspectedGallery no_json = rebuild("gallery.json", "", Bytes(), false);
    CHECK(!no_json.ok && has_error(no_json.errors, "missing gallery.json"));
}

TEST(gallery_replacement_plan_includes_removals) {
    Json installed = parse_or_die("{\"faces\": [{\"id\": \"keep\", \"sha256\": \"" + std::string(64, 'a') + "\"}, {\"id\": \"change\", \"sha256\": \"" +
                                  std::string(64, 'b') + "\"}, {\"id\": \"remove\", \"sha256\": \"" + std::string(64, 'c') + "\"}]}");
    Json desired = parse_or_die("{\"faces\": [{\"id\": \"keep\", \"sha256\": \"" + std::string(64, 'a') + "\"}, {\"id\": \"change\", \"sha256\": \"" +
                                std::string(64, 'd') + "\"}, {\"id\": \"add\", \"sha256\": \"" + std::string(64, 'e') + "\"}]}");
    GalleryPlan plan;
    std::string err;
    CHECK(plan_gallery_changes(installed, desired, plan, err));
    CHECK(plan.add == std::vector<std::string>{"add"});
    CHECK(plan.update == std::vector<std::string>{"change"});
    CHECK(plan.remove == std::vector<std::string>{"remove"});
    Json bad = parse_or_die("{\"faces\": [{\"id\": \"x\", \"sha256\": \"nothex\"}]}");
    CHECK(!plan_gallery_changes(bad, desired, plan, err));
}

TEST(ttproj_save_and_load) {
    Json project = make_project("Proj", "horizontal");
    Image canvas(320, 240, Rgb{0x84, 0xEB, 0xFF});
    canvas.set(5, 6, Rgb{1, 2, 3});
    std::string path = tmp("sample.ttproj");
    std::string err;
    CHECK(save_project_file(path, project, canvas, err));
    Json loaded;
    Image back;
    CHECK(load_project_file(path, loaded, back, err));
    CHECK(loaded == project);
    CHECK(back == canvas);
    Bytes junk = to_bytes("{\"project\": {}, \"canvas_png_b64\": \"!!!\"}");
    CHECK(write_file(tmp("bad.ttproj"), junk, err));
    CHECK(!load_project_file(tmp("bad.ttproj"), loaded, back, err));
    // Missing canvas image falls back to background_color.
    CHECK(write_file(tmp("nocanvas.ttproj"), to_bytes("{\"project\": {\"background_color\": \"#102030\"}}"), err));
    CHECK(load_project_file(tmp("nocanvas.ttproj"), loaded, back, err));
    CHECK_EQ(rgb_to_hex(back.get(0, 0)), std::string("#102030"));
}

TEST(render_preview_draws_elements_and_honours_rules) {
    Json project = parse_or_die(R"({"metadata": {"name": "R"}, "canvas": {"width": 320, "height": 240},
      "display": {"formats": ["horizontal"], "default_format": "horizontal"},
      "elements": [
        {"id": "t", "type": "digital_time", "format": "HH:MM", "x": 20, "y": 20, "width": 200, "height": 60, "color": "#FF0000", "font_size": 48, "is_12h": false, "rule": null},
        {"id": "b", "type": "status_icon", "icon_type": "battery_low", "x": 280, "y": 10, "width": 24, "height": 24, "color": "#00FF00", "rule": "battery_low"}],
      "rules": []})");
    Image blank(320, 240, Rgb{0, 0, 0});
    SimTime t;
    Json low = parse_or_die(R"({"battery_percent": 10})");
    Json full = parse_or_die(R"({"battery_percent": 90})");
    Image with_icon = compose_preview(blank, project, low, t, 0);
    Image without_icon = compose_preview(blank, project, full, t, 0);
    auto count = [](const Image& img, Rgb c, int x0, int y0, int x1, int y1) {
        int n = 0;
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) n += img.get(x, y) == c;
        return n;
    };
    CHECK(count(with_icon, Rgb{0, 255, 0}, 270, 0, 320, 50) > 20);
    CHECK_EQ(count(without_icon, Rgb{0, 255, 0}, 270, 0, 320, 50), 0);
    CHECK(count(with_icon, Rgb{255, 0, 0}, 0, 0, 260, 100) > 200);
    CHECK_EQ(t.weekday(), 3);  // 2026-01-01 is a Thursday
}

int main() {
    int passed = 0;
    for (const testing::Case& c : testing::registry()) {
        int before = testing::failures();
        std::printf("[ RUN  ] %s\n", c.name);
        std::fflush(stdout);
        c.fn();
        if (testing::failures() == before) { ++passed; std::printf("[  OK  ] %s\n", c.name); }
        else std::printf("[ FAIL ] %s\n", c.name);
    }
    std::printf("\n%d/%zu tests passed\n", passed, testing::registry().size());
    std::error_code ec;
    std::filesystem::remove_all(temp_dir(), ec);
    return testing::failures() == 0 ? 0 : 1;
}
