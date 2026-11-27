/* Mistbar - MIT License */

#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace mistbar::json {

class Value;
using Array  = std::vector<Value>;
using Object = std::map<std::string, Value>;

class Value {
public:
    using Variant = std::variant<std::nullptr_t, bool, double, std::string, Array, Object>;

    Value() : data_(nullptr) {}
    Value(std::nullptr_t) : data_(nullptr) {}
    Value(bool b) : data_(b) {}
    Value(int i) : data_(static_cast<double>(i)) {}
    Value(std::int64_t i) : data_(static_cast<double>(i)) {}
    Value(double d) : data_(d) {}
    Value(const char* s) : data_(std::string(s ? s : "")) {}
    Value(std::string s) : data_(std::move(s)) {}
    Value(Array a) : data_(std::move(a)) {}
    Value(Object o) : data_(std::move(o)) {}

    bool is_null() const noexcept { return std::holds_alternative<std::nullptr_t>(data_); }
    bool is_bool() const noexcept { return std::holds_alternative<bool>(data_); }
    bool is_number() const noexcept { return std::holds_alternative<double>(data_); }
    bool is_string() const noexcept { return std::holds_alternative<std::string>(data_); }
    bool is_array() const noexcept { return std::holds_alternative<Array>(data_); }
    bool is_object() const noexcept { return std::holds_alternative<Object>(data_); }

    bool as_bool(bool def = false) const noexcept;
    double as_double(double def = 0.0) const noexcept;
    int as_int(int def = 0) const noexcept;
    std::string as_string(const std::string& def = "") const;
    const Array& as_array() const;
    Array& as_array_mut();
    const Object& as_object() const;
    Object& as_object_mut();

    const Value* find(const std::string& key) const;

    std::string get_string(const std::string& key, const std::string& def = "") const;
    int get_int(const std::string& key, int def = 0) const;
    double get_double(const std::string& key, double def = 0.0) const;
    bool get_bool(const std::string& key, bool def = false) const;

    std::string dump(int indent = 2) const;

private:
    Variant data_;
};

std::optional<Value> parse(const std::string& text, std::string* error_out = nullptr);

} // namespace mistbar::json
