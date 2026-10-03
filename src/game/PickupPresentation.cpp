#include "game/PickupPresentation.h"

#include <cstring>

namespace game {
namespace {
using FactoryDef = PickupPresentation::FactoryDef;
using Kind = PickupPresentation::Kind;
#include "game/PickupPresentation.inc"
constexpr int kCount = (int)(sizeof(kFactories) / sizeof(kFactories[0]));
} // namespace

int PickupPresentation::count() { return kCount; }
const PickupPresentation::FactoryDef& PickupPresentation::def(int i) { return kFactories[i]; }

int PickupPresentation::find(const char* actor) {
    for (int i = 0; i < kCount; ++i) if (std::strcmp(kFactories[i].actor, actor) == 0) return i;
    return -1;
}

} // namespace game
