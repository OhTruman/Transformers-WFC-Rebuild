#include "game/PickupPresentation.h"

#include <cstring>

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
    for (int i = 0; i < kCount; ++i) setPickupVisible(i);     // the PreBeginPlay state (see header)
}

int PickupPresentation::find(const char* actor) {
    for (int i = 0; i < kCount; ++i) if (std::strcmp(kFactories[i].actor, actor) == 0) return i;
    return -1;
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
