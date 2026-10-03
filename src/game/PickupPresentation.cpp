#include "game/PickupPresentation.h"

#include <cstring>

namespace game {
namespace {
using PickupClassSound = PickupPresentation::PickupClassSound;
#include "game/PickupPresentation.inc"
constexpr int kCount = (int)(sizeof(kPickupClassSounds) / sizeof(kPickupClassSounds[0]));
} // namespace

int PickupPresentation::classCount() { return kCount; }
const PickupPresentation::PickupClassSound& PickupPresentation::classDef(int i) { return kPickupClassSounds[i]; }

const char* PickupPresentation::pickupSoundFor(const char* factoryClass) {
    if (!factoryClass) return nullptr;
    for (const PickupClassSound& c : kPickupClassSounds)
        if (std::strcmp(c.factoryClass, factoryClass) == 0) return c.pickupSound;
    return nullptr;
}

} // namespace game
