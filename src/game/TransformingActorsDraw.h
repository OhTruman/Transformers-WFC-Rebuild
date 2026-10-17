#pragma once
// Rendering: the MP transforming world actors' presentation (TnTransformingWorldActor doors / bridges; World::transformingActors()).
// Each actor's skeletal mesh (its glb, Gameplay's loaded model) posed by its one transform clip at the actor's t, drawn at its
// matrix unless hidden - GPU-skinned like a character (the same pose Gameplay's collision uses). Lighting: its two baked
// light-map captures lerped by the transform's position (IRenderer::setDrawInterpolatedLightmap; RE INTERPOLATED_LIGHTMAP_RE.md),
// no light environment. Identity: maps without transforming actors draw nothing more.
#include <cstdint>
#include <vector>
#include "core/Math.h"

namespace render { class IRenderer; }

namespace game {

class TransformingActors;

class TransformingActorsDraw {
public:
    // Main thread (World::draw). Per-actor palettes are re-posed only when t changes (the doors hold still almost always).
    void draw(const TransformingActors& actors, render::IRenderer& r) const;

private:
    struct State { float posedT = -1.0f; uint64_t serial = 0; std::vector<core::Mat4> palette; };
    mutable std::vector<State> states_;
};

} // namespace game
