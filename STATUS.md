# WFC Rebuild — Playable Vertical Slice Status

_Updated as work proceeds. Build: `powershell -ExecutionPolicy Bypass -File build.ps1`
→ `build/bin/wfc_rebuild.exe`. Fidelity audit + provenance: `FIDELITY.md`._

## SYSTEMS M08r (2026-10-06) - opponent spawn hitch (my M08d glue ran for every pawn)
- My M08d glue in World::applyCharacterTo (preloadWeaponAudio + setPlayerVehicleWeaponAudio) runs for EVERY pawn, opponents included, because
  Gameplay applies characters to participants through it. Two effects of an opponent's spawn:
  * a synchronous decode of its weapon cues on the spawn frame (reproduced: 78.6 ms for the first Truck opponent; Integration measured 67 ms);
  * the local player's loadout weapon classes and vehicle-weapon class were overwritten with the opponent's. Mostly masked, since shots use
    the fired weapon's own class, but wrong.
- **Fix:** the glue runs for the local pawn only. `docs/handoff/SYSTEMS_M08R_opponent_spawn_audio_glue.patch` (one hunk, against 08n 1b9344f).
  Opponents fire no weapon sounds in the rebuild; if they ever do, onWeaponFired's ensureWeaponAudio loads on first use.

## SYSTEMS M08q (2026-10-06) - no main-thread decode for large streamed music (match final-stretch hitch)
- **Problem** (Integration 08n): the first final-stretch music of each match decoded synchronously in World::tick.
  * DM_FINALSTRETCH_LP is 248 MB of waves: 686 ms, then 159-190 ms in later matches.
  * This also explains 08m's unattributed 90-111 ms mid-match frames.
- **Fix** (SoundCues):
  * A streamed cue played while not resident, with more than 2 MB of waves (match / mode music: 20-248 MB), now decodes on the worker.
  * Its instance is created waiting (no voices, no timeline) and starts, at age 0, on the tick that adopts the waves.
  * Small streamed cues (HUD ticks, announcer lines: ~0.1 MB, 1-5 ms) still decode at once, so their timing is unchanged.
  * A waiting instance that is stopped is retired at once (it never sounds); its late waves are released.
- **Also fixed:** the silent-layer skip in launch used the runtime level (fade-in from 0, instance volume, a sound-group slider at 0), so such a
  one-shot dropped its voice and never sounded once the level rose. It now uses authored silence only (-96 dB / curves / envelope).
- **Measured:**
  * 08k test tree, DM TimeLimit 75: DM_START and DM_FINALSTRETCH_LP decode on the worker; play call 0.16 ms (was 159-686 ms); they start a few frames later.
  * Countdown ticks / dialogue decode in 1.4-4.9 ms as before. 0 missing cues, 0 leaks.
  * Suite 719 / 0 (an unprefetched track starts after ~11 frames, worst tick < 5 ms; stop-while-decoding is silent and leaves nothing resident).
  * Movie probe OK; lifecycle 40 / 0; slider probe OK; wfc_fidelity 194 / 0 / 19.

## SYSTEMS M08p (2026-10-06) - melee hit effects on the victim, kamikaze mines
- **Melee / whirlwind / slam / ram hits:** the damage type's SharedHitEffectPlayer entry (exact, else nearest ancestor), resolved in the VICTIM's
  SoundEventSet (IMPT_DMG_MELEE_HV / _LT -> BL_MELEE_IMPT.MTL_HV / MTL_LT); bCausesBlood gate; RetriggerTime per victim per entry
  [CONF data + script, as the weapon hit effects]. Before, local melee hits on opponents made no hit sound.
- **Kamikaze mines** (MinePooper streak; martyrdom mines use the same mesh) [RE pass 5 s12 addenda 15-17]:
  * KAMIKAZE_FLIGHT_LP_IDLE from the throw;
  * the first target found: KAMIKAZE_FUSE_START + the idle loop fades 0.25 s into KAMIKAZE_FLIGHT_LP_TRACKING;
  * explosion (target / wall / shot): the loop fades + KAMIKAZE_EXPL_IMPT_WORLD;
  * fizzle (60 s LifeSpan / owner death): the loop stops, no sound.
  * Note for Gameplay: a mine destroyed by being shot explodes in the original (damage + sound); the rebuild removes it
    without damage. Audio follows the original (the explosion plays).
- **Martyrdom** (Leader ExplodeOnDeath, RE addenda 17 / 18): not simulated by Gameplay. TnProjectileMartyrdomGrenade is unused in the original.
- **Glue:** `docs/handoff/SYSTEMS_M08P_melee_hit_mines_glue.patch` (after M08o, against 08k): the melee hit call, plus a KamikazeMine audioKey and per-tick calls.
- **In game** (08k test tree, participant harness): 15 MTL_HV melee hits; Mine Pooper idle x3 -> found x2 (FUSE_START + tracking) -> EXPL x2.
  0 missing cues. Suite 716 / 0; wfc_fidelity 194 / 0 / 19.

## SYSTEMS M08o (2026-10-06) - grenade sounds, death sounds
- **Grenades** (RE pass 5 s12 addendum 13 + HmProjectile):
  * Gameplay's toss built its projectile outside spawnProjectile, so grenades had no flight / fuse / bounce / explosion sound
    and no toss sound. The glue gives them a weapon class + audio key.
  * PerformToss plays GrenadeBagMesh WP_Fire (EMP_DEPLOY / MAGMA_DEPLOY / HEAL_DEPLOY), heard only by the thrower.
  * Every refusal (none left, cooldown, carrying a heavy turret) plays WP_NoAmmoFire GRENADE_DRY_FIRE.
- **Deaths** (addendum 12, CONF):
  * Vehicle form: TnVehicleForm.OnPlayDeath plays the chassis' _Blueprint.DeathSound at the wreck on every machine (the killer too).
  * Robot form: only the TnDeathTypeMelee entry's TnDeathModifierPlaySound (hud_melee_death_disintegrate). It is chosen by the kill
    damage type's DamageDeathType (TnDamageTypeMelee + subclasses; Poke overrides it) [CONF data].
  * Granted weapons (Poke, the rocket-turret streak) play no pickup sound. TnPlayerController.DeathSound is campaign-only.
- **Glue:** `docs/handoff/SYSTEMS_M08O_grenade_death_glue.patch`, made against integration/milestone-08k (e467663).
  * Grenade toss / refusals, the grenade projectile's audio identity, and the victim death sound on PlayerKilled.
- **In game** (08k test tree):
  * FlashBangs toss: EMP_DEPLOY -> EMP_FLIGHT_LP -> EMP_FUSE_BUILD at the first impact -> 6 bounces.
  * Participant kills: KilledRobotSound x7.
  * No melee or vehicle-form kill happened in the harnesses, so the death sounds are suite-verified.
- Suite 708 / 0; wfc_fidelity 194 / 0 / 19.

## SYSTEMS M08n (2026-10-06) - kill-streak announcements, overshield off, dodge wall hit
- **Kill streaks** (RE pass 5 s12 addendum 11, CONF):
  * Data chain: TransCustomization.ini [<Id> TnDataProvider_Killstreak] ObjectPath -> AnnouncementMessageType (TnKillstreakActivated*)
    -> Self / Friendly / EnemyAnnouncementSound; a role with none falls back to FactionAnnouncementSound[activator team].
  * Everything plays through the announcer queue. `MatchAudio::killstreakActivated(id, role, team)`; `__match_messages__.killstreaks`
    (gen_level_audio.py; UE names are case-insensitive: ini TnKillStreak* vs TnKillstreak*).
  * The glue plays the Self line when the local player triggers a streak. Earning a streak is silent in the original.
- **OvershieldOffSound** (TnPlayerPawn.Tick): overshield health reaching 0 (depleted or expired) -> OVERSHIELD_POWER_DOWN at the pawn.
- **HitWallSound** (InRobotForm.HitWall during a dodge) -> MTL_DASH_WALL_IMPT at the pawn.
- **Glue:** `docs/handoff/SYSTEMS_M08N_killstreak_overshield_glue.patch` (after M08m).
  * A Character::dodgeWallHits_ pulse in CharacterMovement's Dodging.OnHitWall.
  * Per-tick overshield / wall-hit reads; the Self announcement in triggerLocalKillstreak.
- **In game** (participant harness with audio on, test tree):
  * Live kills play the killer's KilledRobotSound. This closes M08i's "kill confirm not heard live".
  * Orbital Recon's Self line and Improved Orbital Recon's faction fallback play. Back-to-back streaks queue / drop per the
    announcer's single slot (the harness restarts matches rapidly).
- Suite 700 / 0 (role routing, faction fallback, silent roles, overshield, wall hit); wfc_fidelity 194 / 0 / 19.

## SYSTEMS M08m (2026-10-06) - guided missile, barrier, sentry sounds; default loudness decided
- **Guided missile** (TnGuidedMissile.Mesh = GuidedMissile_PROJMESH): FlightSound SHOOT_TRAIL from launch, following the missile;
  on detonation the flight fades 0.25 s and ExplosionSound EXPL_IMPT_WORLD plays (HmProjectile, as the weapon projectiles).
- **Barrier** (TnBarrierSpawnable, RE pass 5 s12 addendum 3):
  * BARRIER_LP from spawn until the actor goes;
  * BARRIER_RETRACT once when health reaches 0 (damage or the DegenRate lifetime);
  * a re-cast / owner-death removal is silent.
- **Sentry** (TnSentryPawnAbility, addenda 3 / 4):
  * SENTRY_ACTIVATE_LP from deploy; POSTDEPLOY on every EnemyAcquired;
  * each shot TnWeaponDefaultSentryAbility WP_Fire SENTRY_SHOOT, plus SENTRY_IMPT on world hits;
  * destroyed (damage, the 30 s lifetime, or the owner's death: TnSentryPawn.Kill -> Destroyed, CONF RE addendum 5): the loop fades 0.25 s,
    then Sentry_DSYS's SENTRY_EXPL. The 5 s / 30 s (rocket) post-death linger (addendum 6) has no sound.
- **Not wired:**
  * TnAmmoCratePickup.PickupSound: Gameplay's SpawnAmmoCrate drops TnDroppedPickupAmmoBeacon (a damage buff, no sound); no crate pickup exists.
  * The decoy trap: not simulated by Gameplay.
- **Data:** class_sounds gains TnBarrierSpawnable / TnSentryPawnAbility (+ the Sentry_DSYS DestroyedSound) / TnAmmoCratePickup /
  TnGuidedMissile / TnWeaponDefaultSentryAbility. The generator lines for these came from a parallel Systems session (rebuild-systems-7b),
  which stopped by our user's decision; this session merged its work.
- **Glue:** `docs/handoff/SYSTEMS_M08M_ability_actors_glue.patch` (after M08l): the per-tick missile / barrier / sentry state, detonation, and sentry shots.
- **In game** (08h + gameplay + systems test tree):
  * Truck6 missile: SHOOT_BUILDUP -> SHOOT_TRAIL -> EXPL_IMPT_WORLD.
  * Car4 barrier: DEPLOY -> LP -> RETRACT at ~67 s.
  * Car7 sentry: ACTIVATE -> LP -> EXPL at 30 s.
  * 0 missing cues, 0 leaks. Suite 691 / 0 (new [guided missile, barrier, sentry]); wfc_fidelity 194 / 0 / 19.
- **Default loudness: decided** (our user, relayed by the parallel session). Keep the original profile defaults 80 / 80 / 80 -> 0.8 per
  sound group (about -1.9 dB vs pre-M08g builds), as M08g ships. Closed.

## SYSTEMS M08l (2026-10-06) - action-layer sound notifies: melee swings, ability animations, whirlwind
- `RobotFoley::actionLayer`: Gameplay's one-shot action clip (playAction: Melee_*, Skill_AbilityJammer / _Barrier / _GuidedMissile /
  _MarkTarget, Transform_Whirlwind_ROBO, GrenadeThrow) fires its authored AnimNotify_Sound / SoundEvent notifies as it plays.
  Before, only the base locomotion clip's notifies played, so melee and the ability animations were silent.
- Abilities Gameplay plays no animation for (Skill_Shockwave / _Warcry / _SpawnSentry / _TransformDisruptor; RE pass 5 s12 table):
  their clip's notifies fire on the trigger (`onAbilityAnimFallback`), only while Gameplay isn't playing that clip itself.
- Glue `docs/handoff/SYSTEMS_M08L_action_layer_glue.patch` (after M08k): read-only Character::actionClipIndex() / actionTime(), the action
  layer every tick, the trigger fallback.
- In game (08h + gameplay + systems test tree):
  * Warcry: chest hits + WAR_CRY_STATE_START (anim + buff, as the original).
  * Shockwave: SHIELD_PUSH.
  * Whirlwind: WHIRLWIND_COMPLETE + 15 whooshes.
  * Guided Missile: SHOOT_BUILDUP.
  * Spawn Sentry: SENTRY_ACTIVATE.
  * Melee: SWING_LT_02 per swing.
  * 0 missing cues, 0 leaks. Suite 680 / 0; wfc_fidelity 194 / 0 / 19.

## SYSTEMS M08k (2026-10-06) - Plasma Cannon charge sounds, roller mine, dodge footstep
- **TnChargeWeapon** (`WeaponAudio::chargeState` / `chargeFizzle`; fire modes in `fire`), per Gameplay 24l's mapping + RE's EWeaponEvent enum [CONF]:
  * charging -> WP_Looping CHARGE_SHOT; level 2 -> WP_LoopingSecondary CHARGE_LP_02; level 3 -> WP_LoopingTertiary CHARGE_LP_03;
  * release -> all loops fade 0.25 s; the shot plays WP_Fire / FireSecondary / FireTertiary by level (SHOOT_CHARGE_SHOT / _02 / _03);
  * released before level 1 -> WP_NoAmmoFire (22) SHOOT_DRY_FIRE_PLASMA_01.
