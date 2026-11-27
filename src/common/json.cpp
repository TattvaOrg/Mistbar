/* Mistbar - MIT License */

#include "common/json.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace mistbar::json {

namespace {

class Parser {
public:
    explicit Parser(const std::string& input) : src_(input) {}

    std::optional<Value> parse_root(std::string* error_out) {
        skip_ws();
        auto val = parse_value();
        if (!val.has_value()) {
            if (error_out) *error_out = error_;
            return std::nullopt;
        }
        skip_ws();
        if (pos_ != src_.size()) {
            set_error("unexpected trailing characters");
            if (error_out) *error_out = error_;
            return std::nullopt;
        }
        return val;
    }

private:
    const std::string& src_;
    std::size_t pos_ = 0;
    std::string error_;

    void set_error(const std::string& msg) {
        if (error_.empty()) {
            error_ = msg + " at offset " + std::to_string(pos_);
        }
    }

    void skip_ws() {
        while (pos_ < src_.size()) {
            char c = src_[pos_];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    std::optional<Value> parse_value() {
        skip_ws();
        if (pos_ >= src_.size()) {
            set_error("unexpected end of input");
            return std::nullopt;
        }
        char c = src_[pos_];
        if (c == '{') return parse_object();
        if (c == '[') return parse_array();
        if (c == '"') {
            auto s = parse_string();
            return s ? std::optional<Value>(*s) : std::nullopt;
        }
        if (c == 't') return parse_literal("true", Value(true));
        if (c == 'f') return parse_literal("false", Value(false));
        if (c == 'n') return parse_literal("null", Value(nullptr));
        if (c == '-' || (c >= '0' && c <= '9')) return parse_number();
        set_error("invalid token");
        return std::nullopt;
    }

    std::optional<Value> parse_literal(const char* lit, Value val) {
        std::size_t i = 0;
        while (lit[i] != '\0') {
            if (pos_ + i >= src_.size() || src_[pos_ + i] != lit[i]) {
                set_error(std::string("expected ") + lit);
                return std::nullopt;
            }
            ++i;
        }
        pos_ += i;
        return val;
    }

    std::optional<Value> parse_number() {
        const std::size_t start = pos_;
        if (src_[pos_] == '-') ++pos_;
        if (pos_ >= src_.size() || !std::isdigit(static_cast<unsigned char>(src_[pos_]))) {
            set_error("invalid number");
            return std::nullopt;
        }
        if (src_[pos_] == '0') {
            ++pos_;
        } else {
            while (pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_]))) {
                ++pos_;
            }
        }
        if (pos_ < src_.size() && src_[pos_] == '.') {
            ++pos_;
            if (pos_ >= src_.size() || !std::isdigit(static_cast<unsigned char>(src_[pos_]))) {
                set_error("invalid fraction");
                return std::nullopt;
            }
            while (pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_]))) {
                ++pos_;
            }
        }
        if (pos_ < src_.size() && (src_[pos_] == 'e' || src_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < src_.size() && (src_[pos_] == '+' || src_[pos_] == '-')) {
                ++pos_;
            }
            if (pos_ >= src_.size() || !std::isdigit(static_cast<unsigned char>(src_[pos_]))) {
                set_error("invalid exponent");
                return std::nullopt;
            }
            while (pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_]))) {
                ++pos_;
            }
        }
        const std::string token = src_.substr(start, pos_ - start);
        char* endptr = nullptr;
        const double num = std::strtod(token.c_str(), &endptr);
        if (endptr == token.c_str() || !std::isfinite(num)) {
            set_error("number out of range");
            return std::nullopt;
        }
        return Value(num);
    }

    static void append_utf8(std::string& out, unsigned int cp) {
        if (cp <= 0x7F) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    std::optional<unsigned int> parse_hex4() {
        if (pos_ + 4 > src_.size()) {
            set_error("incomplete unicode escape");
            return std::nullopt;
        }
        unsigned int val = 0;
        for (int i = 0; i < 4; ++i) {
            char c = src_[pos_++];
            val <<= 4;
            if (c >= '0' && c <= '9') val |= static_cast<unsigned int>(c - '0');
            else if (c >= 'a' && c <= 'f') val |= static_cast<unsigned int>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') val |= static_cast<unsigned int>(c - 'A' + 10);
            else {
                set_error("invalid hex digit");
                return std::nullopt;
            }
        }
        return val;
    }

    std::optional<std::string> parse_string() {
        if (pos_ >= src_.size() || src_[pos_] != '"') {
            set_error("expected string");
            return std::nullopt;
        }
        ++pos_;
        std::string out;
        while (pos_ < src_.size()) {
            char c = src_[pos_++];
            if (c == '"') {
                return out;
            }
            if (static_cast<unsigned char>(c) < 0x20) {
                set_error("unescaped control character");
                return std::nullopt;
            }
            if (c == '\\') {
                if (pos_ >= src_.size()) {
                    set_error("unterminated escape");
                    return std::nullopt;
                }
                char esc = src_[pos_++];
                switch (esc) {
                    case '"':  out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/':  out.push_back('/'); break;
                    case 'b':  out.push_back('\b'); break;
                    case 'f':  out.push_back('\f'); break;
                    case 'n':  out.push_back('\n'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 't':  out.push_back('\t'); break;
                    case 'u': {
                        auto cp = parse_hex4();
                        if (!cp) return std::nullopt;
                        if (*cp >= 0xD800 && *cp <= 0xDBFF && pos_ + 2 <= src_.size() &&
                            src_[pos_] == '\\' && src_[pos_ + 1] == 'u') {
                            pos_ += 2;
                            auto low = parse_hex4();
                            if (!low) return std::nullopt;
                            if (*low >= 0xDC00 && *low <= 0xDFFF) {
                                unsigned int combined = 0x10000 + (((*cp - 0xD800) << 10) | (*low - 0xDC00));
                                append_utf8(out, combined);
                            } else {
                                append_utf8(out, *cp);
                                append_utf8(out, *low);
                            }
                        } else {
                            append_utf8(out, *cp);
                        }
                        break;
                    }
                    default:
                        set_error("unknown escape sequence");
                        return std::nullopt;
                }
            } else {
                out.push_back(c);
            }
        }
        set_error("unterminated string");
        return std::nullopt;
    }

    std::optional<Value> parse_array() {
        ++pos_;
        Array arr;
        skip_ws();
        if (pos_ < src_.size() && src_[pos_] == ']') {
            ++pos_;
            return Value(std::move(arr));
        }
        while (true) {
            auto elem = parse_value();
            if (!elem) return std::nullopt;
            arr.push_back(std::move(*elem));
            skip_ws();
            if (pos_ >= src_.size()) {
                set_error("unterminated array");
                return std::nullopt;
            }
            if (src_[pos_] == ']') {
                ++pos_;
                return Value(std::move(arr));
            }
            if (src_[pos_] != ',') {
                set_error("expected ',' or ']' in array");
                return std::nullopt;
            }
            ++pos_;
        }
    }

    std::optional<Value> parse_object() {
        ++pos_;
        Object obj;
        skip_ws();
        if (pos_ < src_.size() && src_[pos_] == '}') {
            ++pos_;
            return Value(std::move(obj));
        }
        while (true) {
            skip_ws();
            auto key = parse_string();
            if (!key) return std::nullopt;
            skip_ws();
            if (pos_ >= src_.size() || src_[pos_] != ':') {
                set_error("expected ':' after object key");
                return std::nullopt;
            }
            ++pos_;
            auto val = parse_value();
            if (!val) return std::nullopt;
            obj[std::move(*key)] = std::move(*val);
            skip_ws();
            if (pos_ >= src_.size()) {
                set_error("unterminated object");
                return std::nullopt;
            }
            if (src_[pos_] == '}') {
                ++pos_;
                return Value(std::move(obj));
            }
            if (src_[pos_] != ',') {
                set_error("expected ',' or '}' in object");
                return std::nullopt;
            }
            ++pos_;
        }
    }
};

void write_escaped_string(std::ostringstream& out, const std::string& s) {
    out << '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8]{};
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned int>(c));
                    out << buf;
                } else {
                    out << static_cast<char>(c);
                }
                break;
        }
    }
    out << '"';
}

