// Face Studio data model: registries, declarative rules and project validation.
// Behaviour and message text mirror the Python reference in studio/core/.
#pragma once
#include <string>
#include <vector>

#include "json.h"

namespace tt {

constexpr int kCanvasWidth = 320;
constexpr int kCanvasHeight = 240;
constexpr size_t kBackgroundBytes = static_cast<size_t>(kCanvasWidth) * kCanvasHeight * 2;
extern const char* const kBackgroundEntry;   // "assets/bg.rgb565"
extern const char* const kTargetDevice;      // "TomTom ONE v6 (Model 19)"
extern const char* const kDefaultRendererId; // "ttface.elements.v1"

// ---- display formats (studio/core/formats.py)
struct FormatDef { const char* id; const char* name; const char* renderer; };
extern const FormatDef kFormats[2];
const FormatDef* find_format(const std::string& id);

struct ProjectFormats {
    Json formats = Json::array();  // may contain non-strings when the input is malformed
    std::string default_format;
};
ProjectFormats get_project_formats(const Json& project);
Json make_format_catalog(std::vector<std::string> format_ids);  // sorted, unique

// ---- renderers (studio/core/renderers.py)
struct RendererDef { const char* id; const char* name; int native_face_index; };
extern const RendererDef kRenderers[6];
const RendererDef* find_renderer(const std::string& id);
std::string get_renderer_id(const Json& project);

// ---- data add-on fields (studio/core/data_fields.py)
struct DataFieldDef { const char* id; const char* description; const char* category; const char* type; const char* capability; };
extern const DataFieldDef kDataFields[8];
const DataFieldDef* find_data_field(const std::string& id);
// Appends "Validation Error: ..." messages; returns true when valid.
bool validate_data_requirements(const Json& reqs, std::vector<std::string>& errors);
Json get_provider_capabilities(const Json& reqs);
std::vector<std::string> get_union_data_requirements(const std::vector<Json>& all_reqs);

// ---- declarative rules (studio/core/rules.py)
bool is_supported_signal(const std::string& s);
bool is_supported_operator(const std::string& s);

struct RuleEngine {
    static Json default_state();
    static Json compute_state(const Json& state_input);
    // Unknown, malformed or incomparable rules fail closed (return false).
    static bool is_element_visible(const Json& element, const Json& rules_def, const Json& state);
};

// ---- validation (studio/core/exporter.py::validate_face_project)
std::vector<std::string> validate_face_project(const Json& project);

}  // namespace tt