- **Roller mine** (AbilityAudio::rollerMine, TnRollerMine / TnRollerMineAbility defaults, RE s12 addendum / s13):
  * ROLLER_MINE_LP at spawn; KAMIKAZE_FUSE_START at 3 s (ArmSound); ROLLER_MINE_FUSE_BUILD at 8.5 s;
  * ROLLER_MINE_EXPL on destruction; a silent stop on owner death / kill-Z.
- **Dodge:** FS_DEFAULT_JUMP_CHARGED through the body's sound-event set (Nav_Boost_* notify; Gameplay plays no dodge clip),
  e.g. BL_FS_SML_BOT / BL_FS_LRG_BOT.FS_JUMP_CHARGED.
- **Glue:** `docs/handoff/SYSTEMS_M08K_charge_roller_dodge_glue.patch`, after Gameplay 24l + agents/systems + the M08i glue.
  * It reads Weapon charge state / fizzle and roller_ every tick, the dodge edge, and explodeRollerMine.
  * The shot's level comes from the fire hook's Weapon copy (projClass = mode); its chargeShotLevel is copied before Gameplay sets it.
- **Validation:** a test tree of 08h + agents/gameplay c804fe0 (one World.cpp conflict resolved locally) + agents/systems + M08i/M08k glue.
  * WFC_CHARGETEST with audio: fizzle -> dry fire, L3 -> _03, L1 -> SHOT, L2 -> _02, L3 -> _03.
  * Car6 TDM roller mine: LP -> arm -> buildup -> EXPL at 10 s.
  * Car2 / Truck dodge: FS_JUMP_CHARGED per body.
  * 0 missing cues, 0 leaks. Suite 680 / 0; movie probe OK; lifecycle 40 / 0; wfc_fidelity 194 / 0 / 19.

## SYSTEMS M08j (2026-10-06) - level-start warming (frontend title frame), abandoned-prefetch leak
- **Problem:** Frontend measured 43-55 ms of audio.levelStart on the title's first visible frame (boot and every return). It was the
  title level's eager cue waves decoding synchronously (56-61 ms here; parse < 1 ms). The music decode then landed on the next frame
  (~110 ms) when nothing had prefetched it.
- **Fix:** `prefetchLevel` (which Frontend already calls at boot and at every travel start) now also decodes the level's eager waves on a
  worker (`AmbientAudio::warmLevel` -> `SoundCues::warmCueWaves`). `LevelAudioHost::load` waits for that level's warm, loads (cache
  hits), then releases other levels' unadopted warm samples. Win32Audio::load keeps one copy when a worker and the main thread decode
  the same file.
- **Measured** (real device, probe): UI_FrontEnd_m level start 50 -> 0.5 ms, the next tick 109 -> 0.02 ms; UI_PartyLobby_m 4.8 -> 0.3 ms;
  prefetch call < 1 ms, loading ticks < 0.2 ms; PCM per level unchanged.
- **Leak fixed** (since 8df544b): a prefetched level that never loaded, or that unloaded before its music played, kept its music pinned
  (~83-140 MB). Prefetched music is now unpinned when another level loads, or when its own level unloads. Back to the 36.5 MB base.
- Validation: suite 666 / 0 (new [level-start warming]); movie probe OK; lifecycle 40 / 0; slider probe OK; wfc_fidelity 194 / 0 / 19.
  The 08g frontend boot -> party lobby -> title shows no audio scope in the frame hitches.

## SYSTEMS M08i (2026-10-06) - ability / buff / hover / kill-confirm / transform-failed sounds
- New `AbilityAudio` (Systems owns the sound lifecycle; Gameplay owns abilities, buffs and their timers), data from the class defaults
  (gen_character_audio.py: `abilities`, `buffs`, `class_sounds`; 29+ cues, 0 missing). RE pass 5 s12 / s12 addendum / s13.
- **OnTriggerSound** on a successful trigger, at the pawn, heard by everyone: Barrier, Drain, SpawnAmmoCrate, TransformDisruptor,
  AbilityJammer, AoeHeal, DecoyTrap, Disguise, HardLock. The other abilities author none (their audio is anim notifies / buffs / actors).
