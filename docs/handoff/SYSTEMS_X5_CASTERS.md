# X5: dynamic shadow casters (Rendering package-6 row; Systems data, read only)

Characters (robots / vehicles) already cast. These are the remaining dynamic meshes: what the original had, whether the rebuild draws them today, their bind-pose size (glTF bounds, m) and how many are on screen. tools/systems/x5_casters.py; full data SYSTEMS_X5_CASTERS.json.

## Projectile bodies (World::loadProjectileVisuals; drawn while in flight)

| weapon | mesh | size m (x y z) |
|---|---|---|
| FlakGrenades | WEP_Grenade_Flak_STAT.gltf | [0.41, 0.67, 0.41] |
| FlashBangs | WEP_Grenade_Flashbang_STAT.gltf | [0.41, 0.67, 0.41] |
| HealGrenades | WEP_Grenade_Heal_STAT.gltf | [0.41, 0.67, 0.41] |

Only the thrown grenades have a mesh body. Rockets, the grenade launcher, the plasma cannon and the other launchers are particle-only in the original (weapon.json projectile_visual: no mesh on the projectile class default mesh component; the FlightEffect is the visible body, CONFIRMED), so they have nothing to cast. Grenades (0.4 x 0.7 m) are in flight ~1-3 s; 0-2 on screen in 4v5, a handful in 31v32. Low value: a small moving shadow.

## Deployables (abilities)

| ability | mesh | size m | drawn today |
|---|---|---|---|
| Barrier | WEP_Shield_p.Barrier.WEP_Barrier_SKEL | [1.39, 8.25, 17.66] | yes |
| GuidedMissile | WEP_Rocket_p.WEP_OmegaRocket_STAT | [3.87, 2.1, 2.17] | no |
| Killstreak_GuidedMissileStreak | WEP_Rocket_p.WEP_OmegaRocket_STAT | [3.87, 2.1, 2.17] | no |
| SpawnAmmoCrate | WEP_TurretIonGun_p.WEP_TurretIonGun_SKEL | [5.23, 1.49, 1.51] | no |
| SpawnAmmoCrate | PROP_NEU_Pickups_p.AmmoPickup.PROP_NEU_AmmoPickup_STAT | [0.61, 0.88, 1.28] | no |
| SpawnSentry | WEP_Rocket_p.WEP_StandardRocket_STAT | [2.78, 0.54, 0.62] | no |
| SpawnSentry (World::loadSentry) | WEP_SentryAbility_p.WEP_SentryDeploy_SKEL | [5.2, 2.31, 2.75] | yes |
| KamikazeMines (minepooper / Leader grenade) | WEP_KamikazeMine_p (weapon mesh) | [2.54, 1.19, 0.96] | no - World never draws mines_ (Gameplay item) |

Caps: one barrier per Leader, one sentry per Scientist (TnAbilitySpawnSentry kills the previous), ammo crate / roller / guided missile one per use; mines up to ~7 per minepooper buff (one per 2 s for 15 s). Typical on screen: 0-2 sentries / barriers in 4v5, up to ~6-10 in 31v32. The sentry (bind-pose bounds 5.2 x 2.3 x 2.75 m incl. the folded deploy parts) is the obvious first caster. The barrier is a translucent additive force field in the original (BLEND_Additive): it should NOT cast. The ammo crate / roller / guided missile meshes are not drawn by World today (their abilities spawn effects only), so they wait for presentation first.

## Pickups (map factories; the renderer's pickup meshes, rotating while available)

