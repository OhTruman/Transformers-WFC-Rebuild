// Clean-room reconstruction — minimal JSON reader (enough for glTF 2.0 + metadata files).
// Not a general-purpose library: no pretty printing, tolerant of the subset exporters emit.
#pragma once
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace assets {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolVal = false;
    double numVal = 0.0;
    std::string strVal;
    std::vector<Json> arr;
    std::map<std::string, Json> obj;

    bool isObject() const { return type == Type::Object; }
    bool isArray() const { return type == Type::Array; }
    bool isNumber() const { return type == Type::Number; }
    bool isString() const { return type == Type::String; }

    size_t size() const { return isArray() ? arr.size() : (isObject() ? obj.size() : 0); }
    bool has(const std::string& k) const { return isObject() && obj.find(k) != obj.end(); }

    // Safe accessors — return a static empty node if missing/mismatched.
    const Json& operator[](size_t i) const {
        static const Json empty;
        return (isArray() && i < arr.size()) ? arr[i] : empty;
    }
    const Json& operator[](const std::string& k) const {
        static const Json empty;
        if (!isObject()) return empty;
        auto it = obj.find(k);
        return it == obj.end() ? empty : it->second;
    }

    int asInt(int def = 0) const { return isNumber() ? (int)(numVal + (numVal < 0 ? -0.5 : 0.5)) : def; }
    double asDouble(double def = 0.0) const { return isNumber() ? numVal : def; }
    float asFloat(float def = 0.0f) const { return isNumber() ? (float)numVal : def; }
    bool asBool(bool def = false) const { return type == Type::Bool ? boolVal : def; }
    const std::string& asString() const { static const std::string e; return isString() ? strVal : e; }

    // Parse from a UTF-8 buffer. Returns false on malformed input.
    static bool parse(const char* data, size_t len, Json& out);
    static bool parse(const std::string& s, Json& out) { return parse(s.data(), s.size(), out); }

private:
    struct Parser {
        const char* p;
        const char* end;
        bool ok = true;

        void skipWs() {
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
        }
        bool value(Json& v) {
            skipWs();
            if (p >= end) return fail();
            char c = *p;
            if (c == '{') return object(v);
            if (c == '[') return array(v);
            if (c == '"') { v.type = Type::String; return str(v.strVal); }
            if (c == 't' || c == 'f') return boolean(v);
            if (c == 'n') return null(v);
            return number(v);
        }
        bool object(Json& v) {
            v.type = Type::Object;
            ++p; // {
            skipWs();
            if (p < end && *p == '}') { ++p; return true; }
            for (;;) {
                skipWs();
                if (p >= end || *p != '"') return fail();
                std::string key;
                if (!str(key)) return false;
                skipWs();
                if (p >= end || *p != ':') return fail();
                ++p;
                Json child;
                if (!value(child)) return false;
                v.obj.emplace(std::move(key), std::move(child));
                skipWs();
                if (p >= end) return fail();
                if (*p == ',') { ++p; continue; }
                if (*p == '}') { ++p; return true; }
                return fail();
            }
        }
        bool array(Json& v) {
            v.type = Type::Array;
            ++p; // [
            skipWs();
            if (p < end && *p == ']') { ++p; return true; }
            for (;;) {
                Json child;
                if (!value(child)) return false;
                v.arr.push_back(std::move(child));
                skipWs();
                if (p >= end) return fail();
                if (*p == ',') { ++p; continue; }
                if (*p == ']') { ++p; return true; }
                return fail();
            }
        }
        bool str(std::string& out) {
            ++p; // opening quote
            while (p < end && *p != '"') {
                char c = *p++;
                if (c == '\\' && p < end) {
                    char e = *p++;
                    switch (e) {
                        case 'n': out.push_back('\n'); break;
                        case 't': out.push_back('\t'); break;
                        case 'r': out.push_back('\r'); break;
                        case 'b': out.push_back('\b'); break;
                        case 'f': out.push_back('\f'); break;
                        case '/': out.push_back('/'); break;
                        case '\\': out.push_back('\\'); break;
                        case '"': out.push_back('"'); break;
                        case 'u': {
                            // Minimal \uXXXX -> UTF-8 (BMP only; sufficient for asset names).
                            if (end - p < 4) return fail();
                            unsigned code = 0;
                            for (int i = 0; i < 4; ++i) {
                                char h = *p++;
                                code <<= 4;
                                if (h >= '0' && h <= '9') code |= (h - '0');
                                else if (h >= 'a' && h <= 'f') code |= (h - 'a' + 10);
                                else if (h >= 'A' && h <= 'F') code |= (h - 'A' + 10);
                                else return fail();
                            }
                            if (code < 0x80) out.push_back((char)code);
                            else if (code < 0x800) {
                                out.push_back((char)(0xC0 | (code >> 6)));
                                out.push_back((char)(0x80 | (code & 0x3F)));
                            } else {
                                out.push_back((char)(0xE0 | (code >> 12)));
                                out.push_back((char)(0x80 | ((code >> 6) & 0x3F)));
                                out.push_back((char)(0x80 | (code & 0x3F)));
                            }
                            break;
                        }
                        default: out.push_back(e); break;
                    }
                } else {
                    out.push_back(c);
                }
            }
            if (p >= end) return fail();
            ++p; // closing quote
            return true;
        }
        bool number(Json& v) {
            char* ep = nullptr;
            double d = std::strtod(p, &ep);
            if (ep == p) return fail();
            v.type = Type::Number;
            v.numVal = d;
            p = ep;
            return true;
        }
        bool boolean(Json& v) {
            if (end - p >= 4 && std::string(p, p + 4) == "true") { v.type = Type::Bool; v.boolVal = true; p += 4; return true; }
            if (end - p >= 5 && std::string(p, p + 5) == "false") { v.type = Type::Bool; v.boolVal = false; p += 5; return true; }
            return fail();
        }
        bool null(Json& v) {
            if (end - p >= 4 && std::string(p, p + 4) == "null") { v.type = Type::Null; p += 4; return true; }
            return fail();
        }
        bool fail() { ok = false; return false; }
    };
};

inline bool Json::parse(const char* data, size_t len, Json& out) {
    Parser ps{data, data + len};
    if (!ps.value(out)) return false;
    return ps.ok;
}

} // namespace assets
