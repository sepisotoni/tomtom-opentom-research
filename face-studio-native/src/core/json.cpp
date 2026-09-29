#include "json.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace tt {

Json Json::boolean(bool v) { Json j; j.type_ = Type::Bool; j.b_ = v; return j; }
Json Json::integer(int64_t v) { Json j; j.type_ = Type::Int; j.i_ = v; return j; }
Json Json::big_integer(const std::string& literal) {
    Json j;
    j.type_ = Type::Int;
    j.big_ = literal;
    j.d_ = std::strtod(literal.c_str(), nullptr);
    j.i_ = (!literal.empty() && literal[0] == '-') ? INT64_MIN : INT64_MAX;
    return j;
}
Json Json::number(double v) { Json j; j.type_ = Type::Float; j.d_ = v; return j; }
Json Json::string(std::string v) { Json j; j.type_ = Type::String; j.s_ = std::move(v); return j; }
Json Json::array() { Json j; j.type_ = Type::Array; return j; }
Json Json::object() { Json j; j.type_ = Type::Object; return j; }

const Json* Json::find(const std::string& key) const {
    if (type_ != Type::Object) return nullptr;
    for (size_t k = 0; k < keys_.size(); ++k)
        if (keys_[k] == key) return &vals_[k];
    return nullptr;
}
Json* Json::find(const std::string& key) {
    return const_cast<Json*>(static_cast<const Json*>(this)->find(key));
}
const Json& Json::get(const std::string& key) const {
    static const Json kNull;
    const Json* v = find(key);
    return v ? *v : kNull;
}
Json& Json::set(const std::string& key, Json v) {
    if (type_ != Type::Object) { *this = Json::object(); }
    for (size_t k = 0; k < keys_.size(); ++k) {
        if (keys_[k] == key) { vals_[k] = std::move(v); return vals_[k]; }
    }
    keys_.push_back(key);
    vals_.push_back(std::move(v));
    return vals_.back();
}
bool Json::erase(const std::string& key) {
    if (type_ != Type::Object) return false;
    for (size_t k = 0; k < keys_.size(); ++k) {
        if (keys_[k] == key) {
            keys_.erase(keys_.begin() + static_cast<std::ptrdiff_t>(k));
            vals_.erase(vals_.begin() + static_cast<std::ptrdiff_t>(k));
            return true;
        }
    }
    return false;
}

bool Json::truthy() const {
    switch (type_) {
        case Type::Null: return false;
        case Type::Bool: return b_;
        case Type::Int: return i_ != 0;
        case Type::Float: return d_ != 0.0;
        case Type::String: return !s_.empty();
        case Type::Array: return !a_.empty();
        case Type::Object: return !keys_.empty();
    }
    return false;
}

static std::string float_repr(double v) {
    char buf[64];
    for (int prec = 15; prec <= 17; ++prec) {
        std::snprintf(buf, sizeof buf, "%.*g", prec, v);
        if (std::strtod(buf, nullptr) == v) break;
    }
    std::string s = buf;
    if (s.find_first_of(".eEn") == std::string::npos) s += ".0";
    return s;
}

std::string Json::py_str() const {
    switch (type_) {
        case Type::Null: return "None";
        case Type::Bool: return b_ ? "True" : "False";
        case Type::Int: return big_.empty() ? std::to_string(i_) : big_;
        case Type::Float: return float_repr(d_);
        case Type::String: return s_;
        default: return dump(-1, false);
    }
}

bool Json::operator==(const Json& o) const {
    auto numeric = [](const Json& j) { return j.type_ == Type::Bool || j.type_ == Type::Int || j.type_ == Type::Float; };
    auto as_num = [](const Json& j) { return j.type_ == Type::Bool ? (j.b_ ? 1.0 : 0.0) : j.as_double(); };
    if (numeric(*this) && numeric(o)) {
        if (type_ == Type::Int && o.type_ == Type::Int) return i_ == o.i_ && big_ == o.big_;
        return as_num(*this) == as_num(o);
    }
    if (type_ != o.type_) return false;
    switch (type_) {
        case Type::Null: return true;
        case Type::String: return s_ == o.s_;
        case Type::Array: return a_ == o.a_;
        case Type::Object:
            if (keys_.size() != o.keys_.size()) return false;
            for (size_t k = 0; k < keys_.size(); ++k) {
                const Json* other = o.find(keys_[k]);
                if (!other || !(vals_[k] == *other)) return false;
            }
            return true;
        default: return false;
    }
}

