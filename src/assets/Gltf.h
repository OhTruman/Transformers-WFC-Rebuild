// Clean-room reconstruction — minimal glTF 2.0 / GLB loader.
// Bakes all scene-node primitives into one world-space triangle mesh (positions+normals).
// Ignores skinning (renders bind pose), materials, textures, morphs — added later.
#pragma once
#include <string>
#include "render/Mesh.h"

#include <cstdint>
#include <string>
#include <vector>

namespace assets {

// Every asset data file read (glTF / glb / .bin, and other asset readers that opt in) goes through readDataFile.
// The game registers platform::readFileMaybeCompressed (the plain file, else its lossless compressed twin), so a
// package may ship compressed twins; tools that do not register it read plain files.
using FileReadHook = bool (*)(const std::string& path, std::vector<uint8_t>& out);
void setFileReadHook(FileReadHook h);
bool readDataFile(const std::string& path, std::vector<uint8_t>& out);

// Loads a .glb (binary glTF, embedded BIN). Returns false on error (and logs why).
bool loadGlb(const std::string& path, render::MeshData& out);

class Json;
// Parse glTF material `i` (textures resolved relative to `dir`) into a render::Material:
// base colour + tint, normal map, original UE3 material path, and the AssetTools sibling
// textures (*_basecolor -> *_emissive / *_specular).
void parseGltfMaterial(const Json& root, size_t i, const std::string& dir, render::Material& M);

} // namespace assets
