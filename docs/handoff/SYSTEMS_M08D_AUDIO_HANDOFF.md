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

## M08l: action-layer sound notifies (glue)

`docs/handoff/SYSTEMS_M08L_action_layer_glue.patch`, applied after the M08k patch.
It adds two read-only Character accessors (actionClipIndex / actionTime) and two per-tick calls in tickAbilityAudio.

## M08m: guided missile, barrier, sentry (glue)

`docs/handoff/SYSTEMS_M08M_ability_actors_glue.patch`, applied after M08l. It adds 3 per-tick calls in tickAbilityAudio, the onGuidedMissileExploded call in detonateGuidedMissile, and onSentryShot in tickSentry's fire.

Default loudness is decided: keep 80 / 80 / 80 -> 0.8 (the original; about -1.9 dB vs pre-M08g). No change.

## M08n: kill-streak announcements, overshield off, dodge wall hit (glue)

`docs/handoff/SYSTEMS_M08N_killstreak_overshield_glue.patch`, applied after M08m (Character.h, CharacterMovement.cpp, World.h / .cpp).

## M08o: grenade and death sounds (glue against 08k)

`docs/handoff/SYSTEMS_M08O_grenade_death_glue.patch` (World.cpp / .h), made directly against integration/milestone-08k (e467663).
Merge agents/systems first; the hook definitions come with it.

## M08p: melee hit effects, kamikaze mines (glue)

`docs/handoff/SYSTEMS_M08P_melee_hit_mines_glue.patch`, applied after the M08o patch (against 08k).

## M09b: bot (non-local participant) weapon audio (glue recipe for the 09a merge of agents/gameplay 5151374)

Systems API (agents/systems): `World::preloadParticipantWeaponAudio(classes)`, `onParticipantFired(cls, from)`,
`onParticipantImpact(cls, at, victimPlayer)`. The weapon class string is `"TransContent.TnWeapon" + WeaponDef::id`.

