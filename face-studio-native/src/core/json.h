// Minimal strict JSON value with Python-json-compatible semantics.
//
// The Face Studio package formats (.ttface, .ttgallery, .ttproj) are defined by
// the Python reference implementation, whose validators rely on Python's
// isinstance()/type() behaviour: 10 is an int, 10.0 is a float, true is a bool
// (and must be rejected where an integer is required). This class keeps those
// distinctions so the C++ validators can mirror the reference exactly.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace tt {

class Json {
public:
    enum class Type { Null, Bool, Int, Float, String, Array, Object };

    Json() = default;
    static Json boolean(bool v);
    static Json integer(int64_t v);
    // Integer literal outside int64 (Python ints are unbounded): keeps the exact digits,
    // as_int() saturates. is_int() stays true so range checks report the real value.
    static Json big_integer(const std::string& literal);
    static Json number(double v);
    static Json string(std::string v);
    static Json array();
    static Json object();

    Type type() const { return type_; }
    bool is_null() const { return type_ == Type::Null; }
    bool is_bool() const { return type_ == Type::Bool; }
    bool is_int() const { return type_ == Type::Int; }     // never true for bool
    bool is_float() const { return type_ == Type::Float; }
    bool is_number() const { return type_ == Type::Int || type_ == Type::Float; }
    bool is_string() const { return type_ == Type::String; }
    bool is_array() const { return type_ == Type::Array; }
    bool is_object() const { return type_ == Type::Object; }

    bool as_bool() const { return b_; }
    int64_t as_int() const { return i_; }
    double as_double() const { return (type_ == Type::Int && big_.empty()) ? static_cast<double>(i_) : d_; }
    const std::string& as_string() const { return s_; }

    // Arrays
    std::vector<Json>& items() { return a_; }
    const std::vector<Json>& items() const { return a_; }
    void push(Json v) { a_.push_back(std::move(v)); }

    // Objects (insertion ordered, like a Python dict)
    size_t size() const { return type_ == Type::Object ? keys_.size() : a_.size(); }
    const std::vector<std::string>& keys() const { return keys_; }
    const std::vector<Json>& values() const { return vals_; }
    const Json* find(const std::string& key) const;
    Json* find(const std::string& key);
    bool has(const std::string& key) const { return find(key) != nullptr; }
    // Returns the member or a shared null value (mirrors dict.get(key)).
    const Json& get(const std::string& key) const;
    Json& set(const std::string& key, Json v);  // replace in place or append
    bool erase(const std::string& key);

    // Python-like truthiness (None/False/0/""/[]/{} are falsy).
    bool truthy() const;
    // Approximation of Python's str() for use in validation messages.
    std::string py_str() const;

    // Python-style equality: numbers compare numerically (incl. bool),
    // objects compare irrespective of key order.
    bool operator==(const Json& other) const;
    bool operator!=(const Json& other) const { return !(*this == other); }

    // Serialisation compatible with json.dumps(..., indent=N, sort_keys=...,
    // ensure_ascii=True). indent < 0 produces compact ", " / ": " output.
    std::string dump(int indent = -1, bool sort_keys = false) const;

    // Strict RFC 8259 parser (UTF-8 validated, depth limited). Returns false
    // and fills `error` on failure.
    static bool parse(const std::string& text, Json& out, std::string& error);

private:
    void dump_to(std::string& out, int indent, int level, bool sort_keys) const;

    Type type_ = Type::Null;
    bool b_ = false;
    int64_t i_ = 0;
    double d_ = 0.0;
    std::string big_;  // non-empty only for integers outside int64
    std::string s_;
    std::vector<Json> a_;
    std::vector<std::string> keys_;
    std::vector<Json> vals_;
};

// Python repr() of a list of strings: ['a', 'b'] (used in validator messages).
std::string py_list_repr(const std::vector<std::string>& values);

}  // namespace tt
