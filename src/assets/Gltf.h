// Clean-room reconstruction — minimal glTF 2.0 / GLB loader.
// Bakes all scene-node primitives into one world-space triangle mesh (positions+normals).
// Ignores skinning (renders bind pose), materials, textures, morphs — added later.
#pragma once
#include <string>
#include "render/Mesh.h"

namespace assets {

// Loads a .glb (binary glTF, embedded BIN). Returns false on error (and logs why).
bool loadGlb(const std::string& path, render::MeshData& out);

} // namespace assets
