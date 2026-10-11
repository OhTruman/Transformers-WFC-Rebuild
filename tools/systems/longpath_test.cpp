// platform::longPath test (Systems; not part of the CMake build). Builds a > 300-char directory chain under <work dir>, then:
// short paths come back unchanged; a long path writes / reads / sizes / exists through longPath (ifstream, ofstream,
// std::filesystem, _wfopen, GetFileAttributesW); the narrow open of the same path fails (the bug); the .xpr-aware data
// reader reads it; an FSB bank decodes through FsbDecode's wide FILE*; GDI+ loads a PNG through the \\?\ path (decides
// whether Win32Image may use it).
// Build (repo root): clang++ -std=c++17 -O1 -Isrc -DWFC_VGMSTREAM=1 -Ithird_party/vgmstream/src tools/systems/longpath_test.cpp
//   src/platform/win32/LongPath.cpp src/platform/win32/Win32FileCompression.cpp src/platform/win32/FsbDecode.cpp
//   <libwfc_vgmstream.a + FFmpeg import libs> -lgdiplus -lole32 -lcabinet -static -o longpath_test.exe
// Run: longpath_test.exe <work dir> <a .fsb> <a .png>
#include "platform/FileCompression.h"
#include "platform/FsbDecode.h"
#include "platform/LongPath.h"

#include <windows.h>
#include <gdiplus.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

static int fails = 0;
static void check(bool ok, const char* what) { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what); if (!ok) ++fails; }

int main(int argc, char** argv) {
    if (argc < 4) { std::printf("usage: longpath_test <work dir> <fsb> <png>\n"); return 2; }
    namespace fs = std::filesystem;
    const std::string base = argv[1];

    // short paths: unchanged
    check(platform::longPath("rel/dir/file.json").wstring() == L"rel/dir/file.json", "short relative path unchanged");
    check(platform::longPath("F:/Transformers Rebuild/x.json").wstring() == L"F:/Transformers Rebuild/x.json", "short absolute path unchanged");
    check(platform::longPath("\\\\?\\C:\\x").wstring() == L"\\\\?\\C:\\x", "prefixed path unchanged");

    // a long directory chain (each level 40 chars)
    std::string dir = base;
    for (int i = 0; i < 7; ++i) dir += "/level_" + std::to_string(i) + "_abcdefghijklmnopqrstuvwxyz0123";
    std::error_code ec;
    fs::create_directories(platform::longPath(dir), ec);
    check(!ec, "create a > 300-char directory chain through longPath");
    const std::string file = dir + "/payload_with_a_long_name_for_the_test.bin";
    std::printf("      path length %zu, longPath -> %ls...\n", file.size(), platform::longPath(file).wstring().substr(0, 8).c_str());
    check(file.size() > 300, "test path is longer than MAX_PATH");
    std::vector<char> data(4096); for (size_t i = 0; i < data.size(); ++i) data[i] = (char)(i * 7 + 3);
    { std::ofstream o(platform::longPath(file), std::ios::binary); o.write(data.data(), (std::streamsize)data.size()); }
    { std::ifstream in(platform::longPath(file), std::ios::binary); std::vector<char> back((std::istreambuf_iterator<char>(in)), {});
      check(back == data, "ofstream / ifstream round trip"); }
    check(fs::file_size(platform::longPath(file), ec) == data.size() && !ec, "std::filesystem::file_size");
    check(fs::exists(platform::longPath(file), ec), "std::filesystem::exists");
    check(GetFileAttributesW(platform::longPath(file).c_str()) != INVALID_FILE_ATTRIBUTES, "GetFileAttributesW");
    if (FILE* f = _wfopen(platform::longPath(file).c_str(), L"rb")) { std::fclose(f); check(true, "_wfopen"); } else check(false, "_wfopen");
    { std::ifstream narrow(file, std::ios::binary); check(!narrow, "narrow ifstream of the same path fails (the bug the helper fixes)"); }
    { std::vector<uint8_t> out; check(platform::readFileMaybeCompressed(file, out) && out.size() == data.size(), "readFileMaybeCompressed (data reader)");
      check(platform::dataFileSize(file) == (long long)data.size(), "dataFileSize"); }

    // FSB through FsbDecode (wide FILE* -> libvgmstream)
    const std::string fsb = dir + "/bank.fsb";
    check(CopyFileW(platform::longPath(argv[2]).c_str(), platform::longPath(fsb).c_str(), FALSE) != 0, "copy an FSB into the long dir");
    { std::vector<int16_t> a, b; int ca = 0, ra = 0, cb = 0, rb = 0;
      const bool okShort = platform::decodeFsb(argv[2], a, ca, ra), okLong = platform::decodeFsb(fsb, b, cb, rb);
      check(okShort && okLong && a == b && ca == cb && ra == rb, "decodeFsb: long path decodes identically to the original"); }

    // GDI+ with the \\?\ path (Win32Image)
    const std::string png = dir + "/image.png";
    check(CopyFileW(platform::longPath(argv[3]).c_str(), platform::longPath(png).c_str(), FALSE) != 0, "copy a PNG into the long dir");
    Gdiplus::GdiplusStartupInput gi; ULONG_PTR tok = 0; Gdiplus::GdiplusStartup(&tok, &gi, nullptr);
    { Gdiplus::Bitmap ref(platform::longPath(argv[3]).c_str()), lng(platform::longPath(png).c_str());
      const bool ok = ref.GetLastStatus() == Gdiplus::Ok && lng.GetLastStatus() == Gdiplus::Ok && ref.GetWidth() == lng.GetWidth();
      std::printf("      GDI+ status short %d, long %d\n", (int)ref.GetLastStatus(), (int)lng.GetLastStatus());
      check(ok, "GDI+ Bitmap from a \\\\?\\ path"); }
    Gdiplus::GdiplusShutdown(tok);

    fs::remove_all(platform::longPath(base + "/level_0_abcdefghijklmnopqrstuvwxyz0123"), ec);
    std::printf("%s (%d failed)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
