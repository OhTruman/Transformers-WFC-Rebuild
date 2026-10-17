# Presentation audit 2: vehicle weapons, abilities, transformation, melee

WFC RE lane, 2026-10-18. This continues WEAPON_PRESENTATION_AUDIT_RE.md (same method). Parts are added in order (4), (1), (3),
(2), with a commit after each.

**Grep classes** (read-only, against `Rebuild/src`, `Rebuild/data`, `Rebuild/work/render/**/*.json`):
- **C** = named in src code;
- **E** = only in the asset JSON embedded in `src/game/CharacterAudio.inc`;
- **R** = in render / data JSON;
- **-** = no hit.

**C means named, not faithful.** A value column is marked *absent* only when no src reader exists.

**Data note:** MP uses `TnWeapon.MultiplayerData` (`GetWeaponDataByType(3)`), not `PlayerData` (campaign). Everything here uses the
MP data. The gun table was corrected the same way; see its §3 note.

## Part (4): vehicle weapons (delta to the gun table)

The 7 MP vehicle weapons (WeaponType 3, every chassis, both factions) are already rows in the gun table: muzzle, tracer, squib,
recoil, camera. This part adds the firing mechanics and the projectile presentation.

**Muzzle alternation (CONFIRMED, `HmWeapon.OnPlayFireEffects`):** each shot runs, in order:
1. `PlayWeaponEvent`;
2. `StartMuzzleFlash`;
3. `PlayTracerEffect` (unless bSuppressTracerEffects);
4. `PlayImpactEffects`;
5. `EjectShellCasing`;
6. **`ChangeSocket()`**: `CurrentSocket = (CurrentSocket + 1) mod MuzzleFlashSockets.Length`;
7. then the next weapon mesh, `_CurrentMesh = (+1) mod _WeaponMeshes.Length`.

So two-socket vehicle guns alternate **WeaponSocket_Primary / WeaponSocket_Primary2** every shot.
- `EjectShellCasing` uses the mesh's `ShellCasingPSCTemplate`, which **no WFC weapon mesh authors**. All shell ejection is
  AnimNotify-driven (gun table §2).
- Ours: `ChangeSocket` and `WeaponSocket_Primary2` are in `Weapon.h` / `PlayerController.cpp` / `ChassisDef.*` [C].

| Weapon (MP data) | Fire | Interval (s) | Sockets | Projectile | Flight FX / explosion FX / flight loop | Speed | Grep (proj class, flight, explosion, loop) |
|---|---|---|---|---|---|---|---|
| AssaultRifleVehicle (AssaultRifle_Vehicle_WEPDATA) | instant hit | 0.065 | Primary / Primary2 | — | tracer Tracer_AssaultRifle_FX (gun table) | — | gun table |
| PlaneAssaultRifle (AssaultRifle_Plane) | instant hit | 0.065 | Primary / Primary2 | — | same | — | gun table |
| PlaneMachineGun | instant hit | 0.06 | Primary / Primary2 | — | same | — | gun table |
| RocketVehicle (Rocket_Vehicle) | projectile | 0.7 | Primary / Primary2 | TnProjectilePlaneRocketTemp | Tracer_HomRocket_FX / Squib_HomRocket_01_FX / BL_WPN_GUN_ROCKET.TRAIL_LP | 18000 | R (data json) / R / C (FxLights) / **-** |
| PlaneRocket (Rocket_Plane) | projectile, **burst 4, BurstDelay 0.75** | 0.15 within a burst | Primary / Primary2 | TnProjectilePlaneRocketTemp | same | 18000 | same |
| HomingRocketVehicle | projectile (2 fire types; the second is none) | 1.25 | Primary / Primary2 | TnProjectileHomingRocketVehicle | same FX | 10000 | R / R / C / - |
| TankCannon | projectile | 2.0 | Primary only | TnProjectileTankShell | Trail_TankCannon_FX / **Martyrdom_Grenade_FX** / BL_WPN_GUN_TANK_CANNON.TRAIL_LP | 20000 | R / C (WeaponTable) / C (FxLights) / - |

**The secondary (vehicle-form) grenades** are per chassis family through `ChassisRestriction` and fire alternately from
`MuzzleFlash_R` / `MuzzleFlash_L`:
- FlakGrenades: trucks;
- KamikazeMines: cars;
- HealGrenades: jets;
- FlashBangs: tanks.

The gun table rows have their projectile FX.

**Findings:**
- Vehicle projectile flight FX: in render data and `weapons_mp.json` [R]. The explosion squibs are coded in FxLights [C].
- **Projectile flight loop sounds** (`*.TRAIL_LP`, `BL_WPN_GRENADE.*_FLIGHT_LP`): **no src hit (-)**. Only ability projectiles
  (GuidedMissile) play a FlightSound in `AbilityAudio.cpp`.
- **The tank-shell explosion is Martyrdom_Grenade_FX** (the projectile CDO), not a tank-specific effect. Ours references it [C].
- PlaneRocket's 4-shot burst with a 0.75 s gap alternates sockets per rocket. Interval and burst are in `weapons_mp.json` [R].
  Runtime use: unknown.
