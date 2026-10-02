// Clean-room reconstruction — CPU mesh data + renderer handles.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "core/Math.h"

namespace render {

using TextureHandle = int;   // index into the renderer's texture store; -1 == invalid
constexpr TextureHandle kInvalidTexture = -1;

// A raw decoded image (RGBA8), produced by a platform image decoder.
struct ImageData {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;        // w*h*4
    bool valid() const { return w > 0 && h > 0 && rgba.size() == (size_t)w * h * 4; }
};

// A material slice of a mesh: a contiguous range of indices sharing one material.
struct SubMesh {
    uint32_t indexOffset = 0;
    uint32_t indexCount = 0;
    int material = -1;                // index into MeshData::mats
    // Baked lightmap (per prop instance): atlas texture + UV1 transform (uv1*scale+bias).
    std::string lightmapName;         // _LM atlas object name; resolved to a handle at load
    std::string component;            // source UE3 component object path (lightmap/data join key)
    std::string sourceMesh;           // source StaticMesh object path (glTF node extras.mesh)
    int sourceSection = -1;           // primitive index within that mesh
    TextureHandle lightmapTex = kInvalidTexture;
    float lmScale[2] = {1, 1};
    float lmBias[2] = {0, 0};
    float lmScaleVec[3] = {1, 1, 1};  // HDR coeff-0 scale (reconstructs lightmap brightness)
};

// A resolved material: base-colour + emissive textures (by URI until uploaded) + tint.
struct Material {
    std::string baseColorUri;         // relative to the source asset; empty == untextured
    std::string emissiveUri;          // WFC glow (Autobot lights, energon); empty == none
    std::string normalUri;            // tangent-space normal map; empty == none
    std::string specularUri;          // baked specular colour (character customization); empty == none
    std::string wfcName;              // original UE3 material object path (glTF extras.wfc_material)
    std::string sourceName;           // glTF material name (umodel exports: the UE3 object name)
    core::Vec3 color{1, 1, 1};        // baseColorFactor (tint / fallback colour)
    TextureHandle tex = kInvalidTexture;   // base colour, filled in after upload
    TextureHandle emissiveTexHandle = kInvalidTexture;  // emissive, filled in after upload
};

struct MeshData {
    std::vector<float> positions;      // x,y,z per vertex
    std::vector<float> normals;        // x,y,z per vertex (parallel; may be empty)
    std::vector<float> uv;             // u,v per vertex (parallel; may be empty)
    std::vector<float> uv1;            // lightmap UV per vertex (parallel; may be empty)
    std::vector<uint32_t> indices;     // triangle list
    std::vector<SubMesh> subs;         // material slices; empty == draw all with one colour
    std::vector<Material> mats;
    core::Vec3 boundsMin{0, 0, 0};
    core::Vec3 boundsMax{0, 0, 0};

    size_t vertexCount() const { return positions.size() / 3; }
    size_t triangleCount() const { return indices.size() / 3; }
    bool empty() const { return positions.empty() || indices.empty(); }
    bool hasUV() const { return uv.size() == vertexCount() * 2; }
    bool hasUV1() const { return uv1.size() == vertexCount() * 2; }
};

using MeshHandle = int;   // index into the renderer's mesh store; -1 == invalid
constexpr MeshHandle kInvalidMesh = -1;

} // namespace render
