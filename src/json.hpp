// Minimal JSON implementation for bitcoin-tui.
// Vendored in-tree — no external fetch required.
//
// Supported operations:
//   parse, dump, operator[], contains, value, get<T>,
//   is_null/bool/number/string/array/object, begin/end, size,
//   initializer-list construction (object detection), json::array()

#pragma once

#include <cmath>
#include <concepts>
#include <cstdint>
#include <cstdio>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

class json {
  public:
    // -----------------------------------------------------------------------
    // Exception type
    // -----------------------------------------------------------------------
    class exception : public std::runtime_error {
      public:
        explicit exception(const std::string& msg) : std::runtime_error(msg) {}
    };

    using object_t = std::map<std::string, json>;
    using array_t  = std::vector<json>;

  private:
    enum class Kind { Null, Bool, Int, Float, String, Array, Object };

    Kind        kind_ = Kind::Null;
    bool        bval_ = false;
    int64_t     ival_ = 0;
    double      fval_ = 0.0;
    std::string sval_;
    array_t     aval_;
    object_t    oval_;

    // -----------------------------------------------------------------------
    // Recursive-descent parser
    // -----------------------------------------------------------------------
    struct Parser {
        const std::string& src;
        size_t             pos = 0;

        void skip_ws() {
            while (pos < src.size()) {
                char c = src[pos];
                if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
                    ++pos;
                else
                    break;
            }
        }

        [[nodiscard]] char peek() {
            skip_ws();
            return pos < src.size() ? src[pos] : '\0';
        }

        char consume() {
            skip_ws();
            if (pos >= src.size())
                throw exception("Unexpected end of input");
            return src[pos++];
        }

        void expect(char c) {
            char got = consume();
            if (got != c)
                throw exception(std::string("Expected '") + c + "', got '" + got + "'");
        }

