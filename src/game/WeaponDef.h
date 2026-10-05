// Clean-room reconstruction — weapon definitions (TnWeapon class CDO + its TnWeaponData, TnWeaponMesh, damage type).
// Versus selects MultiplayerData (TnMultiplayerGame.DesiredWeaponDataType = 3) and falls back to PlayerData when a weapon
// has none (TnWeapon: GetWeaponDataByType returns none -> type 1) [CONFIRMED RE TARGETED_PASS3]. The table is generated
// from authored data by tools/gameplay/gen_weapon_table.js (WeaponTable.inc).
#pragma once
#include <string>

namespace game {

// How the weapon delivers damage. Only InstantHit is simulated by the rebuild; the others are equipped and presented as
// unsupported (no fake firing) [PARTIAL].
enum class WeaponFire { InstantHit, Projectile, Melee, Grenade, Other };

struct WeaponDef {
    const char* id;              // class suffix: "AssaultRifle" (TransContent.TnWeaponAssaultRifle)
    const char* provider;        // TnDataProvider_Weapon UniqueId used by CharacterData.WeaponTypes
    const char* display;         // ItemName (INT)
    int typeCode;                // WeaponType: 0 primary, 1 heavy, 2 melee, 3 vehicle, 4 grenade, -1 none
    WeaponFire fire;
    const char* dataSource;      // "MP" (MultiplayerData) or "SP" (PlayerData fallback)
    const char* dataObject;
    float damage; int shots; int clip, maxAmmo, initialReserve;
    float interval, rangeM, falloffNearM, falloffFarMul;
    float spreadMin, spreadMax, spreadPerShot, spreadCooldown, fineAimSpread;
    float reloadTime, equipTime, putDownTime; bool autoFire; float heatMax;
    const char* meshGltf; const char* animGltf;   // ExtractedAssets-relative
    const char* muzzleBone; float muzzleLocUE[3]; int muzzleRotUE[3];
    const char* animFire; const char* animReload; const char* animEquip; const char* animPutDown; const char* animIdle;
    const char* damageType; const char* deathString; const char* suicideString; const char* killFeedIcon;
    const char* muzzleFx; const char* tracerFx;
};

// By provider UniqueId or class id (case-sensitive); null when unknown.
const WeaponDef* findWeaponDef(const std::string& name);
int weaponDefCount();
const WeaponDef& weaponDefAt(int i);

} // namespace game