// ----------------------------------------------------------------- dumping
static void append_escaped(std::string& out, const std::string& s) {
    out.push_back('"');
    size_t p = 0;
    auto emit_u = [&](unsigned cu) {
        char b[8];
        std::snprintf(b, sizeof b, "\\u%04x", cu);
        out += b;
    };
    while (p < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[p]);
        if (c < 0x80) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                default:
                    if (c < 0x20) emit_u(c); else out.push_back(static_cast<char>(c));
            }
            ++p;
            continue;
        }
        // Decode one UTF-8 sequence (input is validated on parse; be lenient here).
        unsigned cp = 0xFFFD; size_t n = 1;
        if ((c & 0xE0) == 0xC0 && p + 1 < s.size()) { cp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(s[p + 1]) & 0x3Fu); n = 2; }
        else if ((c & 0xF0) == 0xE0 && p + 2 < s.size()) { cp = ((c & 0x0Fu) << 12) | ((static_cast<unsigned char>(s[p + 1]) & 0x3Fu) << 6) | (static_cast<unsigned char>(s[p + 2]) & 0x3Fu); n = 3; }
        else if ((c & 0xF8) == 0xF0 && p + 3 < s.size()) { cp = ((c & 0x07u) << 18) | ((static_cast<unsigned char>(s[p + 1]) & 0x3Fu) << 12) | ((static_cast<unsigned char>(s[p + 2]) & 0x3Fu) << 6) | (static_cast<unsigned char>(s[p + 3]) & 0x3Fu); n = 4; }
        if (cp >= 0x10000) { cp -= 0x10000; emit_u(0xD800 + (cp >> 10)); emit_u(0xDC00 + (cp & 0x3FF)); }
        else emit_u(cp);
        p += n;
    }
    out.push_back('"');
}

void Json::dump_to(std::string& out, int indent, int level, bool sort_keys) const {
    auto newline = [&](int lvl) {
        out.push_back('\n');
        out.append(static_cast<size_t>(indent) * static_cast<size_t>(lvl), ' ');
    };
    switch (type_) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += b_ ? "true" : "false"; break;
        case Type::Int: out += big_.empty() ? std::to_string(i_) : big_; break;
        case Type::Float: out += float_repr(d_); break;
        case Type::String: append_escaped(out, s_); break;
        case Type::Array:
            if (a_.empty()) { out += "[]"; break; }
            out.push_back('[');
            for (size_t k = 0; k < a_.size(); ++k) {
                if (k) out += indent >= 0 ? "," : ", ";
                if (indent >= 0) newline(level + 1);
                a_[k].dump_to(out, indent, level + 1, sort_keys);
            }
            if (indent >= 0) newline(level);
            out.push_back(']');
            break;
        case Type::Object: {
            if (keys_.empty()) { out += "{}"; break; }
            std::vector<size_t> order(keys_.size());
            for (size_t k = 0; k < order.size(); ++k) order[k] = k;
            if (sort_keys)
                std::sort(order.begin(), order.end(), [&](size_t x, size_t y) { return keys_[x] < keys_[y]; });
            out.push_back('{');
            for (size_t n = 0; n < order.size(); ++n) {
                if (n) out += indent >= 0 ? "," : ", ";
                if (indent >= 0) newline(level + 1);
                append_escaped(out, keys_[order[n]]);
                out += ": ";
                vals_[order[n]].dump_to(out, indent, level + 1, sort_keys);
            }
            if (indent >= 0) newline(level);
            out.push_back('}');
            break;
        }
    }
}

