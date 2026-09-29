#include "render.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "font.h"
#include "model.h"

namespace tt {

namespace {

// 16x16 monochrome icon bitmasks (identical to studio/core/elements.py).
const uint16_t kBatteryLow[16] = {0x0000, 0x7FF8, 0x8004, 0x8006, 0xB876, 0xB876, 0xB876, 0x8006,
                                  0x8006, 0xB876, 0xB876, 0xB876, 0x8004, 0x7FF8, 0x0000, 0x0000};
const uint16_t kGpsFix[16] = {0x0700, 0x0F80, 0x1DC0, 0x18C0, 0x18C0, 0x1DC0, 0x0F80, 0x0700,
                              0x0200, 0x0200, 0x0500, 0x0500, 0x0880, 0x1040, 0x0000, 0x0000};
const uint16_t kWeatherUnavailable[16] = {0x0700, 0x1FC0, 0x3060, 0x6230, 0xC218, 0x8208, 0x8008, 0xC218,
                                          0x7270, 0x1FC0, 0x0000, 0x0200, 0x0200, 0x0000, 0x0200, 0x0000};

const uint16_t* icon_mask(const std::string& name) {
    if (name == "battery_low") return kBatteryLow;
    if (name == "gps_fix") return kGpsFix;
    if (name == "weather_unavailable") return kWeatherUnavailable;
    return nullptr;
}

int64_t floor_div(int64_t a, int64_t b) {
    int64_t q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

// int(element.get(key, fallback)) for the value types Python's int() accepts here.
int geti(const Json& el, const char* key, int fallback) {
    const Json* v = el.find(key);
    if (!v) return fallback;
    if (v->is_int()) return static_cast<int>(std::max<int64_t>(-100000, std::min<int64_t>(100000, v->as_int())));
    if (v->is_float()) return static_cast<int>(std::max(-100000.0, std::min(100000.0, v->as_double())));
    if (v->is_bool()) return v->as_bool() ? 1 : 0;
    return fallback;
}

std::string getstr(const Json& el, const char* key, const std::string& fallback) {
    const Json* v = el.find(key);
    return (v && v->is_string()) ? v->as_string() : fallback;
}

std::string two(int v) {
    char b[8];
    std::snprintf(b, sizeof b, "%02d", v);
    return b;
}

std::string format_time(const Json& el, const SimTime& t) {
    std::string fmt = getstr(el, "format", "HH:MM");
    const Json* h12 = el.find("is_12h");
    bool is_12h = h12 && h12->truthy();
    bool seconds = fmt.find("SS") != std::string::npos;
    std::string s;
    if (is_12h) {
        int hour = t.hour % 12;
        if (hour == 0) hour = 12;
        s = std::to_string(hour) + ":" + two(t.minute);
        if (seconds) s += ":" + two(t.second);
        const Json* ampm = el.find("show_ampm");
        if (!ampm || ampm->truthy()) s += t.hour < 12 ? " AM" : " PM";
    } else {
        s = two(t.hour) + ":" + two(t.minute);
        if (seconds) s += ":" + two(t.second);
    }
    return s;
}

}  // namespace

int SimTime::weekday() const {
    int y = year, m = month;
    if (m < 3) { m += 12; --y; }
    int k = y % 100, j = y / 100;
    int h = (day + 13 * (m + 1) / 5 + k + k / 4 + j / 4 + 5 * j) % 7;  // 0 = Saturday
    return (h + 5) % 7;                                                 // 0 = Monday
}

void render_element(Image& img, const Json& element, const SimTime& t, int frame_tick) {
    const Json& type = element.get("type");
    int x = geti(element, "x", 0), y = geti(element, "y", 0);
    Rgb color = hex_to_rgb(getstr(element, "color", "#FFFFFF"), Rgb{255, 255, 255});
    int font_size = std::max(1, geti(element, "font_size", 24));
    const std::string kind = type.is_string() ? type.as_string() : "";

    if (kind == "digital_time") {
        std::string time_str = format_time(element, t);
        if (getstr(element, "layout", "horizontal") == "stacked") {
            size_t colon = time_str.find(':');
            std::string hour_text = time_str.substr(0, colon);
            std::string minute_text = time_str.substr(colon + 1);
            std::string suffix;
            size_t sp = minute_text.find(' ');
            if (sp != std::string::npos) {
                suffix = minute_text.substr(sp + 1);
                minute_text = minute_text.substr(0, sp);
            }
            int height = geti(element, "height", 60);
            int row_size = std::max<int>(8, std::min<int>(font_size, static_cast<int>(floor_div(height - 4, 2))));
            std::string lines[2] = {hour_text, minute_text};
            if (!suffix.empty()) lines[1] += " " + suffix;
            int row_height = static_cast<int>(floor_div(geti(element, "height", row_size * 2), 2));
            for (int row = 0; row < 2; ++row) {
                int lw = text_width(lines[row], row_size), lh = text_height(row_size);
                int left = x + static_cast<int>(floor_div(geti(element, "width", lw) - lw, 2));
                int top = y + row * row_height + static_cast<int>(floor_div(row_height - lh, 2));
                draw_text(img, left, top, lines[row], color, row_size);
            }
        } else {
            draw_text(img, x, y, time_str, color, font_size);
        }
    } else if (kind == "date") {
        static const char* const kDays[7] = {"MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN"};
        static const char* const kMonths[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                                "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
        std::string s = std::string(kDays[t.weekday()]) + ", " + kMonths[std::max(1, std::min(12, t.month)) - 1] + " " + two(t.day);
        draw_text(img, x, y, s, color, font_size);
    } else if (kind == "text") {
        draw_text(img, x, y, getstr(element, "text", "LABEL"), color, font_size);
    } else if (kind == "status_icon") {
        int w = geti(element, "width", 24), h = geti(element, "height", 24);
        const Json* blink = element.find("animate_blink");
        if (blink && blink->truthy() && frame_tick % 2 == 1) return;
        if (const uint16_t* mask = icon_mask(getstr(element, "icon_type", "battery_low"))) {
            double sx = w / 16.0, sy = h / 16.0;
            int pw = std::max(1, static_cast<int>(sx)), ph = std::max(1, static_cast<int>(sy));
            for (int row = 0; row < 16; ++row)
                for (int col = 0; col < 16; ++col)
                    if (mask[row] & (1 << (15 - col))) {
                        int px = x + static_cast<int>(col * sx), py = y + static_cast<int>(row * sy);
                        img.fill_rect(px, py, px + pw - 1, py + ph - 1, color);
                    }
        } else {  // unknown icon: outlined square with "!"
            img.fill_rect(x, y, x + w - 1, y + 1, color);
            img.fill_rect(x, y + h - 2, x + w - 1, y + h - 1, color);
            img.fill_rect(x, y, x + 1, y + h - 1, color);
            img.fill_rect(x + w - 2, y, x + w - 1, y + h - 1, color);
            draw_text(img, x + 4, y + 2, "!", color, font_size);
        }
    }
}

Image compose_preview(const Image& canvas, const Json& project, const Json& state, const SimTime& time, int tick) {
    Image out = canvas;
    const Json& rules = project.get("rules");
    const Json& elements = project.get("elements");
    if (!elements.is_array()) return out;
    ProjectFormats pf = get_project_formats(project);
    for (const Json& el : elements.items()) {
        if (!RuleEngine::is_element_visible(el, rules, state)) continue;
        if (el.is_object() && el.get("type") == Json::string("digital_time")) {
            Json copy = el;
            copy.set("layout", Json::string(pf.default_format == "stacked" ? "stacked" : "horizontal"));
            render_element(out, copy, time, tick);
        } else if (el.is_object()) {
            render_element(out, el, time, tick);
        }
    }
    return out;
}

}  // namespace tt
