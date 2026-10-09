// FSB playback gate (Systems; not part of the CMake build). Decodes EVERY original bank under a content root with the game's own
// decoder (platform::decodeFsb: libvgmstream r2117, one pass, PCM16) and compares it with the WAV beside it:
//   PASS  the one-pass PCM equals the WAV's PCM bit for bit from sample 0 for its full length, same channels / rate, and the WAV
//         is not longer - or it is longer only for a loop-flagged bank (vgmstream-cli's default render adds 2 loops + a fade);
//         those are listed with both lengths.
//   FAIL  anything else (decode failure, format mismatch, a differing sample, a longer WAV without a loop flag).
// No WAV is dropped from the package unless this reports 100 % PASS. Read-only on the content tree.
// Build from the repo root (static lib + import libs built from third_party/vgmstream):
//   clang++ -std=c++17 -O2 -Isrc -DWFC_VGMSTREAM=1 -Ithird_party/vgmstream/src tools/systems/fsb_gate.cpp
//     src/platform/win32/FsbDecode.cpp <libvgmstream.a + FFmpeg import libs> -static -o fsb_gate.exe
// Run: fsb_gate.exe <content root> [threads]   (the four FFmpeg DLLs beside the exe)
#include "platform/FsbDecode.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
extern "C" {
#include "libvgmstream.h"
#include "libvgmstream_streamfile.h"
}

namespace fs = std::filesystem;

static bool readWav(const fs::path& p, std::vector<char>& data, int& ch, int& rate) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::vector<unsigned char> b((std::istreambuf_iterator<char>(f)), {});
    if (b.size() < 12 || std::memcmp(b.data(), "RIFF", 4)) return false;
    size_t off = 12;
    while (off + 8 <= b.size()) {
        const unsigned char* c = b.data() + off + 8;
        const size_t sz = b[off + 4] | (b[off + 5] << 8) | (b[off + 6] << 16) | ((size_t)b[off + 7] << 24);
        if (!std::memcmp(b.data() + off, "fmt ", 4)) { ch = c[2] | (c[3] << 8); rate = c[4] | (c[5] << 8) | (c[6] << 16) | (c[7] << 24); }
        else if (!std::memcmp(b.data() + off, "data", 4)) data.assign((const char*)c, (const char*)c + std::min(sz, b.size() - off - 8));
        off += 8 + sz + (sz & 1);
    }
    return !data.empty();
}

static bool loopFlag(const fs::path& bank) {   // the bank's own loop flag (header), as vgmstream reads it
    libstreamfile_t* sf = libstreamfile_open_from_stdio(bank.string().c_str());
    if (!sf) return false;
    libvgmstream_config_t cfg{};
    libvgmstream_t* lib = libvgmstream_create(sf, 0, &cfg);
    const bool l = lib && lib->format->loop_flag;
    if (lib) libvgmstream_free(lib);
    libstreamfile_close(sf);
    return l;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: fsb_gate <content root> [threads]\n"); return 2; }
    const unsigned threads = argc > 2 ? (unsigned)std::atoi(argv[2]) : std::max(2u, std::thread::hardware_concurrency() / 2);
    std::vector<fs::path> banks;
    for (const auto& e : fs::recursive_directory_iterator(argv[1]))
        if (e.is_regular_file() && e.path().extension() == ".fsb") banks.push_back(e.path());
    std::sort(banks.begin(), banks.end());
    std::atomic<size_t> next{0};
    std::atomic<long> pass{0}, longer{0}, noWav{0}, fail{0};
    std::atomic<long long> pcmBytes{0}, bankBytes{0}, wavBytes{0};
    std::mutex outMx;
    std::vector<std::string> fails, loops;
    static std::mutex flagMx;   // vgmstream opens are serialised (see FsbDecode.cpp)
    const auto t0 = std::chrono::steady_clock::now();
    auto work = [&] {
        for (size_t i; (i = next.fetch_add(1)) < banks.size();) {
            const fs::path& bank = banks[i];
            fs::path wav = bank; wav.replace_extension(".wav");
            std::vector<char> ref; int ch = 0, rate = 0;
            if (!readWav(wav, ref, ch, rate)) { ++noWav; std::lock_guard<std::mutex> lk(outMx); fails.push_back("NO WAV " + wav.string()); continue; }
            std::vector<int16_t> pcm; int dch = 0, drate = 0;
            const bool ok = platform::decodeFsb(bank.string(), pcm, dch, drate);
            const size_t bytes = pcm.size() * 2;
            bankBytes += (long long)fs::file_size(bank); wavBytes += (long long)fs::file_size(wav); pcmBytes += (long long)bytes;
            std::string why;
            if (!ok) why = "decode failed";
            else if (dch != ch || drate != rate) why = "format " + std::to_string(dch) + "ch " + std::to_string(drate) + " Hz vs WAV " + std::to_string(ch) + "ch " + std::to_string(rate);
            else if (bytes > ref.size()) why = "one pass longer than the WAV (" + std::to_string(bytes) + " vs " + std::to_string(ref.size()) + ")";
            else if (std::memcmp(pcm.data(), ref.data(), bytes) != 0) why = "PCM differs";
            else if (bytes < ref.size()) {
                bool flag;
                { std::lock_guard<std::mutex> lk(flagMx); flag = loopFlag(bank); }
                if (!flag) why = "WAV longer (" + std::to_string(ref.size()) + " vs one pass " + std::to_string(bytes) + ") without a loop flag";
                else { ++longer; std::lock_guard<std::mutex> lk(outMx);
                       loops.push_back(bank.string() + "  wav " + std::to_string(ref.size()) + " bytes, one pass " + std::to_string(bytes)); }
            }
            if (why.empty()) ++pass;
            else { ++fail; std::lock_guard<std::mutex> lk(outMx); fails.push_back(why + ": " + bank.string()); }
        }
    };
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < threads; ++t) pool.emplace_back(work);
    for (std::thread& t : pool) t.join();
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::sort(loops.begin(), loops.end()); std::sort(fails.begin(), fails.end());
    for (const std::string& l : loops) std::printf("LOOP-FLAGGED (WAV has CLI loops + fade) %s\n", l.c_str());
    for (const std::string& f : fails) std::printf("FAIL %s\n", f.c_str());
    std::printf("banks %zu: PASS %ld (of which loop-flagged with a longer WAV %ld), FAIL %ld, no WAV %ld\n", banks.size(), (long)pass,
                (long)longer, (long)fail, (long)noWav);
    std::printf("banks %.1f MB, WAVs %.1f MB, one-pass PCM %.1f MB; %.1f s on %u threads\n", bankBytes / 1048576.0, wavBytes / 1048576.0,
                pcmBytes / 1048576.0, s, threads);
    std::printf("GATE %s\n", (fail == 0 && noWav == 0 && pass == (long)banks.size()) ? "PASS (100 %)" : "FAIL");
    return (fail == 0 && noWav == 0) ? 0 : 1;
}
