#pragma once
// Minimal JSON value for the IPC protocol and config file. Not a general-purpose parser:
// UTF-8 in, UTF-8 out, numbers are doubles, \uXXXX escapes supported for the BMP.
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace gs {

class Json {
public:
    using Array = std::vector<Json>;
    using Object = std::map<std::string, Json>;

    Json() = default;                      // null
    Json(std::nullptr_t) {}
    Json(bool b) : v_(b) {}
    Json(int n) : v_((double)n) {}
    Json(double n) : v_(n) {}
    Json(const char* s) : v_(std::string(s)) {}
    Json(std::string s) : v_(std::move(s)) {}
    Json(Array a) : v_(std::make_shared<Array>(std::move(a))) {}
    Json(Object o) : v_(std::make_shared<Object>(std::move(o))) {}

    bool isNull() const { return v_.index() == 0; }
    bool isBool() const { return v_.index() == 1; }
    bool isNumber() const { return v_.index() == 2; }
    bool isString() const { return v_.index() == 3; }
    bool isArray() const { return v_.index() == 4; }
    bool isObject() const { return v_.index() == 5; }

    bool asBool(bool def = false) const { return isBool() ? std::get<1>(v_) : def; }
    double asNumber(double def = 0) const { return isNumber() ? std::get<2>(v_) : def; }
    int asInt(int def = 0) const { return isNumber() ? (int)std::get<2>(v_) : def; }
    std::string asString(const std::string& def = {}) const { return isString() ? std::get<3>(v_) : def; }
    const Array& asArray() const;
    const Object& asObject() const;

    // Object access; returns null Json for missing keys.
    const Json& operator[](const std::string& key) const;
    void set(const std::string& key, Json value);

    std::string dump() const;
    static std::optional<Json> parse(const std::string& text);

private:
    std::variant<std::monostate, bool, double, std::string,
                 std::shared_ptr<Array>, std::shared_ptr<Object>> v_;
};

} // namespace gs
