// [Systems, load-time pass] WFC_IMAGECMPTEST: the proof behind Win32Image's WIC classes. Every *.png under WFC_IMAGECMP_ROOTS
// ('|'-separated directories: the package's base tree, the HD pack, the DLC trees) is decoded by GDI+ (the reference) and by WIC and
// the RGBA bytes compared, tallied per PNG class (colour type / depth / interlace / colour chunks). A class is safe for WIC only with
// every file identical and none failing. WFC_IMAGECMP_THREADS (default 8). Read-only: nothing is written but the log.
#include "core/Application.h"
#include "core/Log.h"
#include "platform/Image.h"
#include "platform/LongPath.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace core {

void Application::runImageCompareTest() {
    namespace fs = std::filesystem;
    std::vector<std::string> roots, files;
    if (const char* r = std::getenv("WFC_IMAGECMP_ROOTS")) {
        std::string s(r);
        for (size_t a = 0, b; a <= s.size(); a = b + 1) { b = s.find('|', a); if (b == std::string::npos) b = s.size(); if (b > a) roots.push_back(s.substr(a, b - a)); }
    }
    for (const std::string& root : roots) {
        std::error_code ec;
        const size_t before = files.size();
        for (fs::recursive_directory_iterator it(platform::longPath(root), fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
            if (ec) break;
            if (!it->is_regular_file(ec)) continue;
            std::string ext = it->path().extension().string();
            for (char& c : ext) c = (char)std::tolower((unsigned char)c);
            if (ext == ".png") files.push_back(it->path().string());
        }
        LOG_INFO("IMAGECMP root %s: %zu PNGs", root.c_str(), files.size() - before);
    }
    struct Tally { long n = 0, same = 0, differ = 0, gdiFail = 0, wicFail = 0; std::string example; };
    std::map<std::string, Tally> byClass;
    std::mutex mx;
    std::atomic<size_t> next{0};
    std::atomic<long> doneN{0};
    const int nThreads = std::getenv("WFC_IMAGECMP_THREADS") ? std::max(1, std::atoi(std::getenv("WFC_IMAGECMP_THREADS"))) : 8;
    const auto t0 = std::chrono::steady_clock::now();
    auto work = [&] {
        for (size_t i; (i = next++) < files.size();) {
            const std::string& p = files[i];
            const std::string cls = platform::pngClass(p);
            render::ImageData g, w;
            const bool gok = platform::decodeImageWith(0, p, g), wok = platform::decodeImageWith(1, p, w);
            const bool same = gok && wok && g.w == w.w && g.h == w.h && g.rgba == w.rgba;
            std::lock_guard<std::mutex> lk(mx);
            Tally& t = byClass[cls];
            ++t.n;
            if (!gok) ++t.gdiFail;
            else if (!wok) ++t.wicFail;
            else if (same) ++t.same;
            else { ++t.differ; if (t.example.empty()) t.example = p; }
            const long d = ++doneN;
            if (d % 5000 == 0) LOG_INFO("IMAGECMP progress %ld / %zu", d, files.size());
        }
    };
    std::vector<std::thread> th;
    for (int k = 0; k < nThreads; ++k) th.emplace_back(work);
    for (std::thread& t : th) t.join();
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    long all = 0, allSame = 0;
    std::string safe;
    for (const auto& kv : byClass) {
        const Tally& t = kv.second;
        all += t.n; allSame += t.same;
        const bool ok = t.n > 0 && t.same == t.n;
        if (ok) safe += (safe.empty() ? "" : ", ") + std::string("\"") + kv.first + "\"";
        LOG_INFO("IMAGECMP class %-28s %7ld files: %7ld identical, %5ld differ, %4ld GDI+ fail, %4ld WIC fail%s%s", kv.first.c_str(), t.n, t.same,
                 t.differ, t.gdiFail, t.wicFail, ok ? "  SAFE" : "", t.example.empty() ? "" : ("  e.g. " + t.example).c_str());
    }
    LOG_INFO("IMAGECMP SUMMARY: %ld PNGs in %zu classes, %ld identical (%.1f s, %d threads); WIC-safe classes: %s", all, byClass.size(), allSame,
             secs, nThreads, safe.empty() ? "none" : safe.c_str());
    exitCode_ = all > 0 ? 0 : 1;
}

} // namespace core
