// Clean-room reconstruction — lossless file compression for packaged data (PC ADAPTATION: package size).
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace platform {

// The compressed twin of a data file: `<path>` + kCompressedSuffix, the whole file compressed losslessly (Windows:
// the Compression API's XPRESS_HUFF buffer format, which carries the original size). Packages may ship only the twin.
constexpr const char* kCompressedSuffix = ".xpr";

// Reads `path` if it exists, else `path` + kCompressedSuffix decompressed: byte-identical either way. False if
// neither exists or the twin is corrupt. WFC_FILELOG=1 logs each twin read.
bool readFileMaybeCompressed(const std::string& path, std::vector<uint8_t>& out);

// Existence / size check with the same rule: the plain file's size, else its compressed twin's (packed) size, else -1.
// Use it wherever code checks a data file before loading it (a package may ship only the twin).
long long dataFileSize(const std::string& path);

// Codec (tools and tests): whole-buffer compress / decompress.
bool compressBuffer(const uint8_t* data, size_t n, std::vector<uint8_t>& out);
bool decompressBuffer(const uint8_t* data, size_t n, std::vector<uint8_t>& out);

} // namespace platform