void dump_impl(std::ostringstream& out, const Value& v, int indent, int depth) {
    if (v.is_null()) {
        out << "null";
    } else if (v.is_bool()) {
        out << (v.as_bool() ? "true" : "false");
    } else if (v.is_number()) {
        const double d = v.as_double();
        if (std::floor(d) == d && std::abs(d) < 9e15) {
            out << static_cast<long long>(d);
        } else {
            out << d;
        }
    } else if (v.is_string()) {
        write_escaped_string(out, v.as_string());
    } else if (v.is_array()) {
        const auto& arr = v.as_array();
        if (arr.empty()) {
            out << "[]";
            return;
        }
        out << '[';
        for (std::size_t i = 0; i < arr.size(); ++i) {
            if (indent > 0) {
                out << '\n' << std::string(static_cast<std::size_t>((depth + 1) * indent), ' ');
            }
            dump_impl(out, arr[i], indent, depth + 1);
            if (i + 1 < arr.size()) out << ',';
        }
        if (indent > 0) {
            out << '\n' << std::string(static_cast<std::size_t>(depth * indent), ' ');
        }
        out << ']';
    } else if (v.is_object()) {
        const auto& obj = v.as_object();
        if (obj.empty()) {
            out << "{}";
            return;
        }
        out << '{';
        std::size_t idx = 0;
        for (const auto& [k, val] : obj) {
            if (indent > 0) {
                out << '\n' << std::string(static_cast<std::size_t>((depth + 1) * indent), ' ');
            }
            write_escaped_string(out, k);
            out << (indent > 0 ? ": " : ":");
            dump_impl(out, val, indent, depth + 1);
            if (++idx < obj.size()) out << ',';
        }
        if (indent > 0) {
            out << '\n' << std::string(static_cast<std::size_t>(depth * indent), ' ');
        }
        out << '}';
    }
}

} // namespace

