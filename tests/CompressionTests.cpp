// Clean-room reconstruction — platform file compression: compress -> decompress must be byte-identical, and
// readFileMaybeCompressed must return the same bytes from a plain file and from its compressed twin.
#include "platform/FileCompression.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

static void roundTrip(const std::vector<uint8_t>& in, const char* what) {
    std::vector<uint8_t> packed, back;
    const bool c = platform::compressBuffer(in.data(), in.size(), packed);
    const bool d = c && platform::decompressBuffer(packed.data(), packed.size(), back);
    check(c && d && back == in, what);
}

int main(int argc, char** argv) {
    if (argc > 3) {   // interop: <dir> <path whose twin a tool wrote> <reference copy of the original>
        std::vector<uint8_t> got, ref;
        std::ifstream r(argv[3], std::ios::binary);
        ref.assign(std::istreambuf_iterator<char>(r), std::istreambuf_iterator<char>());
        check(platform::readFileMaybeCompressed(argv[2], got) && got == ref && !ref.empty(), "interop: tool-written twin");
        return failures ? 1 : 0;
    }
    std::mt19937 rng(7);
    std::vector<uint8_t> random(1 << 20), zeros(3 << 20, 0), small{1, 2, 3}, mixed;
    for (auto& b : random) b = (uint8_t)rng();
    for (int i = 0; i < 200000; ++i) {                  // vertex-like data: repeated floats with noise
        float f = (float)(i % 977) * 0.125f;
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&f);
        mixed.insert(mixed.end(), p, p + 4);
    }
    roundTrip(random, "round trip: 1 MB random");
    roundTrip(zeros, "round trip: 3 MB zeros");
    roundTrip(small, "round trip: 3 bytes");
    roundTrip(mixed, "round trip: 800 KB float stream");

    // a plain file and its twin read identically; a corrupt twin is rejected
    const std::string dir = argc > 1 ? argv[1] : ".";
    const std::string plain = dir + "/wfc_compression_test.bin", twin = plain + platform::kCompressedSuffix;
    std::vector<uint8_t> packed;
    platform::compressBuffer(mixed.data(), mixed.size(), packed);
    { std::ofstream(plain, std::ios::binary).write((const char*)mixed.data(), (std::streamsize)mixed.size()); }
    std::vector<uint8_t> a, b;
    check(platform::readFileMaybeCompressed(plain, a) && a == mixed, "readFileMaybeCompressed: plain file");
    std::remove(plain.c_str());
    { std::ofstream(twin, std::ios::binary).write((const char*)packed.data(), (std::streamsize)packed.size()); }
    check(platform::readFileMaybeCompressed(plain, b) && b == mixed, "readFileMaybeCompressed: compressed twin only");
    packed[packed.size() / 2] ^= 0x5A;
    { std::ofstream(twin, std::ios::binary).write((const char*)packed.data(), (std::streamsize)packed.size()); }
    std::vector<uint8_t> c;
    const bool corruptOk = platform::readFileMaybeCompressed(plain, c) && c == mixed;
    check(!corruptOk, "readFileMaybeCompressed: corrupt twin not accepted as the original");
    std::remove(twin.c_str());
    std::printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