- **Buffs:** Apply = a sound attached to the buffed pawn (loops for _LP cues) until the buff ends; Unapply stops it and plays the
  Unapply one-shot; death stops it silently. Only the buffed local player hears them, except Cloak (everyone; Autobot / Decepticon
  cues by the buffed pawn's team). Wired: Cloak, Warcry, HardLocked, TransformDisruptor on the local pawn.
- **Drain:** DRAIN_HEAL per tick on the drainer while it has targets; DRAIN_DAMAGE per tick at each victim.
- **Hover** (TnAcrobaticsManager): HOVER_JUMP_LIFT loop from JumpingToHover through Hovering, 0.5 s fade, HOVER_JUMP_LAND when Hovering ends.
- **AbilitiesJammedSound** on a press refused while jammed; **TransformFailedSound** on a refused transform (Disruptor buff, already
  transforming, no room); **kill confirm** for the killer, 2D: headshot > robot > Jet > Car > other vehicle (by the victim's character class).
- Glue against 08g/08h: `docs/handoff/SYSTEMS_M08I_ability_audio_glue.patch` (PlayerController audio pulses + World::tickAbilityAudio,
  drain per victim, PlayerKilled kill confirm). Dry-run applies to 08h after the agents/systems merge.
- Validation: suite 662 / 0 (new [ability / buff sounds]); 08g in game: Tank Drain + hover lift / land, Truck5 Warcry + ammo crate,
  Car5 cloak (Autobot loop), Car4 Barrier, 0 missing cues, 0 leaks; movie probe OK; lifecycle 40 / 0; wfc_fidelity 194 / 0 / 19.
- Not yet: headshot state (Gameplay has none - the headshot kill confirm is unreachable); the kill confirm was not heard in a live
  kill (the kill harnesses run without audio); ability anim notifies (Shockwave SHIELD_PUSH, Warcry chest hit, sentry, guided
  missile, whirlwind, cloak activate) need Gameplay to play those animations; roller mine / sentry actor sounds; Dodge charged footstep;
  HealthOnBlock (listen-server only); shields are campaign-only.

## SYSTEMS M08h (2026-10-05) - vehicle muzzle flash / tracer at the alternating socket (glue against 08g)
- `docs/handoff/SYSTEMS_M08H_vehicle_muzzle_glue.patch` (World.cpp / World.h, against integration/milestone-08g 4f080a2).
- A vehicle shot draws the FIRED vehicle weapon's MuzzleFlash template at the shot's socket (Gameplay noteVehicleShot:
  WeaponSocket_Primary / _Primary2, socket world = posed vehicle bone x socket), once per shot; instant-hit tracers start
  there (the damage trace still starts at the start-trace location) [CONF RE pass 5 9g]. Projectile vehicle weapons
  (rockets, homing rockets, tank cannon) get the flash too (PlayFireEffects runs for every shot).
- Before: no vehicle flash at all (the robot weapon socket is hidden in vehicle form), the tracer started at the
  camera-ray start point, and the instant-hit path used the robot weapon's templates.
- Validation on 08g: Gameplay's WFC_MUZZLETEST 5/5 (Car2 / Car4 / Jet4 / Truck3 alternate, Tank3 Primary only) with one
  flash per shot and each weapon's own template (AssaultRifle / HomRocket / Blaster / D_MuzzleFlash_TankCannon); TDM Car2
  60 flashes 30 / 30 and Truck3 4 / 3, 0 missing cues, 0 leaks; screenshots show left then right flash + tracer.
- Still Rendering data: Impact_AssaultRifle_FX and CarHover_A_01_FX are not in Streets' map_fx_runtime.json (build_map_fx).

## SYSTEMS M08g (2026-10-05) - profile volume sliders (Music / FX / Dialogue)
- **`LevelAudioHost::applyProfileVolumes(music, fx, dialog)`** (also on FrontendAudioRuntime) = HmPlayerController.UpdateLocalCacheOfProfileSettings:
  SetAudioGroupVolume('Dialog' | 'SFX' | 'MUSIC', slider / 100, clamped).
- **Group -> categories** from Xe-TransEngine.ini SoundGroupCategoryMappings (SFX -> SFX_DRY/SFX_WET, DIALOG -> DX_DRY/DX_WET, MUSIC -> MUSIC_DRY);
  the authored category tree (ChildCategories, now in SoundMixer.inc) carries it to every descendant.
- **Device-global and immediate:** the frontend, match and movie audio follow at once (playing voices too); the movie FX volume is the 'SFX' group.
- **Defaults** TnProfileSettings 80 / 80 / 80 -> 0.8 before any profile: **game audio is now 1.9 dB quieter than before at the default
  profile**, matching the original (movies already used 0.8, so the movie / game balance is now faithful).
- **Integration glue:** Application_Frontend applies the profile at boot and on every `onApplied` (in SYSTEMS_M08D_integration_glue.patch).
- **Validation:** suite 634 / 0 (new [sound groups]); volume_slider_probe on the device: FX 80 -> 40 on the ambience beds -6.7 dB, a running movie
  follows, all 0 silent incl. the reverb return; 08c frontend boot -> TDM with Music 40 / FX 50 / Dialogue 25: cue gains exact
  (countdown tick 0.101 -> 0.063 = 0.5 / 0.8), 0 missing cues, 0 leaks. Movie probe OK; lifecycle 40 / 0; wfc_fidelity 194 / 0 / 19.

## SYSTEMS M08f (2026-10-05) — countdown ticks, objective announcer wiring, grenade sounds, prefetch hitch
- **Pre-match countdown:** 10..0 ticks (GRI.OnCountdownChange → CTF_ROUND_TIMER_01) and the objective-countdown ticks (EXTINCTION_ROUND_TIMER_01, ≤ 5).
  - In the 08c build: 11 ticks per match in TDM / KOTH / DOM.
- **Objective audio from Gameplay's MapState broadcasts:** flag, bomb and domination via `MatchAudio::objectiveBroadcast`.
  - KOTH zone moved / captured / contested / neutral come from the zone state, with the 3 s match-start hysteresis.
  - End-of-match rules confirmed (RE S5).
- **Grenades / projectiles:** FuseSound on the first impact and BounceSound on every impact (TnProjectileGrenadeBase); BounceSound on a world hit (HmProjectile.HitWall).
- **Frontend `prefetchLevel` hitch:** gone. The decode runs on a worker and is adopted on a later tick: the call now takes 0.2 ms (was 37–123 ms), loading-frame ticks about 0.1 ms, PCM unchanged.
- **Validation:** suite 622 / 0; movie probe OK; lifecycle 40 / 0; wfc_fidelity 194 / 0 / 19. The 08c TDM run has 0 missing cues, 0 leaks, and English VO (281 `_LOC/int`).

## SYSTEMS M08e (2026-10-05) — per-chassis vehicle FX through Rendering's runtime (handoff: SYSTEMS_M08D_AUDIO_HANDOFF.md §M08e)
- `VehicleFxDriver`: each chassis's authored HoverFX / BoostFx / JumpFX / RamFX at its own sockets, per the form classes' script (energon / boost colour, hover thruster `Size`, FxAllowed from the transform notifies).
- The hover thruster amount also drives the booster sound parameter.
- 08c snapshot with a recording runtime: 4 classes, own templates, spawns == stops.
- The hand-made Optimus effects remain only while no runtime is bound.

## SYSTEMS M08d (2026-10-05) — vehicle / weapon / projectile / beam audio by identity (handoff: docs/handoff/SYSTEMS_M08D_AUDIO_HANDOFF.md + SYSTEMS_M08D_integration_glue.patch)
- **Silent vehicle forms (car / jet / tank)**
  - Cause: the integration World gated all vehicle audio on the Optimus-only FX gate.
  - Now every chassis runs its own HmVehicleAudioComponent through `VehicleFormAudio`, following the form-class rules for car / truck / tank / jet.
- **Weapon audio by the weapon actually fired** (robot or vehicle weapon): fire, impacts and victim hit sounds.
  - Projectile weapons: launch sound, FlightSound loop and ExplosionSound (16 weapons).
  - The loadout's weapon cues are preloaded.
- **Repair Ray:** beam loops per TnWeaponBeam / TnWeaponRepair — WP_Looping, heal / damage loop by target, tail on release — driven by Gameplay's beam traces.
- **Ownership:** class change, match restart, map unload and death stop every vehicle / beam / flight loop. The `WFC_AUDIOCHECK` audit shows 0 leaks.
- **Validated in an integration/milestone-08c snapshot with the glue patch:**
  - 8 class bodies × 3 maps: 0 missing cues, 0 leaks.
  - Cold boot → TDM: English (282 `_LOC/int` waves, 0 skipped).
  - Suite 617 / 0; movie / Extras probe OK; lifecycle 40 / 0; wfc_fidelity 194 / 0 / 19.

## SYSTEMS M08c (2026-10-05) — playtest audio fixes (handoff: docs/handoff/SYSTEMS_M08C_AUDIO_HANDOFF.md)
- **Extras movie → menu silence** — two fixes:
  - the movie preset is now held by the caller's flag *or* the movie sound, so a GFx script movie releases it when its sound stops;
  - one-shot instances are no longer retired after 10 s while their voice still sounds (the 380 s title music had gone unmanaged).
  - Real device: Extras natural end, skip, back and consecutive all return the same music instance; no streams or voices left.
- **French match-start line**: localized waves now come from the GLanguage `_LOC` twin (AssetTools `content/_LOC/<twin>/`). They are never substituted from another language. All 762 localized waves (match, announcer and character dialogue) resolve to `int` (AssetTools d71cd07 + 768ea21).
- **Vehicle audio per form**: the SpeedSound and tire-tread loops, BoosterSound as a loop (stop, BoosterAmount), and the ascend-stop, descend, roll, 180-turn, enter and exit events — all from HmVehicleAudioComponent.
  - Optimus is unchanged (57 / 57 starts).
  - Integration must feed the per-form signals (table in the handoff).
- **Validation:** suite 612 / 0; movie probe OK; lifecycle 40 / 0; vehicle A/B + per-form OK; wfc_fidelity 194 / 0 / 19.

## SYSTEMS MILESTONE 08 (2026-10-05) — every MP map, mode, character and movie-language audio
- **All 10 processed MP maps** (BrokenHope, Remnant, Berth, Rust, Seed, Streets, Molten, Debris, Complex, Gorge) go
  through one generic runtime. There are no per-map source branches.
  - Each map's Kismet sound graph comes from its own manifest: touch volumes, ambient zones / scenes, delays, gates,
    mixer presets, flybys, music, remote events and gameplay-owned `Game:` events.
  - The shared announcer and match cues are loaded per level.
  - Streets' graph matches the hand-flattened zones.
- **Mode audio:** flag / bomb / domination / CTF / round / KOTH messages are ported from the decompiled script.
  - Gameplay calls `world.matchAudio().*Message(...)`; there are no Systems timers.
  - The flag stingers and the round time-up / switching-sides music are included.
- **Characters / weapons:** 33 roster chassis and 53 weapon classes use authored audio profiles. Optimus is no longer
  hard-coded; the default profile reproduces him exactly.
  - Gameplay calls `setPlayerCharacterAudio(chassis)` and `setPlayerWeaponAudio(class)`.
  - In game, Bumblebee, Megatron, Starscream and the Heavy Pistol play their own sounds, with 0 missing cues.
  - Per-weapon audio:
    - world impacts use the weapon's DefaultImpactSound;
    - target hits use the victim's HitEffectPlayer HitSound, with retrigger and the damage type's bCausesBlood;
    - reload, idle, equip and holster sounds come from each weapon's own AnimSet.
    - The Ion Blaster is unchanged.
  - Vehicle form: each chassis's own HmPlayerVehicleAudioComponent (gears, one-shots, slots, tunables). Optimus is
    identical to the previous port (A/B probe, 92 / 92 events).
- **Movies (RE-confirmed):** language track 5 + L from GLanguage (`WFC_LANGUAGE`), speaker routing, the logos at 0.8
  volume, and other movies at the FX slider / 100 (`setMovieFxSlider`; default 80 → 0.8).
- **Lifecycle:** a 40-cycle real-device soak (`tools/systems/lifecycle_probe.cpp`): frontend (logo skip, title,
  party, lobby) → map N → frontend, rotating all 10 maps and the character profiles.
  - Every cycle returns to 0 voices, 0 streams, 0 level cues and the base mixer presets / cue table.
  - Decoded PCM never grows (61 → 52 MB).
- **Validation:** suite 609 / 0; wfc_fidelity 194 / 0 / 19; movie probe OK.
- **Handoff:** `docs/handoff/SYSTEMS_M08_AUDIO_HANDOFF.md`; FIDELITY.md MILESTONE 08.

## SYSTEMS MILESTONE 07 (2026-10-04) — boot-movie audio, match / announcer audio
- **Silent boot movies, fixed in Systems** (+ a ~25-line Frontend patch):
  - the movies' sound is their Bink audio tracks (10 mono: 5.1 with per-language centres);
  - `audio::MovieAudioPlayer` decodes and streams them beside the game mix, which CINE_MUTE mutes;
  - the movie preset is now unflushable (config);
  - verified on the real executable: all four boot movies play their sound.
- **Match audio:** `MatchAudio` ports TnAnnouncer + TnGameTypeMessage + progress announcements. Covered: the start
  dialogue and music, final stretch, end music by winner, 30 s / 1 min / 2 min and kills / points remaining.
  - Gameplay hookup patch ~20 lines.
  - The announcer voice follows the local team (Optimus / Megatron).
- **Second map:** MP_UND_Gorge loads through the generic path.
- **Lifecycle:** 6 real-executable match cycles, back to 0 voices / 36.5 MB every time.
- **Validation:** suite 566 / 0; wfc_fidelity 194 / 0 / 19.
- **Handoff:** `docs/handoff/SYSTEMS_M07_AUDIO_HANDOFF.md` + the two patches; FIDELITY.md MILESTONE 07.

## SYSTEMS MILESTONE 06 (2026-10-03) — frontend / loading / level audio lifecycle
- **Generic level manifests:**
  - `gen_level_audio.py` → `LevelAudio.inc`, merged with the AssetTools map manifest; one path for every level;
  - the UI levels' authored Kismet audio, including the frontend's Iacon / Kaon camera timeline;
  - the lobbies' music, bed and pools;
  - per-cue-asset limits for every level;
  - `CookedCueLimits.inc` removed; the Master compressor is global data.
- **Mixer:** all 47 categories (MUSIC_DRY 0.708 now applied); `CINE_MUTE_FOR_BINK` movie mute.
- **Lifetime:** `LevelAudioHost` (the level's music player, Kismet sounds, bank, presets; unload releases
  everything, including streamed music, at once); no frontend music under gameplay.
- **Contract:**
  - `FrontendAudioRuntime` mirrors the Frontend lane's `IFrontendAudio` seam 1:1 (standalone, no World);
  - World exposes the same calls in a match; `setAudio(a, false)` skips the slice map.
- **Validation:**
  - suite 544 / 0 (30 + 12 real-device lifecycle cycles, 20 seam cycles, 6 orbit loops);
  - game soaks 46 + 39 + 181 level transitions, 0 errors, no growth;
  - wfc_fidelity 194 / 0 / 19.
- **Handoff and classification:** FIDELITY.md MILESTONE 06.

## SYSTEMS MILESTONE 05 (2026-10-03) — runtime lifecycle for frontend → loading → match → return
- **Map audio lifecycle:**
  - `World::loadMapAudio` / `unloadMapAudio` / `resetSystemsForMatch` / `tickAudioOnly`;
  - unload leaves 0 instances / queued events / map cues / map presets / map samples / voices;
  - verified over 12 Streets ↔ synthetic-map cycles (suite) and 15 in-game reloads (soak).
- **Data-driven:** map reverb presets come from audio.json, cue limits from the generic cooked-cue table, pickup
  sounds per factory class; no Streets branch remains in Systems code.
- **Frontend audio:**
  - `MusicPlayer` (HmMusicPlayer port);
  - `FrontendAudio` (GFx UI sounds by name, the authored UI-level tracks, level change);
  - the 16 UI cues plus 3 streamed music cues with prefetch.
- **Map events:** one PickupSound per take on the receiving pawn; no mover or mode-gated audio; the integration-04
  glue stays compatible.
- **Soak:** robot / vehicle / control 24 000 frames — voices, cues and queues bounded; PCM 97.2 MB flat; 0 dropped
  voices; no frame-time drift.
- **Validation:** suite 505 / 0; wfc_fidelity 194 / 0 / 19.
- **Handoff:** the exact frontend / integration call sequence is in FIDELITY.md (MILESTONE 05).

## SYSTEMS MILESTONE 04 INTEGRATION PREVIEW (2026-10-03)
- **Merges:** clean into integration 356c352 (STATUS.md only) and experimental; Rendering's VehicleFx conflict is
  already resolved in integration.
- **Gameplay d122ef4:** 5 additive conflict hunks, resolved and built in `work/m4/merge_preview`.
  - The pickup-sound glue plays each PickupSound once per take (Gameplay PICKUPTEST with audio).
  - Resolutions are in FIDELITY.md.
- **Harness:** 2 stale HUD-spread expectations, retired by Experimental 28e093f.

## SYSTEMS MILESTONE 04 ADDENDUM (2026-10-03) — map FX runtime yielded to Rendering
- **LevelFx removed:** Rendering 411c970 (WfcMapFx) simulates and draws the 8 steam emitters and pickup effects.
- **PickupPresentation:** now the pickup sound only (`onTaken`); effect state is Rendering's `setMapEffectState`.
  The integration glue is in FIDELITY.md.
- **Objective beam:** confirmed for flag/bomb (RE 00dcb20); the conflict is resolved.
- **Validation:**
  - suite 557/0; audio-attach 325/0/11 (0 player-owned);
  - wfc_fidelity 194/0/19; probe 31/0/1;
  - sustained 5.3–14.3 ms.

## SYSTEMS MILESTONE 04 (2026-10-03) — MP_IAC_Streets world systems (AssetTools a23c675)
- **Ambient bed:** all 70 emitters play natively (auto-play once at level start, per-cue kKillFarthest
  registration, line/volume re-play). 50 of 70 sound, because 4 point cues have more emitters than their limit.
  The PROVISIONAL 24-voice budget, audibility gate and fades are removed.
- **96 channels:** priority stealing from authored Priority (255 − Priority). Sustained fire no longer drops
  weapon voices; only Priority-0 cues are refused when full.
- **Zones / pools:** 9 zones, 10 presets, 11 pools re-validated against the complete manifest and the
  decompiled SeqAct_PlayPlayerPositionalSound.
- **Steam FX:** LevelFx hands Steam_Mat and the authored colour to Rendering's material path.
- **Pickups:**
  - ammo beam active at map start (RE P2 correction);
  - `onTaken` / `onRespawned` adapters for Gameplay's PickupEvents; the integration glue is in FIDELITY.md.
- **Movers:** no authored mover sounds, none added.
- **Validation:**
  - suite 586/0; audio-attach 316/0/14 (0 player-owned);
  - wfc_fidelity 194/0/19; probe 31/0/1;
  - sustained 4.8–14.2 ms; dense region 6.5–13.0 ms; no leaks.

## SYSTEMS MILESTONE 03 PASS 7 (2026-10-02) — vehicle loop enable confirmed (RE d50c2a9)
- **Loop enable:** `SoundNodeWaveEvent.bLooping` → FMOD_LOOP_NORMAL, with no loop points or count, so the whole
  FSB sample loops (CONFIRMED). The rebuild already behaved this way; only comments and provenance change.
- **Start / loop / stop:** the cues, crossfades and fades (0.1 in, 0.2 engine / 0.15 boost out, from the current
  position, 0 = immediate) are unchanged.
- **Validation:**
  - suite 533/0 (new loop-runtime block); audio-attach 238/0/13 (0 player-owned);
  - wfc_fidelity 194/0/19; probe 31/0/1;
  - sustained fire 6.5–14.1 ms.

## SYSTEMS MILESTONE 03 PASS 6 (2026-10-02) — AssetTools 7a69756 authored-data handoff
- **Footsteps / landing:** the default `FS_DEFAULT_*` → `BL_FS_LRG_BOT` cues are the authored Streets sounds
  (one footstep table across all Streets physmats); no surface variants exist.
- **Concurrency:** 71 cues match authored values (no change). The `MECH_WPN_VOICE_THRESHOLD` channel-count duck
  on the SHOOT category stays UNKNOWN (native runtime).
- **Vehicle loops:**
  - FSB header regions = whole sample, applied from `VehicleLoops.inc`;
  - seamless wrap fix in Win32Audio;
  - loop enable: resolved in PASS 7 (wave event bLooping).
- **Pickups:**
  - `PickupPresentation`: 27 authored factories, script-confirmed effect activation (highlight off at spawn, on
    after the first respawn) and PickupSound attached to the recipient;
  - Gameplay drives it (no Systems timers);
  - the particle systems are not drawn: module flag semantics UNKNOWN.
- **Validation:**
  - suite 523/0; audio-attach 247/0/13 (0 player-owned);
  - wfc_fidelity 194/0/19; probe 31/0/1;
  - frame times (ms, range / mean): idle 6.4–10.6 / 7.0, movement 4.6–10.5 / 5.5, firing 6.7–13.1 / 10.3,
    sustained 6.5–13.1 / 9.7, hover 3.7–7.7 / 4.4, Boost 3.6–8.0 / 4.6, Nitro 4.3–7.8 / 5.1; particles/meshes → 0 after vehicle runs.

## SYSTEMS MILESTONE 03 PASS 5 (2026-10-02) — native audio runtime semantics (RE 7c4a2e0, supersedes 7b42621 provisionals)
- **Mixer** (`SoundMixer`):
  - native Enable / Disable / ref-count / priority insertion (equal priority: the earlier wins);
  - per-category first-defining-preset selection;
  - linear ramps in authored units (volume as linear amplitude);
  - FadeIn up / FadeOut down; restart from the current value;
  - Duration expiry (< 0 infinite, 0 on the next tick).
- **Zones:**
  - SeqAct_Reverb's explicit enable-new / disable-previous on a global reverb slot;
  - Touch edges with the last touch winning; no exit restoration (no Streets zone has UnTouched);
  - Default before the first touch and after a level load.
- **Channel modes, dB conversion, emitters:** k2D / k3D / SmartPan confirmed; native dBToLinear (−96 → 0, never above
  0 dB); line and box emitter placement promoted to CONFIRMED.
- **Vehicle loops:** behaviour confirmed. FSB loop regions delivered in PASS 6 (whole sample); previously the exact
  samples stay UNKNOWN pending AssetTools.
- **Validation:**
  - native suite 173/0;
  - audio-attach 240/0/10 (0 player-owned left behind);
  - wfc_fidelity 194/0/19; probe 31/0/1;
  - sustained fire 6.9–13.4 ms; cleanup clean.

## SYSTEMS MILESTONE 03 PASS 4 (2026-10-02) — native audio fidelity + Rendering FX handoff
- **Native spatialization** (RE report 76bb0a):
  - inverse rolloff with a hard cull at DistanceMax;
  - linear rear attenuation;
  - k3D as the class default;
  - SmartPan mix with SmartPanAttenuation3D;
  - **PreferPlayer pan reference** (pawn origin within 1400 UU, 0.5 s linear ramp). Pan only: the emitters
    stay at their true positions and volume uses listener distance.
- **Concurrency:** native rules, with class defaults max 5 / kKillFarthest.
- **Mixer presets:** enabled on cue play and disabled on stop (ref-counted); fade curve and overlap combine
  stay UNKNOWN.
- **Reverb:** REVERB_* priorities; zone switches verified (0.25 s, table values).
- **Vehicle FX** (Rendering 5e74895): original material names and unclamped HDR colour on the material path;
  4 material-only emitters spawned (hover base_glow / rays, ram dust / rays); per-loop bursts.
- **Validation:**
  - 0 player-owned sounds left behind;
  - sustained-fire windows 7.2–13.4 ms; leak suite clean;
  - wfc_fidelity 194/0/19; probe 31/0/1.

## SYSTEMS MILESTONE 03 PASS 3 (2026-10-02) — script-confirmed vehicle audio, landing rules, audio thread
- **Vehicle audio:** a port of the decompiled HmVehicleAudioComponent / HmPlayerVehicleAudioComponentImpl:
  - wheels loop only if grounded at boost start; jump-rev after 0.25 s airborne; reverse load from back input;
    0.1 s fade-ins; 15-sample speed;
  - hover dash = BoosterSound (RAM_BOOST_START); ram impact attached;
  - **ram alert removed** (no script plays it).
- **Mixer presets:** VEHICLE_JUMP (engine −18 dB) and VEHICLE_BOOST_END (−4 dB).
- **Landing:** fall height from where the descent begins and ForwardSpeed `|v · facing|`, per TnAcrobaticsManager.
  The landing cue level is as authored; the earlier report came from the placeholder.
- **Spatialization:** FmodAudioDevice PreferPlayer evidence (1400 UU, 0.5 s) recorded; the listener-based
  default is kept as PROVISIONAL with `WFC_SMARTPAN_PREFERPLAYER` A/B; `WFC_SPATIALLOG` instrumentation;
  `k2D` cues play 2D.
- **Zone pools:** reference = listener (script).
- **Ambient:** MaxConcurrentPlayCount 3 on flood lights and monorail.
- **First-play audio:** game-thread stalls (17 / 170 ms) were waveOutWrite blocking; mixing and submission
  now run on an audio thread.
- **Validation:**
  - perf: idle 6.1 ms; sustained-fire windows 7.7–13.6 ms; leak suite clean;
  - wfc_fidelity 194/0/19; runtime probe 31/0/1; audio-attach 0 player-owned left behind.
- **Handoffs:**
  - Gameplay: CarSimulation.SlipAngle → `World::setTireSlipAngle`; landing-clip state; ram collision → notifyRamHit.
  - Rendering: first-use program/texture prewarm (~520 ms first frame); per-shell light environments.

## SYSTEMS MILESTONE 03 PASS 2 (2026-10-02) — world sound bed, zone reverb, mixer, level FX
- **Attachment:**
  - Reusable owner/socket model; all 20 of Experimental's player-owned "left behind" sounds are fixed
    (their recorder, same scenarios).
  - The remaining 19 KNOWN are world impacts / zone pools, which must stay world-fixed.
- **Streets world audio from audio.json:**
  - 70 map emitters (point / volume / line, virtualized to the 24 most audible);
  - 9 Kismet reverb zones (MASTER_WET reverb + echo with preset fade);
  - zone one-shot pools;
  - Master compressor (−6 dB / 10 / 50 ms).
- **SoundCue runtime:** RearAttenuation, 5.1 pan fold-down, wet/dry category routing, data-loaded map cues,
  tire-squeal parameter, UI owner.
- **Level FX:** the 8 authored Steam_Sm_FX emitters.
- **Tire squeal:** authored cue, curves and gating ready; plays only once Gameplay supplies `World::setTireSlipAngle`.
- **Occlusion:** the original -6 dB / 0.5 s PhysicalMaterial occlusion, 0.25 s line checks; player-owned
  sounds are tested against the pawn body.
- **Validation aid:** debug overlay (B) draws every live sound source, coloured by owner, red when occluded.
- **Not done:** pickup / objective FX (37 components); AssetTools + Gameplay request in FIDELITY.md.
- **Validation:**
  - Perf: idle 6.5 ms; sustained fire 9.6–13.6 ms; mixer about 0.2 ms/frame.
  - Leak suite clean; wfc_fidelity 194/0/19; runtime probe 31/0/1.
- **Handoffs:**
  - **Gameplay:** slip-angle scalar; landing-clip state (Nav_Land_02/_03); ram collision → notifyRamHit.
  - **Rendering:**
    - per-shell light environments;
    - Steam_Mat soft alpha + UV distortion;
    - hover/boost ring materials.
  - **Experimental:** classify IMPT_* / PP_* voices as world in audio-attach.

## SYSTEMS MILESTONE 03 (2026-10-02) — branch `agents/systems` (synced to integration/milestone-02 e8036f6)
- **Firing cost 65–70 → 10–11 ms/frame.** The `CollisionWorld::segmentHit` grid walk (exact, brute-force
  verified) fixes the weapon trace, Gameplay's per-shot camera ray and the renderer's light-visibility rays.
  ~900 RPM cadence untouched.
- **Audio ownership:** cue instances attach to pawn / weapon / muzzle and follow them every tick (one-shots
  and delayed wave events included); world impacts stay world-fixed. Per-cue SmartPan distances from the
  cooked SoundNodeRoot.
- **Transform audio:** the original BL_TRANSFORM.OPTIMUS_BOT2VEH / VEH2BOT at their notify times, attached
  to the pawn. Generic gears wav removed. Vehicle FX enable at 1.8 s of the to-vehicle fold.
- **Robot movement sound:** the original BL_FS_LRG_BOT footsteps / scuffs / jump / land / hard land / high
  fall, idle and pivot foley, from the authored clip notifies and the LandingAnims height table. Replaces
  the RELOAD_AIR_RELEASE_THUMP placeholder landing.
- **Fine aim:** BL_WPN_GUN_PULSE_RIFLE.FINE_AIM_START / END on the weapon.
- Diagnostics: `WFC_SYSPROF=1` (Systems CPU sections, live FX/cue counts), `WFC_FOLEYLOG=1`, and
  `WFC_CUELOG` now logs owner, position and stops.
- Validation:
  - `wfc_fidelity`: 194 pass / 0 FAIL / 19 known / 119 info / 1 skip (unchanged).
  - `runtime-probe.ps1`: 31 pass / 0 FAIL / 1 known (was 30/0/4; `transform_cue_is_authored` now passes).
- **Handoffs:**
  - **Rendering:** mesh-particle light environments (one per PSC, not per shell); hover/boost ring
    material treatment.
  - **Gameplay:**
    - play Nav_Land_02 / _03 per LandingAnims;
    - stop-transition idle↔walk flicker;
    - call `World::notifyRamHit` from ram collision.

## INTEGRATION MILESTONE 02 (2026-10-02) — branch `integration/milestone-02`
Integration and stabilisation only, no new features. Branched from milestone-01 (e62250e).
Each branch was merged with `--no-ff`, one at a time, then built and checked with the harness.

| Order | Branch | Head | Conflicts |
|---|---|---|---|
| 1 | agents/experimental | c9592f0 | none |
| 2 | agents/rendering | 13128bb | `Renderer.h` (Systems particle structs vs Rendering `CharacterColors`: kept both), `STATUS.md` |
| 3 | agents/gameplay | 21862a2 | `STATUS.md`, `FIDELITY.md` (sections kept from both sides; PROVISIONAL list combined) |
| 4 | agents/systems | b8fed05 | `Input.h`, `Win32Window.cpp`, `STATUS.md` |

**Cross-branch resolutions (ownership rules):**
- **Dash input:** Gameplay's mapping is authoritative. **Shift = Dash** (pad RB); **RMB** = Fine Aim
  in robot form and Boost in vehicle form. Systems' duplicate `Dash` enum, temporary **Q**
  binding and World-side Dash latch were dropped. That latch also latched every frame whenever
  `WFC_AUTODASH` was set, which clashed with Gameplay's frame-number hook.
- **Vehicle state:** Gameplay's `CharacterMovement` owns Driving, the hover dash, the nitro timers
  and cooldowns, and the speed/steering scales. Systems' `VehicleNitro::update` timer was replaced
  by `follow(vehicleState().nitroRemain > 0)`. RamFX, the nitro/alert cues and the ram-hit
  registry now track Gameplay's single state machine. Boost FX/audio follow Gameplay's Driving
  state instead of raw button state. Values are identical on both sides (3 s, ×1.5, ×0.3, 8 s).
- `Character::boneWorld` (Systems, vehicle FX sockets) now includes Gameplay's transform
  `meshOffset()`, matching `draw()`.
- Recoil, aim offset and upper-body layering: still exactly one implementation (Gameplay's), and
  recoil fires once per shot (`PlayerController` → `notifyFired`).
- **Ion Blaster cadence:** Systems' native-RE one-shot timer (strict `>`, overshoot discarded,
  ~900 RPM). The 923 RPM patch is not applied; Experimental withdrew it.

**Render data:** `tools\render\build_render_data.ps1` regenerated `work/render/MP_IAC_Streets`:
175/175 materials, 25 decals (`decals.glb`), `clut.png`, the post-process block, BSP, 268 lights
and 78 atlases. Output is byte-identical to agents/rendering's own output, apart from the
embedded worktree path. `slot_materials.json` is `{}` (0 default-material slots) on both.

**Validation (`.\build.ps1 -Jobs 2 -Clean`):** `wfc_rebuild.exe` and `wfc_fidelity.exe` build
with 0 compiler warnings.
- `wfc_fidelity` (latest Experimental harness): **194 pass / 0 FAIL / 19 known / 119 info / 1 skip**.
  `--map`: 196/0/19/135/0. `--no-assets`: 139/0/16/37/18.
- The same harness on milestone-01 (`ab.ps1`) gives 125/0/71/118/3. vs that baseline:
  **0 REGRESSED**, 49 FIXED, and the new checks (fine aim, form switch at t=0, map content,
  vehicle materials) pass. The Systems merge changed no check (identical to post-Gameplay).
- `runtime-probe.ps1`: 30 pass / 0 FAIL / 4 known.
- Runtime scripted runs (logs and stills in `work/fidelity/m2shots/`):
  - Robot jog 14 m/s forward/back/diagonal with directional clips; facing tracks the camera.
  - Jump about 5 m.
  - Fine aim: FOV 80→45; blocked during reload; resumes after.
  - Sustained fire: 200 shells, 3 magazine drops.
  - Reload while jogging: slot 1.0, legs keep `Nav_StrafeJog_F`.
  - Transformations both ways, stationary and moving:
    - robot→vehicle carries 14 → 15 m/s with heading continuous;
    - vehicle→robot carries 15 → 14.7 m/s until a wall;
    - the movement form switches at t=0.
  - Hover 1.85 m, camera-relative strafe 15 m/s.
  - RMB Boost: Driving on wheels, 30 m/s.
  - Shift hover dash: 30 m/s × 0.5 s.
  - Shift while boosting: nitro 3 s above 30 m/s, with RamFX and nitro cues once. Releasing Boost
    ends it immediately.
- Rendering: WFC path, CLUT grade, bloom, far DOF, robot/vehicle WFC materials, boost afterburners.

**Performance (RX 7900 XTX, WFC path):**
- Idle 6.1 ms; jog 4.7 ms; boosting 3.9 ms.
- **Sustained fire 62–68 ms** (Milestone 01: 40–46 ms).
- On the same renderer, firing costs +20.8 ms/frame on both agents/gameplay head and the merged
  build, so the increase is inherited, not a merge error. Gameplay now also traces the camera ray
  per shot. The Milestone 01 light-environment/mesh-particle interaction still applies. Not fixed
  (owners: Gameplay, Systems, Rendering).

**Known issues carried into the playtest:**
- Vehicle→robot: the weapon becomes usable and visible at the mesh handoff (~50% of the fold),
  not the confirmed 25% + 0.2 s equip. KNOWN `weapon_restore_frac_elapsed_to_robot` (Gameplay).
- Vehicle-specific camera incomplete; no vehicle shoulder offset (Gameplay, documented).
- Ram collision is not implemented in any branch; `World::notifyRamHit` exists but nothing calls it.
- Original transformation sounds are not present in any branch: the generic gears wav still
  plays (KNOWN `transform_cue_is_authored`).
- Harness KNOWNs remain:
  - jump apex 5.14 vs 5.0;
  - step-up 0.35 vs 0.37;
  - wall slide;
  - low-obstacle penetration;
  - vehicle jump;
  - dodge clips unreachable;
  - zero-step fire tap;
  - missing prefab/destructible actors;
  - level emitters.
- Two harness notes predate other branches' work. `unrendered.decals` says there's no decal pass,
  but the decals render. The probe's `boost_fx_emitted` reads weapon-FX counters only; vehicle FX
  log `VFX parts=26–28` while boosting. These are for Experimental to update.
- Robot/vehicle energon hue (AssetTools bake blue vs compiled red constant): KNOWN, Rendering.
- `LightMapTexture2D_882/_5049.png` decode warnings are pre-existing (legacy path only).

## INTEGRATION MILESTONE 01 (2026-10-01) — branch `integration/milestone-01`
Integration only: no new features. All four agent checkpoints were merged with `--no-ff`, one at a
time, building and running the fidelity harness after each merge.

| Order | Branch | Head | Conflicts |
|---|---|---|---|
| 1 | agents/experimental | 6dc6523 | none |
| 2 | agents/rendering | dd21838 | `.gitignore` (kept both rule sets) |
| 3 | agents/gameplay | 5dd1b09 | `STATUS.md`, `FIDELITY.md` (kept all sections from both sides) |
| 4 | agents/systems | ca9cd25 | `Character.cpp/.h`, `Recoil.h` (add/add), `SkinnedModel.cpp`, `Renderer.h`, `STATUS.md`, `FIDELITY.md` |

**Duplicate Gameplay/Systems work, resolved per tools/fidelity/CHECKPOINT.md:** both branches
implemented the reload upper-body slot, the TnAnimNodeAimOffset "Default" profile and
HmSkelControlRecoil, with identical recovered data. The merged build keeps **Gameplay's**
implementation, because locomotion, turn-in-place, transform and hover depend on it. Systems'
`AimOffset.h`, `updateUpperBody`/`evalLayered`, shot-serial recoil trigger, second `setAimPitch`
call and parallel `LocalPose` API were not merged. `WeaponMesh` was ported onto Gameplay's
`samplePose`/`blendPose`/`skinPose`, with the same semantics. Recoil fires once per shot
(`PlayerController` → `notifyFired`). Everything unique to Systems is kept: the animated Ion Blaster
and its sockets, AnimNotifies, original FX (muzzle flash, tracer, impact, shells, reload
flare/smoke, magazine drop), SoundCues, the audio voice API, and the glTF+.bin loader.
`Renderer.h` keeps Rendering's `loadMapRenderData`/`setVisibilityQuery` **and** Systems'
`drawParticles`. `GLRenderer::drawParticles` now restores the caller's `GL_FOG` state instead of
force-enabling it. Fixed-function fog stays off in the WFC shader path.

**Validation (clean build, `.\build.ps1 -Jobs 2 -Clean`):** `wfc_rebuild.exe` and `wfc_fidelity.exe`
build with 0 compiler warnings.
- `wfc_fidelity`: **110 pass / 0 FAIL / 6 known / 51 info / 1 skip** (exit 0). `--map`: 112/0/6/67/0.
  `--no-assets`: 79/0/6/25/10.
- vs the Experimental baseline (main code, 100/0/16): 0 REGRESSED, 10 KNOWN→PASS (Gameplay fixes).
- vs agents/gameplay head: identical. vs agents/systems head: 0 REGRESSED, 9 KNOWN→PASS.
- Runtime, with scripted `capture.ps1` runs (logs and PNGs in `work/fidelity/shots/int_*`):
  - World loads; WFC shader path active (168/169 materials, 1973 lightmapped components,
    268 lights, fog). Optimus, the vehicle and the Ion Blaster render through their WFC materials.
    The `WFC_LEGACYRENDER` fallback works.
  - Walk/strafe pick the directional clips. Facing follows the aim with the back to the chase cam
    (`face.toCam -0.91`).
  - Transformation works both ways (to vehicle → hover, to robot). The vehicle drives and boosts,
    and stops at walls.
  - Fire: SoundCue layers, impact cues, recoil, `IonBlaster_Fire` and muzzle/tracer/impact/shell
    FX all run. A full 50-round mag dump gives 50 shell notifies, then auto-reload.
  - Reload on the move: upper slot weight 1 over `Nav_StrafeWalk_F`, reload cues, flare/smoke
    @0.034, magazine drop @0.174 at MagSocket.
- Render data must be generated per worktree:
  `powershell -ExecutionPolicy Bypass -File tools\render\build_render_data.ps1` (→ `work/render`).

**Known issues carried into the playtest (none introduced as harness FAILs):**
- **Frame time while firing:** about 6 ms standing → 40–46 ms during sustained fire (WFC path).
  - About 20–28 ms of this is inherited from Systems: the agents/systems head alone shows
    +20 ms while firing on the legacy renderer.
  - About 8–12 ms is a merge interaction: each moving shell/magazine mesh particle is drawn as a
    dynamic object. It misses Rendering's 4–8 slot light-environment cache, so it re-traces
    visibility rays for every candidate light each frame. Measured with visibility traces
    disabled: 34 ms.
  - Not fixed here, because the fix changes FX lighting behaviour. Owners: Rendering and Systems.
- `collision.max_step_height` is 0.70 m effective, against 0.35 m CONF. Gameplay's constant is 0.35,
  but it is still applied twice (Experimental patch #2, not applied). Jump apex, wall slide,
  low-obstacle and fire-interval KNOWNs remain; they are Experimental patches 1–4 for the owners.
- Reload/owner-anim slot blend: Gameplay `kSlotBlend` 0.15 s [PROV] vs Systems-recovered
  0.1/0.1 s [CONF]. For Gameplay to reconcile.
- Systems' fixed-function particles composite into Rendering's linear HDR target without
  sRGB→linear conversion. They may read slightly brighter/flatter than the original. Visual check
  in playtest.
- `SHOOT_TAIL` can fire mid-burst when a frame hitch exceeds 2× FireInterval (Systems
  edge-detect on wall-clock time).
- `LightMapTexture2D_882/_5049.png` in ExtractedAssets fail to decode for the legacy lightmap path.
  This predates the merge; the WFC path uses its own regenerated atlases.

## RENDERING PASS 9 (2026-10-01) — CHARACTER CUSTOMIZATION PATH (TnCharacterApplier)
- Reconciled native RE evidence + AssetTools `materials_authored.json`: the applier pushes
  `Cust_Color_A` / `Cust_COLOR_B` / `EnergonColor` onto robot, vehicle, separate arm and weapon;
  an all-zero colour skips the override (authored value stands); faction selects the colour set; no
  separate team override in this path. Optimus character data is all-zero, so the authored MIC
  values stand — identical to the previous render (verified). The faction energon constants equal
  the master's three `EnergonColor` switch constants; the Autobot branch compiles to (1.25,0.05,0.05).
- Character/weapon materials now compile those parameters as runtime uniforms; when skipped, each
  same-named expression keeps its own authored value (UE3 duplicate-parameter semantics). World MICs
  that also declare `Cust_Color_A` stay constant (the applier only targets character meshes).
- New `IRenderer::setCharacterColors(CharacterColors)` (default no-op → authored); applied only to
  dynamic (character/weapon) draws. Gameplay ownership unchanged: nothing calls it yet.
- Raw umodel meshes (`CP_OptimusArm_SKEL`, the MP robot `RB_OptimusWeaponArm_SKEL`) resolve their
  original materials by section material name — verified rendering with the original MICs.
- Shared `world.glb` was regenerated by AssetTools with all section materials resolved; the PASS 8
  slot remap is now a no-op for current data (kept for older extractions).
- Verification hooks: `WFC_CHARCOLORS="pr,pg,pb;sr,sg,sb;er,eg,eb"`, `WFC_TESTMESH="file.glb|x,y,z|yaw"`.

## RENDERING PASS 8 (2026-10-01) — VEHICLE MATERIAL FIX + STREETS COMPOSITION AUDIT
- **Optimus vehicle regression fixed.** Root cause: the vehicle MIC enables `UseReconstructedNormal`
  (normal X in the alpha of DXT5 `VH_Optimus_NORM`, Y in green); its cooked `UnpackMin` covers R,G,B
  only, so X stayed in [0,1] and every vehicle normal was tilted (lighting, specular and the
  reflection that feeds emissive all wrong). The original compiled character PS unpacks both
  channels `(A,G)*2-1`; the translator now does the same. Robot unaffected (DXT1 RGB normal map).
  Diffuse/customization path verified identical to the AssetTools bake (`WFC_ALBEDO` A/B).
- **Authored post-process applied** (persistent level = `MP_IAC_Streets_BASE_m`, TransLevels.ini):
  Streets CLUT `ENV_MPCLUT_p.MP_Streets_CLUT` (32³ volume, decoded), Bloom_Scale 0.1, far DOF
  (max 0.6, falloff 40000 UU). PASS 7 wrongly assumed engine defaults (the BL_LVL stub misled it).
- **25 static decals recovered** from cooked receiver geometry (landmark chevrons/corners, additive
  unlit, 2105 tris) → `decals.glb`.
- **15 mesh sections** left on the default grey material by the extractor now use their original
  materials (`slot_materials.json`); 6 are null in the original data.
- Mirrored-instance winding restored (1 prop).
- Streets composition verified complete: BASE streams only ART + AUDIO (both composed); all 34
  prefab-instance actors and all 1952 props present; prop placement cross-checked against decal
  receiver geometry.
- New diagnostics: `WFC_ALBEDO`, `WFC_GLTFMATERIALS`, `WFC_SKIPMAT`, `WFC_NODOF`, `WFC_NOCLUT`,
  `WFC_NODECALS`.

## RENDERING PASS 7 (2026-10-01) — ORIGINAL WFC RENDER PATH (shaders, materials, lighting, post)
Branch `agents/rendering`. The runtime now renders Streets and Optimus through a GL 3.3 shader path
whose every stage was recovered from the original game data/binaries (details + provenance:
FIDELITY.md "PASS 7"). The legacy fixed-function path remains as an automatic fallback.
- **Materials from the original graphs:** `tools/render/matc.py` translates the cooked UE3 material
  expression graphs (master + WFC MaterialFunctions + MIC params, **static switches**, **TextureSets**,
  WFC HLSL `ShaderCode` snippets) into per-instance GLSL — 168/169 materials (world, BSP, Optimus
  robot/vehicle, Ion Blaster). Fixes the old extraction's biggest error: world materials were drawn
  with their *decal* texture (`Rust_C_CLR`) as diffuse; the real diffuse/normal/masks live in TextureSets.
- **Directional lightmaps, all 3 coefficients**, decoded from the original Xenon base-pass shader
  microcode (`tools/render/xenos_dis.py`): `L = Σ dot(N_t,B_i)² · LM_i · Scale_i`, sRGB-decoded atlases.
- **BSP lightmaps:** BSP rebuilt from the cooked `FModelVertexBuffer` (ShadowTexCoord) split per
  ModelComponent element with its own lightmap (180 lit elements, 2460 tris) → `bsp.glb`.
- **Normal maps / specular / gloss / emissive / reflections:** all as authored by the graphs; cubemaps
  and flipbooks decoded natively from Xbox-tiled data (`tools/render/xbox_texture.py`).
- **Dynamic-character lighting:** WFC UberLight decoded from microcode (ambient cube + wrapped²
  diffuse + Phong spec), light environment built from the 268 authored lights (TotalLightCount 2,
  0.3 m update threshold from TnRobotForm/TnVehicleForm), dynamic-only SkyLight, raycast visibility.
- **Fog / post:** UE3 height fog (authored component, LightBrightness 0.1 default), HDR target,
  decoded bloom gather + UberPostProcess (WorldInfo defaults), DisplayGamma 2.2.
- **Performance:** ~200 fps (RX 7900 XTX) standing and walking; VBOs, per-submesh frustum culling.
- **Render data:** generated into `work/render/<Map>` (untracked) by
  `powershell -ExecutionPolicy Bypass -File tools\render\build_render_data.ps1` — run once per worktree
  before launching. Missing data ⇒ legacy renderer.
- **New env vars:** `WFC_LEGACYRENDER`, `WFC_RENDER_DATA=dir`, `WFC_LIGHTINGONLY`, `WFC_NOBLOOM`,
  `WFC_NOFOG`, `WFC_RENDERCAM=x,y,z,yaw,pitch`, `WFC_RENDERSTATS`, `WFC_DUMPSHADER` (`WFC_NOLIGHTMAP` kept).
- **Still open:** LightsVisibilitiesVolume (precomputed light visibility, format partly decoded) not
  used; 24 vertex-lightmapped props unlit by lightmap; dynamic shadows (ShadowMask) = 1;
  DirectLightAmbientContribution = 0; 1 material with an absent master.

## FIDELITY PASS 12 (2026-10-01): RECONCILED WITH NATIVE RE; THREE VEHICLE MECHANICS; INPUT LATCHES
Inputs: confirmed native RE notes (transformation, locomotion, fine aim, input, weapon restore) and the
Systems checkpoint "VEHICLE MECHANICS" (agents/systems 3700965), plus new bytecode (TnCarForm Hovering/
Driving, TnHoverCarSimulation Update/Strafe/Turn/Dash/Drift).
- **Vehicle->robot now enters FALLING with full velocity** [RE]. Pass 11 snapped it to the ground and
  absorbed the drop with a mesh offset; that is removed. Verified: 15 m/s kept, a 1.85 m drop over
  ~0.2 s, then jogging at 14.
- **Robot->vehicle:** velocity is written to the vehicle unchanged (≤35 m/s, no reprojection); the
  vehicle starts on the robot yaw; hover steering authority fades in over 0.5 s
  (DriftScale = (1 − t/0.5)², from Hovering.BeginState → Drift). Verified 14 → 15 m/s as authority returns.
- **Hover mode corrected:** the hover truck faces the VIEW yaw and STRAFES in that frame
  (Hovering.DoUpdate passes the view yaw; UpdateTurn matches it; UpdateStrafe: 15 m/s, 30 m/s²
  clamped radially). Pass 7's "face the travel direction at π rad/s" was wrong for hovering.
- **Three vehicle mechanics, not conflated:**
  - Normal boost: hold RMB / pad LT → Driving (wheels; Truck_Physics 30 m/s, 25 m/s²); blocked during
    the drift ramp; release → Hovering + drift. Animation: Nav_HoverToBoost_VEH → Nav_Idle_Wheels_VEH,
    then Nav_BoostToHover_VEH on exit.
  - Hover dash: Dash while hovering → 30 m/s along the dominant input axis for 0.5 s (100000 UU/s²);
    cooldown 2 s.
  - Nitro: Dash while driving → speed ×1.5 (45 m/s) and steering ×0.3 for 3 s; cooldown 8 s; ends on
    leaving Driving.
- **Dash binding recovered:** Dash = VehicleSpecialMove = **Shift** (pad RightShoulder);
  PlayerInCarForm.StartVehicleSpecialMove → set_DashingInput. (Systems used a provisional Q.) Shift
  is no longer a boost alias.
- **Input latches [RE]:** the fire held flag persists; reload fires on release of a tap < 0.3 s; jump
  and dash edges stay latched until a simulation step consumes them; transform fires on press.
- **Weapon restore [RE]:** vehicle→robot restores the weapon at 25% elapsed (0.75 remaining) and it is
  usable after the 0.2 s equip. Verified usable at t = 0.48 s of the 1.13 s fold.
- **Fine aim:** transforming to the vehicle ends it (the wish is cleared); during vehicle→robot the
  control form is the robot.
- **Locomotion:** no play-rate compensation; clips stay at 1.0× (the original has the same stride
  mismatch) [RE].
- Regression: jump 5.12 m, turn in place, recoil, reload on the move, fine aim 7 m/s / FOV 45, step
  routes unchanged; clean build.
- **Known:** the weapon becomes usable at 25%+0.2 s, but the robot mesh (and so the visible gun)
  appears at the 50% mesh handoff [PROV]. Driving steering/throttle is PROV (wheel physics not
  recovered). The vehicle camera strategy (Truck_Optimus_CAMSET) is not recovered (the robot camera
  is still used). Ram collision is not implemented. Nitro state is duplicated with Systems'
  VehicleNitro: unify at integration.

## FIDELITY PASS 11 (2026-10-01): TRANSFORM MOMENTUM, ROBOT RUN SPEED, FINE AIM (player-control pass)
Driven by the integrated human playtest. Evidence: UnrealScript bytecode decoded from TransGame.xxx
(`work/pass11/ue3dis.py`), shipped input bindings, Optimus/truck/camera content objects.
- **Transform hard-stop: root cause found and fixed.** The rebuild did two non-original things:
  it zeroed velocity in `beginTransform`, and it locked all input during the fold. In WFC nothing on
  the transform path touches velocity, and no movement code checks IsTransforming. The target form
  becomes the movement form at the START of the fold (`BeginTransformation`). Robot→vehicle hands
  the velocity (≤35 m/s, `kMaxTransformSpeed`) and the rotation to the vehicle's rigid body
  (`TnVehicleForm.OnActivate`). Vehicle→robot keeps the velocity, and the robot's `CalcVelocity`
  preserves overspeed with the TruckTransformerMomentum InAir set while transforming.
  Verified on the real executable: running 14→15 m/s into the vehicle; cruising 15→14 m/s into the
  robot; boosting 30 m/s → robot 28.7 m/s after the fold, then a momentum run down to 14; strafe and
  diagonal folds continuous; standstill both ways clean.
- **"Missing fast movement" = the robot's real run speed.** `TnPawn.ApplyTransformer` applies the
  character definition (Optimus_ROBODEF): GroundSpeed 14 m/s, AccelRate 120 m/s², AirSpeed 12,
  AirControl 0.4, jump 5 m, collision r2.0 m. Passes 2/10 had used class defaults (5.5 m/s). WFC
  has no robot sprint key: full input runs at 14 m/s on the jog clips; partial pad input walks at
  ≥4.5 m/s; vehicle→robot carries vehicle/boost speed.
- **Fine aim (robot only):** RMB toggles, pad LT holds. Speed ×0.5 (7 m/s), FOV 80→45 in 0.1 s
  (exit 0.4 s), look speed ×0.5, spread ×0.5. Drops during reload and resumes. In the vehicle,
  RMB/LT = boost.
- **Camera from the robot strategy:** FOV 80, orbit 8 m, anchor 4 m, pitch ±75°, over-the-shoulder
  offset curve, basic camera collision, and traces aimed through the crosshair.
- **Truck:** boost 30 m/s / 0.5 s, hover 1.85 m.
- **Fixed pre-existing bug:** the ground/step query counted step-up and hover twice (robots stepped
  0.7 m; vehicles could snap onto decks 4.4 m above).
- Regression: jump 5.12 m, turn in place, recoil +12°, reload layer, the step A/B identical on 12
  routes; clean build.
- **Known issues:** foot slip is higher at the new speeds (≈1.1–1.8 m/s at 14 m/s, ≈1.6–2.0 m/s at
  7 m/s; no playback-rate scaling evidence); the vehicle hover/steering model is PROV; fine-aim
  target snap and fine-aim sounds are not implemented.

## FIDELITY PASS 10 (2026-10-01): LOCOMOTION BLEND FROM THE SHIPPED TREE; SPEED + STEP HEIGHT CONFIRMED
- **Moving state rebuilt to match Robot_ANIMTREE:** TnVelocityAnimBlend (450→1200 UU/s) mixes a
  walk and a jog TnStraferAnimBlend. Each strafer weights F/B/R/L by the travel direction relative
  to the facing, eased over `_BlendSpeed` 0.2. All 8 sequences are phase-locked like the "Strafers"
  AnimNodeSynch group (shared phase, highest-weight clip sets the rate). Idle↔Moving crossfade
  0.2 s (AmpCrossFadeCondition). Replaces "pick one F/B/L/R clip + 0.15 s crossfade".
- **Clip ground speeds measured:** walk ≈3.5 m/s, jog ≈12.1 m/s. The tree's 1200 UU/s MaxSpeed
  equals the jog's authored speed, so the blend is speed-matched by design.
- **Robot ground speed 5.5 m/s confirmed from script:** `TnPawn.PostBeginPlay` runs
  `_BaseGroundSpeed = GroundSpeed`, and `UpdateSpeeds` sets `GroundSpeed = _BaseGroundSpeed ×
  Π SpeedMultiplierFactors` (bytecode decoded). TnPlayerPawn GroundSpeed 550 × Ion Blaster
  GroundSpeedMultiplier 1.0. So the player's Moving state is ≈87% walk / 13% jog, as authored.
- **MaxStepHeight 35 UU (0.35 m) applied** (`TnRobotForm._MovementCapabilities`; was PROV 0.6).
  A deterministic A/B (`WFC_NOMOUSE`) on 12 routes shows no new snagging.
- Diagnostics: `WFC_NOMOUSE` (ignore live mouse in tests), `WFC_STEPUP=m` (A/B override).
- Regression: reload on the move, transforms both ways, turn in place, recoil +12°, jump 6.36 m,
  vehicle hover; clean build.

## FIDELITY PASS 9 (2026-10-01): AUTHORED AIM OFFSET PROFILE (TnAnimNodeAimOffset "Default")
- **The aim offset is now the shipped profile, not a pose-derived approximation.** The Ion Blaster
  uses the `Default` profile (selected by `WeaponTypeObserved`). It drives 11 bones (spine chain,
  head, both arms) with 9 cells each, baked from `Shooting_Aim_{L,F,R}_{D,C,U}`.
- **Bake rule cracked and verified:** for each bone/cell, rotation = Gp·(L_cell·L_centre⁻¹)·Gp⁻¹
  and position = Gp·(t_cell − t_centre), where Gp is the parent's model-space rotation in that
  cell. UE → glTF conversion: rotation `(−x,−z,−y,w)`, position `(x,z,y)·0.01`. Against the
  authored AimComponents: mean 0.01°, worst 0.07°, translations 0 mm (`work/pass8/verify_aim.js`).
  So the runtime bakes the profile from the clips (no asset data committed) and applies it the
  UE3 way: bilinear cells, each bone rotated/moved in model space, parent first.
- **Input ranges recovered:** profile H [−1,1] / V [−1,0.8]; RemapPawnAimRange from pawn aim
  (fraction of 90°) H [−1,0.85] / V [−0.7,1]. The remap is centre-preserving (PROV reading).
  Measured barrel pitch at aim −69/−34/0/+34/+69° → −42/−21/+3/+27/+50°: the gun trails the
  camera at the extremes, as the authored ranges imply.
- Turn-in-place yaw columns: the barrel stays within 5–13° of the aim while the legs lag up to 67°.
  During a pivot step the gun arm swings ≈50° and back. That swing is authored in
  `Nav_IdlePivot90_*` (root-discarded chest ±7°, forearm −50°), and the shipped tree layers the
  aim offset over it unchanged, so it is kept.
- Regression: recoil +12°, reload on the move, transforms both ways, jump 6.41 m; clean build.

## FIDELITY PASS 8 (2026-10-01): WEAPON RECOIL, TURN IN PLACE, FULL AIM GRID (shipped anim tree)
Gameplay agent. Evidence: shipped `TR_Shared_ANIMTREE_p.Robot_ANIMTREE` and class defaults, read
read-only from cooked packages (`work/pass8/dump_animtree.py` → `work/pass8/dump_*.json`).
- **Weapon recoil (was NOT YET):** a reconstruction of `HmSkelControlRecoil` (UE3
  `GameSkelCtrl_Recoil`), restarted per shot (`TnRecoiler`). Bones come from the tree's
  SkelControlLists: SpineRecoil → `C_Spine02_Lumbar02_XB`, RightHandRecoil → `R_Arm02_Shoulder_XB`.
  Ion Blaster values = `Default__TnWeaponMesh` archetype + IonBlaster_WEPMESH overrides. A/B
  (`WFC_NORECOIL`): during sustained fire the barrel climbs +12° (6.8° → 19.1°) with ±3° random yaw.
- **Turn in place (was NOT YET):** `TnAnimTurnInPlace` / `TnAnimTurnInPlaceRotator` ("UnwindLowerBody").
  Standing, the legs keep their world yaw while the torso follows the aim through the aim
  offset's L/R columns. At 22.5° short of a transition's 90°/180° (`TransitionThresholdAngle` 4096),
  `Nav_IdlePivot90_{L,R}` plays with root rotation discarded (`RRO_Discard`) and unwinds the
  offset along the clip's own root-yaw curve. Blend 0.1 s, abort after 50%. Verified: a slow pan
  holds the legs to 67°, then a 90° step returns the offset to ≈0; a fast pan (143°/s) chains pivots.
- **Aim offset is now the full 3×3 grid:** the yaw columns are calibrated from the poses
  (barrel L −90.7° / R +81.2°), and the inputs interpolate at the authored `InterpSpeed` 12.
- **Fixed (pre-existing):** robot→vehicle picked `Transform_ToVehicle_SuperBoost_Veh` (0.8 s) as
  the incoming clip. It now pairs `Transform_ToVehicle_VEH` by name (matched 1.97 s fold).
- Diagnostics: `WFC_AUTOTURN=rad/s`, `WFC_NORECOIL`, `WFC_LOGEVERY=N`; the frame log adds
  legYaw/aimYawN/turn/recoil.
- Regression: jump 6.39 m, transforms both ways, reload on the move, vehicle hover; clean build.

## FIDELITY PASS 7 (2026-10-01): ANIMATION LAYERS, MESH FACING CORRECTED, VEHICLE HOVER POSES
Gameplay agent (`agents/gameplay`). Verified by runtime screenshots + numeric logs (`work/pass7/`).
- **Mesh facing was 90° off; now fixed (+90°).** The authored straight-ahead aim pose
  `Shooting_Aim_F_C` points the Ion Blaster barrel along model **+X** (logged at load:
  `barrel dir (model space) 1.00 -0.06 -0.02`), UE's forward axis. Pass 6's `kMeshYawOffset=0`
  left the whole robot (and truck) side-on to the chase camera, with the gun 90° right of the
  reticle. Pass 6's `face·toCam` check only tested the yaw math, never the mesh. Restored
  `kMeshYawOffset=+π/2`. Now: the chase cam sees the robot's back and the truck's rear; the muzzle
  points along the aim yaw (`MUZZLE` log); `WFC_FACELOG` measures the real mesh +X.
- **Bone-space pose layering** (`assets::LocalPose`: sample / blend / mesh-space per-bone blend /
  additive / skin). Locomotion crossfades are now bone-space, not vertex lerps.
- **Upper-body aim offset (was NOT YET):** the authored `Shooting_Aim_F_{D,C,U}` poses, applied as
  a delta from F_C on the `C_Spine01_Lumbar01_XB` subtree, driven by camera pitch. The grid is
  calibrated from the poses' own barrel pitch (D −47.6°, C −3.3°, U +72.2°). Verified in profile
  at pitch −0.6/0/+0.6: the barrel measures ≈−27°/+7°/+42°.
- **Reload on the move (was PARTIAL):** the authored `Shooting_Reload_IonBlaster_ROBO` now plays as
  an upper-body slot over locomotion, blended in mesh space like UE3 `AnimNodeBlendPerBone`, so
  the torso stays forward over the strafe clips' turned hips. Full body when standing still.
- **Vehicle animation fixed:** moving used to loop `Nav_BoostToHover_VEH`, a one-shot
  transition. It now blends the authored directional poses `Nav_Hover_{Pose,F,B,L,R}_VEH` by local
  velocity, plus the additive `ADD_Nav_Hover_VEH` hover bob.
- **Vehicle turn rate applied:** the truck steers toward its travel direction at the recovered
  π rad/s (`AiMaxAngularSpeed`) instead of snapping.
- **Robot idle:** `NAV_Idle` (Optimus's own gameplay idle) replaces `Cust_Idle`, the
  customization-screen idle that was being picked as first-of-category. Take-off plays once;
  `Nav_Land` plays on touchdown after ≥0.3 s airborne.
- Regression: jump 6.36 m, transforms both directions, vehicle hover 2 m, no idle drift; clean build.
- New diagnostic: `WFC_FIXPITCH=rad` pins camera/aim pitch. The frame log adds yaw/aimW/aimN/reloadW.
- **Still open (gameplay):** ~~turn-in-place~~, ~~weapon recoil~~ (done in Pass 8), boost/dodge
  clips, camera tuning.

> **Integration note (integration/milestone-01):** Systems Passes 1–2 below describe the Systems
> branch's own upper-body slot, aim offset and recoil code. Gameplay Passes 7–9 implemented the same
> features independently, so the merged build keeps **Gameplay's** implementation
> (`Character.cpp` RobotRig, `Recoil.h`, recoil triggered once per shot from `PlayerController`).
> Systems' `AimOffset.h`, `updateUpperBody`/`evalLayered` and its second recoil trigger were not
> merged. Everything else in the Systems passes (animated weapon mesh, notifies, FX, SoundCues) is
> in the merged build. See INTEGRATION MILESTONE 01.

> **Integration note (integration/milestone-02):** the Systems sections below mention a temporary
> `Q` Dash binding and a Systems-run nitro timer. In the merged build, **Shift** is the only Dash
> binding (Gameplay, CONF). Gameplay's movement code owns the nitro/hover-dash timers, cooldowns and
> scales. `VehicleNitro` follows Gameplay's `vehicleState().nitroRemain` for RamFX, the nitro cues
> and the ram-hit registry. Boost presentation follows Gameplay's Driving state. See INTEGRATION
> MILESTONE 02.

## SYSTEMS NATIVE-RE UPDATE (2026-10-01)
- Ion Blaster cadence = original ~900 RPM: one-shot refire timer reset to 0 per shot, fires when elapsed
  > 0.065 s, overshoot discarded, max one shot per tick (standalone check: 900 shots/min). Experimental's
  923-RPM patch is NOT applied.
- Nitro: cooldown starts on activation [CONF]; ends on Boost release [CONF]; ram hits gated to one per target
  per nitro (`VehicleNitro::registerRamHit`, `World::notifyRamHit`); authored ram damage 175/300/175/300 and
  ExtraRamZVelocity 7000 UU/s exposed for Gameplay.
- Confirmed vehicle states (Boost LT/RMB, Hover Dash RB 3000 UU/s 0.5 s 2 s cooldown, Nitro RB while
  boosting) recorded in FIDELITY.md "Native RE confirmation".
- Gameplay handoff: `PlayerController` clears `wantFire_` after every fixed step but sets it once per render
  frame, so a frame that runs 2 steps drops the second step's shot (measured ~4.4 ticks/shot in-game vs 4.0).
  A held trigger should stay set for every step.

## SYSTEMS CHECKPOINT (2026-10-01) — end of round
- Branch `agents/systems`, clean build. Implemented this round: weapon layering / recoil / aim offset,
  animated Ion Blaster + notifies, weapon FX (muzzle, tracer, impact, shell, magazine, reload), weapon
  SoundCues, vehicle boost / hover / jump / ram FX, boost + nitro + engine / jump / land audio.
- Vehicle mechanics provenance (normal boost vs hover dash vs ram/nitro): FIDELITY.md "VEHICLE MECHANICS".
- Deliberately NOT done (Gameplay-owned): tire squeal (needs a lateral-slip signal), nitro camera change,
  nitro speed/steering scaling, hover dash, final Dash / RMB bindings (RMB: robot Fine Aim, vehicle Boost).

## SYSTEMS PASS 7 (2026-10-01) — VEHICLE ENGINE AUDIO
- Optimus's authored engine audio in vehicle form: off-load (idle/coasting) and on-load (throttle) drive
  loops, the jump-rev loop while airborne and the jump-start one-shot, and hover/wheels light/heavy landing
  cues by time in air; 0.2 s engine fades; all layers follow the mph speed parameter. The drive loop yields
  to the boost loop while boosting. Tire squeal not done (needs a slip signal from Gameplay).
- Read-only `PlayerController::throttleHeld()` added for the on/off-load choice.

## SYSTEMS PASS 6 (2026-10-01) — TRUCK NITRO / RAM (state, FX, audio)
- New abstract input action `Dash` (**PROV** temporary key **Q**). DASH while boosting on wheels starts
  the authored nitro: **3 s**, cooldown **8 s**; `RamFX` (rim-lit flame wedge on RamSocket) runs for its
  duration; `VEH_OPTIMUS_RAM_NITRO_START` + `VEH_TRUCK_RAM_ALERT` play at start. Releasing boost stops it.
- For Gameplay (read-only): `World::vehicleNitro()` -> `nitroActive()`, `ramActive()`, `timeRemaining()`,
  `cooldownRemaining()`, `speedScale()` (1.5 while active), `steeringScale()` (0.3 while active);
  `World::notifyRamImpact(pos)` plays the ram impact cue. Systems does **not** change speed or steering.
- Documented, not changed: Optimus's truck physics blueprints differ from the rebuild's dash values
  (HoverTruck_Physics DashSpeed 3000 / DashDuration 0.5 vs current 5000 / 0.3) — see FIDELITY PASS 10.
- Test: `WFC_STARTVEHICLE=1 WFC_AUTOBOOST=1 WFC_AUTODASH=1 WFC_BOOSTLOG=1` (NITRO lines).

## SYSTEMS PASS 5 (2026-10-01) — HOVER THRUSTERS + JUMP BOOSTERS
- Vehicle form now shows Optimus's authored hover thrusters (`CarHover_A_01_FX` on the six wheel
  HoverBooster sockets, with their socket scale): red light cones and orange rings looping, plus a
  spark / electro-ring / pulse burst whenever hover engages. Hover switches off while boosting (the truck
  drops to its wheels) and when transforming.
- Vehicle jumps fire `Jump_FX` on JumpBoostSocket_C/R/L: a 0.5 s burst of downward thruster cones,
  energon cones, glows, booster smoke, sparks and electro rings.
- `VehicleFx` replaces `VehicleBoostFx` as one data-driven system for boost / hover / jump.
- Verified: idle hover, boost (hover off, boost unchanged), jump take-off, transform out; robot weapon FX unchanged.
- PROV: light-cylinder intensity (DustPower 0.1 stands in for the volumetric shader), procedural spark
  texture, ring velocity reading. Open: drive/jump/land engine audio (RamFX: SYSTEMS PASS 6).

## SYSTEMS PASS 4 (2026-10-01) — VEHICLE BOOST PRESENTATION
- Holding boost in vehicle form now shows Optimus's authored afterburner (`bumble_boost_small1_FX` on
  BoostSocket_L/R, the two exhaust stacks): ignition burst of thruster cones + bullet cone + glow, then
  looping cones (3-4/s) and glows (20/s) attached in local space; release kills them (bKillOnDeactivate).
- Original boost audio: VEH_OPTIMUS_BOOST_START, the speed-driven VEH_OPTIMUS_BOOST_LOOP (5 looping
  layers, mph parameter), VEH_OPTIMUS_BOOST_END with the 0.15 s fade, and the 0.27 s grounded wheels peel-out.
- Verified: accelerating, stationary (against a wall), airborne after a vehicle jump, and transforming
  out while holding boost (effect + loop stop, END plays). Robot fire/reload FX unchanged.
- Diagnostics: `WFC_BOOSTLOG=1` (state, particle count, mph, socket positions). Test:
  `WFC_STARTVEHICLE=1 WFC_AUTOBOOST=1 [WFC_AUTOWALK=1]`.
- Open: ram FX, drive/jump/land engine audio (hover + jump FX: SYSTEMS PASS 5).

## SYSTEMS PASS 3 (2026-10-01) — SHELL, MAGAZINE AND RELOAD FX
- Every shot ejects the authored shell mesh (GrenadeAmmo_STAT) from ShellSocket with a vent smoke puff;
  the reload vents a blue flare + 0.75 s smoke stream at the muzzle (@0.034 s) and drops the Ion Blaster
  magazine mesh from MagSocket (@0.174 s, 3 s life) with a smoke puff. Values from the cooked
  ParticleSystems (FIDELITY PASS 7c); gravity/ground contact for the meshes is PROV (none authored).
- `WFC_ANIMLOG` now also prints live particle / mesh / impact counts.

## SYSTEMS PASS 2 (2026-10-01) — UPPER-BODY AIM OFFSET
- Robot_ANIMTREE `TnAnimNodeAimOffset` (profile Default, 11 bones x 9 authored rotations) now aims the
  spine, head and arms at the camera pitch, between locomotion and the reload slot (original tree order).
  Space/axes verified against the Shooting_Aim_* clips. Measured: aim 22.9 deg -> barrel 21.6-23.8 deg while
  jogging and firing; +0.9 / 0 / -0.9 rad screenshots show the gun raised / level / lowered.
- **Found, for Gameplay:** the robot mesh faces +X in model space but `kMeshYawOffset = 0` draws it as if it
  faced -Z, so Optimus renders 90 deg off the aim (barrel heading = aim - ~100 deg). Fix: `kMeshYawOffset = +pi/2`
  (evidence in FIDELITY PASS 7b). Not changed here (Gameplay-owned).
- Diagnostics: `WFC_AIMPITCH=<rad>` forces the aim pitch (camera untouched); `WFC_ANIMLOG` prints the aim
  values plus barrel pitch/yaw vs aim.

## SYSTEMS PASS 1 (2026-10-01) — WEAPON LAYERING, RECOIL, ANIMATED WEAPON, ORIGINAL FX + SOUNDCUES
Systems-agent branch `agents/systems`. All values recovered from cooked data (details and
confidence in FIDELITY.md PASS 7; decoders in `tools/systems/`).
- **Reload on the move**: reload plays in the original `UpperBodyCustom` slot masked from
  `C_Spine01_Lumbar01_XB` (AnimNodeBlendMultiBone), blend 0.1/0.1 s; the legs keep the strafe/jog
  clip (no more glide). Verified: base `Nav_StrafeJog_F` at 5.5 m/s + upper `Shooting_Reload_IonBlaster_ROBO`.
- **Recoil**: HmSkelControlRecoil port on SpineRecoil / RightHandRecoil with the Ion Blaster's
  authored RecoilDefs (restart per shot, decaying sinusoid in aim space).
- **Animated weapon**: the Ion Blaster is its 34-joint skeletal mesh playing its own
  Fire / Reload_AP / Idle anims with authored sockets; the muzzle is the MuzzleFlash socket.
- **Event timing**: weapon AnimNotifies drive reload/idle sounds at the authored times.
- **FX**: muzzle flash, tracer bolt + smoke trail, impact squib rebuilt from the cooked
  ParticleSystems (original textures, blend modes, bursts, lifetimes, sizes, velocities,
  colour/alpha/size curves, squib rules). Replaces the yellow line + box placeholder.
- **Audio**: original SoundCues: layered fire (near/mid/distant by distance), low-ammo, tail,
  impact, reload, idle; dB/semitone variation, timed events, concurrency, FMOD inverse rolloff.
- Diagnostics: `WFC_ANIMLOG` (base/upper/recoil/weapon clip), `WFC_NOTIFYLOG`, `WFC_CUELOG`.
- **Not done / handed off:** camera recoil + shake (camera owned by Gameplay), dry-fire trigger,
  mixer/reverb. The idle base clip
  `Cust_Idle` (showcase idle) should be `NAV_Idle`; that is Gameplay's locomotion selection.

## FIDELITY PASS 6 (2026-10-01) — INTERACTIVE PLAYER FIXES (orientation, locomotion, muzzle, reload, transform)
Runtime observation (replaying the exe) drove this pass, not headless smoke. Fixed, in the
player's priority order:
- **#1 Orientation/facing** — the robot now faces the **camera/aim (mouse) direction every
  frame** (`CharacterMovement`: robot `setYaw(faceYaw)` always; vehicle faces its travel dir).
  The old "only set yaw while moving" caused the body to **snap right** when you rotated the
  camera while standing then walked. Confirmed numerically: `face·toCam ≈ −0.9` (back to the
  chase cam). Mesh yaw offset is **0** (the earlier "90° bug" was a `WFC_FIXYAW` diagnostic
  artifact — the diagnostic set camYaw *after* faceYaw was latched; fixed its ordering too).
  WASD = move direction, mouse/camera = facing — the WFC strafe-shooter control model.
- **#2 Locomotion** — directional strafe clips: pick `Nav_Strafe{Jog,Walk}_{F/B/L/R}` by the
  travel direction **relative to facing** (dot of velocity with facing fwd/right), replacing the
  hardcoded `_F`. Verified: strafe-right → `Nav_StrafeJog_R`, forward → `_F`.
  (Upper-body aim-offset `Shooting_Aim_*` grid + start/stop/turn-in-place still PROVISIONAL.)
- **#3 Transformation** — robot and vehicle each carry a transform clip of **matching duration**
  (ToVehicle 1.97 s, ToRobot 1.13 s): one physical fold authored per mesh. Was played
  **sequentially** (robot fully → hard cut at full-fold mismatch → vehicle fully = the "crack",
  ~3.1 s). Now the outgoing mesh plays to the **midpoint**, then hands off to the partner mesh's
  clip **resumed at the same normalized time** — one continuous fold (verified frames 35→69→135:
  robot folds → matched mid-fold → clean vehicle). Weapon holstered for the whole transform.
  (Cross-mesh pop is minimised, not eliminated — true alpha cross-fade/visibility point is PROV.)
- **#4 Muzzle** — tracer + flash now originate at the **barrel tip** (weapon-local gltf
  (2.063, 0.017, 0.141), the frontmost vertex slice of `weapon.glb`), transformed by the weapon
  world matrix — **2.07 m forward** of the hand attach, so shots leave the gun, not the fist.
- **#5 Reload** — `Shooting_Reload_IonBlaster_ROBO` now plays (full-body, one-shot) while the
  weapon's reload timer runs. Verified pose + `reloading=1`. (WFC's additive `ADD_Shooting_
  Reload_*` upper-body overlay, to reload on the move, needs additive blending — PARTIAL.)
