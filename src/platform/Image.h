// Clean-room reconstruction — platform image decoding (PNG/etc -> RGBA8).
// The gameplay/asset layers call this; the implementation lives in a platform adapter.
#pragma once
#include <string>
#include "render/Mesh.h"   // render::ImageData

namespace platform {

// Decode an image file (PNG) into RGBA8, top row first. Returns false on failure.
bool decodeImage(const std::string& path, render::ImageData& out);

} // namespace platform
