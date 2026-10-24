// Clean-room reconstruction — platform image decoding (PNG/etc -> RGBA8).
// The gameplay/asset layers call this; the implementation lives in a platform adapter.
#pragma once
#include <string>
#include <vector>
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

// [Systems, load-time pass] Decode a batch on worker threads ahead of the decodeImage calls that follow (same pixels; the caller
// still uploads on its own thread). WFC_IMAGETHREADS=<n> (0 = off). dropPrefetched() forgets what nobody asked for (a load's end).
void prefetchImages(const std::vector<std::string>& paths);
void dropPrefetched();
// The decoder choice and its proof (WFC_IMAGECMPTEST): a PNG's class "ct<colour type>-<depth>[-i][+gAMA+sRGB+iCCP+cHRM+tRNS+sBIT]";
// decodeImageWith(0 GDI+ | 1 WIC). WFC_IMAGEDECODER=gdiplus|wic forces one (A/B).
std::string pngClass(const std::string& path);
bool decodeImageWith(int decoder, const std::string& path, render::ImageData& out);

} // namespace platform