- Diagnostics added (env-gated): `WFC_AUTOSTRAFE/AUTOBACK`, `WFC_AUTORELOAD`, `WFC_FACELOG`,
  `WFC_MUZZLELOG`; frame log now prints `reloading`.
- **Still open:** #6/#7/#8 lighting cohesion (only coeff0 of 3 directional coeffs used; character
  lighting; materials), #10 camera tuning, and a magenta-material prop near spawn (nolightmap view).

## FIDELITY PASS 5 (2026-10-01) — BAKED LIGHTMAPS RECOVERED, DECODED, AND RENDERED
**Streets is now lit by its original authored baked lightmaps.** The licensee-144 lightmap
serialization was cracked: directional lightmap (3 coeff textures), texture = BE export index
into the ART export table (seekfree forward-export → `_LM` atlas), then CoordinateScale/Bias.
- `vs_lightmap.py` decodes all **1793 lightmapped components** → **1755/1952 props** mapped
  (atlas + CoordinateScale/Bias + HDR ScaleVector), 20 atlases (PNG via umodel, 1024² DXT1).
- `vs_map.py` emits TEXCOORD_1 + lightmap node extras → `world.glb`; renderer draws lightmapped
  submeshes unlit × atlas (UV1·scale+bias via texture matrix) × HDR ScaleVector (GL_COMBINE 4×).
  **1962 submeshes lit.** A/B: `WFC_NOLIGHTMAP=1`.
