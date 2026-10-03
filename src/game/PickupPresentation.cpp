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

const char* PickupPresentation::pickupSoundFor(const char* name) {
    if (!name) return nullptr;
    for (const PickupClassSound& c : kPickupClassSounds) {
        const size_t n = std::strlen(c.factoryClass);
        if (std::strncmp(c.factoryClass, name, n) != 0) continue;
        const char* rest = name + n;                          // "" (class) or "_<digits>" (placed actor)
        bool actorSuffix = rest[0] == '_' && rest[1] != 0;
        for (const char* p = rest + 1; actorSuffix && *p; ++p) actorSuffix = *p >= '0' && *p <= '9';
        if (rest[0] == 0 || actorSuffix) return c.pickupSound;
    }
    return nullptr;
}

} // namespace game
