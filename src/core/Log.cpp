#include "core/Log.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#ifdef _WIN32
#include <io.h>
#include <windows.h>
#endif

namespace core {

// Buffered, background log writer: a caller formats its line into a thread-local buffer and appends it to the shared buffer
// under a short lock; a writer thread swaps the buffer out every ~50 ms (or when it grows past 64 KB) and writes it to stdout
// and wfc.log, then flushes - so no game thread waits on a disk / pipe / AV-scanner write. Line order is preserved (one
// buffer, appended under the lock). Errors drain synchronously (an error line is on disk before the caller continues);
// logFlush() drains on demand (the crash handler, shutdown); exit drains through atexit. WFC_LOG_SYNC=1 restores the old
// write-and-flush-per-line behaviour.
namespace {

std::FILE* logFile() {
    // Mirror all logs to a file next to the executable so output is capturable even when
    // launched as a windowed process with no attached console.
    static std::FILE* f = std::fopen("wfc.log", "w");
    return f;
}

struct Writer {
    std::mutex m;                  // guards pending
    std::mutex io;                 // serialises the actual writes (writer thread / synchronous drains)
    std::condition_variable cv;
    std::string pending, writing;
    std::atomic<bool> started{false};
    bool sync = std::getenv("WFC_LOG_SYNC") != nullptr;

    void writeOut(const std::string& s) {
        if (s.empty()) return;
        std::FILE* streams[2] = {stdout, logFile()};
        for (std::FILE* st : streams) {
            if (!st) continue;
            std::fwrite(s.data(), 1, s.size(), st);
            std::fflush(st);
        }
    }
    void drain() {                 // any thread: write everything appended so far, in order
        std::lock_guard<std::mutex> lio(io);
        {
            std::lock_guard<std::mutex> lk(m);
            writing.swap(pending);
        }
        writeOut(writing);
        writing.clear();
    }
    void run() {
        for (;;) {
            {
                std::unique_lock<std::mutex> lk(m);
                cv.wait_for(lk, std::chrono::milliseconds(50), [&] { return pending.size() > (64u << 10); });
            }
            drain();
        }
    }
    void start() {
        bool expected = false;
        if (!started.compare_exchange_strong(expected, true)) return;
        pending.reserve(64u << 10);
        std::atexit([] { writer().drain(); });
        std::thread([this] { run(); }).detach();
    }
    static Writer& writer();
};

Writer& Writer::writer() { static Writer* w = new Writer; return *w; }   // never destroyed: the detached writer may still run at exit

} // namespace

void logFlush() { Writer::writer().drain(); }

namespace { char gLastLine[160]; std::atomic<void (*)(const char*)> gLineHook{nullptr}; }
void setLogLineHook(void (*hook)(const char* line)) { gLineHook.store(hook, std::memory_order_release); }
const char* logLastLine() { return gLastLine; }

bool logTryFlush() {
    // Crash-filter safe: never blocks (try_lock only - the faulting thread may hold either mutex), never allocates, bypasses
    // stdio's own locks (raw WriteFile on the OS handles). Every completed write was already fflush'ed, so the CRT buffers
    // are empty and these bytes land after them, in order.
    Writer& w = Writer::writer();
    if (!w.io.try_lock()) return false;
    if (!w.m.try_lock()) { w.io.unlock(); return false; }
    bool ok = true;
#ifdef _WIN32
    if (!w.pending.empty()) {
        HANDLE hs[2] = {GetStdHandle(STD_OUTPUT_HANDLE), INVALID_HANDLE_VALUE};
        if (std::FILE* f = logFile()) hs[1] = (HANDLE)_get_osfhandle(_fileno(f));
        for (HANDLE h : hs) {
            if (h == INVALID_HANDLE_VALUE || h == nullptr) continue;
            DWORD written = 0;
            ok = WriteFile(h, w.pending.data(), (DWORD)w.pending.size(), &written, nullptr) && ok;
        }
        w.pending.clear();                                  // keeps the capacity: no free
    }
#else
    if (!w.pending.empty()) { w.writeOut(w.pending); w.pending.clear(); }
#endif
    w.m.unlock();
    w.io.unlock();
    return ok;
}

void logMessage(LogLevel level, const char* fmt, ...) {
    const char* tag = "[..]";
    switch (level) {
        case LogLevel::Trace: tag = "[trace]"; break;
        case LogLevel::Info:  tag = "[info ]"; break;
        case LogLevel::Warn:  tag = "[warn ]"; break;
        case LogLevel::Error: tag = "[error]"; break;
    }
    thread_local char line[2048];
    // WFC_LOGTIME=1 (diagnostics): seconds since the first log line in front of the tag, for timing questions across systems.
    static const bool timed = std::getenv("WFC_LOGTIME") != nullptr;
    static const auto t0 = std::chrono::steady_clock::now();
    int n = timed ? std::snprintf(line, sizeof line, "%9.3f %s ", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), tag)
                  : std::snprintf(line, sizeof line, "%s ", tag);
    va_list args;
    va_start(args, fmt);
    const int body = std::vsnprintf(line + n, sizeof line - (size_t)n - 1, fmt, args);
    va_end(args);
    std::string longLine;                                   // rare: a line longer than the buffer
    const char* text = line;
    size_t len;
    if (body >= 0 && (size_t)(n + body) < sizeof line - 1) {
        len = (size_t)(n + body);
        line[len++] = '\n';
    } else {
        longLine.assign(line, (size_t)n);
        va_start(args, fmt);
        const int need = std::vsnprintf(nullptr, 0, fmt, args);
        va_end(args);
        longLine.resize((size_t)n + (size_t)(need > 0 ? need : 0) + 1);
        va_start(args, fmt);
        std::vsnprintf(&longLine[(size_t)n], (size_t)(need > 0 ? need : 0) + 1, fmt, args);
        va_end(args);
        longLine.back() = '\n';
        text = longLine.data();
        len = longLine.size();
    }
    {   // dev-tool context (WFC_ALLOCPROF reports): a plain copy, racy by design
        const size_t k = len < sizeof gLastLine - 1 ? len : sizeof gLastLine - 1;
        std::memcpy(gLastLine, text, k);
        gLastLine[k] = 0;
        if (void (*hook)(const char*) = gLineHook.load(std::memory_order_acquire)) hook(gLastLine);
        if (k && gLastLine[k - 1] == '\n') gLastLine[k - 1] = 0;
    }
    Writer& w = Writer::writer();
    if (w.sync) {                                           // WFC_LOG_SYNC: the old per-line write + flush
        std::lock_guard<std::mutex> lio(w.io);
        w.writeOut(std::string(text, len));
        return;
    }
    w.start();
    {
        std::lock_guard<std::mutex> lk(w.m);
        w.pending.append(text, len);
    }
    if (level == LogLevel::Error) w.drain();               // an error is on disk before the caller goes on
}

} // namespace core