        std::string parse_string_val() {
            expect('"');
            std::string out;
            while (pos < src.size()) {
                char c = src[pos++];
                if (c == '"')
                    return out;
                if (c != '\\') {
                    out += c;
                    continue;
                }
                if (pos >= src.size())
                    break;
                char e = src[pos++];
                switch (e) {
                case '"':
                    out += '"';
                    break;
                case '\\':
                    out += '\\';
                    break;
                case '/':
                    out += '/';
                    break;
                case 'b':
                    out += '\b';
                    break;
                case 'f':
                    out += '\f';
                    break;
                case 'n':
                    out += '\n';
                    break;
                case 'r':
                    out += '\r';
                    break;
                case 't':
                    out += '\t';
                    break;
                case 'u': {
                    if (pos + 4 > src.size())
                        throw exception("Truncated \\u escape");
                    unsigned cp = 0;
                    for (int i = 0; i < 4; ++i) {
                        cp <<= 4;
                        char h = src[pos++];
                        if (h >= '0' && h <= '9')
                            cp |= static_cast<unsigned>(h - '0');
                        else if (h >= 'a' && h <= 'f')
                            cp |= static_cast<unsigned>(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F')
                            cp |= static_cast<unsigned>(h - 'A' + 10);
                        else
                            throw exception("Bad hex in \\u escape");
                    }
                    // Encode code point as UTF-8
                    if (cp < 0x80) {
                        out += static_cast<char>(cp);
                    } else if (cp < 0x800) {
                        out += static_cast<char>(0xC0 | (cp >> 6));
                        out += static_cast<char>(0x80 | (cp & 0x3F));
                    } else {
                        out += static_cast<char>(0xE0 | (cp >> 12));
                        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                        out += static_cast<char>(0x80 | (cp & 0x3F));
                    }
                    break;
                }
                default:
                    out += e;
                    break;
                }
            }
            throw exception("Unterminated string");
        }

        [[nodiscard]] json parse_number() {
            size_t start    = pos;
            bool   is_float = false;
            if (pos < src.size() && src[pos] == '-')
                ++pos;
            while (pos < src.size() && src[pos] >= '0' && src[pos] <= '9')
                ++pos;
            if (pos < src.size() && src[pos] == '.') {
                is_float = true;
                ++pos;
                while (pos < src.size() && src[pos] >= '0' && src[pos] <= '9')
                    ++pos;
            }
            if (pos < src.size() && (src[pos] == 'e' || src[pos] == 'E')) {
                is_float = true;
                ++pos;
                if (pos < src.size() && (src[pos] == '+' || src[pos] == '-'))
                    ++pos;
                while (pos < src.size() && src[pos] >= '0' && src[pos] <= '9')
                    ++pos;
            }
            const std::string num = src.substr(start, pos - start);
            json              j;
            if (is_float) {
                j.kind_ = Kind::Float;
                j.fval_ = std::stod(num);
            } else {
                j.kind_ = Kind::Int;
                j.ival_ = std::stoll(num);
            }
            return j;
        }

        [[nodiscard]] json parse_value() {
            const char c = peek();

            if (c == '"') {
                json j;
                j.kind_ = Kind::String;
                j.sval_ = parse_string_val();
                return j;
            }

            if (c == '{') {
                consume();
                json j;
                j.kind_ = Kind::Object;
                if (peek() == '}') {
                    consume();
                    return j;
                }
                while (true) {
                    std::string key = parse_string_val();
                    expect(':');
                    j.oval_[key]   = parse_value();
                    const char sep = peek();
                    if (sep == '}') {
                        consume();
                        break;
                    }
                    if (sep == ',') {
                        consume();
                        continue;
                    }
                    throw exception("Expected ',' or '}'");
                }
                return j;
            }

            if (c == '[') {
                consume();
                json j;
                j.kind_ = Kind::Array;
                if (peek() == ']') {
                    consume();
                    return j;
                }
                while (true) {
                    j.aval_.push_back(parse_value());
                    const char sep = peek();
                    if (sep == ']') {
                        consume();
                        break;
                    }
                    if (sep == ',') {
                        consume();
                        continue;
                    }
                    throw exception("Expected ',' or ']'");
                }
                return j;
            }

            if (c == 't') {
                if (src.substr(pos, 4) != "true")
                    throw exception("Expected 'true'");
                pos += 4;
                json j;
                j.kind_ = Kind::Bool;
                j.bval_ = true;
                return j;
            }
            if (c == 'f') {
                if (src.substr(pos, 5) != "false")
                    throw exception("Expected 'false'");
                pos += 5;
                json j;
                j.kind_ = Kind::Bool;
                j.bval_ = false;
                return j;
            }
            if (c == 'n') {
                if (src.substr(pos, 4) != "null")
                    throw exception("Expected 'null'");
                pos += 4;
                return json{};
            }
            if (c == '-' || (c >= '0' && c <= '9'))
                return parse_number();

            throw exception(std::string("Unexpected character: ") + c);
        }
    };

