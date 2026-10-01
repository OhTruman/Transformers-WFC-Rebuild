#include "core/Log.h"
#include <cstdarg>
#include <cstdio>

namespace core {

static std::FILE* logFile() {
    // Mirror all logs to a file next to the executable so output is capturable even when
    // launched as a windowed process with no attached console.
    static std::FILE* f = std::fopen("wfc.log", "w");
    return f;
}

void logMessage(LogLevel level, const char* fmt, ...) {
    const char* tag = "[..]";
    switch (level) {
        case LogLevel::Trace: tag = "[trace]"; break;
        case LogLevel::Info:  tag = "[info ]"; break;
        case LogLevel::Warn:  tag = "[warn ]"; break;
        case LogLevel::Error: tag = "[error]"; break;
    }
    std::FILE* streams[2] = {stdout, logFile()};
    for (std::FILE* s : streams) {
        if (!s) continue;
        std::fputs(tag, s);
        std::fputc(' ', s);
        va_list args;
        va_start(args, fmt);
        std::vfprintf(s, fmt, args);
        va_end(args);
        std::fputc('\n', s);
        std::fflush(s);
    }
}

} // namespace core