- Verified across spawn 0 / region 6 / region 18: per-region baked shadows + coloured bounce
  (purple/green/warm), bright lit doorways, high contrast — authentically WFC; no artefacts.
- Regression: collision 1.85 M tris, vehicle hover, robot, emissive glow all intact; exit 0.
- **Pipeline order:** vs_lightmap → vs_map → vs_collision (vs_map resets collision.glb).

## FIDELITY PASS 4 (2026-10-01) — lightmap atlases recovered; emissive glow applied
- **Lightmap ATLASES RECOVERED:** umodel decodes the 360-tiled `MP_IAC_Streets_ART_m_LM.xxx` →
  **78 LightMapTexture2D** (1024² PF_DXT1 RGB), preserved in `…/MP_IAC_Streets/lightmaps/`.
  Confirmed real baked lighting. **Component→atlas binding still BLOCKED** (seekfree GUID
  resolution; no instance imports; mixed-endian native block). Exact layout + blocker in
  FIDELITY.md. **No guessed lightmap mapping applied.**
- **EMISSIVE GLOW APPLIED:** Optimus's authored `*_emissive.png` glow masks are loaded and
  drawn as an additive self-illumination pass — blue glowing eyes/energon/Autobot vents (iconic
  WFC look), from original data. Robot/vehicle/weapon. Verified via screenshots.
