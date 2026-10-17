# Weapon firing presentation audit: authored per MP weapon vs our runtime (grep)

WFC RE lane, 2026-10-18.

Sources:
- authored.db: the TnWeapon CDO → `WeaponMeshTemplate` (TnWeaponMesh) / `PlayerData` (TnWeaponData), weapon AnimSet notifies,
  PhysicalMaterial / TnPhysicalMaterialProperty;
- script: TnWeaponMesh / HmWeaponMesh;
- the 34 MP weapons from TransWeapons.ini `TnDataProvider_Weapon`.

Generator, read-only: `tools/weapon_presentation_audit.py`, writing `notes/data/weapon_presentation_audit.json` (full detail,
including all notifies and owner anims) and `.md` (the table below).

**Grep classes** (read-only, against `Rebuild/src`, `Rebuild/data`, `Rebuild/work/render/**/*.json`; texture_formats.json is
ignored):
- **C** = named in our src code (excluding the embedded JSON);
- **E** = only in the asset JSON embedded in `src/game/CharacterAudio.inc`;
- **R** = in work/render or data JSON;
- **-** = no hit.

C means the name is referenced, not that it is rendered faithfully.

**E is not "unused" (Systems, 2026-10-18):** our audio code (AbilityAudio / CharacterAudio) plays cues looked up by class and field in the embedded JSON tables (`classSound` / buff sounds), so an E-only cue can still be played (e.g. the cloak loops via TnBuffCloak). Treat **-** (no hit) as the real gap list; E items need a runtime check (Systems' CUELOG).

## 1. How the original picks impacts per physical material (CONFIRMED script; the rule our runtime needs)

`TnWeaponMesh` overrides; first hit wins:

| | 1st | 2nd | 3rd |
|---|---|---|---|
| **Squib (particle)** | `TnPhysicalMaterialProperty.GetWeaponSpecificEffect(WeaponEffectsType)` | `PhysicalMaterial.ImpactEffect` | weapon `DefaultSquib` |
| **Decal** | `GetWeaponSpecificDecal(WeaponEffectsType)` (if the Material is set) | — | weapon `DefaultDecal` |
| **Sound** | `GetWeaponSpecificImpactSound(WeaponEffectsType)` | `PhysicalMaterial.ImpactSound` | weapon `DefaultImpactSound` |

**Data reality:**
- **No physmat authors a weapon-specific squib or sound.** The TnPhysicalMaterialProperty exports carry only
  `WeaponTypeSpecificDecals` (by TnWeaponEffectsType Bullet / Energy / Explosive), NoDecal, footsteps and random decal rotation.
- So squibs are the **weapon DefaultSquib**, except on surfaces whose PhysicalMaterial authors `ImpactEffect`:
  - force fields: ForceField_Squib(_Blue)_FX;
  - coloured water / coolant: Liquid_Impact_*_FX;
  - Omega / Trypticon / Corruptor / A4 minibosses: Squib_Trypticon_Bullet_FX, Impact_AssaultRifle_Vehicle_FX,
    A4_Miniboss_HitSquib_FX, Corruptor_Impact_*;
  - SlugRide: Martyrdom_Grenade_FX / Impact_NullRay_FX;
  - Masher: Squib_Transformer_FX;
  - and a few more.
- The same surfaces carry impact sounds: ForceField `BL_SHIELD.FORCE_FIELD_IMPT`, destructible metal `DES_MTL_IMPT`, the D1
  wall / bridge events.

**Squib culling** (HmWeaponMesh CDO, overridden per weapon in the table): ImpactSquibPercentage 1.0, ImpactSquibMaxCount 10,
MaxImpactEffectDistance 400 (TnWeaponMesh CDO 2500).

**Our side:**
- The physmat ImpactEffect squibs appear only in `work/render/*/map_fx_runtime.json` [R]. `FORCE_FIELD_IMPT` has no hit.
- Per-effects-type decals are handled (`GLRenderer.cpp` + `impact_decals.json` [C/R]).
- `World.cpp` comments name the sound chain [C]. Whether the physmat squib override is applied at runtime is **unknown**.

## 2. Value columns: what our runtime does with them

- **Spine / hand recoil:** `Character.h` initialises `recoilSpine_` / `recoilHand_` from **`ionBlasterSpineRecoil()` /
  `ionBlasterRightHandRecoil()` for every weapon** (Recoil.h). The authored per-weapon values below are **not used**: C, but
  IonBlaster's only.
- **Camera recoil (RecoilCameraParams: pitch kick, FOV kick, time, delay) and per-shot camera shake (CameraShakeParams):**
  present in `data/gameplay/weapons_mp.json` [R], but **no src reader found**. The only src camera shake is the gibbed shake.
  Treat as **absent** at runtime (H).
- **Muzzle light:** not cross-checked per weapon (unknown). Light values are in the json.
- **Reload / equip / drop / fire anim names:** in `WeaponTable.inc` [C]. Reload-anim **notifies** (reload FX at MuzzleFlash,
  magazine drops, sounds) are coded only for the IonBlaster / AssaultRifle shell and magazine set (`WeaponFx.cpp`
  spawnNotifyEffect); the rest are R or -.
- **Shell ejection** is a weapon AnimNotify (`HmAnimNotify_PlayEffect` on the Fire anim at `ShellEject`, or on the Reload anim at
  Magazine / Clip / Ammo sockets), not a mesh property. Ours codes Shell_AssaultRifle_FX and Magazine_IonBlaster_FX only.

## 3. Table (MP weapons; per-asset grep class in brackets)

Notes:
- "(CDO)" in FX type = not authored on the mesh (class default).
- Squib % / max: per weapon, otherwise the CDO values 100 % / 10.
- **Correction (2026-10-18):** the value columns use the MP data (`MultiplayerData`); the first version used campaign `PlayerData`. They differ for: HeavyPistol camera pitch 1.5 (SP 2.0); Bazooka pitch 1.5 (SP 0.75); RocketVehicle RecoilTime 0.35 (SP 0.30); HomingRocketVehicle reload 2.5 (SP 2.0); PlaneMachineGun reload 2.0 (SP 2.5).
- Recoil rotator amplitudes are in UE units (65536 = 360°).
- Melee weapons (sword … hammer) author only the equip glow FX in MuzzleFlashes; their hit presentation is the melee system's.

| Weapon | FX type | Muzzle flash (light) | Tracer / projectile | Impact squib / decal / sound | Shell eject | Reload / overheat (anim: notifies) | Spine / hand recoil (dur, rot amp) | Camera recoil / shake |
|---|---|---|---|---|---|---|---|---|
| PlasmaCannon (TnWeaponPlasmaCannon) | (CDO) | PowerUp_PlasmaCannon_FX [CER], MuzzleFlash_PlasmaCannon_FX [R] (light -/3000.0) | proj TnProjectilePlasmaCannonCharge1, TnProjectilePlasmaCannonCharge2, TnProjectilePlasmaCannonCharge3 [CER] | none / MetalDecal_02_MAT_INST [R] / EXPL_IMPT_WORLD (100%, max 10) | none | PlasmaCannon_Reload: PlayEffect:Reload_PlasmaCannon_FX@MuzzleFlash; Sound:ANIM_RELOAD [anim CE] | 1.0 (-4000,3000,0) / 1.0 (8000,0,0) | pitch 0 fov -1.0 t 0.65 d 0.15 / I 0.8 roll 0.8 fall 0.4 |
| SniperRifle (TnWeaponSniperRifle) | (CDO) | MuzzleFlash_Sniper_FX [CER] (light 10.0/3000.0) | Tracer_SniperRifle_FX [CER] | Impact_AssaultRifle_FX [CER] / none / IMPT_WORLD (100%, max 10) | Shell_AssaultRifle_FX [CR] @ShellEject | SniperRifle_Reload: PlayEffect:Reload_AssaultRifle_FX@MuzzleFlash; Sound:ANIM_RELOAD [anim CE] | 0.6 (2000,6000,-1000) / 0.8 (3000,-6000,-0) | pitch 1.0 fov 0.5 t 0.7 d 0.05 / I 0.1 roll 0.3 fall 0.2 |
| AssaultRifle (TnWeaponAssaultRifle) | (CDO) | MuzzleFlash_AssaultRifle_FX [CER] (light 10.0/3000.0) | Tracer_AssaultRifle_FX [CER] | Impact_AssaultRifle_FX [CER] / MetalDecal_02_MAT_INST [R] / IMPT_WORLD (60%, max 5) | Shell_AssaultRifle_FX [CR] @ShellEject | AssaultRifle_Reload: PlayEffect:None@ClipSocket; PlayEffect:Reload_AssaultRifle_FX@MuzzleFlash; Sound:ANIM_RELOAD / AssaultRifle_Overheat: Sound:ANIM_RELOAD [anim CE] | 0.8 (500,1000,0) / 0.5 (2000,500,-2000) | pitch 0.05 fov 0 t 0.05 d 0.25 / I 0.17 roll 0.15 fall 0.06 |
| IonBlaster (TnWeaponIonBlaster) | (CDO) | MuzzleFlash_AssaultRifle_FX [CER] (light 10.0/3000.0) | Tracer_AssaultRifle_FX [CER] | Impact_IonBlaster_FX [CER] / MetalDecal_02_MAT_INST [R] / IMPT_WORLD (60%, max 5) | Magazine_IonBlaster_FX [CR], Shell_AssaultRifle_FX [CR] @MagSocket | Shooting_Reload_IonBlaster_AP: PlayEffect:Reload_AssaultRifle_FX@MuzzleFlash; PlayEffect:Magazine_IonBlaster_FX@MagSocket; Sound:ANIM_RELOAD_02; Sound:ANIM_RELOAD_01 [anim CE] | 0.8 (500,1000,0) / 0.5 (2000,500,-2000) | pitch 0.1 fov 0.085 t 0.05 d 0.25 / I 0.17 roll 0.15 fall 0.06 |
| HeavyPistol (TnWeaponHeavyPistol) | (CDO) | MuzzleFlash_AssaultRifle_FX [CER] (light 10.0/3000.0) | Tracer_AssaultRifle_FX [CER] | Squib_HeavyPistol_FX [CER] / none / IMPT_WORLD (100%, max 10) | Shell_HeavyPistol_FX [R] @ShellEject | heavypistol_reload: Sound:ANIM_RELOAD [anim CE] | 0.8 (1000,2000,1800) / 0.6 (1200,-1000,0) | pitch 1.5 fov -1.0 t 0.7 d 0.05 / I 0.6 roll 0.3 fall 0.2 |
| BurstRifle (TnWeaponBurstRifle) | (CDO) | MuzzleFlash_BurstRifle_FX [CER] (light 10.0/3000.0) | Tracer_AssaultRifle_FX [CER] | Impact_AssaultRifle_FX [CER] / DecalDefault_MAT [R] / IMPT_WORLD (100%, max 10) | Shell_BurstRifle_FX [R] @ShellEject | BurstRifle_Reload: PlayEffect:Reload_BurstRifle_FX@MuzzleFlash; PlayEffect:None@ClipSocket; Sound:ANIM_RELOAD / BurstRifle_Overheat: Sound:ANIM_RELOAD [anim CE] | 0.5 (1500,-1000,0) / 0.5 (1000,100,-3000) | pitch 0 fov 0 t 0.05 d 0.25 / I 0.15 roll 0.15 fall 0.15 |
| HeavyMG (TnWeaponHeavyMG) | (CDO) | MuzzleFlash_HeavyMG_FX [CER] (light 10.0/3000.0) | Tracer_AssaultRifle_FX [CER] | Impact_HeavyMG_FX [CER] / MetalDecal_02_MAT_INST [R] / IMPT_WORLD (60%, max 5) | Magazine_HeavyMG_FX [R], Shell_ElectronPulseRifle_FX [R] @MagazineSocket | HeavyMG_Reload: PlayEffect:Magazine_HeavyMG_FX@MagazineSocket; PlayEffect:Reload_HeavyMG_FX@MuzzleFlash_1; Sound:ANIM_RELOAD [anim CE] | 0.6 (1800,3000,500) / 0.4 (-1000,500,-250) | pitch 0.2 fov 0 t 0.05 d 0.25 / I 0.15 roll 0.1 fall 0.1 |
| GrenadeLauncher (TnWeaponGrenadeLauncher) | (CDO) | MuzzleFlash_GrenadeLauncher_FX [CER] (light 10.0/3000.0) | proj TnProjectileGrenadeLauncher, TnProjectileGrenadeLauncher [ER] | none / none / EXPL_IMPT_WORLD (100%, max 10) | Ammo_GrenadeLauncher_FX [R] @AmmoShell | GrenadeLauncher_Reload: PlayEffect:Reload_GrenadeLauncher_FX@MuzzleFlash; Sound:ANIM_RELOAD [anim CE] | 0.6 (5000,150,0) / 0.8 (-10000,-3000,0) | pitch 0.5 fov -1.0 t 0.6 d 0.25 / I 0.5 roll 0.3 fall 0.3 |
| Bazooka (TnWeaponBazooka) | (CDO) | Muzzleflash_FusionCannon_FX [CER] (light 5.0/800.0) | proj TnProjectileBazooka, TnProjectileBazooka [ER] | none / none / EXPL_IMPT_WORLD (100%, max 10) | none | Bazooka_CoolDown: - [anim CE] | 1.0 (6000,6000,-3000) / 0.8 (-2000,150,0) | pitch 1.5 fov -2.0 t 0.6 d 0.25 / I 0.45 roll 0.45 fall 0.4 |
| RepairRay (TnWeaponRepairRay) | (CDO) | none (light 5.0/700.0) | Tracer_RepairBeam_FX [CR] | Squib_RepairEnemy_FX [CER] / none / IMPT_WORLD (100%, max 10) | none | RepairBeam_Reload: Sound:ANIM_RELOAD / RepairBeam_Reload: Sound:ANIM_RELOAD [anim CE] | 1.0 (500,-150,0) / 0.0 (0,0,0) | pitch 0 fov 0.4 t 1.0 d 0.05 / I 0.025 roll 0.5 fall 0.1 |
| EMPShotgun (TnWeaponEmpShotgun) | TnWeaponEffectsTypeExplosive | MuzzleFlash_EMPShotgun_FX [CER] (light 10.0/3000.0) | Trail_EMPShotgun_FX [CER] | Squib_EMPShotgun_FX [CER] / none / IMPT_WORLD (100%, max 13) | Magazine_EMPShotgun_FX [R], Shell_EMPShotgun_FX [R] @MagazineSocket | Shotgun_Reload: PlayEffect:Magazine_EMPShotgun_FX@MagazineSocket; PlayEffect:Reload_EMPShotgun_FX@MuzzleFlash; Sound:ANIM_RELOAD [anim CE] | 0.8 (1500,2100,2000) / 0.6 (-4000,-4000,2000) | pitch 4.0 fov -1.0 t 0.4 d 0.25 / I 0.2 roll 0.5 fall 0.5 |
| Shotgun (TnWeaponShotgun) | TnWeaponEffectsTypeExplosive | MuzzleFlash_EMPShotgun_FX [CER] (light 10.0/3000.0) | Trail_EMPShotgun_FX [CER] | Squib_EMPShotgun_FX [CER] / none / IMPT_WORLD (100%, max 10) | Magazine_EMPShotgun_FX [R], Magazine_Shotgun_FX [R], Shell_EMPShotgun_FX [R] @MagazineSocket | Shooting_Reload_Shotgun: PlayEffect:Reload_EMPShotgun_FX@MuzzleFlash; PlayEffect:Magazine_Shotgun_FX@MagazineSocket; Sound:ANIM_RELOAD [anim CE] | 0.8 (1500,2100,2000) / 0.6 (-4000,-4000,2000) | pitch 2.0 fov -1.0 t 0.4 d 0.25 / I 0.2 roll 0.5 fall 0.5 |
| HomingRocket (TnWeaponRocketLauncher) | TnWeaponEffectsTypeExplosive | MuzzleFlash_HomRocket_FX [CER] (light 5.0/700.0) | none | none / none / EXPL_IMPT_WORLD (100%, max 10) | none | RocketLauncher_Reload: Sound:ANIM_RELOAD [anim CE] | 1.0 (-4000,3000,0) / 0.7 (8000,0,0) | pitch 0.25 fov 1.0 t 0.6 d 0.25 / I 0.6 roll 0.4 fall 0.5 |
| RocketVehicle (TnWeaponRocketVehicle) | (CDO) | MuzzleFlash_Blaster_FX [CER] (light -/-) | none | none / none / EXPL_IMPT_WORLD (100%, max 10) | none | none | -  / -  | pitch 0 fov 0 t 0.35 d 0.05 / I 0.2 roll 0.2 fall 0.3 |
| AssaultRifleVehicle (TnWeaponAssaultRifleVehicle) | (CDO) | MuzzleFlash_AssaultRifle_FX [CER] (light -/-) | Tracer_AssaultRifle_FX [CER] | Impact_AssaultRifle_FX [CER] / DecalDefault_MAT [R] / IMPT_WORLD (60%, max 5) | none | none | -  / -  | pitch 0 fov 0 t 0.05 d 0.25 / I 0.1 roll 0.1 fall 0.1 |
| TankCannon (TnWeaponTankCannon) | (CDO) | D_MuzzleFlash_TankCannon_FX [CER] (light 50.0/2000.0) | Trail_TankCannon_FX [CER] | none / none / EXPL_IMPT_WORLD (100%, max 10) | none | none | -  / -  | pitch 0 fov -4.0 t 0.8 d 0.2 / I 0.38 roll 0.65 fall 0.79 |
| HomingRocketVehicle (TnWeaponHomingRocketVehicle) | (CDO) | MuzzleFlash_HomRocket_FX [CER] | proj TnProjectileHomingRocketVehicle, None [ER] | none / none / EXPL_IMPT_WORLD (100%, max 10) | none | none | -  / -  | pitch 0 fov 1.0 t 0.6 d 0.25 / I 0.3 roll 0.2 fall 0.2 |
| PlaneAssaultRifle (TnWeaponAssaultRiflePlane) | (CDO) | MuzzleFlash_AssaultRifle_FX [CER] (light -/-) | Tracer_AssaultRifle_FX [CER] | Impact_AssaultRifle_FX [CER] / DecalDefault_MAT [R] / IMPT_WORLD (60%, max 5) | none | none | -  / -  | pitch 0 fov 0 t 0.05 d 0.25 / I 0.1 roll 0.1 fall 0.1 |
| PlaneMachineGun (TnWeaponPlaneMachineGun) | (CDO) | MuzzleFlash_AssaultRifle_FX [CER] (light -/-) | Tracer_AssaultRifle_FX [CER] | Impact_AssaultRifle_FX [CER] / DecalDefault_MAT [R] / IMPT_WORLD (60%, max 6) | none | none | -  / -  | pitch 0 fov 0 t 0.05 d 0.25 / I 0.1 roll 0.1 fall 0.1 |
| PlaneRocket (TnWeaponRocketPlane) | (CDO) | MuzzleFlash_Blaster_FX [CER] (light -/-) | none | none / none / EXPL_IMPT_WORLD (100%, max 10) | none | none | -  / -  | pitch 0 fov 0 t 0.35 d 0.05 / I 0.15 roll 0.2 fall 0.25 |
| FlakGrenades (TnWeaponFlakGrenades) | (CDO) | none (light -/700.0) | proj TnProjectileGrenade, None [CER] | none / none / MAGMA_EXPL_IMPT_WORLD (100%, max 10) | none | none | -  / -  | pitch 0 fov 0 t - d - / I - roll - fall - |
| KamikazeMines (TnWeaponKamikazeMines) | (CDO) | none (light -/700.0) | proj TnProjectileKamikazeMine, None [CER] | none / none / KAMIKAZE_EXPL_IMPT_WORLD (100%, max 10) | none | none | -  / -  | pitch 0 fov 0 t - d - / I - roll - fall - |
| HealGrenades (TnWeaponHealGrenades) | (CDO) | none (light -/700.0) | proj TnProjectileHealGrenade, None [ER] | none / none / HEAL_EXPL_IMPT_WORLD_PROTRACT (100%, max 10) | none | none | -  / -  | pitch 0 fov 0 t - d - / I - roll - fall - |
| FlashBangs (TnWeaponFlashBangs) | (CDO) | none (light -/700.0) | proj TnProjectileFlashBang, None [ER] | none / none / EMP_EXPL_IMPT_WORLD (100%, max 10) | none | none | -  / -  | pitch 0 fov 0 t - d - / I - roll - fall - |
| ShortSword (TnWeaponShortSword) | (CDO) | SwordEquip_Red_FX [CR] (light -/-) | none | none / none / none (100%, max 10) | none | none | -  / -  | pitch 0 fov 0 t - d - / I - roll - fall - |
| BeatDownClub (TnWeaponBeatDownClub) | (CDO) | SwordEquip_Red_FX [CR] (light -/-) | none | none / none / none (100%, max 10) | none | none | -  / -  | pitch 0 fov 0 t - d - / I - roll - fall - |
| ButterflyAxe (TnWeaponButterflyAxe) | (CDO) | SwordEquip_Red_FX [CR] (light -/-) | none | none / none / none (100%, max 10) | none | none | -  / -  | pitch 0 fov 0 t - d - / I - roll - fall - |
| SpikedBat (TnWeaponSpikedBat) | (CDO) | MeleeEquip_Purple_FX [CR] (light -/-) | none | none / none / none (100%, max 10) | none | none | -  / -  | pitch 0 fov 0 t - d - / I - roll - fall - |
| SpikedMace (TnWeaponSpikedMace) | (CDO) | MeleeEquip_Purple_FX [CR] (light -/-) | none | none / none / none (100%, max 10) | none | none | -  / -  | pitch 0 fov 0 t - d - / I - roll - fall - |
| TortureSword (TnWeaponTortureSword) | (CDO) | MeleeEquip_Purple_FX [CR] (light -/-) | none | none / none / none (100%, max 10) | none | none | -  / -  | pitch 0 fov 0 t - d - / I - roll - fall - |
| ViciousAxe (TnWeaponViciousAxe) | (CDO) | MeleeEquip_Purple_FX [CR] (light -/-) | none | none / none / none (100%, max 10) | none | none | -  / -  | pitch 0 fov 0 t - d - / I - roll - fall - |
| Maul (TnWeaponMaul) | (CDO) | MeleeEquip_Purple_FX [CR] (light -/-) | none | none / none / none (100%, max 10) | none | none | -  / -  | pitch 0 fov 0 t - d - / I - roll - fall - |
| Axe (TnWeaponAxe) | (CDO) | SwordEquip_Red_FX [CR] (light -/-) | none | none / none / none (100%, max 10) | none | none | -  / -  | pitch 0 fov 0 t - d - / I - roll - fall - |
| Hammer (TnWeaponHammer) | (CDO) | MeleeEquip_Purple_FX [CR] (light -/-) | none | none / none / none (100%, max 10) | none | none | -  / -  | pitch 0 fov 0 t - d - / I - roll - fall - |

## 4. Gaps in priority order (where the eye lands)

1. **Camera recoil / FOV kick and per-shot camera shake:** authored for every gun; no runtime reader (§2).
2. **Per-weapon spine / hand recoil:** every weapon uses IonBlaster's values.
3. **Shell / magazine / reload notifies for all guns except AssaultRifle / IonBlaster:**
   - shell: Shell_HeavyPistol, Shell_BurstRifle, Shell_ElectronPulseRifle (HeavyMG), Shell_EMPShotgun, Ammo_GrenadeLauncher;
   - magazine: Magazine_HeavyMG / EMPShotgun / Shotgun;
   - reload: Reload_*_FX.
4. **Physmat ImpactEffect / ImpactSound overrides** (force fields, water, bosses): render data only; runtime use unknown.
5. **Assets with R only:** MuzzleFlash_PlasmaCannon_FX (the charged-shot release flash); everything marked [R] or [-] in the
   muzzle and tracer columns.
