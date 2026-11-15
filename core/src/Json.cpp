#include "Json.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace gs {

static const Json kNull;
static const Json::Array kEmptyArray;
static const Json::Object kEmptyObject;

const Json::Array& Json::asArray() const { return isArray() ? *std::get<4>(v_) : kEmptyArray; }
const Json::Object& Json::asObject() const { return isObject() ? *std::get<5>(v_) : kEmptyObject; }

const Json& Json::operator[](const std::string& key) const {
    if (!isObject()) return kNull;
    auto& o = *std::get<5>(v_);
    auto it = o.find(key);
    return it == o.end() ? kNull : it->second;
}

void Json::set(const std::string& key, Json value) {
    if (!isObject()) v_ = std::make_shared<Object>();
    (*std::get<5>(v_))[key] = std::move(value);
}

static void dumpString(const std::string& s, std::string& out) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) { char buf[8]; snprintf(buf, sizeof buf, "\\u%04x", c); out += buf; }
            else out += (char)c;
        }
    }
    out += '"';
}

static void dumpValue(const Json& j, std::string& out) {
    if (j.isNull()) out += "null";
    else if (j.isBool()) out += j.asBool() ? "true" : "false";
    else if (j.isNumber()) {
        double d = j.asNumber();
        char buf[32];
        if (std::isfinite(d) && d == std::floor(d) && std::fabs(d) < 1e15) snprintf(buf, sizeof buf, "%.0f", d);
        else snprintf(buf, sizeof buf, "%.6g", d);
        out += buf;
    } else if (j.isString()) dumpString(j.asString(), out);
    else if (j.isArray()) {
        out += '[';
        bool first = true;
        for (auto& e : j.asArray()) { if (!first) out += ','; first = false; dumpValue(e, out); }
        out += ']';
    } else {
        out += '{';
        bool first = true;
        for (auto& [k, v] : j.asObject()) {
            if (!first) out += ',';
            first = false;
            dumpString(k, out); out += ':'; dumpValue(v, out);
        }
        out += '}';
    }
}

std::string Json::dump() const { std::string s; dumpValue(*this, s); return s; }

namespace {
struct Parser {
    const std::string& s;
    size_t i = 0;
    bool ok = true;

    void ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) ++i; }
    bool lit(const char* t) {
        size_t n = strlen(t);
        if (s.compare(i, n, t) == 0) { i += n; return true; }
        return false;
    }
    static void appendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) out += (char)cp;
        else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
        else { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
    }
    std::string str() {
        std::string out;
        ++i; // opening quote
        while (i < s.size() && s[i] != '"') {
            char c = s[i++];
            if (c == '\\' && i < s.size()) {
                char e = s[i++];
                switch (e) {
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u':
                    if (i + 4 <= s.size()) { appendUtf8(out, (unsigned)strtoul(s.substr(i, 4).c_str(), nullptr, 16)); i += 4; }
                    else ok = false;
                    break;
                default: out += e;
                }
            } else out += c;
        }
        if (i >= s.size()) { ok = false; return out; }
        ++i; // closing quote
        return out;
    }
    Json value(int depth) {
        if (depth > 64) { ok = false; return {}; }
        ws();
        if (i >= s.size()) { ok = false; return {}; }
        char c = s[i];
        if (c == '{') {
            ++i;
            Json::Object o;
            ws();
            if (i < s.size() && s[i] == '}') { ++i; return Json(std::move(o)); }
            while (ok) {
                ws();
                if (i >= s.size() || s[i] != '"') { ok = false; break; }
                std::string k = str();
                ws();
                if (i >= s.size() || s[i] != ':') { ok = false; break; }
                ++i;
                o[k] = value(depth + 1);
                ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == '}') { ++i; break; }
                ok = false;
            }
            return Json(std::move(o));
        }
        if (c == '[') {
            ++i;
            Json::Array a;
            ws();
            if (i < s.size() && s[i] == ']') { ++i; return Json(std::move(a)); }
            while (ok) {
                a.push_back(value(depth + 1));
                ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == ']') { ++i; break; }
                ok = false;
            }
            return Json(std::move(a));
        }
        if (c == '"') return Json(str());
        if (lit("true")) return Json(true);
        if (lit("false")) return Json(false);
        if (lit("null")) return Json();
        char* end = nullptr;
        double d = strtod(s.c_str() + i, &end);
        if (end == s.c_str() + i) { ok = false; return {}; }
        i = end - s.c_str();
        return Json(d);
    }
};
} // namespace

std::optional<Json> Json::parse(const std::string& text) {
    Parser p{text};
    Json v = p.value(0);
    p.ws();
    if (!p.ok || p.i != text.size()) return std::nullopt;
    return v;
}

} // namespace gs