- **Regression:** vehicle hover (Y −722.5 = ground+2 m) + cruise ~15 m/s + collision (1.85 M
  tris) + robot all intact after the renderer change; clean build, exit 0.

## FIDELITY PASS 3 (2026-10-01) — vehicle physics recovered; lightmaps investigated
- **Optimus VEHICLE PHYSICS RECOVERED** from `Default__TnHoverCarSimulationBlueprint`
  (TransGame.xxx) — WFC vehicles **hover**. Applied: max speed **15 m/s** (MaxLinearSpeed 1500),
  accel **30 m/s²** (3000), dash/boost **50 m/s** (DashSpeed 5000, 0.3 s burst), hover height
  **2 m** (SuspensionRadius 200), jump **12 m/s** (1200), turn ~π rad/s, terminal 80 m/s.
  Verified: vehicle floats 2 m above the street, cruises ~13.6→15 m/s. Boost on Sprint.
  (The Form CDOs only held `=1.0` modifiers; base values live on the *Simulation* blueprints.)
- **Baked lightmaps — investigated deeply, BLOCKED.** 78 LightMapTexture2D atlases exist;
  umodel can decode the 360 textures; meshes already carry TEXCOORD_1. Blocker: the per-
  component lightmap binding is native-serialized (licensee 144) with **GUID-based** texture
  refs (0 imports) and unresolved scale/bias float offsets. Precise findings in FIDELITY.md.