bool Value::as_bool(bool def) const noexcept {
    if (auto p = std::get_if<bool>(&data_)) return *p;
    return def;
}

double Value::as_double(double def) const noexcept {
    if (auto p = std::get_if<double>(&data_)) return *p;
    return def;
}

int Value::as_int(int def) const noexcept {
    if (auto p = std::get_if<double>(&data_)) return static_cast<int>(*p);
    return def;
}

std::string Value::as_string(const std::string& def) const {
    if (auto p = std::get_if<std::string>(&data_)) return *p;
    return def;
}

const Array& Value::as_array() const {
    static const Array empty;
    if (auto p = std::get_if<Array>(&data_)) return *p;
    return empty;
}

Array& Value::as_array_mut() {
    if (!is_array()) data_ = Array{};
    return std::get<Array>(data_);
}

const Object& Value::as_object() const {
    static const Object empty;
    if (auto p = std::get_if<Object>(&data_)) return *p;
    return empty;
}

Object& Value::as_object_mut() {
    if (!is_object()) data_ = Object{};
    return std::get<Object>(data_);
}

const Value* Value::find(const std::string& key) const {
    if (auto p = std::get_if<Object>(&data_)) {
        auto it = p->find(key);
        if (it != p->end()) return &it->second;
    }
    return nullptr;
}

std::string Value::get_string(const std::string& key, const std::string& def) const {
    const Value* v = find(key);
    return v ? v->as_string(def) : def;
}

int Value::get_int(const std::string& key, int def) const {
    const Value* v = find(key);
    return v ? v->as_int(def) : def;
}

double Value::get_double(const std::string& key, double def) const {
    const Value* v = find(key);
    return v ? v->as_double(def) : def;
}

bool Value::get_bool(const std::string& key, bool def) const {
    const Value* v = find(key);
    return v ? v->as_bool(def) : def;
}

std::string Value::dump(int indent) const {
    std::ostringstream out;
    dump_impl(out, *this, indent, 0);
    if (indent > 0) out << '\n';
    return out.str();
}

std::optional<Value> parse(const std::string& text, std::string* error_out) {
    Parser parser(text);
    return parser.parse_root(error_out);
}

} // namespace mistbar::json
