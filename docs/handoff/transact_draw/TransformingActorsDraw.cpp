#include "game/TransformingActorsDraw.h"
#include <algorithm>
#include "assets/SkinnedModel.h"
#include "game/GpuSkin.h"
#include "game/TransformingActors.h"
#include "render/Renderer.h"

namespace game {

void TransformingActorsDraw::draw(const TransformingActors& actors, render::IRenderer& r) const {
    const std::vector<TransformingActors::Actor>& list = actors.actors();
    if (list.empty()) return;
    if (states_.size() != list.size()) states_.assign(list.size(), State{});   // (built once per map: Gameplay keeps the order)
    assets::LocalPose lp;
    std::vector<core::Mat4> globals;
    for (size_t i = 0; i < list.size(); ++i) {
        const TransformingActors::Actor& a = list[i];
        if (a.hidden || !a.model || a.model->joints.empty()) continue;
        State& s = states_[i];
        // the transform clip at t (RE: one AnimNodeSequence, position 0 = Untransformed, 1 = Transformed): the pose collision uses
        if (s.posedT != a.t || s.palette.empty()) {
            assets::samplePose(*a.model, a.clipIndex, std::clamp(a.t, 0.0f, a.length), false, lp);
            assets::poseGlobals(*a.model, lp, globals);
            buildPalette(*a.model, globals, s.palette);
            s.posedT = a.t;
            ++s.serial;
        }
        render::MeshData& bind = bindMeshOf(*a.model);
        syncMats(bind.mats, a.model->mats);
        drawSkinnedGpu(r, bind, a.model->joints, a.model->weights, s.palette, nullptr, 1.0f, a.matrix, core::Vec3{1, 1, 1}, &s,
                       s.serial, 0);
    }
}

} // namespace game