- **Robot movement sanity:** jump re-measured 6.41 m (analytically 6.25; no regression);
  GroundSpeed/accel/air/jump/capsule all intact.

## FIDELITY PASS 2 (2026-10-01) — recover from the executable + extend extraction
Ghidra/ReVa is live with `default.xex`; pawn/vehicle CDOs read from cooked packages
(`Game Dump/TransGame/CookedXenon/{TransGame,Engine}.xxx`) via `ue3pkg`+`props`.
- **Street collision RECOVERED.** props.json already carries authored block flags: **1693 of
  1952 props block**, 259 decorative don't. New `AssetTools/scripts/wfc/vs_collision.py`
  regenerates `collision.glb` = BSP + blocking volumes + **1693 blocking props** (instanced,
  5.46 MB → 1.85 M world tris). Player now stands on the street floor and is blocked by real
  building walls (previously walked through). Verified across 4 spawns (~180×164 m).
- **Robot movement RECOVERED** from `Default__TnPlayerPawn`/`Default__TnPawn`:
  GroundSpeed 5.5 m/s, AccelRate 20.48 m/s², AirControl 0.70, AirSpeed 15 m/s — all applied.
- **Jump/collision RECOVERED:** MaxJumpHeight 6.25 m (JumpZ derived 19.17 m/s; **measured
  6.39 m**), capsule radius 1.75 m / half-height 2.0 m (4 m tall), eye height 2.8 m.
