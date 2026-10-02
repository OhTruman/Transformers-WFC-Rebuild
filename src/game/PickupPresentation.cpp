#include "game/PickupPresentation.h"

namespace game {
namespace {
using FactoryDef = PickupPresentation::FactoryDef;
using Kind = PickupPresentation::Kind;
#include "game/PickupPresentation.inc"
constexpr int kCount = (int)(sizeof(kFactories) / sizeof(kFactories[0]));
} // namespace

PickupPresentation::PickupPresentation() { reset(); }

int PickupPresentation::count() { return kCount; }
const PickupPresentation::FactoryDef& PickupPresentation::def(int i) { return kFactories[i]; }

void PickupPresentation::reset() {
    state_.assign((size_t)kCount, EffectState{});
    for (int i = 0; i < kCount; ++i)
        state_[(size_t)i].customActive = kFactories[i].customEffect != nullptr;   // bAutoActivate default true
    // PickupEffect: archetype bAutoActivate=false -> inactive until the first SetPickupVisible.
}

void PickupPresentation::setPickupHidden(int i) {
    EffectState& s = state_[(size_t)i];
    if (kFactories[i].customEffect) { s.customHidden = true; s.customActive = false; }
    if (kFactories[i].highlightFx) s.highlightActive = false;
}

void PickupPresentation::setPickupVisible(int i) {
    EffectState& s = state_[(size_t)i];
    if (kFactories[i].customEffect) { s.customHidden = false; s.customActive = true; }
    if (kFactories[i].highlightFx) s.highlightActive = true;
}

} // namespace game
