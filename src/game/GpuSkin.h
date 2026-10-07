#pragma once
// GPU skinning helpers (IRenderer::drawSkinnedMesh, agents/rendering; compile-time detected), shared by Character and WeaponMesh.
#include "assets/SkinnedModel.h"
#include "render/Renderer.h"
#include <cstdint>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace game {

// The full material set of THIS model on every draw: fields may be filled after load (texture handles, renderer-side material
// data), and a reused output must never keep another model's materials. Equal-size assignment is element-wise and reuses the
// strings' capacity, so the steady state does not allocate.
inline void syncMats(std::vector<render::Material>& dst, const std::vector<render::Material>& src) { dst = src; }

// IRenderer::drawSkinnedMesh (agents/rendering), compile-time detected: the renderer skins on the GPU from a bind mesh kept per model
// plus this step's bone palette (and the previous step's for the blend, skinned twice and lerped exactly as the CPU path). Returns
// false when the renderer cannot (no such method, too many joints, WFC_NOGPUSKIN): the CPU skin path draws instead.
template <class R> inline auto drawSkinnedGpu(R& r, const render::MeshData& bind, const std::vector<uint16_t>& j, const std::vector<float>& w,
                                              const std::vector<core::Mat4>& pal, const std::vector<core::Mat4>* prev, float a,
                                              const core::Mat4& m, const core::Vec3& c, const void* key, uint64_t s, int)
    -> decltype(r.drawSkinnedMesh(bind, j, w, pal, prev, a, m, c, key, s), bool()) { return r.drawSkinnedMesh(bind, j, w, pal, prev, a, m, c, key, s); }
template <class R> inline bool drawSkinnedGpu(R&, const render::MeshData&, const std::vector<uint16_t>&, const std::vector<float>&,
                                              const std::vector<core::Mat4>&, const std::vector<core::Mat4>*, float, const core::Mat4&,
                                              const core::Vec3&, const void*, uint64_t, long) { return false; }
template <class R, class = void> struct HasSkinnedApi : std::false_type {};
template <class R> struct HasSkinnedApi<R, std::void_t<decltype(std::declval<R&>().drawSkinnedMesh(
    std::declval<const render::MeshData&>(), std::declval<const std::vector<uint16_t>&>(), std::declval<const std::vector<float>&>(),
    std::declval<const std::vector<core::Mat4>&>(), std::declval<const std::vector<core::Mat4>*>(), 0.0f, std::declval<const core::Mat4&>(),
    std::declval<const core::Vec3&>(), std::declval<const void*>(), uint64_t(0)))>> : std::true_type {};
// Contract check (compile time): a renderer with Rendering's exact drawSkinnedMesh signature is detected and the call site compiles.
namespace {
struct SkinApiContract {
    bool drawSkinnedMesh(const render::MeshData&, const std::vector<uint16_t>&, const std::vector<float>&, const std::vector<core::Mat4>&,
                         const std::vector<core::Mat4>*, float, const core::Mat4&, const core::Vec3&, const void*, uint64_t) { return true; }
};
static_assert(HasSkinnedApi<SkinApiContract>::value, "drawSkinnedMesh contract (agents/rendering) not detected");
[[maybe_unused]] bool skinApiContractCallSite(SkinApiContract& r, const render::MeshData& b, const std::vector<uint16_t>& j, const std::vector<float>& w,
                                             const std::vector<core::Mat4>& p) {
    return drawSkinnedGpu(r, b, j, w, p, &p, 0.5f, core::Mat4{}, core::Vec3{1, 1, 1}, &b, uint64_t(1), 0);
}
}
template <class M> inline auto setBindTangents(M& dst, const std::vector<float>& t, int) -> decltype(dst.tangents = t, void()) { dst.tangents = t; }
template <class M> inline void setBindTangents(M&, const std::vector<float>&, long) {}

// The bind-pose mesh of a model for the GPU path (built once; its address is the renderer's static-buffer key).
inline render::MeshData& bindMeshOf(const assets::SkinnedModel& m) {
    static std::unordered_map<const assets::SkinnedModel*, render::MeshData> cache;
    auto it = cache.find(&m);
    if (it != cache.end()) return it->second;
    render::MeshData& b = cache[&m];
    b.positions = m.positions; b.normals = m.normals; b.uv = m.uv; b.indices = m.indices; b.subs = m.subs; b.mats = m.mats;
    if (!m.tangents.empty()) setBindTangents(b, m.tangents, 0);
    return b;
}

inline void buildPalette(const assets::SkinnedModel& m, const std::vector<core::Mat4>& globals, std::vector<core::Mat4>& out) {
    out.resize(m.skinJoints.size());
    for (size_t j = 0; j < m.skinJoints.size(); ++j) {
        const size_t node = (size_t)m.skinJoints[j];
        const core::Mat4 gm = node < globals.size() ? globals[node] : core::Mat4{};
        out[j] = j < m.invBind.size() ? gm * m.invBind[j] : gm;
    }
}


} // namespace game
