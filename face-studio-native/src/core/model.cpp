#include "model.h"

#include <algorithm>
#include <set>

namespace tt {

const char* const kBackgroundEntry = "assets/bg.rgb565";
const char* const kTargetDevice = "TomTom ONE v6 (Model 19)";
const char* const kDefaultRendererId = "ttface.elements.v1";

const FormatDef kFormats[2] = {
    {"horizontal", "Side by side", "digital-time-horizontal-v1"},
    {"stacked", "Stacked", "digital-time-stacked-v1"},
};

const FormatDef* find_format(const std::string& id) {
    for (const FormatDef& f : kFormats)
        if (id == f.id) return &f;
    return nullptr;
}

ProjectFormats get_project_formats(const Json& project) {
    ProjectFormats out;
    static const Json kEmptyObject = Json::object();
    const Json* dp = project.find("display");
    if (dp && !dp->is_object()) return out;  // display present but not a dict -> [], ""
    const Json& display = dp ? *dp : kEmptyObject;

    const Json* fp = display.find("formats");
    bool formats_none = !fp || fp->is_null();
    Json default_val = display.get("default_format");
    Json formats;
    if (formats_none && display.has("layout")) {
        formats = Json::array();
        formats.push(display.get("layout"));
        default_val = display.get("layout");
    } else if (formats_none) {
        formats = Json::array();
        if (default_val.truthy()) {
            formats.push(default_val);
        } else {
            formats.push(Json::string("horizontal"));
            default_val = Json::string("horizontal");
        }
    } else {
        formats = *fp;
    }
    out.default_format = default_val.is_string() ? default_val.as_string() : "";
    if (!formats.is_array()) return out;  // [] plus default
    out.formats = std::move(formats);
    return out;
}

Json make_format_catalog(std::vector<std::string> ids) {
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    Json arr = Json::array();
    for (const std::string& id : ids) {
        const FormatDef* def = find_format(id);
        Json entry = Json::object();
        entry.set("id", Json::string(id));
        entry.set("name", Json::string(def ? def->name : ""));
        entry.set("renderer", Json::string(def ? def->renderer : ""));
        arr.push(std::move(entry));
    }
    return arr;
}

// ---------------------------------------------------------------- renderers
const RendererDef kRenderers[6] = {
    {"ttface.elements.v1", "Declarative face", -1},
    {"opentom.builtin.frost-outline.v1", "Frost Outline", 0},
    {"opentom.builtin.hydro-aqua.v1", "Hydro Aqua Wave", 1},
    {"opentom.builtin.solid-lavender.v1", "Solid Lavender", 2},
    {"opentom.builtin.vivid-sunset.v1", "Vivid Sunset", 3},
    {"opentom.builtin.real-telemetry.v1", "Real Telemetry", 4},
};

const RendererDef* find_renderer(const std::string& id) {
    for (const RendererDef& r : kRenderers)
        if (id == r.id) return &r;
    return nullptr;
}

std::string get_renderer_id(const Json& project) {
    const Json* v = project.find("renderer_id");
    if (!v) return kDefaultRendererId;
    return v->is_string() ? v->as_string() : "";
}

// -------------------------------------------------------------- data fields
const DataFieldDef kDataFields[8] = {
    {"battery.percent", "Battery percentage level (0-100%)", "battery", "integer", "device-telemetry-v1"},
    {"gps.fix", "GPS fix status boolean", "gps", "boolean", "device-gps-v1"},
    {"weather.current.temperature", "Current outdoor temperature in Celsius", "weather", "integer", "weather-provider-v1"},
    {"weather.current.condition", "Current weather condition summary string", "weather", "string", "weather-provider-v1"},
    {"weather.hourly.temperature", "24-hour forecast hourly temperatures", "weather", "array[integer]", "weather-provider-v1"},
    {"weather.hourly.condition", "24-hour forecast hourly conditions", "weather", "array[string]", "weather-provider-v1"},
    {"weather.hourly.precipitation_probability", "24-hour forecast precipitation probability (0-100%)", "weather", "array[integer]", "weather-provider-v1"},
    {"weather.daily.condition", "7-day forecast summary conditions", "weather", "array[string]", "weather-provider-v1"},
};

const DataFieldDef* find_data_field(const std::string& id) {
    for (const DataFieldDef& f : kDataFields)
        if (id == f.id) return &f;
    return nullptr;
}

bool validate_data_requirements(const Json& reqs, std::vector<std::string>& errors) {
    const size_t before = errors.size();
    if (reqs.is_null()) return true;
    if (!reqs.is_array()) {
        errors.push_back("Validation Error: data_requirements must be a list of string field IDs.");
        return false;
    }
    std::vector<std::string> supported;
    for (const DataFieldDef& f : kDataFields) supported.push_back(f.id);
    std::sort(supported.begin(), supported.end());
    std::set<std::string> seen;
    for (size_t idx = 0; idx < reqs.items().size(); ++idx) {
        const Json& field = reqs.items()[idx];
        if (!field.is_string()) {
            errors.push_back("Validation Error: data_requirements[" + std::to_string(idx) + "] must be a string.");
            continue;
        }
        const std::string& id = field.as_string();
        if (!find_data_field(id))
            errors.push_back("Validation Error: Unknown data field requirement '" + id + "'. Supported: " + py_list_repr(supported));
        if (seen.count(id))
            errors.push_back("Validation Error: Duplicate data field requirement '" + id + "'.");
        seen.insert(id);
    }
    return errors.size() == before;
}

Json get_provider_capabilities(const Json& reqs) {
    Json catalog = Json::array();
    if (!reqs.is_array()) return catalog;
    std::set<std::string> caps;
    for (const Json& f : reqs.items())
        if (f.is_string())
            if (const DataFieldDef* def = find_data_field(f.as_string())) caps.insert(def->capability);
    for (const std::string& cap : caps) {
        Json entry = Json::object();
        entry.set("capability", Json::string(cap));
        entry.set("version", Json::string("1.0.0"));
        catalog.push(std::move(entry));
    }
    return catalog;
}

std::vector<std::string> get_union_data_requirements(const std::vector<Json>& all_reqs) {
    std::set<std::string> u;
    for (const Json& reqs : all_reqs)
        if (reqs.is_array())
            for (const Json& f : reqs.items())
                if (f.is_string() && find_data_field(f.as_string())) u.insert(f.as_string());
    return std::vector<std::string>(u.begin(), u.end());
}

// -------------------------------------------------------------------- rules
bool is_supported_signal(const std::string& s) {
    static const char* const kSignals[] = {"battery_percent", "battery_low", "gps_status", "gps_fix",
                                           "weather_status", "weather_unavailable", "charging"};
    for (const char* k : kSignals)
        if (s == k) return true;
    return false;
}

bool is_supported_operator(const std::string& s) {
    static const char* const kOps[] = {"==", "!=", "<=", ">=", "<", ">"};
    for (const char* k : kOps)
        if (s == k) return true;
    return false;
}

Json RuleEngine::default_state() {
    Json s = Json::object();
    s.set("battery_percent", Json::integer(100));
    s.set("battery_low", Json::boolean(false));
    s.set("gps_status", Json::integer(1));
    s.set("gps_fix", Json::boolean(true));
    s.set("weather_status", Json::integer(1));
    s.set("weather_unavailable", Json::boolean(false));
    s.set("charging", Json::boolean(false));
    return s;
}

static bool numeric_like(const Json& j) { return j.is_bool() || j.is_number(); }
static double num_of(const Json& j) { return j.is_bool() ? (j.as_bool() ? 1.0 : 0.0) : j.as_double(); }

Json RuleEngine::compute_state(const Json& in) {
    Json state = default_state();
    if (in.is_object())
        for (size_t k = 0; k < in.keys().size(); ++k) state.set(in.keys()[k], in.values()[k]);
    const Json& pct = state.get("battery_percent");
    state.set("battery_low", Json::boolean(numeric_like(pct) && num_of(pct) <= 20.0));
    state.set("gps_fix", Json::boolean(state.get("gps_status") == Json::integer(1)));
    state.set("weather_unavailable", Json::boolean(state.get("weather_status") == Json::integer(0)));
    return state;
}

// Mirrors Python comparison semantics; `ok` is false where Python raises TypeError.
static bool py_compare(const std::string& op, const Json& a, const Json& b, bool& ok) {
    ok = true;
    if (op == "==") return a == b;
    if (op == "!=") return !(a == b);
    int cmp;
    if (numeric_like(a) && numeric_like(b)) {
        double x = num_of(a), y = num_of(b);
        cmp = x < y ? -1 : (x > y ? 1 : 0);
        if (a.is_int() && b.is_int()) cmp = a.as_int() < b.as_int() ? -1 : (a.as_int() > b.as_int() ? 1 : 0);
    } else if (a.is_string() && b.is_string()) {
        int c = a.as_string().compare(b.as_string());
        cmp = c < 0 ? -1 : (c > 0 ? 1 : 0);
    } else {
        ok = false;
        return false;
    }
    if (op == "<=") return cmp <= 0;
    if (op == ">=") return cmp >= 0;
    if (op == "<") return cmp < 0;
    return cmp > 0;  // ">"
}

bool RuleEngine::is_element_visible(const Json& element, const Json& rules_def, const Json& current_state) {
    if (!element.is_object()) return false;
    const Json& rule_name = element.get("rule");
    if (!rule_name.truthy()) return true;  // no rule bound
    if (!rule_name.is_string()) return false;
    const std::string& name = rule_name.as_string();
    Json state = compute_state(current_state);

    if (name == "battery_low") return state.get("battery_low").truthy();
    if (name == "gps_fix") return state.get("gps_fix").truthy();
    if (name == "weather_unavailable") return state.get("weather_unavailable").truthy();
    if (const Json* v = state.find(name))
        if (v->is_bool()) return v->as_bool();

    if (!rules_def.is_array()) return false;
    for (const Json& r : rules_def.items()) {
        if (!r.is_object() || !(r.get("name") == rule_name)) continue;
        const Json& sig = r.get("signal");
        const Json* opp = r.find("op");
        Json op = opp ? *opp : Json::string("==");
        if (!sig.truthy() || !sig.is_string() || !is_supported_signal(sig.as_string()) || !op.is_string() ||
            !is_supported_operator(op.as_string()))
            return false;
        const Json& cur = state.get(sig.as_string());
        if (cur.is_null()) return false;
        bool ok;
        bool result = py_compare(op.as_string(), cur, r.get("value"), ok);
        return ok && result;
    }
    return false;  // unknown rule name -> fail closed
}

// --------------------------------------------------------------- validation
static const char* const kV = "Validation Error: ";

std::vector<std::string> validate_face_project(const Json& project) {
    std::vector<std::string> errors;
    auto err = [&](const std::string& m) { errors.push_back(std::string(kV) + m); };

    if (!project.is_object()) {
        err("Project data must be a JSON object.");
        return errors;
    }

    const std::string renderer_id = get_renderer_id(project);
    if (!find_renderer(renderer_id)) err("Unsupported face renderer '" + renderer_id + "'.");

    static const Json kEmptyObject = Json::object();
    static const Json kEmptyArray = Json::array();
    const Json* displayp = project.find("display");
    if (displayp && !displayp->is_object()) {
        err("Display settings must be an object.");
    } else {
        ProjectFormats pf = get_project_formats(project);
        const Json* elementsp = project.find("elements");
        bool has_digital_time = false;
        if (!elementsp || elementsp->is_array()) {
            const Json& els = elementsp ? *elementsp : kEmptyArray;
            for (const Json& e : els.items())
                if (e.is_object() && e.get("type") == Json::string("digital_time")) { has_digital_time = true; break; }
        }
        const std::vector<Json>& formats = pf.formats.items();
        if (!formats.empty()) {
            bool unsupported = false;
            for (const Json& f : formats)
                if (!f.is_string() || !find_format(f.as_string())) unsupported = true;
            bool duplicates = false;
            for (size_t a = 0; a < formats.size() && !duplicates; ++a)
                for (size_t b = a + 1; b < formats.size(); ++b)
                    if (formats[a] == formats[b]) { duplicates = true; break; }
            bool default_listed = false;
            for (const Json& f : formats)
                if (f.is_string() && f.as_string() == pf.default_format) default_listed = true;
            if (unsupported) err("Display formats contain an unsupported format ID.");
            else if (duplicates) err("Display formats must not contain duplicates.");
            else if (!default_listed) err("Default display format must be listed as supported.");
        } else if (has_digital_time) {
            err("A digital-time face must support at least one display format.");
        } else if (!pf.default_format.empty()) {
            err("A face without display formats cannot set a default format.");
        }
    }

    // Project name: metadata.name, else face_name.
    {
        Json name;
        const Json& meta = project.get("metadata");
        if (meta.is_object()) name = meta.get("name");
        if (!name.truthy()) name = project.get("face_name");
        if (!name.truthy() || !name.is_string()) err("Project name must be a non-empty string.");
    }

    // Canvas
    {
        const Json* cp = project.find("canvas");
        if (cp && !cp->is_object()) {
            err("Canvas specification must be a dictionary.");
        } else {
            const Json& canvas = cp ? *cp : kEmptyObject;
            const Json& cw = canvas.get("width");
            const Json& ch = canvas.get("height");
            if (!cw.is_int() || cw.as_int() != kCanvasWidth || !ch.is_int() || ch.as_int() != kCanvasHeight)
                err("Canvas dimensions must be integer 320x240. Found " + cw.py_str() + "x" + ch.py_str() + ".");
        }
    }

    // Elements
    {
        const Json* ep = project.find("elements");
        if (ep && !ep->is_array()) {
            err("Elements field must be a list.");
        } else if (ep) {
            static const char* const kTypes[] = {"digital_time", "date", "text", "status_icon"};
            for (size_t idx = 0; idx < ep->items().size(); ++idx) {
                const Json& el = ep->items()[idx];
                const std::string n = std::to_string(idx + 1);
                if (!el.is_object()) {
                    err("Element #" + n + " must be an object.");
                    continue;
                }
                const Json& type = el.get("type");
                bool known = false;
                if (type.is_string())
                    for (const char* t : kTypes)
                        if (type.as_string() == t) known = true;
                if (!known) err("Element #" + n + " has unknown or non-string type '" + type.py_str() + "'.");

                const char* const fields[4] = {"x", "y", "width", "height"};
                bool all_int = true;
                for (const char* f : fields) {
                    if (!el.get(f).is_int()) {
                        err("Element #" + n + " field '" + f + "' must be an integer.");
                        all_int = false;
                    }
                }
                if (all_int) {
                    int64_t x = el.get("x").as_int(), y = el.get("y").as_int();
                    int64_t w = el.get("width").as_int(), h = el.get("height").as_int();
                    if (x < 0 || y < 0 || w <= 0 || h <= 0 || x > kCanvasWidth || y > kCanvasHeight ||
                        w > kCanvasWidth || h > kCanvasHeight || x + w > kCanvasWidth || y + h > kCanvasHeight) {
                        const Json* idp = el.find("id");
                        std::string id = idp ? idp->py_str() : "unnamed";
                        err("Element #" + n + " ('" + id + "') extends outside 320x240 screen boundary (x=" +
                            el.get("x").py_str() + ", y=" + el.get("y").py_str() + ", w=" + el.get("width").py_str() +
                            ", h=" + el.get("height").py_str() + ").");
                    }
                }
                const Json& fs = el.get("font_size");
                if (!fs.is_null() && (!fs.is_int() || fs.as_int() <= 0))
                    err("Element #" + n + " font_size must be a positive integer.");
            }
        }
    }

    // Rules
    {
        const Json* rp = project.find("rules");
        if (rp && !rp->is_array()) {
            err("Rules field must be a list.");
        } else if (rp) {
            std::vector<std::string> signals = {"battery_low", "battery_percent", "charging", "gps_fix",
                                                "gps_status", "weather_status", "weather_unavailable"};
            std::vector<std::string> ops = {"!=", "<", "<=", "==", ">", ">="};
            for (size_t idx = 0; idx < rp->items().size(); ++idx) {
                const Json& rule = rp->items()[idx];
                const std::string n = std::to_string(idx + 1);
                if (!rule.is_object()) {
                    err("Rule #" + n + " must be an object.");
                    continue;
                }
                const Json& sig = rule.get("signal");
                if (!sig.is_string() || !is_supported_signal(sig.as_string()))
                    err("Rule #" + n + " references unsupported or malformed signal '" + sig.py_str() +
                        "'. Supported: " + py_list_repr(signals));
                const Json& op = rule.get("op");
                if (!op.is_string() || !is_supported_operator(op.as_string()))
                    err("Rule #" + n + " uses unsupported operator '" + op.py_str() + "'. Supported: " + py_list_repr(ops));
            }
        }
    }

    // Background entry path
    {
        const Json& bg = project.get("background");
        if (bg.is_object() && bg.has("file")) {
            const Json& file = bg.get("file");
            if (!(file == Json::string(kBackgroundEntry)))
                err("Manifest background file '" + file.py_str() + "' must match ZIP entry 'assets/bg.rgb565'.");
        }
    }

    // Data requirements
    {
        const Json* dr = project.find("data_requirements");
        if (dr && !dr->is_null()) validate_data_requirements(*dr, errors);
    }
    return errors;
}

}  // namespace tt