std::string Json::dump(int indent, bool sort_keys) const {
    std::string out;
    dump_to(out, indent, 0, sort_keys);
    return out;
}

std::string py_list_repr(const std::vector<std::string>& values) {
    std::string out = "[";
    for (size_t k = 0; k < values.size(); ++k) {
        if (k) out += ", ";
        out += "'" + values[k] + "'";
    }
    return out + "]";
}

// ----------------------------------------------------------------- parsing
namespace {

struct Parser {
    const std::string& t;
    size_t p = 0;
    std::string err;
    static constexpr int kMaxDepth = 64;

    explicit Parser(const std::string& text) : t(text) {}

    bool fail(const char* msg) {
        if (err.empty()) err = std::string(msg) + " at byte " + std::to_string(p);
        return false;
    }
    void ws() { while (p < t.size() && (t[p] == ' ' || t[p] == '\t' || t[p] == '\n' || t[p] == '\r')) ++p; }

    static void put_utf8(std::string& s, unsigned cp) {
        if (cp < 0x80) s.push_back(static_cast<char>(cp));
        else if (cp < 0x800) { s.push_back(static_cast<char>(0xC0 | (cp >> 6))); s.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
        else if (cp < 0x10000) { s.push_back(static_cast<char>(0xE0 | (cp >> 12))); s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))); s.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
        else { s.push_back(static_cast<char>(0xF0 | (cp >> 18))); s.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F))); s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))); s.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
    }

    bool hex4(unsigned& out) {
        if (p + 4 > t.size()) return fail("Truncated \\u escape");
        out = 0;
        for (int k = 0; k < 4; ++k) {
            char c = t[p++];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') out |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= static_cast<unsigned>(c - 'A' + 10);
            else return fail("Invalid \\u escape");
        }
        return true;
    }

    bool str(std::string& out) {
        ++p;  // opening quote
        while (true) {
            if (p >= t.size()) return fail("Unterminated string");
            unsigned char c = static_cast<unsigned char>(t[p]);
            if (c == '"') { ++p; return true; }
            if (c < 0x20) return fail("Control character in string");
            if (c == '\\') {
                ++p;
                if (p >= t.size()) return fail("Unterminated escape");
                char e = t[p++];
                switch (e) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        unsigned cu;
                        if (!hex4(cu)) return false;
                        if (cu >= 0xD800 && cu <= 0xDBFF) {
                            if (p + 1 < t.size() && t[p] == '\\' && t[p + 1] == 'u') {
                                p += 2;
                                unsigned lo;
                                if (!hex4(lo)) return false;
                                if (lo < 0xDC00 || lo > 0xDFFF) return fail("Invalid surrogate pair");
                                cu = 0x10000 + ((cu - 0xD800) << 10) + (lo - 0xDC00);
                            } else return fail("Lone surrogate");
                        } else if (cu >= 0xDC00 && cu <= 0xDFFF) return fail("Lone surrogate");
                        put_utf8(out, cu);
                        break;
                    }
                    default: return fail("Invalid escape");
                }
                continue;
            }
            if (c < 0x80) { out.push_back(static_cast<char>(c)); ++p; continue; }
            // Validate one UTF-8 sequence (reject overlongs, surrogates, > U+10FFFF).
            size_t n = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
            if (!n || p + n > t.size()) return fail("Invalid UTF-8");
            unsigned cp = n == 2 ? (c & 0x1Fu) : n == 3 ? (c & 0x0Fu) : (c & 0x07u);
            for (size_t k = 1; k < n; ++k) {
                unsigned char cc = static_cast<unsigned char>(t[p + k]);
                if ((cc & 0xC0) != 0x80) return fail("Invalid UTF-8");
                cp = (cp << 6) | (cc & 0x3Fu);
            }
            if ((n == 2 && cp < 0x80) || (n == 3 && cp < 0x800) || (n == 4 && cp < 0x10000) ||
                cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
                return fail("Invalid UTF-8");
            out.append(t, p, n);
            p += n;
        }
    }

    bool num(Json& out) {
        size_t start = p;
        bool is_float = false;
        if (p < t.size() && t[p] == '-') ++p;
        if (p >= t.size()) return fail("Invalid number");
        if (t[p] == '0') ++p;
        else if (t[p] >= '1' && t[p] <= '9') { while (p < t.size() && std::isdigit(static_cast<unsigned char>(t[p]))) ++p; }
        else return fail("Invalid number");
        if (p < t.size() && t[p] == '.') {
            is_float = true; ++p;
            size_t d = p;
            while (p < t.size() && std::isdigit(static_cast<unsigned char>(t[p]))) ++p;
            if (p == d) return fail("Invalid number");
        }
        if (p < t.size() && (t[p] == 'e' || t[p] == 'E')) {
            is_float = true; ++p;
            if (p < t.size() && (t[p] == '+' || t[p] == '-')) ++p;
            size_t d = p;
            while (p < t.size() && std::isdigit(static_cast<unsigned char>(t[p]))) ++p;
            if (p == d) return fail("Invalid number");
        }
        std::string lit = t.substr(start, p - start);
        if (!is_float) {
            errno = 0;
            char* end = nullptr;
            long long v = std::strtoll(lit.c_str(), &end, 10);
            if (errno == 0) { out = Json::integer(v); return true; }
            out = Json::big_integer(lit);  // beyond int64: keep the exact digits
            return true;
        }
        double d = std::strtod(lit.c_str(), nullptr);
        if (!std::isfinite(d)) return fail("Number out of range");
        out = Json::number(d);
        return true;
    }

    bool value(Json& out, int depth) {
        if (depth > kMaxDepth) return fail("Nesting too deep");
        ws();
        if (p >= t.size()) return fail("Unexpected end of input");
        char c = t[p];
        if (c == '{') {
            ++p;
            out = Json::object();
            ws();
            if (p < t.size() && t[p] == '}') { ++p; return true; }
            while (true) {
                ws();
                if (p >= t.size() || t[p] != '"') return fail("Expected object key");
                std::string key;
                if (!str(key)) return false;
                ws();
                if (p >= t.size() || t[p] != ':') return fail("Expected ':'");
                ++p;
                Json v;
                if (!value(v, depth + 1)) return false;
                out.set(key, std::move(v));
                ws();
                if (p < t.size() && t[p] == ',') { ++p; continue; }
                if (p < t.size() && t[p] == '}') { ++p; return true; }
                return fail("Expected ',' or '}'");
            }
        }
        if (c == '[') {
            ++p;
            out = Json::array();
            ws();
            if (p < t.size() && t[p] == ']') { ++p; return true; }
            while (true) {
                Json v;
                if (!value(v, depth + 1)) return false;
                out.push(std::move(v));
                ws();
                if (p < t.size() && t[p] == ',') { ++p; continue; }
                if (p < t.size() && t[p] == ']') { ++p; return true; }
                return fail("Expected ',' or ']'");
            }
        }
        if (c == '"') { std::string s; if (!str(s)) return false; out = Json::string(std::move(s)); return true; }
        if (t.compare(p, 4, "true") == 0) { p += 4; out = Json::boolean(true); return true; }
        if (t.compare(p, 5, "false") == 0) { p += 5; out = Json::boolean(false); return true; }
        if (t.compare(p, 4, "null") == 0) { p += 4; out = Json(); return true; }
        if (c == '-' || (c >= '0' && c <= '9')) return num(out);
        return fail("Unexpected character");
    }
};

}  // namespace

bool Json::parse(const std::string& text, Json& out, std::string& error) {
    Parser ps(text);
    Json v;
    if (!ps.value(v, 0)) { error = ps.err; return false; }
    ps.ws();
    if (ps.p != text.size()) { error = "Extra data at byte " + std::to_string(ps.p); return false; }
    out = std::move(v);
    return true;
}

}  // namespace tt