- **Weapon socket rotation RECOVERED** (rotator yaw 172°/roll 30° → gltf matrix); applied.
- **Vehicle base physics**: still in a data asset (not parsed) — provisional.
- **Camera distance**: orbit-list driven (data asset not extracted) — provisional (9 m).
- **HeightFog RECOVERED**: LightColor (234,91,116) warm red-pink, Density 2e-5/UU, StartDistance
  2048 UU (from `MP_IAC_Streets_ART_m HeightFogComponent`) — applied (was guessed blue-grey).
- **Lightmaps**: 78 LightMapTexture2D in `MP_IAC_Streets_ART_m_LM.xxx`; per-component binding is
  native-serialized + 360-tiled textures — path documented in FIDELITY.md, #1 remaining visual gap.
- Fidelity table + provenance: `FIDELITY.md`.

## FIDELITY PASS 1 (2026-10-01) — recover original WFC behaviour from authored data
Evidence root: cooked UE3 config `ExtractedAssets/config/Coalesced_ini/.../Cooked/*.ini`,
map metadata `ExtractedAssets/maps/*.json`, asset metadata `VerticalSlice/**/*.json`.
- **Gravity [CONF]** −29.4 m/s² (WorldInfo.DefaultGravityZ −2940); vehicles −19.4 (RB ×0.66).
- **Transformation [CONF+fix]** now **crossfades** (blend-in 0.115 s / blend-out 0.25 s from
  TnTransformation); was a hard cut at every clip boundary → the jaggedness. Cross-skeleton
  mesh handoff is still an inherent cut.
- **Camera FOV [CONF]** 75° horizontal (Xe-TransCamera TnFovCameraBehavior), converted to
  vertical by aspect; was a guessed 70° vertical.
- **Ion Blaster [CONF]** damage 15, interval 0.065, mag 50, reserve max 250 confirmed;
  **fixed** initial reserve 150, reload 1.5 s, range 300 m + 1.0→0.5× falloff, per-shot spread.
- **Lighting/materials [fix, PROV]** washed-out cause = ambient floored brightness at ~0.70;
  lowered ambient + warm key + specular + exp2 fog. (Baked lightmaps not extracted.)
- **Audio [fix, PROV]** master 0.5 + 3D attenuation/pan (`playAt`/`setListener`); was full-
  volume mono.
- **Map [investigated]** Streets composed from all 3 sublevels (BSP + 1952 props). Gaps:
  static-mesh collision not extracted (confines walkable area), 16 PrefabInstances + HeightFog
  not composed, lightmaps not extracted. See FIDELITY.md.

## DONE (verified via screenshots / logs)
- **P1 Scale / placement / camera.** World is authored in **metres** (no global scale
  applied). Optimus ≈ 4.4 m. Huge ±4800 bounds were the **skydome**, not a unit mismatch.
  Player spawns at the authored FFA start (363.5, −723.6, −341.8), ground-snapped onto the
  collision surface, facing the play-area centroid. Third-person follow camera
  (dist 9 m, height 3.2 m, mouse look, near 0.1 / far 20000). **Framing bug fixed:** a
  leftover `cam.y < 0.5 → 0.5` clamp was launching the camera ~720 m into the sky at the
  map's negative Y; removed. Optimus is now clearly framed on the Streets.
- **P2 Collision.** Flat-ground hack removed. Loads `collision.glb` (3,495 tris) into a
  uniform XZ grid (2 m cells). Downward ground raycast + grounding + gravity; horizontal
  wall blocking via segment tests; respawn when falling below kill-Z. Auto-walk test: player
  walks across real geometry staying grounded at Y≈−724.5.
- **P3 Skeletal animation.** Full glTF skin path: 106-joint skeleton, inverse-bind
  matrices, JOINTS_0/WEIGHTS_0, animation samplers/channels (T/R/S), LINEAR/STEP/CUBICSPLINE
  interpolation, quaternion slerp, hierarchical node transforms, **CPU skinning** each step
  into a dynamic mesh. State machine (ROBOT: idle/walk/run/jump/fall; VEHICLE:
  idle/move) keyed off the **authoritative `extras.category`** strings in the GLBs.
  Additive (`ADD_`) clips are skipped for base poses. Debug log: form / anim name / anim time.
- **P4 Transformation.** ROBOT↔VEHICLE via the paired transform clips
  (`transform_to_vehicle` → handoff → `vehicle_transform_to_vehicle`, and the reverse).
  **Separate skeletons, no interpolation** between them; skeleton/mesh handoff at the end of
  the outgoing clip; input locked for the whole transition; settles into the new form's idle.
  Verified: ROBOT → Transform_ToVehicle_ROBO → VEHICLE → …_Veh → Nav_Hover_Pose_VEH.

- **P5 Materials / textures.** glTF materials parsed (baseColorTexture + baseColorFactor);
  meshes split into per-material submeshes with UVs on both the static baker (map) and the
  skinned loader (characters). PNGs decoded via a platform adapter (GDI+) behind
  `platform::decodeImage` → `render::ImageData` → `IRenderer::uploadTexture` (GL texture),
  cached by URI. Fixed-function GL modulates the texture by the directional+ambient light.
  Verified: Optimus red/blue paint + back wheel; Streets wear their rust/metal textures.
  24 textures loaded, 0 failed. (Normal/emissive/specular deferred — not straightforward in
  GL 1.1 fixed-function.)

- **P6 Ion Blaster.** `weapon.glb` loaded + textured, held at `WeaponSocket_Primary`
  (bone `R_Arm03_Elbow_XB`, node 42) — the weapon follows the bone's world transform each
  frame (robot form only). Hitscan while trigger held: 15 dmg, 0.065 s interval, 50-round
  mag, 250 reserve, auto-reload on empty + manual reload (R), ~1.8 s. Ray vs collision mesh +
  vs damageable targets (AABB). Yellow tracer (muzzle→impact) + muzzle-flash box. A practice
  **DamageTarget** dummy spawns ahead of the player. Verified: weapon in hands, tracer fires,
  ammo 50→25 under auto-fire.
- **P7 Audio.** `audio::IAudio` abstraction + Win32 `waveOut` polling mixer (decodes PCM WAV,
  nearest-resamples to 48 kHz stereo, mixes overlapping one-shot voices; silent no-op if no
  device). Cues loaded & wired by gameplay event (edge-detected in `World::tick`): fire
  (Ion Blaster), reload (gun foley), transform (gears), land (thump). Gameplay never touches
  Windows audio directly. Footsteps deferred (no clear footstep asset located).
- **P8 Dev tools.** Screenshot capture (kept). Runtime debug overlay toggle (B / env
  `WFC_DEBUGDRAW`): world/collision bounds, player capsule, aim ray, weapon-socket marker.
  Window-title HUD now shows FPS, position, speed, form, grounded/airborne, current anim +
  time, HP, ammo/reserve, reload + DBG state. STATUS.md maintained; headless verification
  knobs documented below.

## CURRENT
- P1–P8 complete and screenshot-verified. Polish candidates below.

## NEXT (polish, optional)
- Weapon socket rotation (currently translation-only offset; orientation is approximate).
- Walk clip picks a strafe-jog; prefer a forward locomotion clip by name.
- Normal/emissive/specular maps (needs a programmable path; GL 1.1 fixed-function can't).
- Footstep audio; smoother transform-handoff frame (config-driven fraction < 1.0).

## BLOCKERS
- None. (Headless smoke runs ~real-time, so long clips need proportionally more
  `WFC_SMOKE_FRAMES`; not a product blocker.)

## CONTROLS
- WASD move, mouse look, Space jump, LMB fire (hold = auto), RMB fine aim (robot, toggle) / boost (vehicle, hold),
  Shift dash (vehicle: hover dash; nitro/ram while boosting) [CONF], R reload (tap), F transform,
  C free/capture cursor, B debug overlay, Esc quit.

## ASSET PATHS (root = `F:/Transformers Rebuild/ExtractedAssets/VerticalSlice`, override `WFC_ASSETS`)
- Map:        `Maps/MP_IAC_Streets/world.glb`, `collision.glb`, `spawnpoints.json`
- Optimus:    `Characters/Optimus/robot.glb` (skin+75 clips), `vehicle.glb` (skin+13 clips),
              `character.json`
- (P6) Weapon: `Weapons/IonBlaster/…`

## DEV / TEST ENV VARS
- `WFC_SMOKE_FRAMES=N`  headless; run N frames then exit.
- `WFC_SHOT=path.bmp`   capture a screenshot on the final smoke frame.
- `WFC_DEBUGCAM=front|top`  diagnostic camera + a bright beacon on the player.
- `WFC_AUTOWALK=1`      hold Forward (scripted locomotion test).
- `WFC_AUTOFIRE=1`      hold the trigger (scripted weapon test).
- `WFC_AUTOTRANSFORM=F` trigger a transform at frame F.
- `WFC_DEBUGDRAW=1`     enable the debug overlay from start (same as toggling B).
- `WFC_FIXYAW=rad` / `WFC_FIXPITCH=rad`  pin the camera (= aim) yaw / pitch.
- `WFC_STARTVEHICLE=1`  start in vehicle form. `WFC_AUTOSTRAFE/AUTOBACK/AUTORELOAD/AUTOJUMP=1`
  scripted inputs; `WFC_FACELOG`/`WFC_MUZZLELOG` facing/muzzle diagnostics.
- `WFC_ASSETS=dir`      override the asset root.
- `WFC_ANIMLOG=1` / `WFC_NOTIFYLOG=1` / `WFC_CUELOG=1` / `WFC_BOOSTLOG=1`  weapon layering / AnimNotify / SoundCue / vehicle boost logs.
