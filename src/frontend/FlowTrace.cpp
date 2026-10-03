#include "frontend/FlowTrace.h"
#include "core/Log.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace frontend {

namespace {
double g_clock = 0.0;
long g_seq = 0;

std::FILE* traceFile() {
    static std::FILE* f = [] {
        const char* p = std::getenv("WFC_FLOWLOG");
        return p ? std::fopen(p, "w") : nullptr;
    }();
    return f;
}

std::string escape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if (c == '\n') o += "\\n";
        else if ((unsigned char)c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += c;
    }
    return o;
}

bool isRaw(const std::string& v) {   // numbers / booleans are written unquoted
    if (v == "true" || v == "false" || v == "null") return true;
    if (v.empty()) return false;
    char* end = nullptr;
    std::strtod(v.c_str(), &end);
    return end && *end == 0 && (v[0] == '-' || (v[0] >= '0' && v[0] <= '9'));
}
} // namespace

void FlowTrace::setClock(double seconds) { g_clock = seconds; }

std::string FlowTrace::num(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.3f", v);
    return b;
}

void FlowTrace::emit(const char* ev, std::initializer_list<Field> fields) {
    ++g_seq;
    std::string line = std::string("FLOW ") + ev;
    for (const Field& f : fields) line += std::string(" ") + f.first + "=" + f.second;
    LOG_INFO("%s", line.c_str());
    if (std::FILE* f = traceFile()) {
        std::string js = "{\"t\":" + num(g_clock) + ",\"seq\":" + std::to_string(g_seq) + ",\"ev\":\"" + escape(ev) + "\"";
        for (const Field& fl : fields)
            js += ",\"" + escape(fl.first) + "\":" + (isRaw(fl.second) ? fl.second : "\"" + escape(fl.second) + "\"");
        js += "}\n";
        std::fputs(js.c_str(), f);
        std::fflush(f);
    }
}

} // namespace frontend
