# Systems M08d handoff: vehicle / weapon / projectile / beam audio by identity (agents/systems)

**For Integration:** merge agents/systems, then apply `SYSTEMS_M08D_integration_glue.patch` (`patch -p1 --ignore-whitespace` or `git apply --ignore-whitespace`; the target files are CRLF). It contains the World.cpp, World.h and PlayerController.h seam changes against integration/milestone-08c. All three have been built and run in an 08c snapshot (git archive into Systems' work/).

## What the human heard, and why

| Report | Cause (verified in the 08c build) | Fix |
|---|---|---|
| Car, jet and tank vehicle forms silent | `tickVehicleBoost` gated **everything**, audio included, on `optimusFx` (chassis Truck / Truck7). The gate was meant for the Optimus-only vehicle FX. | The FX keep that gate. Vehicle audio runs for every chassis through `World::tickVehicleAudio(VehicleFormSignals)`, filled from Gameplay's VehicleState and the chassis VehicleFormType. |
| Vehicle firing wrong | The fire sound and impacts followed the *robot* weapon (`weaponClass_`); vehicle shots played e.g. the Heavy Pistol. | Sound by the weapon actually fired (`w.def->id`): fire, impact and victim hit sounds. Vehicle weapons are emitted at the vehicle. |
| Projectile weapons silent / wrong | `WP_Fire` only played on the instant-hit path. Projectiles had no flight or explosion sound, and the vehicle weapons' cues were never loaded. | Fire sound on launch for every fire type; the projectile's FlightSound from spawn; ExplosionSound on Explode. The loadout's weapon cues (robot + vehicle) are loaded per level. |
| Repair Ray incomplete | A beam weapon treated as a plain hitscan gun: the looping `WP_Fire` (heal loop) was started as a one-shot on every trace. | The TnWeaponBeam / TnWeaponRepair rules: `WP_Looping` while tracing, a heal loop on a teammate or a damage loop on an enemy, `WP_LoopingTail` on release. Driven by Gameplay's actual traces. |

## Systems pieces (agents/systems)

* **`VehicleFormAudio`** (header): Gameplay vehicle state → the form class's HmVehicleAudioComponent calls, per form. Source: decompiled TnCarForm / TnTruckForm / TnTankForm / TnPlaneForm.
  * Car / Truck: hover, Driving, dash, roll, nitro, jump.
  * Tank: boost, jump, 180.
  * Jet: hover boosters loop start / stop, ascend / descend held, roll, Flying = boost.
  * It also produces **`VehicleFormEvents`** for Rendering (see the last section).
  * Optimus: same voice events as M08c, 99 / 99 on the A/B probe.
* **`WeaponAudio`**: fire by identity; the beam loop state machine; projectile flight / explosion; `stopAll`.
* **`VehicleAudio::stopAll`**:
  * runs on class change, match restart and map unload;
  * detach also stops the booster loop: nothing the form owned outlives it.
* **CharacterAudio data**:
  * per weapon: projectile (16 weapons; FlightSound / ExplosionSound / FlightEffect / ExplosionEffect), beam flag (Repair Rays) and LoopingFade times;
  * per chassis: `vehicleForm`.
* **World hooks** (Systems-owned; Gameplay reports, Systems plays):
  * `tickVehicleAudio`;
  * `setPlayerVehicleWeaponAudio`, `preloadWeaponAudio`;
  * `onWeaponFired`;
  * `onProjectileSpawned` / `Moved` / `Exploded` / `Removed`;
  * `onBeamWeapon`;
  * `vehicleEvents()`.
* **`WFC_AUDIOCHECK`**: an ownership audit; it logs `LEAK` if vehicle loops exist outside vehicle form.

## Validation (08c snapshot + this branch)

* **Vehicle-form runs, before vs after:**
  * Car / Jet / Tank: 0 vehicle cues before; now each plays its own authored set.
  * Truck: unchanged.
* **Matrix:** the default class bodies, Car2 / Car4 / Jet4 / Jet / Truck3 / Truck4 / Tank3 / Tank2, × Streets / Gorge / Molten, with transform every 2.5 s, boost cycles, special moves and autofire.
  * 0 missing cues and 0 ownership leaks.
  * Each body plays its own vehicle set: Sideswipe, Barricade, Starscream, Soundwave, Megatron.
* **Weapons:**
  * Car / Jet fire `NEUTRON_RIFLE.VEH_SHOOT` with its own impacts.
  * Truck: `ROCKET.SHOOT` + `TRAIL_LP` + `VEH_EXPL_IMPT_WORLD`.
  * Tank: `TANK_CANNON` shoot, trail and explosion.
* **Match-restart cycle in vehicle form:** 0 leaks.
* **Cold boot** (`WFC_FRONTEND_AUTOPLAY=TDM,508`):
  * the four intro movies play their audio, followed by the party / lobby music;
  * TDM start line SC002657 + description;
  * **localized waves: 282 from the `_LOC/int` twin, 0 not played** (English).
* **Systems suite:** 617 / 0, including fire identity, rocket flight / explosion / removal, the Repair Ray heal / damage / tail without duplicates, and `stopAll`.
* **Regressions:**
  * real-device movie + Extras probe OK;
  * 40-cycle lifecycle 0 FAIL;
  * wfc_fidelity 194 / 0 / 19.

## Gameplay requests (state Systems needs, not duplicated in Systems)

* **Tank 180 quick turn:** not implemented in movement (`CharacterMovement` notes PARTIAL). When it is, set `VehicleFormSignals::special180` on its start. The sound (`Auto_180_Turn`) is already wired.
* **Hover thruster contribution** (`set_BoosterAmount` = the largest thruster linear / angular contribution, RE pass 4):
  * set `VehicleFormSignals::thrusterAmount` when the hover sim exposes it;
  * until then the BoosterParameter keeps its default.
* **Car Driving SlipAngle:** `World::setTireSlipAngle` (existing) feeds the squeal parameter.
* **The Repair Ray** must be equipped and firing as its beam traces. The glue derives the beam from those traces: target by team; stop after 1.5 × FireInterval without a trace. A dedicated beam state (`onBeamWeapon(cls, firing, target)` per tick) can replace that derivation.
* **`PlayerController::moveIntent()`:** a read-only accessor added by the glue patch, for the jet Hover Up / Down inputs.

## Event outputs for Rendering (vehicle propulsion / projectile FX)

**`World::vehicleEvents()`** (per step, authored per-form semantics):

| Field | Meaning |
|---|---|
| `enter` / `exit` | form OnBeginPlay / OnEndPlay |
| `hoverOn` / `hoverOff`, `hovering` | HoverFX |
| `boostOn` / `boostOff`, `boosting` | BoostFx: Driving / Flying / tank boost |
| `jump` | JumpFX |
| `roll`, `dash`, `nitro`, `special180` | the special moves |
| `ascendOn` / `Off`, `descendOn` / `Off` | jet hover up / down |

**Projectiles:**
* `CharacterAudio::weaponProjectile(cls)` gives the authored FlightEffect / ExplosionEffect templates.
* The projectile hooks give spawn, move, explode and remove per projectile key.

## Not in this pass

* The frontend `prefetchLevel` hitch (37–94 ms on the first loading frame; Frontend report): queued.
* RE pass 5 extras: the 10 s countdown ticks (GRI.LowCountdownTickSound) and the grenade bounce / fuse sounds. Gameplay needs to expose those events first.
* Truck nitro: the decompiled TnTruckForm.Driving.StartNitro calls **PlayNitroSound** (kept). RE pass 5 lists PlayRamSound; that is the ram *hit* (ClientPlayRammingSound).

## M08e: per-chassis vehicle FX (VehicleFxDriver → Rendering's particle runtime)

**What it does:**
- Every chassis plays its own authored VEHDEF effects: HoverFX, BoostFx, JumpFX and RamFX (31 / 33 chassis; the minions have none).
- Effects are attached to their vehicle sockets, with the same socket conversion as ChassisDef.
- The form classes decide when each set plays [CONF decompiled TnCarForm / TnTruckForm / TnTankForm / TnPlaneForm / TnVehicleFxPlayer / HoverPhysics]:

| Rule | Behaviour |
|---|---|
| `Color` | EnergonColor at Play |
| car / truck Driving | `Color` = lerp(Energon, Yellow, NormalizedJumpTimeRemaining), alpha 100–255 |
| HoverFX `Size` | per socket = min(1, linear + angular thruster contribution) × RelativeScale; smoothing 0.1 for car / truck, 0.3 for the plane; limits 3000 / 6 |
| tank | HoverFX always (no Size) |
| JumpFX | on a jump and on the car's Driving roll; stopped at OnEndPlay |
| RamFX | during the truck's nitro |
| `FxAllowed` | from the transform clips' ToggleVehicleFx notify (per-chassis enable fraction) |

- The largest hover `Size` is the audio `BoosterAmount`, so the hover-booster sound now gets its parameter.

**Integration:**
- After merging agents/rendering (spawnParticleEffect / setParticleEffectTransform / setParticleEffectParam / stopParticleEffect), bind it in `World::load`. The exact call is in the comment there (the glue patch includes it).
- While unbound, the hand-made Optimus effects stay as the fallback. Once bound, they are off, so nothing is drawn twice.
- `WFC_VFX_FAKE=1` binds a recording stand-in that logs spawns, params and stops.

**Rendering confirmation:** the calls match the current API exactly (no changes since e15862f / 3f44104). Integration must also:
- merge Rendering's additive f875640 (Trail2 tessellation, which helps the Trails_Jet_A wing-tip trails; Beam2 taper) and aa1fb2a (class-data templates);
- regenerate the render data with **build_map_fx**;
- note that `Size` only scales templates whose SizeMultiplyLife module is parameter-driven, as authored.

**Validated in the 08c snapshot with `WFC_VFX_FAKE`:**
- Car2 / Jet4 / Truck3 / Tank3, cycling robot → vehicle → boost → jump → robot.
- Each class spawns its own templates at its sockets, and spawns == stops (25/25, 39/39, 23/23, 23/23).
- Hover `Size` is live (anisotropic where the socket scale is); `Color` = team energon.
- No on-screen check yet: that needs Rendering's runtime in the build.

**Approximations:**
- The vehicle rigid-body gravity is taken as the pawn's kGravity [HIGH].
- The body frame is the vehicle mesh matrix (yaw + rigid-body pitch / roll).
- Cloaking is not wired: Gameplay has no cloak state yet.

## M08f: countdown / objective / grenade audio wiring (in the glue patch)

The glue patch now also wires these (all on Gameplay's existing events):
* `MatchEvent::CountdownTick` → `matchAudio().countdownChanged`;
* `MatchStarted` → `kothMatchStarting`;
* MapState `sc.messages` → `matchAudio().objectiveBroadcast`;
* the KOTH active zone / defender → `kothZoneActivated` / `kothDefenderChanged`;
* the planted bomb's fuse → `objectiveCountdownChanged`;
* grenade impacts → `onProjectileHitWall` (fuse on the first impact);
* non-grenade world hits → `onProjectileHitWall`.

`prefetchLevel` no longer blocks: Frontend's hitch report is fixed on the Systems side, with no Frontend change.

## M08g: profile volume sliders (in the glue patch)

`src/core/Application_Frontend.cpp`: the profile's Music / FX / Dialogue Volume go to `game::LevelAudioHost::applyProfileVolumes` at boot
and in `profile().onApplied` (replaces the "pending: Systems volumes" note). Static, device-global: no runtime object needed.
Note for playtests: at the default profile (80) all game audio is now 0.8 (about -1.9 dB) relative to before - this matches the original.

## M08h: vehicle muzzle flash at the alternating socket

Separate patch against integration/milestone-08g: `docs/handoff/SYSTEMS_M08H_vehicle_muzzle_glue.patch` (World.cpp / World.h,
`git apply --ignore-whitespace` or `patch -p1 --ignore-whitespace`). Uses Gameplay's noteVehicleShot state; no Gameplay or
Rendering change. The unused beamSinceShot_ / beamInterval_ / beamClassFiring_ members Integration noted can be deleted freely.

## M08i: ability / buff sounds

Merge agents/systems first (AbilityAudio, CharacterAudio data, World hooks). Then apply
`docs/handoff/SYSTEMS_M08I_ability_audio_glue.patch` (`patch -p1 --ignore-whitespace`).

The patch touches four Gameplay-side files:
* PlayerController.h / .cpp: read-only audio pulses (abilityTriggerCount + lastTriggeredAbility, abilitiesJammedCount, transformFailedCount). They're counted where Gameplay already accepts or refuses; there is no behaviour change.
* World.cpp / .h: `tickAbilityAudio()` after tickAbilityEffects, drain per-victim / per-tick calls, and the PlayerKilled kill confirm.

It was dry-run against integration/milestone-08h a1388fa after applying the branch's World diff: 0 failed hunks. The snapshot test used a test-only WFC_AUTOABILITY1, which is not in the patch.

## M08k: Plasma Cannon charge, roller mine, dodge (glue)

`docs/handoff/SYSTEMS_M08K_charge_roller_dodge_glue.patch` (World.cpp / World.h). Order:
1. Gameplay 24l.
2. agents/systems.
3. The M08i glue.
4. This patch.

**Conflict note:** merging agents/gameplay (c804fe0) into 08h conflicts once, in the weaponFireHook projectile branch. Keep Gameplay's `spawnProjectile(o, ...)` line *and* the 08h Systems blocks (vehicle muzzle flash + onWeaponFired). This patch then adds the fire-mode argument to that onWeaponFired call.