    // -----------------------------------------------------------------------
    // Serialization helpers
    // -----------------------------------------------------------------------
    [[nodiscard]] static std::string quote(const std::string& s) {
        std::string out = "\"";
        for (unsigned char c : s) {
            switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<int>(c));
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
            }
        }
        out += '"';
        return out;
    }

    [[nodiscard]] std::string dump_impl(int indent, int depth) const {
        const bool pretty = indent >= 0;
        switch (kind_) {
        case Kind::Null:
            return "null";
        case Kind::Bool:
            return bval_ ? "true" : "false";
        case Kind::Int:
            return std::to_string(ival_);
        case Kind::Float: {
            if (!std::isfinite(fval_))
                return "null";
            std::ostringstream ss;
            ss << std::setprecision(17) << fval_;
            return ss.str();
        }
        case Kind::String:
            return quote(sval_);
        case Kind::Array: {
            if (aval_.empty())
                return "[]";
            std::string out = "[";
            for (size_t i = 0; i < aval_.size(); ++i) {
                if (i)
                    out += ',';
                if (pretty) {
                    out += '\n';
                    out += std::string(static_cast<size_t>((depth + 1) * indent), ' ');
                }
                out += aval_[i].dump_impl(indent, depth + 1);
            }
            if (pretty) {
                out += '\n';
                out += std::string(static_cast<size_t>(depth * indent), ' ');
            }
            out += ']';
            return out;
        }
        case Kind::Object: {
            if (oval_.empty())
                return "{}";
            std::string out   = "{";
            bool        first = true;
            for (const auto& [k, v] : oval_) {
                if (!first)
                    out += ',';
                if (pretty) {
                    out += '\n';
                    out += std::string(static_cast<size_t>((depth + 1) * indent), ' ');
                }
                out += quote(k);
                out += ':';
                if (pretty)
                    out += ' ';
                out += v.dump_impl(indent, depth + 1);
                first = false;
            }
            if (pretty) {
                out += '\n';
                out += std::string(static_cast<size_t>(depth * indent), ' ');
            }
            out += '}';
            return out;
        }
        }
        return "null"; // unreachable
    }

  public:
    // -----------------------------------------------------------------------
    // Constructors
    // -----------------------------------------------------------------------
    json() = default;
    json(std::nullptr_t) {}

    // Bool must come before the integral concept to take precedence
    json(bool v) : kind_(Kind::Bool), bval_(v) {}

    // All non-bool integral types (int, long, long long, size_t, …)
    template <std::integral T>
        requires(!std::same_as<T, bool>)
    json(T v) : kind_(Kind::Int), ival_(static_cast<int64_t>(v)) {}

    // All floating-point types
    template <std::floating_point T>
    json(T v) : kind_(Kind::Float), fval_(static_cast<double>(v)) {}

    json(const char* v) : kind_(Kind::String), sval_(v ? v : "") {}
    json(const std::string& v) : kind_(Kind::String), sval_(v) {}
    json(std::string&& v) : kind_(Kind::String), sval_(std::move(v)) {}

    json(array_t v) : kind_(Kind::Array), aval_(std::move(v)) {}
    json(object_t v) : kind_(Kind::Object), oval_(std::move(v)) {}

    // Initializer-list: {{"key",val},{"key2",val2}} → object
    //                   {val1, val2, …}             → array
    json(std::initializer_list<json> init) {
        bool all_pairs = true;
        for (const auto& el : init) {
            if (!el.is_array() || el.aval_.size() != 2 || !el.aval_[0].is_string()) {
                all_pairs = false;
                break;
            }
        }

        if (all_pairs) {
            kind_ = Kind::Object;
            for (const auto& el : init)
                oval_[el.aval_[0].sval_] = el.aval_[1];
        } else {
            kind_ = Kind::Array;
            aval_ = array_t(init);
        }
    }

    // -----------------------------------------------------------------------
    // Type queries
    // -----------------------------------------------------------------------
    [[nodiscard]] bool is_null() const noexcept { return kind_ == Kind::Null; }
    [[nodiscard]] bool is_bool() const noexcept { return kind_ == Kind::Bool; }
    [[nodiscard]] bool is_number() const noexcept {
        return kind_ == Kind::Int || kind_ == Kind::Float;
    }
    [[nodiscard]] bool is_number_integer() const noexcept { return kind_ == Kind::Int; }
    [[nodiscard]] bool is_number_float() const noexcept { return kind_ == Kind::Float; }
    [[nodiscard]] bool is_string() const noexcept { return kind_ == Kind::String; }
    [[nodiscard]] bool is_array() const noexcept { return kind_ == Kind::Array; }
    [[nodiscard]] bool is_object() const noexcept { return kind_ == Kind::Object; }

    // -----------------------------------------------------------------------
    // Typed get<T>  (uses concepts in if constexpr for clarity)
    // -----------------------------------------------------------------------
    template <typename T> [[nodiscard]] T get() const {
        if constexpr (std::same_as<T, bool>) {
            if (kind_ != Kind::Bool)
                throw exception("get<bool> on non-bool");
            return bval_;
        } else if constexpr (std::integral<T>) {
            if (kind_ == Kind::Int)
                return static_cast<T>(ival_);
            if (kind_ == Kind::Float)
                return static_cast<T>(fval_);
            throw exception("get<integer> on non-number");
        } else if constexpr (std::floating_point<T>) {
            if (kind_ == Kind::Float)
                return static_cast<T>(fval_);
            if (kind_ == Kind::Int)
                return static_cast<T>(ival_);
            throw exception("get<float> on non-number");
        } else if constexpr (std::same_as<T, std::string>) {
            if (kind_ != Kind::String)
                throw exception("get<string> on non-string");
            return sval_;
        }
        throw exception("get: unsupported type");
    }

    // -----------------------------------------------------------------------
    // Access operators
    // -----------------------------------------------------------------------
    json& operator[](const std::string& key) {
        if (kind_ == Kind::Null)
            kind_ = Kind::Object;
        if (kind_ != Kind::Object)
            throw exception("operator[string] on non-object");
        return oval_[key];
    }

    [[nodiscard]] const json& operator[](const std::string& key) const {
        static const json null_val{};
        if (kind_ != Kind::Object)
            return null_val;
        auto it = oval_.find(key);
        return it != oval_.end() ? it->second : null_val;
    }

    json& operator[](size_t i) {
        if (kind_ != Kind::Array)
            throw exception("operator[size_t] on non-array");
        return aval_[i];
    }
    [[nodiscard]] const json& operator[](size_t i) const {
        if (kind_ != Kind::Array)
            throw exception("operator[size_t] on non-array");
        return aval_[i];
    }

    [[nodiscard]] bool contains(const std::string& key) const noexcept {
        return kind_ == Kind::Object && oval_.contains(key);
    }

    // value() with typed default (bool, integral, float)
    template <typename T> [[nodiscard]] T value(const std::string& key, const T& def) const {
        if (kind_ != Kind::Object)
            return def;
        auto it = oval_.find(key);
        if (it == oval_.end() || it->second.is_null())
            return def;
        try {
            return it->second.get<T>();
        } catch (...) {
            return def;
        }
    }

    // value() with const char* default → returns std::string
    [[nodiscard]] std::string value(const std::string& key, const char* def) const {
        const char* fallback = def ? def : "";
        if (kind_ != Kind::Object)
            return fallback;
        auto it = oval_.find(key);
        if (it == oval_.end() || it->second.is_null())
            return fallback;
        if (it->second.kind_ != Kind::String)
            return fallback;
        return it->second.sval_;
    }

    [[nodiscard]] size_t size() const noexcept {
        if (kind_ == Kind::Array)
            return aval_.size();
        if (kind_ == Kind::Object)
            return oval_.size();
        return 0;
    }
    [[nodiscard]] bool empty() const noexcept { return size() == 0; }

    // -----------------------------------------------------------------------
    // Iteration (arrays)
    // -----------------------------------------------------------------------
    auto begin() { return aval_.begin(); }
    auto end() { return aval_.end(); }
    auto begin() const { return aval_.begin(); }
    auto end() const { return aval_.end(); }

    // -----------------------------------------------------------------------
    // Static factories
    // -----------------------------------------------------------------------
    [[nodiscard]] static json array() {
        json j;
        j.kind_ = Kind::Array;
        return j;
    }
    [[nodiscard]] static json object() {
        json j;
        j.kind_ = Kind::Object;
        return j;
    }

    [[nodiscard]] static json parse(const std::string& s) {
        Parser p{s};
        json   result = p.parse_value();
        p.skip_ws();
        if (p.pos != s.size())
            throw exception("Trailing content after JSON value");
        return result;
    }

    // -----------------------------------------------------------------------
    // Serialization
    // -----------------------------------------------------------------------
    [[nodiscard]] std::string dump(int indent = -1) const { return dump_impl(indent, 0); }
};
