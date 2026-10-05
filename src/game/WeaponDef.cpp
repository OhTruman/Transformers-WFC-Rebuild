// Clean-room reconstruction — weapon definition table (generated rows, see WeaponDef.h).
#include "game/WeaponDef.h"

namespace game {

namespace {
const WeaponDef kWeapons[] = {
#include "game/WeaponTable.inc"
};
}

int weaponDefCount() { return (int)(sizeof(kWeapons) / sizeof(kWeapons[0])); }
const WeaponDef& weaponDefAt(int i) { return kWeapons[i]; }

const WeaponDef* findWeaponDef(const std::string& name) {
    for (const WeaponDef& w : kWeapons)
        if (name == w.provider || name == w.id) return &w;
    // CharacterData may carry the class path ("TransContent.TnWeaponAssaultRifle").
    const std::string pre = "TransContent.TnWeapon";
    if (name.compare(0, pre.size(), pre) == 0) return findWeaponDef(name.substr(pre.size()));
    return nullptr;
}

} // namespace game
