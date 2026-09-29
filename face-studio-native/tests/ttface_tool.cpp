// Command-line harness around the core library. Used by
// tests/parity_check.py to cross-validate the native code against the Python
// reference implementation; not shipped to end users.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../src/core/fileio.h"
#include "../src/core/gallery.h"
#include "../src/core/model.h"
#include "../src/core/package.h"
#include "../src/core/png.h"

using namespace tt;

static bool load_json(const char* path, Json& out) {
    Bytes data;
    std::string err;
    if (!read_file(path, data, 32u << 20, err) || !Json::parse(to_string(data), out, err)) {
        std::fprintf(stderr, "cannot load %s: %s\n", path, err.c_str());
        return false;
    }
    return true;
}

static int print_lines(const std::vector<std::string>& lines) {
    for (const std::string& l : lines) std::printf("%s\n", l.c_str());
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: ttface_tool <validate|export-ttface|inspect-ttface|export-ttgallery|inspect-ttgallery|plan> ...\n");
        return 2;
    }
    const std::string cmd = argv[1];
    if (cmd == "validate") {  // one error per line; nothing when valid
        Json project;
        if (!load_json(argv[2], project)) return 2;
        return print_lines(validate_face_project(project));
    }
    if (cmd == "export-ttface" && argc == 5) {  // <project.json> <bg.png|-> <out>
        Json project;
        if (!load_json(argv[2], project)) return 2;
        Image bg;
        const Image* bgp = nullptr;
        if (std::strcmp(argv[3], "-") != 0) {
            Bytes png;
            std::string err;
            if (!read_file(argv[3], png, 32u << 20, err) || !png_decode(png, bg, err)) {
                std::fprintf(stderr, "cannot load background: %s\n", err.c_str());
                return 2;
            }
            bgp = &bg;
        }
        ExportResult r = export_ttface_package(project, bgp, argv[4]);
        print_lines(r.messages);
        return r.ok ? 0 : 1;
    }
    if (cmd == "inspect-ttface") {  // prints errors, then "loaded=<0|1> name=<face_name>"
        InspectedFace f = inspect_ttface_file(argv[2]);
        print_lines(f.errors);
        std::printf("loaded=%d name=%s bg=%d\n", f.loaded ? 1 : 0,
                    f.loaded && f.manifest.get("face_name").is_string() ? f.manifest.get("face_name").as_string().c_str() : "",
                    f.has_background ? 1 : 0);
        return 0;
    }
    if (cmd == "export-ttgallery" && argc >= 6) {  // <out> <name> <version> <face>...
        std::vector<std::string> faces;
        for (int i = 5; i < argc; ++i) faces.push_back(argv[i]);
        GalleryResult r = export_ttgallery(faces, argv[2], argv[3], argv[4]);
        print_lines(r.errors);
        return r.ok ? 0 : 1;
    }
    if (cmd == "inspect-ttgallery") {  // prints errors or "ok faces=<n>"
        InspectedGallery g = inspect_ttgallery_file(argv[2]);
        print_lines(g.errors);
        if (g.ok) std::printf("ok faces=%d\n", static_cast<int>(g.manifest.get("face_count").as_int()));
        return 0;
    }
    if (cmd == "plan" && argc == 4) {
        Json a, b;
        if (!load_json(argv[2], a) || !load_json(argv[3], b)) return 2;
        GalleryPlan plan;
        std::string err;
        if (!plan_gallery_changes(a, b, plan, err)) { std::printf("error: %s\n", err.c_str()); return 0; }
        for (const auto& s : plan.add) std::printf("add %s\n", s.c_str());
        for (const auto& s : plan.update) std::printf("update %s\n", s.c_str());
        for (const auto& s : plan.remove) std::printf("remove %s\n", s.c_str());
        return 0;
    }
    std::fprintf(stderr, "bad arguments\n");
    return 2;
}
