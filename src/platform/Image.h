// Clean-room reconstruction — platform image decoding (PNG/etc -> RGBA8).
// The gameplay/asset layers call this; the implementation lives in a platform adapter.
#pragma once
#include <string>
#include "render/Mesh.h"   // render::ImageData

namespace platform {

// Decode an image file (PNG) into RGBA8, top row first. Returns false on failure.
bool decodeImage(const std::string& path, render::ImageData& out);

// A second source for an image file: the renderer registers its original-block (DDS) decoder, whose top level is
// verified identical to the PNG (tools/render/build_texture_formats.py), so a package without those PNGs decodes the
// same pixels. Used when the file does not exist (or first, with WFC_DDSFIRST=1, to test a PNG-free package).
// WFC_PNGLOG=1 logs every image file decodeImage actually opens.
using ImageFallback = bool (*)(const std::string& path, render::ImageData& out);
void setImageFallback(ImageFallback f);

} // namespace platform