| map | mesh | count | size m |
|---|---|---|---|
| MP_ESC_BrokenHope | PROP_NEU_Pickups_p.AmmoPickup.PROP_NEU_AmmoPickup_STAT | 3 | [0.61, 0.88, 1.28] |
| MP_ESC_Remnant | PROP_NEU_Pickups_p.AmmoPickup.PROP_NEU_AmmoPickup_STAT | 6 | [0.61, 0.88, 1.28] |
| MP_IAC_Berth | PROP_NEU_Pickups_p.AmmoPickup.PROP_NEU_AmmoPickup_STAT | 5 | [0.61, 0.88, 1.28] |
| MP_IAC_Berth | WEP_CodeOfPower_p.WEP_CodeOfPower2H_SKEL | 3 | [1.75, 4.55, 0.68] |
| MP_IAC_Berth | PROP_NEU_Pickups_p.MPBomb.NEU_MPBomb_SKEL | 2 | [2.19, 3.11, 1.25] |
| MP_IAC_Rust | PROP_NEU_Pickups_p.AmmoPickup.PROP_NEU_AmmoPickup_STAT | 6 | [0.61, 0.88, 1.28] |
| MP_IAC_Rust | WEP_CodeOfPower_p.WEP_CodeOfPower2H_SKEL | 2 | [1.75, 4.55, 0.68] |
| MP_IAC_Rust | PROP_NEU_Pickups_p.MPBomb.NEU_MPBomb_SKEL | 1 | [2.19, 3.11, 1.25] |
| MP_IAC_Seed | PROP_NEU_Pickups_p.AmmoPickup.PROP_NEU_AmmoPickup_STAT | 9 | [0.61, 0.88, 1.28] |
| MP_IAC_Seed | WEP_CodeOfPower_p.WEP_CodeOfPower2H_SKEL | 2 | [1.75, 4.55, 0.68] |
| MP_IAC_Seed | PROP_NEU_Pickups_p.MPBomb.NEU_MPBomb_SKEL | 1 | [2.19, 3.11, 1.25] |
| MP_IAC_Streets | PROP_NEU_Pickups_p.AmmoPickup.PROP_NEU_AmmoPickup_STAT | 14 | [0.61, 0.88, 1.28] |
| MP_IAC_Streets | WEP_CodeOfPower_p.WEP_CodeOfPower2H_SKEL | 2 | [1.75, 4.55, 0.68] |
| MP_IAC_Streets | PROP_NEU_Pickups_p.MPBomb.NEU_MPBomb_SKEL | 1 | [2.19, 3.11, 1.25] |
| MP_KON_Molten | PROP_NEU_Pickups_p.AmmoPickup.PROP_NEU_AmmoPickup_STAT | 16 | [0.61, 0.88, 1.28] |
| MP_KON_Molten | WEP_CodeOfPower_p.WEP_CodeOfPower2H_SKEL | 2 | [1.75, 4.55, 0.68] |
| MP_KON_Molten | PROP_NEU_Pickups_p.MPBomb.NEU_MPBomb_SKEL | 1 | [2.19, 3.11, 1.25] |
| MP_ORB_Debris | PROP_NEU_Pickups_p.AmmoPickup.PROP_NEU_AmmoPickup_STAT | 5 | [0.61, 0.88, 1.28] |
| MP_ORB_Debris | WEP_CodeOfPower_p.WEP_CodeOfPower2H_SKEL | 2 | [1.75, 4.55, 0.68] |
| MP_ORB_Debris | PROP_NEU_Pickups_p.MPBomb.NEU_MPBomb_SKEL | 1 | [2.19, 3.11, 1.25] |
| MP_UND_Complex | PROP_NEU_Pickups_p.AmmoPickup.PROP_NEU_AmmoPickup_STAT | 10 | [0.61, 0.88, 1.28] |
| MP_UND_Complex | WEP_CodeOfPower_p.WEP_CodeOfPower2H_SKEL | 2 | [1.75, 4.55, 0.68] |
| MP_UND_Complex | PROP_NEU_Pickups_p.MPBomb.NEU_MPBomb_SKEL | 1 | [2.19, 3.11, 1.25] |
| MP_UND_Gorge | PROP_NEU_Pickups_p.AmmoPickup.PROP_NEU_AmmoPickup_STAT | 7 | [0.61, 0.88, 1.28] |
| MP_UND_Gorge | WEP_CodeOfPower_p.WEP_CodeOfPower2H_SKEL | 2 | [1.75, 4.55, 0.68] |
| MP_UND_Gorge | PROP_NEU_Pickups_p.MPBomb.NEU_MPBomb_SKEL | 1 | [2.19, 3.11, 1.25] |

All visible at once where the camera sees them (static placement, 10-30 per map); small (0.5-1 m), so shadows are cheap and read on the floor under each pickup.

## Effect meshes

- content/FX_GrenadeLauncher_p/GrenadeAmmo_STAT.gltf: shell casings (Shell_AssaultRifle_FX: 1 per shot, 1 s), size [0.28, 0.2, 0.2] m. Too small and short-lived to matter; skip.
- content/FX_IonBlaster_p/IonBlaster_Mag_STAT.gltf: dropped magazines (Magazine_IonBlaster_FX: 1 per reload, 3 s), size [0.38, 0.13, 0.09] m. Too small and short-lived to matter; skip.

## Not drawn by the rebuild (no caster until drawn)

- Dropped weapons (HmDroppedPickup: a dead pawn's weapon on the floor): not presented; Gameplay item.
- Kamikaze mines (minepooper): simulated, never drawn; reported to Gameplay.

Suggested order for X5: sentries > pickups > projectile bodies (rockets, grenades) > ammo crate / roller; never the barrier.