Glue (Integration, after merging Gameplay's bots; untested until a tree has both):
1. Right after `launchMatch` / when the roster is final: for every `Match::players()[p]` with p != localPlayer_, collect
   `"TransContent.TnWeapon" + id` for each of `selection.weapons` (via findWeaponDef) plus the vehicle weapons of
   `resolveChassis(selection, match.faction(p))`, and call `preloadParticipantWeaponAudio(classes)` once. This decodes the
   bots' cues at load, not on a bot's first shot.
2. At the end of `World::tick` (participantShots_ is cleared at the start of the next tick), for this step's
   `participantShots()`:
   - once per distinct `player`: `onParticipantFired(cls(shot.weapon), shot.from)` (a shotgun's pellets share one fire sound);
   - for every shot with `impact`: `onParticipantImpact(cls(shot.weapon), shot.to, shot.hitPlayer)` (agents/gameplay 7ed5faf
     added `hitPlayer`: -1 = world / destructible -> DefaultImpactSound; a player -> the hit-effect sound, as on the local path).
3. Projectiles need nothing: bot rockets go through spawnProjectile, which already reaches onProjectileSpawned / Exploded.
Bots' muzzle / tracer FX are not drawn yet (Gameplay PARTIAL), so the fire sound plays at the shot origin (eye + aim).

## M09c: spawn-hitch fix - warm every spawnable selection's audio at match load

`World::preloadSelectionAudio(chassisKeys, weaponClasses)`: decodes (on a worker) the waves of each chassis' character
cue set (CharacterAudio::loadCues: voice / vehicle / foley / clip notifies / weapon events) and of each weapon class (weapon +
hit cues), tagged with the level. The spawn-frame `setPlayerCharacterAudio` / `preloadWeaponAudio` loads then find
the waves in the device cache. If the level's audio isn't loaded yet, it is applied when it loads. Unused warm samples are
released at the next level load (no decode on release).
Glue (Integration, from Gameplay's selectionPreloadHook / after launchMatch): for every spawnable selection (the 4 faction
presets, CaC slots, bot rosters): the chassis keys of `resolveChassis(selection, faction)` for each faction it can play, and
`"TransContent.TnWeapon" + id` of its robot and vehicle weapons. Call it once.
Measured (real device): spawn-frame loads cold 30-60 ms per selection -> 1.3-4.6 ms warmed; the warm call is 8 ms at load
(behind the loading screen); PCM back to base after unload.

## M09d / M09e: bot (participant) ability audio

- Trigger: Gameplay's `participantAbilityHook(player, abilityId, chassisId, pos)` (agents/gameplay e018b18) ->
  `World::onParticipantAbility`. It plays the OnTriggerSound + that ability's animation notifies (Skill_<id>;
  Dodge -> Nav_Boost_F; Whirlwind -> Transform_Whirlwind_ROBO) through the caster's body sound set, attached via
  `participantPositionHook`. `tickParticipantAudio(dt)` runs once per step.
- Per tick over opponents_ (M09e):
  - spawned: `setParticipantBuffAudio(player, "TnBuffCloak", pawn.cloakRemain_ > 0, team, pawn.actorLocation())` and
    `setParticipantHoverAudio(player, pawn.hoverState_, pawn.actorLocation())`;
  - otherwise: `onParticipantGone(player)`.
  - All three are idempotent and change-driven. A death stops the loops silently; match end / unload goes through stopAll.
- Fields read: MatchOpponent::spawned() / matchPlayer() / pawn(); Character::cloakRemain_, hoverState_, actorLocation();
  Match::players()[p].team.
- **Cloak animation sounds - correctly absent in versus (CONFIRMED, AssetTools):** Nav_CloakActivate / Nav_CloakDeactivate
  (AI_CQC_ROBO_ANIM*, BL_CHR_CQC.CQC_TRANSFORM_CLOAK_ACTIVATE / DEACTIVATE) are cooked only into campaign levels and
  MP_ESC_BrokenHope / Remnant (Escalation), never a versus map, so the original versus cloak plays only the buff loop / off
  sound. AssetTools exports them as character.json "notify_only_clips"; Systems deliberately does not import them for versus
  (it would be PC ADAPTATION). World::playCloakAnimNotifies plays them only if a profile carries them (an Escalation mode).
- Verified (09c 49115b2 + glue, 7v8 TDM):
  - bots' cloak START_LP x16 (Autobot 8 / Decepticon 8) and OFF x13;
  - hover LIFT x5 / LAND x3;
  - Warcry / Shockwave notifies;
  - 0 missing cues, 0 leaks.

## M09f - bot body audio (foley / transform / vehicle component) [audit finding #2]

`World::tickParticipantBodyAudio(player, chassisKey, pawn, VehicleFormSignals, alive, dt)` once per step per live participant:
RobotFoley from the pawn's own animation (footsteps / jump / land / idle), the Transform_To*_ROBO notifies, and a per-participant
VehicleAudio + VehicleFormAudio at AUDIO_ROOT. Beyond 70 m from the listener (past the body cues' audible range) nothing runs and
its vehicle loops stop (33-participant budget); `onParticipantGone` stops and forgets it. Cue sets load on first appearance
(1-2 ms per chassis, measured). Glue (in the bot loop, next to setParticipantHoverAudio):

```cpp
const Character::VehicleState& vst = bp.vehicleState();
const VehicleFormType ft = bp.vehicleParams().form;
VehicleFormSignals vsig;
vsig.kind = ft == VehicleFormType::Car ? VehicleFormSignals::Kind::Car : ft == VehicleFormType::Tank ? VehicleFormSignals::Kind::Tank
          : ft == VehicleFormType::Jet ? VehicleFormSignals::Kind::Jet : VehicleFormSignals::Kind::Truck;
vsig.vehicle = bp.form() == Form::Vehicle && !bp.isTransforming();
vsig.onGround = bp.onGround();
vsig.boostState = vsig.kind == VehicleFormSignals::Kind::Tank ? vst.tankBoost : vsig.kind == VehicleFormSignals::Kind::Jet ? vst.flying : vst.driving;
vsig.velocity = bp.velocity();
vsig.forward = core::forwardFromYawPitch(bp.yaw(), 0.0f);
const float fwdSpeed = core::dot(core::Vec3{vsig.velocity.x, 0.0f, vsig.velocity.z}, vsig.forward);
vsig.stickForward = fwdSpeed > 0.5f ? 1.0f : (fwdSpeed < -0.5f ? -1.0f : 0.0f);   // PC ADAPTATION: bot throttle from motion
vsig.dashing = vst.dashRemain > 0.0f;
vsig.rolling = vst.rollRemain > 0.0f;
tickParticipantBodyAudio(pl, bp.chassis().id, bp, vsig, true, dt);
```

Measured on 09c + glue, MP_IAC_Streets TDM 8 v 8 (the 09c clamp): bot-owned BL_FS_* 102, BL_TRANSFORM 9, BL_VEH_* 18
(were 0 / 0 / 0); 0 not-in-table, 0 LEAK; voices max 96, instances 108. Not yet: nitro / 180 / wheel slip for bots (need
Gameplay per-pawn signals; silent, no fake), and the 70 m cull radius is a PC budget choice (PC ADAPTATION).

## M09g - voice budget for 16 v 16 / 32 v 32

Measured on 09c f5fa838 (+ M09f glue), Streets TDM ExtendedPlayers 15 + 16 bots, 2 min. Before: the 96-channel pool was full
most of the time (stolen 705-781, refused 95-145 per 2 min); about 60 of the 96 channels were held by sounds beyond their
audible distance at zero gain, and the local player's idle foley got no channel.

- **Inaudible one-shots are not started** [UE3 AActor::PlaySound -> USoundCue::IsAudible, HIGH]: a positional one-shot
  beyond its DistanceMax from the listener; loops and 2D sounds start regardless (`SoundCues::inaudibleSkipped()`).
- **Virtual voices** [FMOD Ex VOL0_BECOMES_VIRTUAL, HIGH]: a positional voice beyond its max distance holds no channel, is not
  mixed, keeps its timeline, and is mixed again when back in range. The 96 real channels and their stealing rule
  (priority, then the quietest) are unchanged. PC ADAPTATION: up to 1024 logical voices (`MixStats::virtualVoices`).
- **The local player's own sounds are protected** (PC ADAPTATION, asked by Integration for 32 v 32): never a steal victim, and a
  new one takes the least important other channel. At <= 16 participants the pool does not fill, so 5 v 5 is unaffected.
- **Bot body cue sets are registered at most one per step** (8 at once on the first step was an 11 ms spike; now max ~2 ms).
- Bug fixed on the way: with the larger pool the steal loop could pick a free slot (went above 96 real).

After, same run: stolen 141, refused 8, local no-channel 0, virtual peak 64, mixer avg 0.32 / max 0.53 ms per 21 ms block
(about 1.5-2.5 % of one core), body audio avg 0.03 ms per step. Suite 736 / 0, fidelity 194 / 0 / 19.

Glue update (Gameplay agents/gameplay 4b33f0b): in the M09f bot-loop glue replace the speed-derived throttle with
`if (const MoveIntent* mi = participantIntent(pl)) vsig.stickForward = mi->moveForward;` (exact, no longer PC ADAPTATION).
Far bots' animation advancing every 2nd / 4th step is fine: RobotFoley detects notifies between the previous and the current
animation time.

## M09h - per-instance ability actor audio (any owner: Barrier, Sentry, Roller Mine, Guided Missile)

AbilityAudio keeps one state per (actor kind, owner) instead of one local instance. World API (heard by everyone; world
actors, not OnlyPlaySoundOnLocalPlayer):

```cpp
beginAbilityActorAudio();
setAbilityActorAudio(AbilityActor kind, int ownerPlayer, bool ownerIsLocal, AbilityActorState{alive, fading, age, target, pos});
onAbilityActorExploded(AbilityActor::RollerMine / GuidedMissile, ownerPlayer, ownerIsLocal, pos);
endAbilityActorAudio();   // an instance not reported this tick stops silently (despawned)
```

Same sounds per instance as before (barrier BARRIER_LP / RETRACT at health 0; sentry SENTRY_ACTIVATE_LP, POSTDEPLOY per new
enemy, fade + SENTRY_EXPL when destroyed; roller loop / arm 3 s / buildup 8.5 s / explosion; missile SHOOT_TRAIL /
EXPL_IMPT_WORLD). Report a destroyed actor once with alive = false before it leaves the list. The actors' lifetime is
Gameplay's state (onParticipantGone does not touch them). Ammo Crate needs nothing per instance (AMMO_DEPLOY is its
OnTriggerSound, PICK_UP is authored silent). The local-only setters still work (key 0); do not drive one actor through both.

Glue for 09c with Gameplay 26e (1842e20) - replaces the four local calls in the [Systems M08k] block (the interim adapter):

```cpp
beginAbilityActorAudio();
for (const BarrierState& b : barriers_)
    if (b.owner >= 0) {
        AbilityActorState s; s.alive = b.alive && b.delay < 0.0f; s.fading = s.alive && b.fade >= 0.0f; s.pos = b.pos;
        setAbilityActorAudio(AbilityActor::Barrier, b.owner, b.owner == localPlayer_, s);
    }
for (const Sentry& se : sentries_)
    if (se.owner >= 0) {
        AbilityActorState s; s.alive = se.alive && se.delay < 0.0f; s.target = se.target; s.pos = se.pos;
        setAbilityActorAudio(AbilityActor::Sentry, se.owner, se.owner == localPlayer_, s);
    }
{ AbilityActorState s; s.alive = roller_.alive; s.age = roller_.t; s.pos = roller_.pos;
  setAbilityActorAudio(AbilityActor::RollerMine, localPlayer_, true, s); }
{ AbilityActorState s; s.alive = missile_.alive; s.pos = missile_.pos;
  setAbilityActorAudio(AbilityActor::GuidedMissile, localPlayer_, true, s); }
endAbilityActorAudio();
```

The existing onRollerMineExploded / onGuidedMissileExploded calls stay (they address the local instance, key 0). When Gameplay
makes Roller / Missile per owner, loop over them the same way and call onAbilityActorExploded(kind, owner, local, pos).
Suite 740 / 0 (three owners' sentries at once, per-owner POSTDEPLOY, silent sweep, per-owner destroy).

## M09i - other participants' killstreak announcements

`World::onParticipantKillstreakActivated(id, activatorTeam, sameTeamAsLocal)`: Friendly when OnSameTeam (team game, same team),
else Enemy (FFA: everyone else); a role without an authored sound falls back to FactionAnnouncementSound[activator team],
else silent - exactly the authored TnKillstreakActivated* data (most streaks author only Self; Ammo Matrix / Intercooler also
Friendly; Orbital Recon Friendly + Enemy; Jammer / Orbital Beacon 2.0 by faction). Streak effects add no world sound here
(their buffs are OnlyPlaySoundOnLocalPlayer; spawned actors go through setAbilityActorAudio). Glue (09c 26f, killstreak
trigger, after the "triggered (p%d)" log):

```cpp
if (player == localPlayer_) onLocalKillstreakActivated(id, mp.team);
else onParticipantKillstreakActivated(id, mp.team, match_.settings().teamGame && localPlayer_ >= 0 && match_.sameTeam(player, localPlayer_));
```

Checked on 09c 7d42b7b + glue, 16 v 16: friendly Ammo Matrix -> MP_AmmoMatrixOnlineDialog; enemy Ammo Matrix / Intercooler /
Poke / Energon bonus -> silent (no Enemy sound authored); 0 missing cues.
Note: since Gameplay 26h (deadTicks) destroyed sentries report alive = false before removal: SENTRY_ACTIVATE_LP fades 0.25 s +
SENTRY_EXPL (11 / 11 fades on 11ab06f + a report line; SENTRY_EXPL not started when beyond its audible distance).
