# WFC Reconstruction — Fidelity Audit

**Principle:** the original Xbox 360 build is the specification. Recover what WFC actually
did; never let a provisional rebuild constant masquerade as original behaviour.

**Source-of-truth order:** (1) observed original behaviour · (2) `default.xex` code/constants ·
(3) authored cooked data (packages, `Coalesced_*` `.ini`) · (4) extracted metadata ·
(5) current rebuild · (6) guesswork.

**Evidence roots:**
- Cooked UE3 config (decompiled Coalesced): `ExtractedAssets/config/Coalesced_ini/TransGame/Config/Xenon/Cooked/*.ini`
- Per-sublevel map metadata: `ExtractedAssets/maps/*.json`
- Extracted asset metadata: `ExtractedAssets/VerticalSlice/**/{character,weapon,map}.json`
- Executable: `Game Dump/default.xex` (via Ghidra/ReVa) — for compiled class defaults.

Legend — CONFIDENCE: **CONF**(irmed from authored data/exe) · **HI** · **MED** · **LOW/GUESS**.

---

## MILESTONE 03 SYSTEMS PASS 3 — SCRIPT-CONFIRMED VEHICLE AUDIO, LANDING RULES, SPATIALIZATION EVIDENCE, AUDIO THREAD (2026-10-02, agents/systems)

New evidence used (read-only):
* the RE lane's decompiled UnrealScript (`RE-Workspace/work/script/decomp`): HmVehicleAudioComponent,
  HmPlayerVehicleAudioComponentImpl, TnCarForm, TnTruckForm, TnAcrobaticsManager, HmFootstepComponent,
  HmPawn, SeqAct_PlayPlayerPositionalSound;
* its notes (`TARGETED_PASS2`);
* `Xe-TransEngine.ini [HM_Engine.FmodAudioDevice]`;
* cooked SoundMixerProperties and MP_IAC_Streets_AUDIO_m.

Classification: **CONF** = confirmed original, **HIGH** = high confidence, **PROV** = provisional, **UNK** = unknown.

### Vehicle audio — now a port of the original script (`VehicleAudio`)
| Behaviour | Original | Class |
|---|---|---|
| Boost start | `PlayBoostSound`: BoostSound (BOOST_START) as a looping component, FadeIn 0.1; **wheels loop only if `_IsOnGround`**, same fade | CONF |
| Boost wheels | stopped (fade 0.15) once `BoostWheelsTimer >= 0.27` and not on ground | CONF |
| Boost end | `StopBoostSound`: fade both 0.15, play BoostStopSound (BOOST_END) | CONF |
| Engine states | Boosting > JumpReving (airborne >= JumpRevTime 0.25) > Forward/Reverse On/OffLoad from MovementDirection (speed > 1 mph, `Velocity . Rotation >= 0`) and `_EngineLoadState` | CONF |
| Engine load | Hovering.UpdateSounds: 1 if stick forward > 0.01, 2 if back < -0.01, else 0 | CONF |
| Engine fades | EngineFadeIn 0.1 / FadeOut 0.2, BoostFadeIn 0.1 / FadeOut 0.15; loops faded in (new `SoundCues::fadeIn`) | CONF |
| Speed parameter | 15-sample moving average of `|Velocity| x 0.0223694` mph | CONF |
| Land | on touchdown, the highest TimeInAirThreshold (0.15 / 2.0) reached; Boosting -> wheels table, else hover | CONF |
| Hover dash | `TnTruckForm.Hovering.DoDash` -> `PlayBoosterSound` = VEH_OPTIMUS_RAM_BOOST_START (was unassigned) | CONF |
| Nitro | `StartNitro` -> `PlayNitroSound` only | CONF |
| Ram alert | no script calls `PlayCustomLoopingSound` -> **removed** (was played at nitro start) | CONF (absent from script) |
| Ram impact | `ClientPlayRammingSound` -> `PlayRamSound` on the truck: **owner-attached** (was world at the hit point) | CONF path / HIGH attach |
| Jump | `PlayAscendSound` on Hovering/Driving jumps | CONF |
| Tire squeal | `_IsOnGround && avg mph >= 20` with `_WheelSlipRatio`: Hovering feeds 0, Driving feeds `CarSimulation.SlipAngle` (Gameplay). Not gated on boost | CONF; driving input pending Gameplay |
| One-shot events | `HmPawn.PlaySoundEvent` -> `PlaySound(cue, bNotReplicated, , bStopWhenOwnerDestroyed=true)` without SoundLocation | CONF script / HIGH attach |

### Mixer presets (PlayMixerPreset) — new
`DRIVE_JUMP_START` → **VEHICLE_JUMP**: SFX_WET_VEH_ENGINE ×0.126 (−18 dB), fade in 0.3, duration 1.0, fade out 1.0, priority 270.
`BOOST_END` → **VEHICLE_BOOST_END**: ×0.631 (−4 dB), 0.2 / 1.0 / 3.0, priority 264.
Values CONF; envelope and priority resolution HIGH. Verified: boost end triggers the duck.

### Robot landing / footsteps
* **[CONF] Landing selection** (TnAcrobaticsManager):
  * `FallDistance = _FallBaseHeight - Height`, where `_FallBaseHeight` is set by `Falling.BeginState` (the
    ledge, or the jump apex: Jumping → FallingFromJump on descent) and on ground.
  * `ForwardSpeed = |Velocity . Rotation|`.
  * First `LandingAnims` match in array order; none while transforming / meleeing.
  * Implemented exactly (the speed was previously horizontal magnitude).
  * Standing and running jumps fall 514 UU → Nav_Land_02 → FS_LAND_HARD.
* **[CONF] Footsteps** (HmFootstepComponent):
  * FootstepType 0 → Walk, 4 → Run, 1 Scuff, 3 Land, 10 HardLand; no speed-based choice.
  * The surface PhysicalMaterial's HmPhysicalMaterialProperty.FootstepSounds override the defaults.
  * None are in authored.db (0 objects), so the component defaults apply [HIGH] (AssetTools request).
* **Landing loudness, proved from data:**
  * The milestone-02 "soft/squishy" landing was the wrong cue (`RELOAD_AIR_RELEASE_THUMP` placeholder,
    Experimental's spy log).
  * The authored FS_LAND_HARD is −5 dB root (main layer), servo −4 (var −3) and groan −4 (var −6), category
    SFX_WET_NAV (1.0), DistanceMin 15 m. Ion Blaster SHOOT is −9 dB root × its distance curve, same
    category gain. A hard landing is therefore authored ~4–6 dB above a single shot.
  * No class or attenuation error was found. Levels unchanged.

### Player-owned spatialization (PRIORITY 1)
* **Evidence:**
  * 32 of the slice's 41 table cues are `kSmartPan_PreferPlayer`; 9 author none (class default enum UNK).
  * `[HM_Engine.FmodAudioDevice] SmartPanPreferPlayerTransitionTime=0.5, MaxPlayerSmartPanRadius=1400.0`
    (also `MaxChannels=96`, `OcclusionCheckInterval=0.25`).
  * The names indicate PreferPlayer SmartPan is measured from the local player while the player is within
    14 m of the listener, which would centre the player's own sounds. The native code is not decoded.
* **Default kept: listener (camera) reference — PROV.**
  * `WFC_SMARTPAN_PREFERPLAYER=1` enables the player-referenced model (ramp 0.5 s, 1400 UU radius) for A/B.
    Verified: footsteps pan −0.29 → 0.00, weapon −0.13 → −0.06.
* **`k2D` cues play non-positional and unoccluded [CONF]:** only `PP_OPEN_ROOMS` (AUTO_ROOM_02 pool).
* **Instrumentation** `WFC_SPATIALLOG=1` (every 0.25 s per attached instance): cue, owner/socket, source,
  owner position, source–owner distance, listener, distance, pan, attenuation, occlusion, channel gains —
  read back from the mixer.
* **Measured** (walk, transform, jump, boost, fire):
  * Sources stay 0–0.3 m from the pawn origin; the weapon sits 2.7–3.9 m out (the muzzle/hand socket);
    vehicle cues sit at AUDIO_ROOT 1.5 m.
  * Camera listener 5–10 m away; own sounds pan −0.29 (Optimus left of centre), vehicle 0.00 (inside its
    2–10 m SmartPan band).
  * Footstep / transform / vehicle distance gain 1.0; weapon idle notifies 0.43 (DistanceMin 4 m).

### Streets environment
* **[CONF] Structure:** no ReverbVolume / AudioVolume actors in Streets; the environment is 9
  SeqAct_AmbientAudioZone + 9 SeqAct_Reverb on 10 TriggerVolumes.
* **[CONF] Zone pools** (SeqAct_PlayPlayerPositionalSound script):
  * first delay `Rand(DelayMin, DelayMax)` on START, re-rolled per play, STOP ends scheduling;
  * position: random yaw 0–359°, `Rand(DistanceMin, DistanceMax)` in the horizontal plane, world-fixed
    component;
  * reference = "Source Actor" variable, else `AudioDevice.Listeners[0].Location`. No variable is linked
    [HIGH], so the reference is now the **listener** (was the pawn).
* **PROV:** the reverb DSP topology is still my reconstruction from authored I3DL2/Echo parameters; FMOD Ex's
  SFX reverb implementation is not recoverable from local data (ReVa request). Fade and wet/dry routing per
  category are CONF.

### Ambient emitters
* **[CONF]** 40 AmbientSound AudioComponents: bAutoPlay, VolumeMultiplier 1, PitchMultiplier 1,
  bAllowSpatialization, bUseOwnerLocation.
* **[CONF] Concurrency:** MaxConcurrentPlayCount 3 on EMIT_FLOOD_LIGHTS (5 emitters) and EMIT_MONRAIL_IDLE_LP
  (from the cooked cues; audio.json lacks it). The virtualizer keeps the 3 most audible.
* **PROV:**
  * shaped-emitter placement (GetLinePoints / GetExtents are native);
  * the 24-voice budget and 0.5 s fades;
  * always-on activation of the 30 shaped emitters (no component data).

### SoundCue nodes (PRIORITY 4)
* **[CONF] Nodes in use:** the slice cues use only SoundNodeRoot (41) → SoundNodeWaveEvent (134) →
  SoundNodeWaveEx (262); Streets uses the same three classes.
* **No other node types exist in the slice:** random, mixer, modulator, concatenator, delay, attenuation and
  distance-crossfade nodes are absent. Their roles are fields of these classes and are implemented:
  * random variant choice and ChanceToPlayNone;
  * volume/pitch variation;
  * event `Time` delays;
  * bLooping;
  * distance attenuation and SmartPan;
  * VolumeCurve over SOUND_DISTANCE (distance crossfade);
  * Envelope.
* **Authored but not modelled:**
  * LoopStart/LoopEnd (8/11 vehicle roots, semantics native: UNK, ReVa);
  * SmartPanAttenuation3D (19), EnableDoppler (15), Priority / OverridePriority / PlayWhenSilent;
  * NonLocalPlayerPitch (remote players only);
  * SecondaryCategory (all None in the slice).

### First-play audio cost (PRIORITY 5)
* **Measured:** cue `play()` 0.05–0.2 ms, wave decode at load only, mixing 0.7–1.4 ms per 21 ms block.
* **The cost was `waveOutWrite` blocking inside the driver on the game thread:** 17–19 ms at device start,
  and **~170 ms** when the 4-block queue had drained during load (driver restart).
* **Fixed:** mixing and submission run on a dedicated audio thread (2 ms pump; game-thread calls take a short
  mutex; `waveOutWrite` outside the lock; wave storage made reference-stable).
* **Game thread now:** no audio stall. The remaining first-frame hitch (~520 ms at tick 4) is Rendering's
  first-use shader/texture builds.
* **Prewarm:** the original loads the level's banks with the map, which is equivalent to our load-time decode;
  no extra preloading.

### Occlusion validation (PRIORITY 6)
* **Rays:** 76–192 occlusion rays/s (4/s per live instance at the authored 0.25 s); Systems cue section
  0.05–0.17 ms/frame.
* **Player-owned:** body-referenced, 2 of ~300 standing shots and 0.8 % while walking into walls muffled.
* **Map emitters:** about half the live instances are occluded at the indoor spawn.

### Cleanup (PRIORITY 8)
* **Suite:** idle, burst, sustained fire (full magazines + reloads), boost/nitro/transform back, transform
  cycling, fire+move+turn.
* **Results:**
  * queued events return to 0;
  * weapon particles / meshes → 0;
  * vehicle parts → 0 after leaving the vehicle;
  * cue instances settle at the ~25 ambient loops;
  * level FX steady at ~30 particles.
* No caps added.

### Performance
* Idle 6.1 ms (audio off the game thread).
* Sustained-fire windows 7.7–13.6 ms.
* Boost / nitro / back 5.2–8.4 ms; transform cycling 5.0–5.9 ms.
* ~900 RPM unchanged.
* wfc_fidelity 194/0/19; runtime probe 31/0/1; collision 0 mismatches.
* Experimental audio-attach (local spy build): 0 player-owned left behind; 11 KNOWN = world pools/impacts.

### Requests
* **ReVa:**
  1. `FmodAudioDevice` SmartPan PreferPlayer code (the reference point and radius use).
  2. SoundNodeRoot LoopStart/LoopEnd playback (is a looping component looped over that region?).
  3. `HmAmbientSoundLineEmitter.GetLinePoints` / `HmAmbientSoundVolumeEmitter.GetExtents` and emitter
     activation.
  4. FMOD SFX reverb / DSPEffectConfig bit map.
  5. MaxConcurrentPlayCount resolution (steal vs reject).
  6. SpatializationType enum order (class default).
* **AssetTools:**
  1. Streets PhysicalMaterial → HmPhysicalMaterialProperty.FootstepSounds.
  2. Cue-level MaxConcurrentPlayCount in audio.json.
  3. (standing) pickup FX mesh data.

---

## MILESTONE 03 SYSTEMS PASS 2 — WORLD SOUND BED, ZONE REVERB, MIXER, LEVEL FX, ATTACHMENT AUDIT (2026-10-02, agents/systems)

### Attached-audio model (reusable; nothing hard-coded per cue)
`SoundCues::Emitter{pos, owner, offset, socket}` with a World resolver (`World::resolveCueOwner`):
| Class | Original mechanism | Owner | Examples | Conf |
|---|---|---|---|---|
| A actor-attached | HmAnimNotify_Sound / HmAnimNotify_SoundEvent (no SocketName), HmPlayerVehicleAudioComponent | `kOwnPawn` (+ world offset, e.g. AUDIO_ROOT +147.25 UU) | transform, footsteps, landing, idle foley, vehicle boost / engine / jump / land / nitro / alert | CONF class / HI attach |
| B socket-attached | notify SocketName / weapon sockets | `kOwnPawn` + bone, `kOwnWeapon` + socket | SHOOT / SHOOT_TAIL at MuzzleFlash, reload / idle notifies on the weapon mesh, fine aim | HI |
| C persistent loops | vehicle audio component loops | attached as A | engine ONLOAD / OFFLOAD / JUMP_LOOP, boost loop, tire squeal | CONF |
| D world one-shots / world loops | impacts at the hit, map AmbientSound / shaped emitters, Kismet PlayerPositionalSound pools | `kWorld` | IMPT_WORLD / IMPT_DMG / RAM_IMPACT, 70 map emitters, PP_* pools | CONF |
| E non-positional | — (no slice cue needs it) | `kUI` | API only | — |

Delayed wave events start at the owner's position at launch time. Voices of attached instances follow the owner
every tick until the voice ends (`IAudio::isPlaying`; a backend that cannot report keeps updating to the
10 s bound). A holstered weapon (mid-transform) resolves to the pawn carrying it.
**Measured with Experimental's `audio-attach.ps1`** (their recording backend built locally against this
branch, same six scenarios; not committed):
* milestone-02: 20 KNOWN "left behind".
* now: **0 player-owned left behind**. The 19 remaining KNOWN are world sounds that must stay put:
  Ion Blaster `IMPT_WORLD` impact layers (MTL_BULLET_IMPT_SHEET_*, ELEC_SPARKBLAST_FLANGE_*) and the
  `PP_CORRIDORS` zone pool (PP_DECO_MECH_*).
* Experimental handoff: classify `IMPT_*` / `PP_*` voices as world.
* `WFC_CUETRACK=1` logs the transform cue position vs the pawn: it tracks within one step (≤0.26 m at
  14 m/s) in both directions.

### SoundCue runtime
* **Node types:** the slice uses only SoundNodeRoot → SoundNodeWaveEvent → SoundNodeWaveEx. That covers 41
  table cues + 32 map cues: weapon, Optimus, vehicle, transform, Streets. No mixer / concat / modulator /
  attenuation nodes exist in WFC cues; the root carries attenuation and the wave event carries randomization,
  delay (Time), looping and curves.
* **Supported now:** root volume/pitch + variation, DistanceMin/Max + RolloffFactor (FMOD inverse), SmartPan
  2D/3D, **RearAttenuation**, Category (wet/dry routing), SoundParameter (distance / speed / tire slip);
  event Time, volume/pitch + variation, ChanceToPlayNone, bLooping, random wave choice, Volume/PitchCurve,
  Envelope; **5.1 pan matrix folded to stereo** (Default__SoundNodeWaveEvent: Center/BackL/BackR/LFE −96 dB;
  rear-only layers −3 dB [MED]).
* **Data-loaded cues:** cues load from the generated table and from a map's `audio.json`.
* **Not used by any slice cue (not implemented):** root DelayMin/Max, LoopStart/LoopEnd (2 map cues carry the
  class-default LoopEnd), Doppler, SecondaryCategory. Occlusion: see "Occlusion" below.

### Attenuation / panning (audit)
* **Source:** each instance's resolved 3D position (owner, socket or world).
* **Listener:** the camera pose given to `IAudio::setListener` each frame (position, forward, right).
* **Gain:** FMOD inverse rolloff `min/(min + rolloff·(d−min))`, flat inside DistanceMin, held past DistanceMax
  (FMOD Ex inverse semantics).
* **Pan:** equal-power `dot(dir, listenerRight)` (sin of the azimuth, gentler than FMOD's speaker-angle pan,
  no exaggerated separation), blended to centre inside SmartPanDistance2D and full beyond SmartPanDistance3D.
* **Rear:** RearAttenuation dB for sources behind the listener.
* **Not world origin, not player-based.** [MED] kSmartPan_PreferPlayer may measure the local player's own
  sounds from the pawn rather than the camera (that would make own sounds more centred); unresolved without
  native RE.

### Mixer / sound class (gain staging)
* **Routing:** voices route to MASTER_WET (every `SFX_WET_*` category; all slice cues) or MASTER_DRY, as in
  `SoundMixerProperties.SoundGroupCategoryMappings`.
* **Category volumes:** all 1.0 on the slice's paths (see pass 1), so no relative category gain.
* **Master compressor:** Master's Default preset, Threshold −6 dB, Attack 10 ms, Release 50 ms, GainMakeup 0.
  Read from audio.json; applied as a hard-knee limiting compressor [MED: DSPEffectConfig bit 32 read as the
  compressor; FMOD Ex compressor ratio not documented].
  * Measured peaks: −19…−15 dBFS idle, −8 dBFS sustained fire, −7 dBFS fire + movement. It does **not**
    engage in these scenarios, so the weapon mix is unchanged.
* **Master level:** Master's own Default volume is 0.708 (−3 dB); the rebuild keeps master 0.5 [PROV] (the
  original's FMOD output scaling is unknown), so the relative mix is what is reproduced.

### Environment / reverb
`AmbientAudio` loads ExtractedAssets/VerticalSlice/Maps/MP_IAC_Streets/audio.json at runtime.
* **9 Kismet zones:**
  * TriggerVolume polygons, a ray-parity point test on the pawn (camera ignored, as authored), checked every 0.2 s.
  * Entering a zone applies its `REVERB_TRANS_MP_STREETS_*` MASTER_WET preset (Reverb + Echo) with the
    preset's FadeInTime (0.25 s) and starts its PlayPlayerPositionalSound pools (looping random one-shots,
    DelayMin–Max, 20 m from the player, world-fixed, [MED] random horizontal bearing).
  * Leaving without entering another keeps the zone (INFERRED, no on_untouched ops).
  * The default spawn is in DEC_ROOM_LOWER.
* **Reverb DSP [MED structure, CONF parameters]:**
  * send HF shelf at HFReference/RoomHF; 4 early-reflection taps from ReflectionsDelay at Room+Reflections mB;
  * stereo 8-comb + 4-allpass late tank at Room+Reverb mB, comb feedback for the authored DecayTime (RT60),
    DecayHFRatio as in-loop damping, Diffusion as allpass gain, pre-delay ReflectionsDelay+ReverbDelay;
  * Echo: Delay / DecayRatio / WetMix / DryMix;
  * parameters cross-fade over the preset fade.
* **70 map emitters:**
  * 40 point, 17 volume, 13 line, all looping map-bank cues at their authored volume/distances.
  * Volume emitters sound from the nearest point of their box (Radius × actor scale) to the listener, and on
    the listener when inside (the 7 AMB_* room-tone beds); line emitters from the nearest point of their segment [MED].
  * [PROV] voice budget: the 24 most audible play, the rest are virtual; 0.5 s fades in/out.
* **Cost:** mixer 0.7 ms per 21 ms block (1.1–1.4 ms with ~70 voices), about 0.2 ms per frame.
* **Diagnostic:** `WFC_AMBLOG=1` logs zone, emitters, voices, peak, gain reduction and mix cost.

### Occlusion [CONF parameters, MED trace geometry]
* **Parameters:**
  * Engine: `AudioDevice.bEnableOcclusion = true`, `OcclusionCheckInterval = 0.25 s` (Xe-TransEngine.ini).
  * Per cue: `SoundNodeRoot.EnableOcclusionVolume` defaults to true; only the BL_TRANSFORM cues author it off.
  * Per surface: `PhysicalMaterial.AudioOcclusionVolume = -6 dB`, `AudioOcclusionTransitionTime = 0.5 s`. That
    is the class default, used by 61 of 63 materials (one sets the same values explicitly, one 0 / 0);
    `AudioOcclusionPitch` is unset → no pitch change.
* **Implemented:**
  * Every occluding instance re-checks a listener → source line against the collision mesh every 0.25 s
    (staggered), and fades to -6 dB over 0.5 s.
  * New instances start at the current state.
  * The collision mesh carries no physical materials, so the default applies everywhere.
* **[MED] trace geometry:**
  * Attached (player-owned) sounds are tested against the pawn body (mesh origin + 1.5 m) rather than the
    socket. The gun / arm have no collision and the muzzle can poke into walls: testing the socket occluded
    26 % of shots while walking into walls, the body test 0.8 %.
  * The last 0.5 m at the source and 0.25 m at the listener are ignored (floor under the feet, an emitter's
    mounting surface).
* **Effect:**
  * Sustained fire standing: 2 of ~300 shots occluded; the weapon mix is unchanged in normal play.
  * About a third of the map emitter / pool instances are occluded behind walls at the spawn.

### Human-validation aid
With the debug overlay (B or `WFC_DEBUGDRAW=1`), every live sound source draws as a wire cube: green = pawn-attached,
yellow = weapon-attached, blue = world, red = occluded. `WFC_CUELOG` lines now carry the occlusion level.

### Pickup / objective FX — not implemented (request)
`map_fx.json` has 37 more authored particle components on pickup factories:
* `Pickup_FX` ×27 on ammo, health, flag, bomb and overshield factories;
* `HealthPickup_FX` ×9;
* `OvershieldPickup_FX` ×1.

They are not instantiated, because:
* the factories themselves are not in the rebuild (Gameplay owns pickups: placement, availability, respawn);
* their emitters with no material are mesh emitters whose TypeDataMesh / mesh is not in map_fx.json;
* several of their vector distributions are ambiguous between size and scale.

**AssetTools request:** TypeDataMesh (Mesh, bOverrideMaterial) and the per-emitter module class list, if any
survives, for FX_Pickups_p.FX.{Pickup_FX, HealthPickup_FX, OvershieldPickup_FX}.
**Gameplay request:** pickup factory actors from spawnpoints.json, with an availability flag the FX can follow.

### Robot movement / landing (pass-1 work kept; status)
* The landing cue already follows the authored data: LandingAnims by fall height (and horizontal speed) →
  FS_LAND_DEFAULT / FS_LAND_HARD / FS_LAND_HIGH_FALL + groan.
* Gameplay still plays Nav_Land for every landing and exposes no landing-clip state, so Systems evaluates the
  same authored table (handoff below).
* Footstep notifies: only the Strafers master fires, MinWeight-gated, with no idle/walk flicker duplicates.
* "Too loud / disconnected": the old placeholder thump (0.8 linear, linear rolloff) is gone. Footsteps now use
  authored SmartPan 75/150 UU, sit in the zone reverb and pass through the master compressor; no level was
  changed by ear.

### Vehicle sound bed (state map)
| State | Cues (authored) | Notes |
|---|---|---|
| Hover idle / movement | DRIVE_OFFLOAD (no throttle) / DRIVE_ONLOAD (throttle), speed-keyed pitch, 0.2 s EngineFadeOutTime | gear set MaxSpeed 20 / 110 share the cues |
| Normal boost | BOOST_START, BOOST_LOOP (speed), BOOST_END, BOOST_WHEELS after 0.27 s on ground, 0.15 s fade | engine yields to the boost loop [MED] |
| Hover dash | none authored (no dash clip / notify; BoosterSound trigger is native, undecoded) | not invented |
| Ram / nitro | RAM_NITRO_START + VEH_TRUCK_RAM_ALERT (one-shot, cut at nitro end) | RAM_IMPACT via notifyRamHit (world) |
| Jump / air | DRIVE_JUMP_START (AscendSound) + DRIVE_JUMP_LOOP (JumpRev) | |
| Landing | HOVER_ / WHEELS_LAND_LIGHT (>=0.15 s air) / _HEAVY (>=2.0 s) | |
| Transform | BL_TRANSFORM BOT2VEH / VEH2BOT attached; vehicle FX on at 1.8 s of the fold | |
| Tire squeal | VEH_OPTIMUS_TIRE_SQUEAL, parameter = slip angle 0..pi/2 rad (Max 1.57), full volume at 0.425 rad, pitch −1 → +2 st, 0.5 s crossfade, speed >= 20 | **hook only** (below) |
All vehicle loops are attached (AUDIO_ROOT) and move with the truck.

**Tire squeal — prepared, not invented.** Gameplay exposes no slip scalar. Heading vs horizontal velocity measured
in Systems reads 0.3–1.2 rad in straight-line Driving in the current model (velocity not aligned to yaw), so it is
not a usable slip signal. The squeal plays only when Gameplay calls `World::setTireSlipAngle(rad)` each step
(Driving, grounded, >= 20 mph). `WFC_TIRESLIP_DERIVED=1` enables the measured value for diagnostics only.

### Level effects
`LevelFx` reproduces the 8 authored Streets level emitters (`Emitter` actors with `FX_Level_Generic_p.FX.Steam_Sm_FX`,
map_fx.json):
* emitter "Smoke_Dup", Steam_Mat → SmokeBall_CLR translucent, emissive ×0.5;
* LOD0 stream values CONF, roles by the established order MED: spawn U[2,3]/s, life U[1,2] s, size U[6,10] m
  ×1→3, alpha 0→0.3→0, velocity ±(3,1,1) m/s, ±5 m along the emitter X, colour (0.9,0.9,1)→1;
* UE location/yaw → glTF as the other map records.
Verified in fixed-camera stills (`work/m3/steam_sheet.png`). Not reproduced (Rendering): the material's panned
SmokeTile UV distortion and depth-biased (soft) alpha.

### Vehicle FX event completeness
OptimusTruckForm authors exactly BoostFx (BoostSocket_L/R), HoverFX (6 HoverBooster_*), JumpFX (JumpBoostSocket_C/R/L)
and RamFX (RamSocket); TnCarForm / TnTruckForm / TnVehicleForm defaults add none (no dash or landing FX).
* **Driven:** Hover while hovering and from 1.8 s of the to-vehicle fold; Boost while Driving; Jump on take-off,
  killed when the vehicle form ends; Ram for the nitro.
* **Cleanup:** after transforming back to robot all vehicle parts drain to 0.

### Performance (final suite, RX 7900 XTX, WFC path)
* Idle 6.4–6.5 ms (ambient bed, reverb and level FX included).
* Short burst + reload 6.5 ms.
* Sustained fire: firing windows **9.6–13.6 ms** (drawFx 2.4–5.9 ms = Rendering's per-shell light environments).
* Boost / nitro / transform back 6.3–8.7 ms; transform cycling 5.5 ms; fire + move + turn 5.7–8.9 ms.
* ~900 RPM cadence unchanged.
* Leaks: cue instances settle at the ~25 ambient loops; pending events bounded; weapon particles → 0 after
  firing; vehicle parts → 0 after leaving the vehicle; level FX steady at ~30 particles.
* Collision: `work/segtest` 0 mismatches.
* Harness: wfc_fidelity 194/0/19/119/1, runtime probe 31/0/1.

---

## MILESTONE 03 SYSTEMS — AUDIO OWNERSHIP, ROBOT MOVEMENT SOUND, TRANSFORM AUDIO, FIRING COST (2026-10-02, agents/systems)

### Audio source ownership (systemic fix)
Every SoundCue instance now has an **owner**. `SoundCues::Emitter{pos, owner, offset}` with a World resolver:
`kWorld` (fixed position), `kOwnPawn` (pawn mesh origin + offset, e.g. the truck AUDIO_ROOT +147.25 UU),
`kOwnWeapon` (Ion Blaster mesh), `kOwnMuzzle` (MuzzleFlash socket). Attached instances re-resolve their
position **every tick for every voice**: one-shots, loops and *delayed wave events*, which now launch at the
owner's current position. Before this, one-shot voices were frozen where they started. That was the
"transform sound stays behind" defect, and it applied to every non-looping cue. One-shot instances now live
exactly as long as their voices (`IAudio::isPlaying`, default false; Win32 implements it), so a long attached
wave keeps following to its end.

| Source | Original mechanism | Owner now | Conf |
|---|---|---|---|
| Ion Blaster SHOOT / LOW_AMMO / SHOOT_TAIL | TnWeapon WP_Fire / WP_LoopingTail on the weapon | muzzle (attached) | HI |
| Reload / idle weapon notifies | HmAnimNotify_Sound on WEP_IonBlaster_ANIM (no socket) | weapon mesh | CONF class / HI attach |
| IMPT_WORLD / IMPT_DMG, VEH_TRUCK_RAM_IMPACT | impact at hit location | world | CONF |
| Transform BOT2VEH / VEH2BOT | HmAnimNotify_Sound on the Optimus transform clips (no SocketName, bStopWhenActorDestroyed) | pawn | CONF |
| Footsteps / scuffs / jump / landing / idle + pivot foley | AnimNotify_Footstep, HmAnimNotify_SoundEvent, HmAnimNotify_Sound on the robot clips | pawn | CONF |
| Fine aim START / END | TnWeaponIonBlaster WP_StartFineAim / WP_EndFineAim | weapon | CONF cue / HI attach |
| Vehicle boost / engine / jump / land / nitro / alert | HmPlayerVehicleAudioComponent on OptimusTruckForm | pawn @ AUDIO_ROOT | CONF |

**Per-cue SmartPan [CONF]:** `SoundNodeRoot.SmartPanDistance2D/3D` (class default 400/800 UU) is now read per
cue. The player previously used 200/400 UU for every cue. Footsteps author 75/150 UU and FS_LAND_HIGH_FALL
1000/1500, so close movement sounds are placed at the actor instead of mixed nearly centred. Vehicle cues are
200/1000; transform and idle foley use the default 400/800. **The Ion Blaster fire cues author exactly
200/400, so the weapon mix is unchanged.**

**Mixer categories [CONF, `SoundConfig.SoundMixerProperties`]:** 47 categories with DSP presets. Each cue's
`Category` is now emitted into the table (SFX_WET_COMBAT_ROBOT_WPN, SFX_WET_NAV, SFX_WET_COMBAT_TRANS,
SFX_WET_VEH*). Every category on these cues' paths has a `Default` preset volume of **1.0**. The exceptions are
**Master 0.708 (−3 dB)**, MUSIC_DRY 0.708 and SFX_SWORD_HUM 0.501, so the original mixer adds **no relative
category gain** between weapon, movement, transform and vehicle sounds. Reverb lives on `MASTER_WET` zone
presets (map Kismet zones): audited, **not implemented** (out of scope this pass). The rebuild's own master
level 0.5 [PROV] is not the original −3 dB; it is left unchanged to keep the weapon mix.

Still MED: `kSmartPan_PreferPlayer`. Pan and attenuation are measured from the camera listener; the original
may measure the local player's own sounds from the pawn. EnableOcclusionVolume/Pitch (on by default, off on the
transform cues) and Doppler are not modelled.

### Transformation audio [CONF]
* Robot→vehicle: `Transform_ToVehicle_ROBO` HmAnimNotify_Sound **BL_TRANSFORM.OPTIMUS_BOT2VEH @0.125 s of 2.0 s**.
  The cue is −4 dB, 1500–15000 UU, category SFX_WET_COMBAT_TRANS, and layers six wave events:
  servos 0.0, main 0.152, truck land thump (3 variants) 0.642, boost flare 1.440, air release 1.586, boost
  finish 1.674 s.
* Vehicle→robot: `Transform_ToRobot_ROBO` **BL_TRANSFORM.OPTIMUS_VEH2BOT @0.0** (MinWeight 0). The cue is −7 dB:
  servos and a truck light impact (3 variants) at 0.0, main at 0.141.
* Fired by fold progress (notify time / authored length), attached to the pawn. The generic
  `EVENT_IACON_BRIDGE_TRANSFORM_GEARS.wav` placeholder and `World::playSfx` are removed.
* Measured while moving: the delayed layers start at the pawn's current position (VEH2BOT main layer 0.15 s
  later at 358.3,−345.0 vs 360.0,−344.0 at the start). Runtime probe `transform_cue_is_authored`: KNOWN → PASS.
* Vehicle FX: `Transform_ToVehicle_VEH` TnAnimNotify_ToggleVehicleFx enables at **1.8 s** of the 2.0 s fold.
  Hover FX now start there instead of at fold completion; `Transform_ToRobot_VEH` disables them at 0.0.

### Robot movement sound [CONF data; MED where noted]
Chain: AnimNotify on the playing clip → `HmFootstepComponent` default type→event → `Optimus_ROBODEF.SoundEventSet
= SoundEvents.CHR_OPTIMUS` → `BL_FS_LRG_BOT.*` (large-robot footsteps).

| Event | Clip notifies (authored s / length) | Cue | Root dB, distance, SmartPan |
|---|---|---|---|
| run step (kFootstepRun) | Nav_StrafeJog_F 0.091 / 0.513 of 0.767 (B 0.194/0.543, L 0.137/0.523, R 0.132/0.529) | FS_RUN_DEFAULT | −8 (var −3), 1500–15000 rolloff 2, 75/150 |
| walk step (kFootstep) [MED: →WALK] | Nav_StrafeWalk_F 0.238 / 0.855 of 1.133 (B/L/R similar) | FS_WALK_DEFAULT | −12 (var −3) |
| scuff + steps + servo groan | Nav_IdlePivot90_L/R | FS_SCUFF_DEFAULT, FOLEY_FS_GROAN_SERVO_01 (−19) | |
| jump | Nav_TakeOff_01 FS_DEFAULT_JUMP @0 | FS_JUMP | −10 |
| land | Nav_Land kLand @0 | FS_LAND_DEFAULT | −8 |
| hard land | Nav_Land_02 kHardLand @0 | FS_LAND_HARD | −5 |
| high fall | Nav_Land_03 FS_DEFAULT_LAND_HIGH_FALL + kHardLand @0, groan @0.432, Long_Fall_Landing_1_FX | FS_LAND_HIGH_FALL (0 dB, SmartPan 1000/1500) + FS_LAND_HARD | |
| idle foley | Optimus NAV_Idle BL_FOLY_IDLES.OPTIMUS_IDLE @0 (15 events) | OPTIMUS_IDLE | −21, 650 UU |

* **Heavier-landing threshold exists [CONF]:** `TR_Acrobatics_p.SharedAcrobatics.LandingAnims` (MinHeight /
  MinSpeed UU) are {1200,1200} Nav_Land_03, {1000,1200} Nav_Land, {4500,0} Nav_Land_03, {500,0} Nav_Land_02
  and {250,0} Nav_Land. A fall under 250 UU plays no landing anim and so no landing sound.
  [MED] They are tested in array order on apex→touchdown height and horizontal speed. A standing jump
  (514 UU) → Nav_Land_02 → **FS_LAND_HARD**.
* **The old landing sound was not the original.** `WL_GUN_FOLEY/RELOAD_AIR_RELEASE_THUMP.wav` played at 0.8
  linear for every robot landing, with a linear 5–50 m rolloff. That is the "soft/squishy, too loud" sound.
  It is replaced by the authored cues above.
* [MED] Only the Strafers sync master fires notifies (AnimNodeSynch bFireSlaveNotifies default false), gated
  by the master weight vs MinWeight (default 0.25). Non-looping clip notifies are gated by the blend-in
  reaching MinWeight (Idle↔Moving 0.2 s, pivot 0.1 s), so a one-step idle flicker does not fire them.
* Runtime: jog 14 m/s gives two FS_RUN steps per 0.767 s cycle at phases 0.119 / 0.669.
* `Character` gained read-only `locoPhase()` / `locoMasterWeight()` (Gameplay file, additive).

### Vehicle sound bed
The three mechanics stay distinct. **Normal boost** uses BOOST_START / LOOP (speed parameter) / END / WHEELS
(0.27 s ground check). **Nitro** uses RAM_NITRO_START + VEH_TRUCK_RAM_ALERT. **Hover dash** has no authored cue:
no dash clip and no dash notify exist in Optimus_VEH_ANIM, and `BoosterSound` (VEH_OPTIMUS_RAM_BOOST_START, a
7 s cue with LoopStart 6.62 / LoopEnd 7.20) is not referenced in TransGame script, so its trigger is native and
undecoded. It is not assigned to the dash. The engine (ONLOAD / OFFLOAD / JUMP_LOOP, speed-keyed pitch) and
the land cues (hover vs wheels, 0.15 / 2.0 s) are unchanged; all vehicle cues are now attached at AUDIO_ROOT.
Correction to the M02 note: **VEH_TRUCK_RAM_ALERT's wave event is authored non-looping**. It plays once per
nitro and is cut if still sounding when the nitro ends; it never looped forever.

### Vehicle FX
Driven at the authored sockets as before (BoostSocket_L/R, 6 × HoverBooster_*, JumpBoostSocket_C/R/L,
RamSocket) from decoded templates. The only change is the ToggleVehicleFx timing above.
Not reconstructed (authored, but not reachable or not on Optimus):
* Nav_Land_03 `FX_Navigation_p.Long_Fall_Landing_1_FX` @BoosterSocket_R (high falls only).
* Robot dodge `DashPulse_1_FX` (dodge unreachable, Gameplay).
* `Trails_Bumblebee_FX` (sockets not on the Optimus mesh).
The "crude" look of the hover/boost rings is material/blend treatment → Rendering handoff.

### Firing performance [measured, RX 7900 XTX, WFC path, sustained auto-fire]
| | avg frame | Systems hitscan | controller (incl. Gameplay camera ray) | drawFx |
|---|---|---|---|---|
| before (milestone-02 head) | **65–70 ms** (peaks 70) | 21–26 ms | 32–52 ms | 3.5 ms |
| after | **10–11 ms** (idle 6.4–7.1) | 0.02 ms | 0.02–0.06 ms | 3.5–4.9 ms |

* Root cause: `CollisionWorld::segmentHit` tested **every grid cell of the segment's XZ bounding box**. The
  300 m weapon trace and Gameplay's per-shot camera ray each scanned thousands of 2 m cells per call.
* It now walks only the cells the ray crosses (2D DDA, clipped to the grid) and stops at the first cell whose
  exit lies beyond the nearest hit. It is exact against a brute-force all-triangle reference: 20,000 random,
  vertical and axis-aligned segments, 0 mismatches (`work/segtest`).
* The renderer's light-visibility callback no longer marches 2 m pieces.
* Cadence untouched: ~900 RPM, one-shot timer.
* No leaks: particles, mesh parts, cue instances and pending events all drain to 0 after firing (4000-frame run).
* Remaining firing cost is Rendering's: each shell/magazine mesh particle gets its own dynamic light
  environment (computeEnv with visibility traces against 268 lights), ~0.25 ms per mesh part, 3–5 ms with 15
  live. CPU skinning in drawPlayer is 3.2 ms.

---

## PASS 9 — CHARACTER CUSTOMIZATION (2026-10-01, branch agents/rendering)
| Item | Original (WFC) | Source | Conf | Rebuild |
|---|---|---|---|---|
| Applier parameters | Cust_Color_A, Cust_COLOR_B, EnergonColor on robot, vehicle, arm, weapon | Default__TnCharacterApplier; native RE (user-supplied) | CONF | runtime uniforms in CHR + WEP MICs |
| Zero semantics | all-zero character colour skips the override; authored value stands | native RE | CONF | **APPLIED** (RGB all zero = skip) |
| Colour set | faction selects the set; no separate team override in this path | native RE | CONF | caller passes the faction's set |
| Optimus data | PrimaryColors/SecondaryColors (0,0,0,255) per faction, EnergonColor (0,0,0,1) | OptimusPrime_PCD_SP / Leader_PCD_MP | CONF | all skipped → MIC values (Cust_A/B per MIC, EnergonColor (1.25,0.05,0.05) compiled Autobot branch) |
| Parameter reads | robot+vehicle MICs read all three; WEP_IonBlaster_MATINST reads Cust_Color_A + EnergonColor; InteriorAlt reads none | compiled MIC uniform expressions | CONF | matches |
| Arm mesh | CP_OptimusArm_SKEL used when no weapon is drawn (early vehicle→robot transform, holster, melee); slots Cust_Mat_INST_B + InteriorAlt | native RE; materials_authored.json | CONF | renders with original MICs (gameplay drives visibility) |
| MP robot mesh | RB_OptimusWeaponArm_SKEL (forearm removed; slot order differs from the slice's campaign RB_Optimus_A_SKELMESH) | materials_authored.json | CONF | renderer supports it; mesh choice is gameplay/asset ownership |

## PASS 8 — VEHICLE MATERIAL + MAP COMPOSITION (2026-10-01, branch agents/rendering)
| Item | Original (WFC) | Source | Conf | Rebuild |
|---|---|---|---|---|
| Vehicle normal map | `RB_OptimusPrime_Cust2_Mat_INST` `UseReconstructedNormal`=True: X=A, Y=G of DXT5 `VH_Optimus_NORM`, both `*2-1`, Z=sqrt(1-x²-y²) | MIC static params; BASE ShaderCache character PS (`tfetch .yw; mad r.xy, r.zy, 2.0, -1.0`) | CONF | **FIXED** (alpha was left in [0,1]) |
| Vehicle diffuse/customization | CHR master with Cust_Color_A/B, ColorBrightness 0.6, etc. | MIC params | CONF | verified = AssetTools bake |
| EnergonColor (robot+vehicle) | 3 same-named VectorParameters; compiled permutation binds (1.25,0.05,0.05) | MIC compiled uniform expressions | CONF | as compiled (team override at runtime not modelled) |
| Persistent level | `MP_IAC_Streets_Base_m` streams ART + AUDIO only | TransLevels.ini MapFilename; LevelStreamingKismet | CONF | composition complete |
| Post-process | TnWorldInfo: Bloom_Scale 0.1, bEnableDOF, DOF_MaxFarBlurAmount 0.6, FocusFarFalloff 40000, ColorCorrectionTexture MP_Streets_CLUT | BASE TnWorldInfo over Default__WorldInfo | CONF | **APPLIED** (replaces PASS 7 defaults) |
| CLUT | 32³ A8R8G8B8 Texture3D, SRGB=False (Default__Texture3D), applied after gamma: tex3D(c*(N-1)/N+0.5/N) | Xbox 3D tiling (xenia Tiled3D); uber+CLUT PS | CONF | **APPLIED**; strong authored desaturation |
| DOF | gather a' from avg depth; resolve ((1-a)scene+blur.rgb)/((1-a)+blur.a); PackedParameters (Focus, 1/NearFalloff, Exponent, 1/FarFalloff) | decoded PS | CONF shader / MED parameter packing | **APPLIED** |
| Static decals | 25 DecalComponents with cooked receiver geometry (28-byte verts, u16 indices); BSP receivers clipped, static meshes keep whole triangles; UV = 0.5 - M·(P-L) + offset, rows HitTangent·TileX/Width, HitBinormal·TileY/Height | DecalComponent native; decal VS | CONF geometry+UV / MED box clip of static-mesh receivers | **APPLIED** |
| Section materials | umodel glTF section names → original MIC paths (15); 6 null (`dummy_material_N`) | StaticMesh sections | HI | **APPLIED** |
| Prefabs | 16 PrefabInstances → 34 placed StaticMeshActors (already composed) + 1 SteamVent emitter | ArchetypeToInstanceMap | CONF | complete |
| Emitters | 8 `FX_Level_Generic_p.FX.Steam_Sm_FX` + prefab steam vent | ART actors | CONF | **NOT RENDERED** (particle FX) |
| Power-tube "flat planes" | PowerTubeCircle emissive panels (mask R, lavender ×(1+pulse)) | MIC/mask | CONF | authored; renders as designed |

## PASS 7 — ORIGINAL RENDER PATH (2026-10-01, branch agents/rendering)
Specification sources: the ORIGINAL compiled Xenon shader microcode in the cooked `ShaderCache`
exports (disassembled with `tools/render/xenos_dis.py`, bit layouts per xenia's public `ucode.h`),
the cooked material expression graphs, and cooked lighting objects. No values tuned by eye.

| Stage | Original (WFC) | Source | Conf | Rebuild |
|---|---|---|---|---|
| Base-pass lighting policy | `FDirectionalTextureLightMapPolicy` (also Vertex/Interpolated variants) | ShaderCache type names, ART pkg | CONF | **APPLIED** |
| Directional lightmap decode | `L = Σ_i dot(N_t,B_i)² · tex_i.rgb · ScaleVector_i`; B0=(0,√(2/3),1/√3) B1=(−1/√2,−1/√6,1/√3) B2=(1/√2,−1/√6,1/√3); no-normal materials use ×1/3 (=(1/√3)²) | base-pass PS microcode + literal pools | CONF | **APPLIED** (3 coefficients) |
| Lightmap colour space | `LightMapTexture2D` inherits `SRGB=True` (Default__Texture); no instance override | Engine.xxx CDOs + texture tags | CONF | **APPLIED** (GL sRGB) |
| Lightmap resolution | atlases cooked at 256² (Xenon `MaxLODSize=512`, top mips absent in dump) | `_LM` pkg sizes, Xe-TransEngine.ini | CONF | as cooked |
| Base-pass output | `Emissive + Diffuse·L·ShadowMask`, ×SceneColorBiasFactor; **no lightmap specular** | base-pass PS | CONF | **APPLIED** (ShadowMask=1) |
| What is baked | Beast bake (`FBeastAreaLightPolicy`); dominant red directional (Brightness 6) LightmapGuid present in 1932 components; no ShadowMap2D textures | component native LightGuids | CONF | data used as-is |
| BSP lightmaps | per ModelComponent element `FLightMap2D`; per-vertex ShadowTexCoord in cooked `FModelVertexBuffer` (36 B verts, indexed by `FBspNode.iVertexIndex`, 889/889 nodes verified) | Model + ModelComponent native layout (decoded) | CONF | **APPLIED** (`bsp.glb`) |
| Materials | UE3 graphs + WFC MaterialFunctions (ENV_*_MF, CHR_*), static switches & TextureSets (Color_NormX/Masks_NormY, alpha UnpackMin −1) per MIC; WFC HLSL ShaderCode | cooked Material/MIC/TextureSet exports (Streets BASE+ART) | CONF | **APPLIED** 168/169 (offline GLSL translation) |
| Old extraction error | `materials.json` mapped decal `Rust_C_CLR` as base colour; real diffuse lived in TextureSets | MIC TextureSetParameterValues | CONF | **FIXED** |
| Cubemaps / flipbooks | cooked Xbox-tiled DXT TextureCube (6 faces) / TextureFlipBook | native tails (decoded) | CONF | **APPLIED** (`xbox_texture.py`) |
| Dynamic-object lighting | WFC UberLight: `Diff·AmbientCube(N)` + per light `(Diff·sat(N·L·0.6778+0.3333)² + Spec·pow(sat(R·L),SpecPower))·Color·sat(1−(d/R)²)^Falloff·spot²·S` | uber-light PS microcode | CONF | **APPLIED** |
| Light environment | WFC `LightEnvironmentComponent`; TnRobotForm/TnVehicleForm TotalLightCount **2**, UpdateDistanceThreshold **30 UU**; dynamic-only SkyLight (Brightness 0.1, LowerBrightness 0) + dynamic fill directional | TransGame.xxx CDOs, ART lights | CONF | **APPLIED**; composition of non-direct lights into the ambient cube = MED |
| Light visibility | `LightsVisibilitiesVolume` (745 KB precomputed per-cell light visibility, leaf records `(lightIdx u16, vis u16)`) | ART native | PARTIAL | **NOT USED**; collision raycasts instead (PROV) |
| Height fog | UE3 4-layer vertex fog: `exp2(FogDistanceScale·max(d−Start,0)·layerFrac)`, cut at ExtinctionDistance; authored Height −57273.8, Density 2e-5, Start 2048, LightColor (234,91,116) × **LightBrightness 0.1** (class default) | fog VS microcode + HeightFogComponent | CONF shader / MED constants (`−Density/ln2`) | **APPLIED** (replaces softened GL_EXP pink fog) |
| Post (superseded by PASS 8: map TnWorldInfo overrides) | bloom gather (4 taps clamp [0,4], tap kept if any channel > Threshold, ×0.25·Scale) + blur; UberPostProcess tone; no PPVolumes, persistent level stub ⇒ WorldInfo defaults (Bloom 1/1, Shadows 0, HighLights 1, MidTones 1, Desat 0, identity CLUT); DisplayGamma 2.2 | DOFAndBloomGather / UberPostProcess PS microcode, Default__WorldInfo, Xe-TransEngine.ini | CONF (blur kernel PROV) | **APPLIED** |
| Character materials | `CHR_Transformer_NormSpec_Cust_E_Mat` with MIC customization, spec-power S-curve, energon pulse, metal cubemap | Streets BASE | CONF | **APPLIED** (replaces provisional GL_LIGHT0) |

**Remaining gaps:** LightsVisibilitiesVolume decode; 24 `FLightMap1D` vertex-lightmapped props;
dynamic shadow mask (characters do not cast/receive dynamic shadows); DirectLightAmbientContribution;
`ENV_ORB_D1_Debris_p SideSupport_MATINST` (master not in Streets packages); `MotionBlurEffect` in
DefaultScenePostProcess not implemented; light-env transition blending (0.5 s) not implemented.

---

## PASS 12 — RECONCILED WITH NATIVE RE (2026-10-01, gameplay agent)

| Behaviour | Original (WFC) | Source | Conf | Rebuild status |
|---|---|---|---|---|
| Vehicle→robot entry | Local player keeps full velocity and enters falling | native RE | CONF | **APPLIED** (replaces Pass 11 ground snap) |
| Robot→vehicle entry | Velocity clamped 3500 UU/s written into the RB, no reprojection; starts at robot yaw | native RE + OnActivate bytecode | CONF | **APPLIED** |
| Hover steering authority | Fades in over 0.5 s: accel × (1 − Drift/0.5)², Drift started by Hovering.BeginState | native RE + CalculateDriftScale/Drift bytecode | CONF | **APPLIED** (also after leaving Driving) |
| Hover control frame | View yaw: Hovering.DoUpdate → HoverSimulation.Update(..., ViewRotation.Yaw); UpdateTurn matches the yaw; UpdateStrafe toward (fwd, right) × 15 m/s, ClampLength accel 30 m/s² × drift × stability | bytecode | CONF | **APPLIED** (supersedes Pass 7 travel-direction steering; stability scale = 1 on flat ground) |
| Normal boost | Boost held → TnCarForm.Driving (Truck_Physics MaxSpeed 3000, MaxAcceleration 2500); not while drifting; release → Hovering | Hovering/Driving.UpdateBoosting bytecode; Truck_Physics | CONF (state, values) / PROV (steering, throttle) | **APPLIED** with wheels pose (HoverToBoost → Idle_Wheels; BoostToHover on exit) |
| Hover dash | Dash while hovering: dominant input axis (local), DashSpeed 3000, 0.5 s, accel 100000; cooldown TimeBetweenDashes 2.0 | Hovering.DoDash/UpdateDashing, UpdateDash, get_TimeBetweenDashes | CONF | **APPLIED** |
| Nitro / ram | Dash while driving: 3 s, speed ×1.5, steering ×0.3, cooldown 8 s, stop on leaving Driving | Systems checkpoint (script literals) | CONF | speed/steering **APPLIED**; ram collision not implemented; state duplicated with Systems VehicleNitro (unify at integration) |
| Dash binding | VehicleSpecialMove: PC Shift ("Ability0 \| VehicleSpecialMove"), pad RightShoulder → PlayerInCarForm.StartVehicleSpecialMove → set_DashingInput | bindings + bytecode | CONF | **APPLIED** (Systems' temporary Q superseded) |
| Input latching | Fire held flag persists; reload on release of a tap < 0.3 s; jump/dash edge latched until consumed; transform immediate | native RE | CONF | **APPLIED** |
| Weapon restore | _RestoreWeaponTransformFractionRemaining 0.75 = restore at 25% elapsed (vehicle→robot); usable after EquipTime 0.2 s | native RE + ini | CONF | **APPLIED** (usable at 0.48 s of 1.13 s fold); visible gun waits for the 50% mesh handoff [PROV] |
| Fine aim on transform | Transforming to vehicle ends it | native RE | CONF | **APPLIED** (wish cleared) |
| Locomotion play rate | Walk/jog at 1.0×; the original shows the same stride mismatch | native RE | CONF | no compensation (as built) |

---

## PASS 11 — TRANSFORM MOMENTUM, ROBOT SPEED, FINE AIM (2026-10-01, gameplay agent)

Evidence: UnrealScript bytecode decoded from `TransGame.xxx` with `work/pass11/ue3dis.py` (validated on
`TnPawn.UpdateSpeeds`), shipped bindings (`Xe-TransInput.ini`), character/vehicle/camera content
objects (`Optimus_ROBODEF`, `HoverTruck_Physics`, `TruckTransformerMomentum`, `OverTheShoulder_STRATEGY`).

| Behaviour | Original (WFC) | Source | Conf | Rebuild status |
|---|---|---|---|---|
| Velocity at transform start | Untouched. `TnPawn.Transform` → `Transforming.BeginTransformation` → `TnTransformation.Execute` never zero velocity | bytecode | CONF | **FIXED** (rebuild zeroed velocity in beginTransform) |
| Input during a fold | Movement keeps working; only `IsAbleToFire`, `PreWeaponSwitch`, `StartTransform` check IsTransforming | bytecode (all 20 get_IsTransforming call sites) | CONF | **FIXED** (rebuild locked all input); fire/reload stay blocked, jump blocked [PROV] |
| Movement form switch | `BeginTransformation`: _CurrentForm = TargetForm + target movement capabilities at fold START | bytecode | CONF | **APPLIED** (moveForm(); mesh still hands off mid-fold) |
| Robot→vehicle handoff | `TnVehicleForm.OnActivate`: Velocity = ClampLength(pawn Velocity, 3500); RB rotation = pawn rotation | bytecode; `kMaxTransformSpeed` 3500 | CONF | **APPLIED** |
| Vehicle→robot handoff | `TnRobotForm.OnActivate` leaves velocity; robot `CalcVelocity` continues it | bytecode | CONF | **APPLIED** |
| Momentum preservation | Above max speed: MaxAccel = AccelRate/(1+P); P from TruckTransformerMomentum OnGround 7/3/1, InAir 100/100/5 (fwd/neutral/back; fwd = input within 30° of velocity); InAir set while falling OR transforming | `CalculateMomentumPreservation`, `CalculateMaxAcceleration` bytecode | CONF | **APPLIED** |
| Robot velocity model | Desired = Normal(input)·max(450, ‖input‖·MaxSpeed); no input → 0 (ground) / keep (air); local-frame per-axis accel clamp; no speed cap | `TnPawn.CalcVelocity` / `CalculateDesiredFlatVelocity` bytecode | CONF | **APPLIED** (AirControl application PROV) |
| Character movement values | BaseGroundSpeed 1400, AccelRate 12000, AirSpeed 1200, AirControl 0.4, TerminalVelocity 6000, Collision r200/h200, JumpHeight 500 (SharedAcrobatics) | `Optimus_ROBODEF` via `TnPawn.ApplyTransformer` bytecode; identical on 6 playable ROBODEFs | CONF | **APPLIED** (replaces Passes 2/10 class defaults) |
| Robot fast movement | No sprint key exists. Full input = 14 m/s jog (the "missing fast movement"); partial pad input ≥4.5 m/s walk; vehicle→robot keeps vehicle/boost speed (momentum run, ≤35 m/s handoff) | bindings + above | CONF | **APPLIED** |
| Boost input | RightMouseButton / LeftTrigger are bound to both FineAim and Boost; robot OnStartBoost empty; vehicle states boost | `Xe-TransInput.ini`, bytecode | CONF | **APPLIED** (RMB/LT boost in vehicle; Shift kept as alias) |
| Truck physics | DashSpeed 3000, DashDuration 0.5, SuspensionRadius 185 (MaxLinearSpeed default 1500) | `VEH_SHARED_p.HoverTruck_Physics` | CONF | **APPLIED** |
| Fine aim input | PC RMB = ToggleFineAim; pad LT = FineAim (hold) | `Xe-TransInput.ini` | CONF | **APPLIED** |
| Fine aim rules | Robot only (PlayerWalking); blocked while meleeing/reloading/dodging; wants persist → resumes after reload | `TnFineAimManager.Tick/Start/Stop`, `PlayerWalking.CanFineAim` bytecode | CONF | **APPLIED** (also blocked while transforming [PROV]) |
| Fine aim movement | SetSpeedMultiplier(0.5) → 7 m/s ground (air too) | `_GroundSpeedMultiplier`, UpdateSpeeds | CONF | **APPLIED** (excess speed bleeds through momentum preservation) |
| Fine aim camera | FOV 80→45 (SmoothTime 0.1), exit 0.4; look speed 25/12.5 vs 50/25; orbit distance unchanged for Ion Blaster; shoulder curve unchanged | `OverTheShoulder_STRATEGY` behaviours | CONF values / PROV smoothing curve | **APPLIED** |
| Fine aim weapon | Spread × FineAimSpreadModifier 0.5; weapon events 15/16 (sounds) | `TnWeapon.Start/StopFineAim`, WEPDATA | CONF | spread **APPLIED**; sounds not wired |
| Fine aim upper body | No fine-aim pose (WeaponPose slot plays weapon PoseName only) | `TnWeaponPoseSlotLogic` bytecode | HI | none (aim offset continues) |
| Fine aim target snap | TnOrbitRotateToTarget FineAim: snap, speed 10; _TargetSnapTimeout 1.0 | strategy + ini | CONF | NOT implemented (no target system) |
| Robot camera | Anchor actor+200 UU; orbit 800; DefaultFOV 80; pitch ±75; shoulder offset [150,300,150] by pitch, SmoothTime 0.3; third-person collision | `OverTheShoulder_STRATEGY` | CONF values / PROV offset semantics + collision rules | **APPLIED**; traces aim through the crosshair point |
| Support/step query | groundHeight(nearY = body, stepUp = 35 UU) | — | fix | **FIXED** (pre-existing double count: robots stepped 0.7 m, vehicles could snap to decks 4.4 m higher) |

---

## PASS 10 — LOCOMOTION BLEND, GROUND SPEED, STEP HEIGHT (2026-10-01, gameplay agent)

| Behaviour | Original (WFC) | Source | Conf | Rebuild status |
|---|---|---|---|---|
| Moving-state structure | TnVelocityAnimBlend(Walking, Jogging) over TnStraferAnimBlend(F,B,R,L) of `Nav_Strafe{Walk,Jog}_*` (OneHanded) | Robot_ANIMTREE | CONF | **APPLIED** |
| Walk→jog speeds | MinSpeed 450 / MaxSpeed 1200 UU/s (class default 100/1000 overridden) | `TnVelocityAnimBlend_10449` | CONF | **APPLIED** (linear weight; PROV formula) |
| Clip ground speeds | walk ≈3.5 m/s, jog ≈12.1 m/s (planted-toe stance speed) | robot.glb measurement (`work/pass8/stride.js`) | HI | Explains 1200 = jog speed |
| Direction weights | from local velocity; `_BlendSpeed` 0.2 | `Default__TnStraferAnimBlend` | CONF (value) / PROV (max(0,±dot) normalized, eased) | **APPLIED** |
| Phase sync | "Strafers" AnimNodeSynch group (all 16 strafe sequences, RateScale 1) | `AnimNodeSynch_581` | CONF | **APPLIED** (shared phase, master = highest weight) |
| Idle↔Moving transition | AmpCrossFadeCondition 0.2 s | IdleToMoving / MovingToIdle | CONF | **APPLIED** (was PROV 0.15) |
| Robot ground speed | GroundSpeed = _BaseGroundSpeed (=GroundSpeed 550 at PostBeginPlay) × Π speed multipliers; Ion Blaster GroundSpeedMultiplier 1.0 | TnPawn.PostBeginPlay / UpdateSpeeds bytecode; Default__TnWeaponData | CONF | **SUPERSEDED (Pass 11):** ApplyTransformer later calls set_BaseGroundSpeed(ROBODEF.BaseGroundSpeed = 1400) → 14 m/s |
| MaxStepHeight | 35 UU = 0.35 m (WalkableFloorZ 0.7, MaxFallHeight 3400) | `Default__TnRobotForm._MovementCapabilities` | CONF | **APPLIED** step 0.35 m (was PROV 0.6); deterministic A/B shows no new snagging. WalkableFloorZ not yet enforced. |
| Idle clip names | Tree defaults `NAV_Idle_01` / `AI_Nav_Idle_Pose_05` / `ADD_NAV_Idle` resolved per character by choosers | Robot_ANIMTREE idle branch | — | Optimus set lacks them; `NAV_Idle` kept |

---

## PASS 9 — AUTHORED AIM OFFSET PROFILE (2026-10-01, gameplay agent)

| Behaviour | Original (WFC) | Source | Conf | Rebuild status |
|---|---|---|---|---|
| Active profile | `Default` for the Ion Blaster (`ActiveProfileProperty`=WeaponTypeObserved; other profiles are TwoHandedMelee, TwoHandedMeleeCharge, TwoHandedGun) | Robot_ANIMTREE `TnAnimNodeAimOffset_14979` | HI | **APPLIED** |
| Profile bones | Lumbar01, Lumbar02, Neck01, Head, L Clav/Shoulder/Elbow/Hand, R Clav/Shoulder/Elbow | `AimComponents[].BoneName` | CONF | **APPLIED** (replaces the whole-upper-body local deltas of Passes 7–8) |
| Cell offsets | 9 rotations (+ translations on R_Arm01_Clav) per bone, baked from `Shooting_Aim_{L,F,R}_{D,C,U}` (`AnimName_*`) | `AimComponents` | CONF | **APPLIED**, baked at load by the verified rule: rot = Gp·(L_cell·L_C⁻¹)·Gp⁻¹, pos = Gp·(t_cell−t_C), Gp = parent model rotation in that cell. Matches the shipped quaternions to 0.01° mean / 0.07° worst and translations to 0 mm (UE→glTF: q `(−x,−z,−y,w)`, v `(x,z,y)·0.01`). |
| Application | Model-space rotation/translation about each bone's pivot, parent first; bilinear between cells | UE3 AnimNodeAimOffset (consistent with the bake: reproduces every cell pose from the centre pose) | HI | **APPLIED** (slerp per axis, weight = aimW). |
| Ranges | Profile H [−1,1], V [−1,0.8]; `RemapPawnAimRange` from PawnAimOffsetRange H [−1,0.85], V [−0.7,1]; pawn aim = angle/90° | Default profile | CONF (values) / PROV (centre-preserving remap) | **APPLIED**. Barrel pitch at aim −69/−34/0/+34/+69° = −42/−21/+3/+27/+50°. Replaces the Pass 7 barrel-pitch calibration. |
| Pivot arm swing | `Nav_IdlePivot90_*` swings the gun forearm ≈50° mid-step (root-discarded chest ±7°) | clip data; tree layers the aim offset over the turn unchanged | HI | Kept as authored. |

---

## PASS 8 — RECOIL, TURN IN PLACE, SHIPPED ANIM TREE (2026-10-01, gameplay agent)

Primary evidence: `TR_Shared_ANIMTREE_p.Robot_ANIMTREE` (cooked in `MP_IAC_Streets_BASE_m.xxx`)
plus class defaults in `TransGame.xxx` / `HM_Engine.xxx`, read with AssetTools `ue3pkg`/`props`
(read-only; dumps in `work/pass8/`).

| Behaviour | Original (WFC) | Source | Conf | Rebuild status |
|---|---|---|---|---|
| Upper-body blend root | `AnimNodeBlendMultiBone` start bone `C_Spine01_Lumbar01_XB` (plus a left-arm branch at `L_Arm01_Clav_XB`) | Robot_ANIMTREE | CONF | Matches the Pass 7 mask root (PROV → **CONF**). |
| Recoil bones | RightHandRecoil → `R_Arm02_Shoulder_XB`; LeftHandRecoil → `L_Arm01_Clav_XB`; SpineRecoil → `C_Spine02_Lumbar02_XB` | Robot_ANIMTREE `SkelControlLists` | CONF | **APPLIED** (spine + right hand; the Ion Blaster defines no left-hand recoil). |
| Recoil values | Spine: 0.8 s, RotAmp [500,1000,0], Freq [10,10,0], all ERS_Zero. Hand: 0.5 s, RotAmp [2000,500,−2000], Freq [15,10,10], Yaw ERS_Random, LocAmp X −8 UU @10 | `Default__TnWeaponMesh` archetype + IonBlaster_WEPMESH overrides; struct default `Default__HmSkelControlRecoil` (0.33 s, zeros) | CONF | **APPLIED** (`game/Recoil.h`), restarted per shot (`TnRecoiler`). |
| Recoil update law | sin-wave per axis × smoothstep(TimeToGo/Duration), mesh-space in the aim frame (`bBoneSpaceRecoil` false) | UE3 GameSkelCtrl_Recoil (reconstructed; native not decompiled) | MED | **APPLIED**. UE rotator → model-axis mapping (yaw/roll sign) PROV. Measured: +12° muzzle climb under sustained fire. |
| Turn-in-place values | Threshold 4096 UU (22.5°), TransitionBlendTime 0.1 s, PercentageToAllowAbort 0.5, RotTransitions ±16384/±32768; player `RRO_Discard` | `Default__TnAnimTurnInPlace`, `Default__TnAnimTurnInPlacePlayer` | CONF | **APPLIED**. |
| Turn clips | Rt_90/Rt_180 → `Nav_IdlePivot90_R`, Lt_90/Lt_180 → `Nav_IdlePivot90_L` (WS_ONE_HANDED) | TnWeaponAnimChooser in Robot_ANIMTREE | CONF | **APPLIED**. Clips author ~81° on `C_Root_Reference_XR` over 0.53 s. |
| Turn trigger / unwind | Inferred: legs planted (UnwindLowerBody), trigger when \|offset\| ≥ RotationOffset − threshold, unwind RotationOffset along the clip's root-yaw curve | Field names + values; native logic not recovered | PROV | **APPLIED**. Chosen over "trigger at 22.5° and unwind the offset" because it keeps the feet consistent with the authored step. |
| Aim offset yaw columns | Leg/aim difference feeds the aim offset horizontally (`TnAnimNodeAimOffset.TurnInPlaceOffset`) | TransGame class layout | HI | **APPLIED** (Pass 9: through the authored profile ranges). |
| Aim offset interpolation | InterpSpeed 12 | `Default__TnAnimNodeAimOffset` | CONF | **APPLIED** (FInterpTo-style). |
| Aim offset authored profile | `TnAnimNodeAimOffset` "Default" profile | Robot_ANIMTREE | CONF | **APPLIED in Pass 9** (see below). |
| Incoming transform clip | Matched pair `Transform_ToVehicle_ROBO` ↔ `Transform_ToVehicle_VEH` | clip durations (1.967 s both) | HI | **FIXED** (was SuperBoost_Veh, 0.8 s). |

---

## PASS 7 — ANIMATION LAYERS & MESH FACING (2026-10-01, gameplay agent)

| Behaviour | Original (WFC) | Source | Conf | Rebuild status |
|---|---|---|---|---|
| Mesh forward axis | Skeletal meshes face model **+X** (UE convention; umodel `(x,z,y)` keeps X). | `Shooting_Aim_F_C` barrel dir in model space = (1.00,−0.06,−0.02) | CONF | **FIXED** `kMeshYawOffset=+π/2` (Pass 6's 0 left robot/truck side-on, gun 90° off the reticle). Verified: chase cam sees back/rear; muzzle along aim yaw. |
| Upper-body aim offset | `Shooting_Aim_*` 9-pose grid points the gun at the aim. | `robot.glb` category `aim`; weapon.json `owner_animations.aim` | HI | **APPLIED**: F column (body faces aim yaw → yaw offset 0), delta from F_C on `C_Spine01_Lumbar01_XB` subtree, driven by controller pitch. |
| Aim pitch → grid mapping (superseded by Pass 9 authored ranges) | UE3 AimOffset normalized pitch range | Calibrated from the poses: barrel pitch D −47.6° / C −3.3° / U +72.2° | CONF-derived | **APPLIED**: piecewise-linear so barrel pitch ≈ aim pitch (measured −27/+7/+42° at −34/0/+34°). |
| Reload while moving | Owner reload clip over locomotion | weapon.json `owner_animations.reload_robot` = `Shooting_Reload_IonBlaster_ROBO` | HI (clip) / PROV (mask) | **APPLIED**: upper-body slot (mask root `C_Spine01_Lumbar01_XB` PROV), full body when standing, mesh-space per-bone blend (UE3 `AnimNodeBlendPerBone` default). |
| Slot / aim / additive ease time | — | — | GUESS | `kSlotBlend=0.15 s` **PROVISIONAL**. |
| Vehicle move animation | Directional single-frame hover poses + additive hover bob | `vehicle.glb` `Nav_Hover_{Pose,F,B,L,R}_VEH`, `ADD_Nav_Hover_VEH` (additive=true) | HI (clips) / PROV (weights) | **FIXED** (was looping the one-shot `Nav_BoostToHover_VEH` transition). Weights = local velocity / MaxLinearSpeed per axis. |
| Vehicle turn rate | **~π rad/s** | `TnHoverCarSimulationBlueprint.AiMaxAngularSpeed` | CONF | **SUPERSEDED (Pass 11):** AiMaxAngularSpeed is AI-only (truck: 20); player steering rate PROV |
| Robot gameplay idle | Character-specific nav idle | `NAV_Idle` from `Optimus_ROBO_ANIM`; `Cust_Idle` = customization screen | MED | **FIXED** (was `Cust_Idle`, first of category). |
| Jump / land | Take-off once → descent loop → land | `Nav_TakeOff_01`, `Nav_Jump_Descent`, `Nav_Land` | MED | **APPLIED**: take-off non-looping; land after ≥0.3 s airborne (`kLandMinAirTime` PROV), skipped when moving. |
| Weapon recoil | see Pass 8 | weapon.json + archetype | CONF | **APPLIED in Pass 8**. |
| Turn in place | see Pass 8 | Robot_ANIMTREE | CONF | **APPLIED in Pass 8**. |

Additive-clip convention: `ADD_*` clips are stored as **deltas** (identity-ish quats, zero
translations at rest), so `samplePose(additive=true)` starts from identity and `addPose` composes
`base * delta` in bone-local space.

---

## PASS 7 — WEAPON LAYERING, RECOIL, WEAPON MESH, FX, SOUNDCUES (Systems agent, 2026-10-01)

> **Integration note (integration/milestone-01):** the recovered data in this pass is kept as
> provenance. In the merged build, the *Reload layering*, *Recoil controls/defs/evaluation* and
> *Upper-body aim offset* (PASS 7b) rows are implemented by **Gameplay's** code (Passes 7–9), not
> by the Systems implementation described here. Both branches recovered the same Robot_ANIMTREE /
> RecoilDef values. The Systems `AimOffset.h`, `updateUpperBody`/`evalLayered` and shot-serial
> recoil trigger were not merged, to avoid a second aim offset and double recoil per shot.
> Remaining value difference: Systems reads the owner-anim slot blend as **0.1/0.1 s [CONF]**
> (`TnWeaponOwnerAnimator` CDO), while Gameplay's reload slot uses `kSlotBlend` 0.15 s [PROV].
> This is left for the Gameplay owner to reconcile. The weapon mesh, sockets, notifies, FX, SoundCues
> and audio rows below are all live in the merged build.
Recovered from cooked packages with AssetTools `objtree`/`typed_props` (read-only); decoders in
`tools/systems/`. Owner animation, recoil, weapon-mesh, FX and cue data come from
`WEP_IonBlaster_p` / `FX_*_p` / `BL_WPN_*` exports cooked into `A1_IAC_Base_m` / `MP_IAC_Streets_BASE_m`,
plus class defaults (CDOs) in `TransGame.xxx` / `HM_Engine.xxx`.

| Behaviour | Original (WFC) | Source | Conf | Rebuild status |
|---|---|---|---|---|
| Reload layering | Reload plays in Robot_ANIMTREE's **UpperBodyCustom** AnimNodeSlot, fed through AnimNodeBlendMultiBone_4930 (InitTargetStartBone **C_Spine01_Lumbar01_XB**, PerBoneIncrease 1.0: spine + arms + head = 1, hips/legs = 0). Legs keep locomotion. | `TR_Shared_ANIMTREE_p.Robot_ANIMTREE` | CONF | **APPLIED** (bone-local blend, as AnimNodeBlendMultiBone). Verified: base `Nav_StrafeJog_F` @5.5 m/s while upper = `Shooting_Reload_IonBlaster_ROBO`. |
| Reload owner anim | `ReloadAnimation = Shooting_Reload_IonBlaster_ROBO`, non-additive, BlendIn/Out **0.1/0.1 s** | `IonBlaster_WEPDATA.TnWeaponOwnerAnimator_10029` + `Default__TnWeaponOwnerAnimator` | CONF | **APPLIED**. Slot releases at reload end (1.5 s) and blends out while the 1.633 s clip finishes. |
| Fire owner anim | none (`FireAmmoAnimations` empty for the Ion Blaster); firing drives skel-control recoil only | same | CONF | APPLIED |
| Equip/overheat anims | `ADD_Shooting_equip_weapon` (CAMT_UpperBody, additive, 0.2/0.3 s); overheat Start/Loop/End additive | same | CONF | not yet (no equip/overheat gameplay) |
| Recoil controls | HmSkelControlRecoil: **RightHandRecoil** on R_Arm02_Shoulder_XB, **SpineRecoil** on C_Spine02_Lumbar02_XB, LeftHandRecoil on L_Arm01_Clav_XB | Robot_ANIMTREE SkelControlLists | CONF | **APPLIED** (left hand: no Ion Blaster RecoilDef, idle) |
| Recoil defs | Spine: 0.8 s, RotAmp (500,1000,0), RotFreq (10,10,0), RotParams all Zero. RightHand: 0.5 s, RotAmp (2000,500,-2000), RotFreq (15,10,10), Y=Random, LocAmp X=-8 UU @10 | `TnWeaponMesh` CDO merged with `IonBlaster_WEPMESH` | CONF | **APPLIED** |
| Recoil evaluation | RecoilDef layout identical to UE3 GameSkelCtrl_Recoil: restart per shot, smoothstep(TimeToGo/Duration) x Amp x sin(phase + TimeToGo x Freq), aim space | struct identity with UE3 | HI | **APPLIED** (rotation about the bone origin in model aim space) |
| Camera recoil / shake | RecoilCameraParams (FOV +0.085, pitch 0.1 deg, 0.05 s, delay 0.25 s); CameraShake (0.17, roll 0.15, falloff 0.1/0.06 s) | `IonBlaster_WEPDATA` | CONF | **not applied**: camera is owned by the Gameplay agent |
| Weapon mesh | 34-joint skeletal mesh with its own AnimSet: WP_Fire -> `IonBlaster_Fire`, WP_Reload -> `Shooting_Reload_IonBlaster_AP`, idle `IonBlaster_Idle`; HmAnimatedMesh BlendOutTime 0.2 s | `IonBlaster_WEPMESH.WeaponEventAnims`, `HmAnimatedMesh_9126` | CONF | **APPLIED** (was a static mesh) |
| Weapon sockets | MuzzleFlash -> C_Robo04_XT; ShellSocket -> C_Robo15_XT (+loc/rot); MagSocket -> C_Robo01_XT (+loc/rot) | `WEP_IonBlaster_SKEL` SkeletalMeshSockets | CONF | **APPLIED**; muzzle/tracer origin = MuzzleFlash socket (replaces the geometric barrel tip) |
| Event timing (AnimNotifies) | Fire: Shell_AssaultRifle_FX @0.005 ShellSocket. Reload_AP: ANIM_RELOAD_01 @0.000, Reload_AssaultRifle_FX @0.034 MuzzleFlash, ANIM_RELOAD_02 @0.137, Magazine_IonBlaster_FX @0.174 MagSocket. Idle: IDLE_01 @0.022, IDLE_02 @2.751 | `WEP_IonBlaster_ANIM` (HmAnimNotify_Sound / HmAnimNotify_PlayEffect) | CONF | **APPLIED** for sounds and effects (shell/reload/magazine FX: PASS 7c) |
| Muzzle flash | `FX_AssaultRifle_p.FX.MuzzleFlash_AssaultRifle_FX`, local space at MuzzleFlash | WEPMESH.MuzzleFlashes | CONF | **APPLIED**: Long (MuzzleFlash_Side_02, velocity-aligned, burst 10, life 0.15-0.2, 1.9-2.1 x 3.5-5 m, +1.9 m, 9 m/s), Top (smokeball_02 star), Sparks (SparksSheet 2x2 SubUV). Omitted: Glow_Mod (modulate), distortion ring, BackSteam/BackJet (alpha <= 0.05) |
| Tracer | `Tracer_AssaultRifle_FX`: Bolt (Bolt_ADD_MAT, PSA_Velocity, burst 1, life 0.6, 2 x 5-7 m, 15000 UU/s) + Trail2 smoke ribbon (Tracer_Smoke, life 0.9) | WEPMESH.TracerTemplates | CONF | **APPLIED**; bolt removed at the impact point; smoke ribbon opacity 0.35 **PROV** |
| Impact squib | `Impact_IonBlaster_FX`: GLOW_Dup (MuzzleFlash2, burst 10), Sparks_bolts (Spark_Tail, burst 20, 1.5-10 m/s), Smoke (SmokeThin, burst 4, x5 growth); rules 60 %, max 5 live, 25 m max distance | WEPMESH.DefaultSquib / TnWeaponMesh CDO | CONF | **APPLIED**; surface normal approximated by -shot dir (collision query returns no normal) |
| Effect colour | native colour constant `ff 33 19 ff` read as ARGB = (51,25,255) blue-violet; EnergonColor param (255,255,255,A=0) = no override; SwitchableColorScaleOverLife A/B chosen by Team | LOD streams, WEPMESH params, editor thumbnails | HI | APPLIED (team A) |
| FX module roles | WFC compiles modules into a native per-LOD stream; distributions decode exactly (type/op/n/chunk + BE float table), but **which module each belongs to is inferred from order** (Lifetime, StartRotation, ..., AlphaOverLife, StartSize, SizeMultLife, Velocity, ColorOverLife, Location) | stream analysis | MED | used as above; x4 / x2 overbright assignment MED |
| Fire cue | `BL_WPN_GUN_ION_BLASTER.SHOOT`: root -9 dB, Distance 4000-35000 UU, MaxConcurrent 4; layers by SOUND_DISTANCE: HEAD (<=200 UU), HANDCANNON_LR (400-4000), NRG_DISTANT (4000-8000), shell drops (+0.474 s) | cooked SoundCue / SoundNodeRoot / SoundNodeWaveEvent | CONF | **APPLIED** (generated table) |
| Low ammo / tail / impacts | SHOOT_LOW_AMMO (ammo <= 5), SHOOT_TAIL (WP_LoopingTail, 3000-12000 UU), IMPT_WORLD (-12 dB), IMPT_DMG (-9 dB, rolloff 2), max 6 | same | CONF | **APPLIED** |
| Reload / idle cues | ANIM_RELOAD_01 (3 events), ANIM_RELOAD_02 (12 timed events to 1.33 s, Distance 1000 UU, rolloff 2), IDLE_01/02 (-21 dB) | same | CONF | **APPLIED** via AnimNotifies (replaces the single provisional clip-reload wav) |
| Attenuation model | DistanceMin/Max + RolloffFactor = FMOD Ex inverse rolloff (banks ship as .fsb) | field names + FMOD banks | HI | APPLIED |
| SOUND_DISTANCE for own weapon | `kSmartPan_PreferPlayer`: parameter measured from the owning player | inferred from the name | MED | APPLIED |
| Concurrency | MaxConcurrentPlayCount -> steal the oldest instance | - | MED | APPLIED |
| Mixer categories / reverb | SFX_WET_COMBAT_ROBOT_WPN(_SHOOT) category levels, WET reverb sends, occlusion | not extracted | - | **not applied**; master level 0.5 still PROV |
| Dry fire | `BL_WPN_FOLEY.SHOOT_DRY_FIRE_ELECTRICITY` (WP_NoAmmoFire) | cue table present | CONF | not triggered (needs a trigger-on-empty event from PlayerController, owned by Gameplay) |

Open items for the Gameplay agent: the robot's idle base clip is currently `Cust_Idle` (the
customisation-screen showcase idle, first entry of category `idle`), which turns the body and
points the gun away from the aim; `NAV_Idle` / `Nav_Idle_Pose` is the gameplay idle. Upper-body
aim offset: applied in PASS 7b. Mesh yaw offset: see PASS 7b (should be +pi/2).

### PASS 7b — upper-body aim offset (Systems agent)
- **Space and axes verified from data:** recomputing the `Shooting_Aim_F_U` vs `Shooting_Aim_F_C` mesh-space
  delta in robot.glb reproduces the authored Spine01 CU rotation exactly (0.0926), and the Spine02 delta
  (0.318) is the sum of the Spine01 + Spine02 increments. The authored rotations are therefore **mesh-space
  increments applied in hierarchy order**. The UE -> glTF quaternion mapping is (x,y,z,w) -> (-x,-z,-y,w).
- **Tree order [CONF]:** the AimOffset node sits under the `UpperBodyCustom` slot's source (via
  TnAnimTurnInPlaceRotator "UnwindLowerBody"), so it is: locomotion -> aim offset -> reload slot -> recoil.
  A playing reload replaces the aimed spine/arm rotations, as in the original.
- **Pawn aim input [MED]:** X = 0 (the body already faces the aim yaw); Y = camera pitch / 90 deg (UE3 pawn
  aim convention), remapped piecewise-linearly through PawnAimOffsetRange -> profile range. The exact
  TnAnimNodeAimOffset remap and the active-profile choice (`WeaponTypeObserved`; Default for the Ion
  Blaster) are not decompiled.
- **Mesh facing bug found (Gameplay-owned, not changed here):** the skeleton faces **+X** in model space
  (eyes are +X of the head, left clavicle at -Z, gun forearm along +X in NAV_Idle, StrafeJog_F and
  Shooting_Aim_F_C), but `core::config::kMeshYawOffset = 0` renders the mesh as if it faced -Z. The robot
  is therefore drawn rotated 90 deg from the aim; measured barrel heading = aim - 97..102 deg. With the
  renderer's rotateY convention the correct value is **kMeshYawOffset = +pi/2**. The recoil aim frame
  now uses the measured +X forward, so it is correct either way.

### PASS 7c — shell, magazine and reload FX (Systems agent)
Spawned by the weapon AnimNotifies at their authored times and sockets (see the Event timing row).

| Effect / emitter | Original data | Conf | Rebuild |
|---|---|---|---|
| Shell_AssaultRifle_FX "Shell" (ShellSocket, every shot @0.005) | mesh `FX_GrenadeLauncher_p.GrenadeAmmo_STAT` (WEP_GrenadeLauncher_MATINST -> WEP_GrenadeLauncher_CLR), burst 1, life 1.0, StartSize (0.75,0.3,0.3), spin U[(-1,-1,-1),(5,5,1)] turns/s | CONF | **APPLIED** as a mesh particle |
| shell ejection velocity | not on the Shell emitter; the paired ShellGlow emitter (same socket) carries U[(300,-300,100),(1000,-300,300)] UU/s | MED | used for the shell mesh |
| Shell "SMOKE" | SmokeCoolDepth, burst 4, life U[0.5,0.75], size U[50,100] UU growing x3, alpha 0.1 -> 0, velocity U[(100,-300,-300),(300,300,300)] UU/s | CONF (role order MED) | **APPLIED** |
| Magazine_IonBlaster_FX "Shell" (MagSocket, reload @0.174) | mesh `FX_IonBlaster_p.IonBlaster_Mag_STAT` (IonBlaster_Mag_MATINST -> WEP_IonBlaster_CLR), burst 1, life 3.0, StartSize 1, spin U[-1,1] turns/s, velocity (200,300,300) UU/s | CONF (velocity role MED) | **APPLIED** |
| Magazine "Smoke_Dup_Dup_Dup" | as Shell SMOKE, alpha 1 -> 0, colour 1 -> 0.1 | CONF | **APPLIED** |
| Reload_AssaultRifle_FX "GLOW_Dup_Dup" (MuzzleFlash, reload @0.034) | flareball01 (smokeball_01), local space, burst 10, life U[0.2,0.5], size U[20,35] UU x (1.5 -> 0.1), alpha peak 0.2, brightness curve 20 -> 1 | CONF data / MED roles (size and velocity from uniform-curve tables) | **APPLIED** |
| Reload "Smoke_Dup" | SmokeCoolDepth, SpawnRate 20/s for 0.75 s following the muzzle, life U[0.5,0.75], size U[100,200] UU x3, alpha peak 0.25, grey 0.83-0.90, -50 UU behind the muzzle | CONF | **APPLIED** (continuous emitter) |
| Gravity / ground contact for the mesh particles | no acceleration or collision module authored | - | **PROV**: world pawn gravity (-29.4 m/s^2) and rest on the collision floor |
| Omitted | ShellGlow / GLOW (Glow_Mod_MAT modulate), Shimmer (distortion), BackSteam / BackJet (alpha <= 0.05), Blaster_Trail ribbons on shell and magazine, the "Trail" emitters | - | not rendered |

Asset loading: umodel `.gltf` + `.bin` meshes are now accepted by `assets::loadGlb` (text glTF branch).

### PASS 8 — vehicle boost presentation (Systems agent)
Source: `TR_Optimus_VEHDEF_p.OptimusTruckForm` (TnTruckFormBlueprint) and its
`HmPlayerVehicleAudioComponent_6670`; sockets from `character.json` (VH_OptimusPrime_SKEL); FX from
`FX_Navigation_p.bumble_boost_small1_FX` (cooked in A1_IAC_Base_m); cues from `BL_VEH_OPTIMUS_PRIME`
via `SoundEvents_Vehicles_Trans.Veh_Optimus_Prime_SoundSet`.

| Item | Original (WFC) | Conf | Rebuild |
|---|---|---|---|
| Boost FX binding | `BoostFx` = BoostSocket_L / BoostSocket_R -> `bumble_boost_small1_FX` | CONF | **APPLIED** |
| Boost sockets | BoostSocket_L on L_Robo23_XT (0,-35,0) UU yaw -90 deg; BoostSocket_R on R_Robo23_XT (0,35,0) yaw +90 deg: the two exhaust stacks behind the cab | CONF | **APPLIED** (bone x socket, UE -> glTF via vs_common) |
| Ignition burst (EmitterLoops 1) | "thruster": 3 Boostermesh_02 cones, life U[0.3,0.5], scale U[(4,1,2),(5,1,1)] x size curve 0 -> 2.06 -> 1, colour (1,0.6,0.05); "Cone_thrust_Dup": bulletshape mesh, life U[0.3,0.4], scale (50,37.5,15) x 21-entry growth, colour (5,1.5,0.05) x 3; "Particle Emitter_Dup": SphereGlow sprite 30 UU x growth, colour (5,1.5,0.05) x 3, +25 UU | CONF data / MED roles | **APPLIED** |
| Looping while held | "loopcone": Boostermesh_02 at U[3,4]/s after a 0.1 s first-loop delay, life 1.0, scale U[(4,1,2),(5,1,1)] x U[1,1.1], colour (1.07,0.59,0.16); "Particle Emitter_Dup_Dup": SphereGlow at 20/s after 0.2 s, life U[0.4,0.6], 30 UU growing to x3.08, spin U[-0.75,0.75] turns/s, colour (0.8,0.8,0.3) | CONF data / MED roles | **APPLIED** |
| Alpha over life | 0 -> 1 at 20 % of life, linear to 0 (every emitter) | CONF | APPLIED |
| Local space / deactivation | every emitter bUseLocalSpace + bKillOnDeactivate | CONF | **APPLIED**: particles ride the sockets (turning, jumping); release kills them at once |
| Materials | Boostermaterial_02_MAT (additive, two-sided; LightBeam_Falloff_01 + DiffClouds + Spot), bumble_boostcone_MAT (additive, SphereGlow_01 x 1.2), Basic_Particle_Add_MAT (additive, SphereGlow_01) | CONF | APPLIED with the primary texture only (LightBeam_Falloff_01 for the cones) **PROV** |
| HDR colour | colours > 1 (x3 colour scale) | CONF | approximated by hue-preserving normalisation + GL x2/x4 overbright **PROV** |
| Boost audio | BoostSound Auto_Boost_Start -> VEH_OPTIMUS_BOOST_START (5 timed events); BoostLoops Auto_Boost_Loop -> VEH_OPTIMUS_BOOST_LOOP (5 looping layers from 0.44 s, volume/pitch curves on `Optimus_Prime_Speed` (Max 120) + time envelopes); BoostStopSound -> VEH_OPTIMUS_BOOST_END; BoostWheelsSound -> VEH_OPTIMUS_BOOST_WHEELS (50 % ChanceToPlayNone) after BoostWheelsGroundCheckDelay 0.27 s if grounded; BoostFadeOutTime 0.15 s | CONF | **APPLIED** |
| Speed parameter units | `Optimus_Prime_Speed` in mph (curves put nominal pitch at 33 = 15 m/s cruise) | MED | APPLIED |
| START cue curves | VolumeCurve/PitchCurve on an event of a cue with no root SoundParameter | MED | fed the speed parameter |
| Cue loop region | root LoopStart/LoopEnd | - | not used; looping layers loop their whole wave |
| Boost activation | presentation follows the movement code's boost condition (vehicle form + boost held, not transforming), so stationary boost shows the effect | MED | the 0.3 s DashDuration-vs-sustained question belongs to Gameplay movement |
| Material / emissive changes on boost | none authored on OptimusTruckForm (only overshield / defrag materials) | CONF (absence) | n/a |
| Camera feedback | not on the truck form or its audio component; camera behaviours belong to Gameplay | - | not applied |
| Related | HoverFX / JumpFX: PASS 9. RamFX + nitro: PASS 10. Engine / jump / land audio: PASS 11 | CONF data | done (tire squeal open) |

Cue system extension (used by the boost cues): `bLooping` wave events, VolumeCurve/PitchCurve keyed by the
root SoundParameter (SOUND_DISTANCE or speed), Envelope volume/pitch curves over playback time,
ChanceToPlayNone, live parameter/position updates and fade-out stop. The cue table is now generated
by `tools/systems/gen_cues.py` into `src/game/SoundCues.inc` (weapon cues regenerated unchanged).

### PASS 9 — hover thrusters and jump boosters (Systems agent)
Same source object as PASS 8 (`OptimusTruckForm`): `HoverFX` (6 x HoverBooster_* -> `CarHover_A_01_FX`)
and `JumpFX` (JumpBoostSocket_C/R/L -> `Jump_FX`). Implemented in the generic `VehicleFx` (which now also
carries the PASS 8 boost tables unchanged).

| Item | Original (WFC) | Conf | Rebuild |
|---|---|---|---|
| Hover sockets | HoverBooster_LFront/RFront on L/R_Wheel01_XB (scale 3), _LBack/RBack on Wheel02, _LBack2/RBack2 on Wheel03 (scale 2/2.5/2.5); +-13 UU, rotation (180, +-90, 0): emission axis down/out from the wheels | CONF | **APPLIED** incl. socket scale (positions, sizes, meshes scale with the socket) |
| Hover looping emitters | Rings_Dup (Ring_Distort_Add -> Ring_CLR, 3/s + 1, life U[0.35,0.5], 120 UU shrinking to 0.5, spin, colour (1,0.5,0.25)); lightcone_Dup (Light_Cylinder_STAT mesh, 10/s, life U[1.5,2], scale U[(0.3,0.3,0.075),(0.25,0.25,0.1)] x 1 -> 1.2 -> 1, alpha 0.5, brightness flicker 0.8-1.33, colour (1,0.1,0.1) x 2; NOT bKillOnDeactivate) | CONF data / MED roles | **APPLIED** |
| Hover one-shot on activation | Sparks_bolts (Spark_MAT, burst 10 + 100/s for 0.3 s, velocity U[(600,-50,100),(1200,50,400)] UU/s, colour (2.5,2,2) x 3); ElectroRing (lightningring_01, 20/s for 0.2 s, colour (1,0.5,0.2) x 3); Pulse (Boostermesh_03, 3-4/s for 0.5 s, scale (0.8,4,4), colour (2,0.1,0.1)) | CONF data / MED roles | **APPLIED** |
| Hover active state | no flag authored; the audio component's Hover vs Boost/Wheels land sounds and the boost wheels peel-out imply: hover thrusters whenever in vehicle form except while boosting | MED | **APPLIED**: vehicle form, not boosting, not transforming |
| Jump FX | 12 emitters, all EmitterLoops 1 / 0.5 s (0.3 / 0.2 s for sparks / electro ring), bKillOnDeactivate: glow bursts (SphereGlow 80-120 UU x5, colour (4,2,1) x 3), booster smoke (20/s, 9-10 m/s down), Boostermesh_03 thruster streaks / bases (burst 3, growth 0 -> 3 -> 1), Boost_Circuit energon cones, Spark_MAT burst 30 + tail sparks, electro ring (6,4,2) x 3, all 50 UU below the socket (socket X points straight down) | CONF data / MED roles | **APPLIED** |
| Jump trigger | not in data; the vehicle jump (JumpLinearSpeed 12 m/s) | MED | take-off edge with vy > 2 m/s; ends early only if the vehicle form ends |
| Light cylinder material | LightCylinder_Rays_MAT_INST -> LightVolume_Base_MAT: view-dependent volumetric (SideViewV, NearFade, DepthBias, dust panners); instance DustPower 0.1 | CONF params | **PROV**: drawn with LightBeam_Falloff_01 x DustPower 0.1 (shader graph not evaluated) |
| Spark_MAT | no texture; procedural streak from texture-coordinate math | CONF (absence) | **PROV**: generated soft-streak texture |
| Rings trailing (150,0,0) | role undecided (location vs velocity) | MED | used as velocity (as an offset it puts rings 4.5 m from the wheels) |
| Omitted | hover base_glow (Glow_Mod_MAT modulate), rays_Dup (Trail_Distort distortion); every material's secondary panning cloud/energon layers | - | not rendered |

### PASS 10 — truck nitro / ram (Systems state + FX + audio; movement effect owned by Gameplay)
Source: TransGame.TnTruckForm compiled UnrealScript (TransGame.xxx) — function/state names and float
literals in the getters' bytecode — plus `OptimusTruckForm.RamFX` and the audio component / sound set.

| Item | Original (WFC) | Conf | Rebuild |
|---|---|---|---|
| Trigger | state **Driving** (on wheels = boosting): `UpdateNitro` starts the nitro on the **DASH** input (`_Dashing`) when the cooldown allows; `Driving.EndState` calls `StopNitro`; in state **Hovering** dash is a plain hover dash (`DoDash`) | CONF (script structure) | **APPLIED** in `VehicleNitro`: driving = vehicle form + boost held, not transforming |
| Nitro duration | `get_NitroDuration` = **3.0 s** x NitroDurationModifier (1.0) | CONF | **APPLIED** |
| Speed scale | `get_NitroSpeedScale` = **1.5** x modifier | CONF | **exposed** (`speedScale()`), **not applied** — Gameplay owns movement |
| Steering scale | `get_NitroSteeringScale` = **0.3** x modifier | CONF | **exposed** (`steeringScale()`), **not applied** — Gameplay owns handling |
| Cooldown | `get_TimeBetweenNitros` = **8.0 s** x modifier, starting on activation | CONF (native RE) | **APPLIED** |
| Max ram mass | `get_MaxRamMass` = 1000 | CONF | exposed constant (ram collision is Gameplay's) |
| StartNitro side effects | RamFX on RamSocket, NitroForceFeedback (3 s), nitro camera state | CONF | RamFX **APPLIED**; force feedback / camera not (no rumble path; camera = Gameplay) |
| RamFX | `Truck_ram_FX` on RamSocket (C_Body_XB (380,0,-40) UU, scale (1,1.5,1.5)): Ram_STAT wedge mesh, 20/s, life 1.0, alpha 0.35, colour (2,1.8,1.3), -250 UU; dust + rays are distortion (omitted) | CONF data / MED roles | **APPLIED** |
| Ram_model_MAT | emissive = 2 x (vertex colour x c)^2, c = saturate(pow(1-N.V, FresnelExponent 2) x FresnelScaleUp 1.5) x 2 x lerp(A x L1, L1, 0.4) over four panning Flame_Tile layers | CONF graph | fresnel rim applied per vertex (squared); panning layers = one static Flame_Tile **PROV** |
| Nitro audio | NitroSound Auto_Ram_Nitro -> `VEH_OPTIMUS_RAM_NITRO_START` (7 events); CustomLoopingSound Auto_Ram_Alert -> `BL_VEH_SOUNDWAVE.VEH_TRUCK_RAM_ALERT` | CONF | **APPLIED** at nitro start; the alert plays once **MED** (component-level looping not decoded) |
| Ram impact audio | RamSound Auto_Ram_Impact -> `BL_VEH_SOUNDWAVE.VEH_TRUCK_RAM_IMPACT` (from AttemptToRam) | CONF | hook `World::notifyRamImpact(pos)` for Gameplay's ram collision |
| BoosterSound | Auto_Ram_Boost -> `VEH_OPTIMUS_RAM_BOOST_START` | CONF mapping | in the cue table; trigger not decoded, not played |
| Dash input | abstract `platform::Button::Dash` | - | ~~**PROV** temporary key **Q**~~ → **Shift** in the merged build (Gameplay Pass 12 [CONF]: Shift = "Ability0 \| VehicleSpecialMove"); Q removed at integration/milestone-02 |

**Value conflict to resolve in Gameplay (documented, not changed here):** the rebuild's vehicle boost uses
`Default__TnHoverCarSimulationBlueprint` DashSpeed 5000 UU/s / DashDuration 0.3 s (core::config
kVehicleBoostSpeed / kVehicleDashTime). Optimus's truck actually references `VEH_SHARED_p.HoverTruck_Physics`
(TnHoverCarSimulationBlueprint) with **DashSpeed 3000 UU/s, DashDuration 0.5 s**, SuspensionRadius 185 UU,
and, for the Driving (wheels) state, `VEH_SHARED_p.Truck_Physics` (TnCarPhysicsBlueprint) with MaxSpeed
3000 UU/s, MaxAcceleration 2500, JumpLinearVelocity (600,0,1400), Mass 2500. The hover DASH is a separate
mechanic from the nitro (Hovering.DoDash vs Driving nitro). Systems did not modify any vehicle movement value.

### PASS 11 — vehicle engine audio (Systems agent)
Source: `OptimusTruckForm.HmPlayerVehicleAudioComponent_6670` and `Veh_Optimus_Prime_SoundSet`; cues generated
from `BL_VEH_OPTIMUS_PRIME` (tools/systems/gen_cues.py).

| Item | Original (WFC) | Conf | Rebuild |
|---|---|---|---|
| Drive loops | DriveSounds gears (MaxSpeed 20, 110): OnLoadLoops Auto_Engine_Gear_1_OnLoad -> `VEH_OPTIMUS_DRIVE_ONLOAD` (2 looping layers), OffLoadLoops -> `VEH_OPTIMUS_DRIVE_OFFLOAD` (3 looping layers incl. idle); one-shots map to None; ReverseSound maps to the same cues | CONF | **APPLIED**; both gears share the cues, so gear selection is audible only through the speed curves |
| Speed response | every layer's volume/pitch curves on `Optimus_Prime_Speed` (mph, Max 120) | CONF curves / MED units | APPLIED |
| On-load vs off-load | native component logic not decoded | MED | on-load while throttle input is held, off-load otherwise |
| Transitions | EngineFadeOutTime 0.2 s | CONF | APPLIED (outgoing loop fades 0.2 s) |
| Airborne | JumpRevSounds UseJumpRev, Auto_Jump_Loop -> `VEH_OPTIMUS_DRIVE_JUMP_LOOP` | CONF | APPLIED while airborne |
| Jump start | AscendSound Auto_Jump_Start -> `VEH_OPTIMUS_DRIVE_JUMP_START` | CONF | APPLIED on take-off |
| Landing | HoverLandSound {0.15 s: `HOVER_LAND_LIGHT`, 2.0 s: `HOVER_LAND_HEAVY`}, BoostLandSound {0.15 s: `WHEELS_LAND_LIGHT`, 2.0 s: `WHEELS_LAND_HEAVY`} by time in air | CONF | APPLIED (wheels variant while boosting); the robot-form placeholder thump no longer plays in vehicle form |
| Boost | the boost loop cue carries its own engine layers | MED | the drive loop yields to `VEH_OPTIMUS_BOOST_LOOP` while boosting |
| Tire squeal | TireSquealSoundParameter / Auto_Tire_Squeal_Default -> `VEH_OPTIMUS_TIRE_SQUEAL`, crossfade 0.5, TireSquealSpeedMin 20 | CONF data | **not applied** (needs a lateral-slip signal from Gameplay's vehicle handling) |

### VEHICLE MECHANICS — normal boost vs hover dash vs ram/nitro (consolidated, Systems checkpoint)
Three distinct mechanics in the original; do not conflate them. Values marked CONF are authored data or
compiled-script literals; "current rebuild" is what the movement code uses today (Gameplay-owned, unchanged
by Systems).

| Mechanic | Original trigger / state | Authored values | Current rebuild | Owner |
|---|---|---|---|---|
| **Normal boost** (drive on wheels) | holding Boost switches TnCarForm from state **Hovering** (HoverBlueprint) to **Driving** (CarBlueprint, wheels on the ground); `get_IsBoosting` / `set_BoostingInput`. Original PC binding: **right mouse in vehicle form** (same button as robot Fine Aim) | `VEH_SHARED_p.Truck_Physics` (TnCarPhysicsBlueprint): MaxSpeed **3000 UU/s (30 m/s)**, MaxAcceleration **2500**, Mass 2500, JumpLinearVelocity (600,0,1400). Presentation (CONF): BoostFx bumble_boost_small1_FX on BoostSocket_L/R; BOOST_START / BOOST_LOOP / BOOST_END cues, BoostFadeOutTime 0.15 s, BoostWheelsGroundCheckDelay 0.27 s | movement: Sprint (Shift) held, top speed `kVehicleBoostSpeed` 50 m/s reached over `kVehicleDashTime` 0.3 s (taken from the hover-sim **class default** DashSpeed/DashDuration, not Truck_Physics). Presentation: Systems PASS 8 | movement + binding: Gameplay; FX/audio: Systems |
| **Hover dash** | DASH input while **Hovering**: `TnTruckForm.Hovering.DoDash` -> TnHoverCarSimulation dash (`_DashTimeRemaining`, TimeBetweenDashes) | `VEH_SHARED_p.HoverTruck_Physics` (Optimus's actual hover sim): **DashSpeed 3000 UU/s (30 m/s), DashDuration 0.5 s**, SuspensionRadius 185 UU; class defaults (`Default__TnHoverCarSimulationBlueprint`): DashSpeed 5000, DashDuration 0.3, MaxLinearSpeed 1500, accel 3000, JumpLinearSpeed 1200, SuspensionRadius 200 | **not implemented as a separate mechanic**; its class-default values currently drive the normal boost (see above). Systems reads no hover-dash state | Gameplay |
| **Ram / nitro** | DASH input while **Driving** (boosting on wheels): `TnTruckForm.Driving.UpdateNitro` -> `StartNitro`; leaving Driving (`EndState`) -> `StopNitro` | script literals: NitroDuration **3.0 s**, NitroSpeedScale **x1.5**, NitroSteeringScale **x0.3**, TimeBetweenNitros **8.0 s**, MaxRamMass **1000**; StartNitro: RamFX (Truck_ram_FX on RamSocket), NitroForceFeedback 3 s, nitro camera state; NitroSound VEH_OPTIMUS_RAM_NITRO_START, CustomLoopingSound VEH_TRUCK_RAM_ALERT, RamSound VEH_TRUCK_RAM_IMPACT | state/timer/cooldown + RamFX + audio: **Systems** (`VehicleNitro`, abstract `Dash` action, PROV key Q); speed/steering scales **exposed, not applied**; nitro camera + force feedback not applied | state/FX/audio: Systems; speed, steering, handling, ram collision, camera, final binding: Gameplay |

Input notes: `Dash` is an abstract action (no PC binding recovered; temporary **Q**, PROVISIONAL). Right mouse is
deliberately left unbound by Systems so Gameplay can map it per the original: **robot form = Fine Aim,
vehicle form = Boost**.

### Native RE confirmation (Systems, 2026-10-01) — weapon cadence + vehicle states
**Ion Blaster cadence [CONF, native RE]:** one-shot refire timer reset to zero after each shot; fires when
elapsed **> 0.065 s**; fractional overshoot discarded; at most one shot per simulation tick. At 60 Hz this is a
shot every 4th tick = **900 RPM**, the original runtime cadence. `Weapon` now uses exactly this timer
(standalone check: 900 shots/min, every gap 4 ticks). Experimental's `systems-1-fire-interval-remainder.patch`
(923 RPM) is **not applied**: it would make the rebuild faster than the original.

**Vehicle states [CONF, native RE]** (movement math stays with Gameplay):

| State | Input | Behaviour | Values |
|---|---|---|---|
| Normal Boost | **LT / RMB held** | Driving (wheeled) behaviour; returns to Hover when released | top speed ~**3000 UU/s**, accel ~**2500**, special low-speed acceleration (Truck_Physics; TnCarForm CarLowSpeedBoost* modifiers) |
| Hover Dash | **RB / abstract Dash**, Hovering only | forward burst | **3000 UU/s** for **0.5 s**, **2 s** cooldown (HoverTruck_Physics DashSpeed/DashDuration; TimeBetweenDashes) |
| Ram / Nitro | **RB / abstract Dash** while Driving with Boost already active | top speed x1.5 (~**4500 UU/s**), steering x0.3 | **3 s**; **8 s** cooldown from activation; ends immediately when Boost is released; no authored ram animation (RamFX + nitro/ram cues are the presentation) |
| Ram collision | during Nitro only | **one hit per target per Nitro** | OptimusTruckForm: RamDamageToPlayerRobots **175**, ToAiRobots **300**, ToPlayerVehicles **175**, ToAiVehicles **300** (class defaults 50/100/50/100); ExtraRamZVelocity **7000 UU/s** (default 1000); MaxRamMass **1000**; all x TnTruckForm modifiers 1.0 |

Systems side: `VehicleNitro` (state, 8 s-from-activation cooldown, ends on Boost release, `registerRamHit`
one-hit-per-target gate, authored damage/momentum constants exposed) and `World::notifyRamHit(target, pos)`
(gate + ram impact cue). Not applied by Systems: speed/steering scales, hover dash, ram damage/impulse.
Input: LT/RMB = Boost (Gameplay mapping; robot form RMB = Fine Aim); RB = abstract `Dash` (temporary PC key **Q**,
PROVISIONAL). The Dash action is consumed by Systems only to start the Nitro while Driving; Gameplay reads the
same `Button::Dash` for the hover dash.

> **Integration note (integration/milestone-02):** the provenance above is unchanged. In the merged
> build, Gameplay Pass 12 implements all three mechanics in `CharacterMovement` (Driving 30 m/s /
> 2500, hover dash 30 m/s × 0.5 s with a 2 s cooldown, nitro 3 s ×1.5 speed ×0.3 steering with an
> 8 s cooldown, ending on Boost release). It also owns the Dash input: **Shift** / pad RB, latched in
> `PlayerController`. The Systems-side `VehicleNitro::update` timer and its `Q`/World Dash latch
> were replaced by `VehicleNitro::follow(vehicleState().nitroRemain > 0)`, so RamFX, the nitro cues
> and the ram-hit registry track Gameplay's single state machine. Boost presentation (afterburners,
> boost cues) follows Gameplay's Driving state. The values are identical on both sides, so
> nothing was re-tuned. Ram collision is not implemented in either branch; the `notifyRamHit`
> hook is unused.

---

## PASS 6 — PLAYER-CONTROL, ANIMATION & WEAPON PRESENTATION (2026-10-01)
Driven by replaying the exe (runtime observation overrides headless smoke). Priority order as
the player reported it.

| Behaviour | Original (WFC) | Source | Conf | Rebuild status |
|---|---|---|---|---|
| Body facing | Robot faces the **aim/camera (mouse) yaw every frame**; WASD = move dir relative to it (strafe shooter). User confirmed: "camera facing = character facing; WASD = move direction." | Observed original + user | HI | **APPLIED** — `CharacterMovement` robot `setYaw(faceYaw)` always; removed the stand-then-walk snap. `face·toCam≈−0.9` verified. |
| Mesh yaw offset | ~~Extracted meshes align to the rebuild's −Z-forward yaw with no extra rotation.~~ **Superseded by Pass 7:** meshes face model +X; offset is +90°. | ~~Runtime geometry check~~ (only tested yaw math) | — | ~~`kMeshYawOffset=0`~~ → **+π/2** (Pass 7). |
| Vehicle facing | Faces its **travel direction** (steering), not the aim. | Observed | MED | **APPLIED** `yaw=atan2(-vx,-vz)` when moving. |
| Directional locomotion | `Nav_Strafe{Jog,Walk}_{F/B/L/R}` chosen by travel dir **relative to facing**. | `robot.glb` clip set (category `run`/`walk`) | HI | **APPLIED** (dot of velocity with facing fwd/right). Verified strafe-R → `Nav_StrafeJog_R`. |
| Upper-body aim offset | `Shooting_Aim_{F/L/R}_{C/D/U}` 9-pose grid points the gun at the reticle. Robot_ANIMTREE `TnAnimNodeAimOffset_14979`, profile **Default**: 11 bones (spine chain, head, both arms) x 9 authored rotations (L/C/R x U/C/D) baked from `Shooting_Aim_*`; ranges H [-1,1] V [-1,0.8]; RemapPawnAimRange from pawn H [-1,0.85] V [-0.7,1] | `robot.glb` category `aim`; `TR_Shared_ANIMTREE_p.Robot_ANIMTREE` | CONF (data) / MED (remap) | ~~NOT YET~~ **APPLIED in Gameplay Pass 7/9** (the implementation in the merged build). Systems PASS 7b recovered the same profile independently (aim 22.9 deg -> barrel 21.6-23.8 deg); that implementation was not merged. |
| Transform pairing | Robot & vehicle transform clips share a duration (ToVehicle **1.97 s**, ToRobot **1.13 s**) — one fold authored per mesh, played **in sync**. | `robot.glb`/`vehicle.glb` clip durations | HI | **APPLIED** — outgoing mesh → midpoint → partner mesh resumed at same normalized time (was sequential = the "crack"). Weapon holstered through the fold. |
| Transform cross-fade point | exact visibility/alpha handoff curve | — | GUESS | `kTransformHandoffFrac=0.5` **PROVISIONAL**; cross-mesh pop minimised, not removed. |
| Muzzle origin | Ion Blaster **barrel tip** (MuzzleFlash socket). Socket transform not extracted; used geometric tip. | `weapon.glb` frontmost vertex slice | CONF-derived | **APPLIED** — weapon-local (2.063,0.017,0.141) m → +2.07 m forward of the hand; tracer+flash leave the barrel. |
| Reload anim | `Shooting_Reload_IonBlaster_ROBO` (full-body); `WeaponReloadAnimTime` 1.5 s (clip 1.633 s). | `robot.glb` category `reload`; `weapon.json` | CONF | **APPLIED** full-body one-shot while reloading. Additive `ADD_Shooting_Reload_*` (reload on the move) = PARTIAL. |

**Lighting (#6/#7/#8) — superseded by PASS 7 (original render path).** World still uses only **coeff0**
of the 3-coefficient directional baked lightmap (fixed-function can't apply the directional basis
per-pixel); character uses a provisional `GL_LIGHT0` not sampled from the world. True fidelity
(3 coeffs · normal, gamma/sRGB, env-probe character lighting) needs a GL2+ shader path — a large,
isolated effort, not cut into this pass to avoid leaving the build broken.

---

## PARAMETER TABLE (recovered so far)

| Property | Original value | Source | Conf | Rebuild status |
|---|---|---|---|---|
| World gravity (pawn) | DefaultGravityZ **−2940 UU/s²** = −29.4 m/s² | `Xe-TransGame.ini [Engine.WorldInfo]` | CONF | **APPLIED** (was 22) |
| RB physics gravity scale (vehicles) | **0.66** → −19.4 m/s² | `Xe-TransGame.ini [Engine.WorldInfo] RBPhysicsGravityScaling` | CONF | applied to vehicle form |
| Transformation blend-in | **0.115 s** | `Xe-TransGame.ini [TransGame.TnTransformation] _BlendInTime` | CONF | **APPLIED** (crossfade) |
| Transformation blend-out | **0.25 s** | `Xe-TransGame.ini [TransGame.TnTransformation] _BlendOutTime` | CONF | **APPLIED** (crossfade) |
| Camera default FOV | **75° horizontal** (SmoothTime 0.4) | `Xe-TransCamera.ini [AnimatedFovCameraBehavior TnFovCameraBehavior] DefaultFOV` | CONF | **SUPERSEDED (Pass 11):** robot strategy instance OverTheShoulder TnFovCameraBehavior DefaultFOV 80 |
| Fine-aim (ADS) FOVs | 35 / 45 / 55 (close/med/far POI) | `Xe-TransGame.ini [TnPointOfInterest]` | CONF | **SUPERSEDED (Pass 11):** these are point-of-interest focus FOVs; fine aim uses TnPCS_FineAim FOV 45 (applied) |
| Fine-aim ground-speed mult | **0.5×** | `Xe-TransGame.ini [TnFineAimManager] _GroundSpeedMultiplier` | CONF | **SUPERSEDED (Pass 11):** APPLIED via SetSpeedMultiplier |
| Camera pawn-cylinder padding | R=30, H=30 UU | `Xe-TransCamera.ini [TnCamera]` | CONF | n/a (no camera collision yet) |
| Camera pawn fade start | 200 UU | `Xe-TransCamera.ini [TnCamera] PawnFadeStartDistance` | CONF | not yet |
| Ion Blaster fire interval | **0.065 s**; runtime cadence **~900 RPM** (one-shot timer reset to 0 per shot, fires when elapsed > 0.065 s, overshoot discarded, max one shot per tick) | `weapon.json` + native RE | CONF | **APPLIED** (Systems native-RE pass); Experimental's 923-RPM remainder patch deliberately NOT applied |
| Ion Blaster damage | **15** (InstantHit) | `weapon.json gameplay.InstantHitDamage` | CONF | correct ✓ |
| Ion Blaster magazine | **50** | `weapon.json MaxAmmoClipCount` | CONF | correct ✓ |
| Ion Blaster max reserve | **250** | `weapon.json MaxAmmoCount` | CONF | correct ✓ |
| Ion Blaster initial reserve | **150** | `weapon.json InitialReserveAmmoCount` | CONF | **FIXED** (was 250) |
| Ion Blaster reload time | **1.5 s** | `weapon.json WeaponReloadAnimTime` | CONF | **FIXED** (was 1.8) |
| Ion Blaster range | **30000 UU = 300 m** | `weapon.json WeaponRange` | CONF | **FIXED** (was 400) |
| Ion Blaster damage falloff | 1.0× ≤5000 UU → 0.5× @30000 UU | `weapon.json RangeDamageModifiers` | CONF | **APPLIED** |
| Ion Blaster per-shot spread | 0.08→0.18, +0.005/shot, 2.0 s cooldown | `weapon.json PerShotSpreadModifier` | CONF | **APPLIED** |
| Ion Blaster fine-aim spread | 0.5× | `weapon.json FineAimSpreadModifier` | CONF | **SUPERSEDED (Pass 11):** APPLIED |
| Ion Blaster equip/putdown | 0.2 / 0.5 s | `weapon.json EquipTime/PutDownTime` | CONF | not yet |
| Weapon socket | WeaponSocket_Primary, bone R_Arm03_Elbow_XB | `character.json sockets` | CONF | attached (rotation approx) |
| Robot ground speed | **550 UU/s = 5.5 m/s** | `TransGame.xxx Default__TnPlayerPawn.GroundSpeed` | CONF | **SUPERSEDED (Pass 11):** 14 m/s (Optimus_ROBODEF BaseGroundSpeed 1400) |
| Robot accel | **2048 UU/s² = 20.48 m/s²** | `Engine.xxx Default__Pawn.AccelRate` | CONF | **SUPERSEDED (Pass 11):** 120 m/s² (ROBODEF AccelRate 12000) |
| Robot air control | **0.70** | `Default__TnPlayerPawn.AirControl` | CONF | **SUPERSEDED (Pass 11):** 0.4 (ROBODEF) |
| Robot air speed | **1500 UU/s = 15 m/s** | `Default__TnPlayerPawn.AirSpeed` | CONF | **SUPERSEDED (Pass 11):** 12 m/s (ROBODEF AirSpeed 1200) |
| Robot max jump height | **625 UU = 6.25 m** | `Default__TnPawn._WorkingMovementCapabilities.MaxJumpHeight` | CONF | **SUPERSEDED (Pass 11):** 5.0 m (SharedAcrobatics.JumpHeight 500; JumpZ formula from ApplyTransformer; measured 5.12 m) |
| Pawn cylinder radius | **175 UU = 1.75 m** | `Default__TnPawn._WorkingMovementCapabilities.CylinderRadius` | CONF | **SUPERSEDED (Pass 11):** 2.0 m (Optimus_ROBODEF Collision) |
| Pawn cylinder half-height | **200 UU = 2.0 m** (full 4.0) | `…CylinderHeight` | CONF | **APPLIED** |
| Pawn base eye height | **80 UU = 0.8 m** (above centre → 2.8 m above feet) | `Default__TnPawn.BaseEyeHeight` | CONF | **SUPERSEDED (Pass 11):** Default__TnTransformer.BaseEyeHeight 150 → eye 3.5 m (trace origin); camera anchor 4.0 m (strategy Offset Z 200) |
| Robot walk pct | 0.5 (walk = 0.5× ground) | `Engine.xxx Default__Pawn.WalkingPct` | CONF | n/a (keyboard jogs) |
| Robot max fall speed | uncapped in movement | `…_WorkingMovementCapabilities.MaxFallSpeed=100000` | CONF | no terminal cap |
| HeightFog colour | **(234,91,116)** warm red-pink | `MP_IAC_Streets_ART_m HeightFogComponent.LightColor` | CONF | **APPLIED** (was blue-grey) |
| HeightFog density / start | 2e-5/UU (0.002/m) / 2048 UU | `…HeightFogComponent.Density/StartDistance` | CONF | APPLIED (GL_EXP, softened) |
| Vehicle max speed | **1500 UU/s = 15 m/s** | `Default__TnHoverCarSimulationBlueprint.MaxLinearSpeed` | CONF | **APPLIED** (was guessed 32) |
| Vehicle acceleration | **3000 UU/s² = 30 m/s²** | `…MaxLinearAcceleration` | CONF | **APPLIED** |
| Vehicle dash/boost speed | **5000 UU/s = 50 m/s** | `…DashSpeed` | CONF | **SUPERSEDED (Pass 11):** 30 m/s (HoverTruck_Physics DashSpeed 3000) |
| Vehicle dash duration | **0.3 s** | `…DashDuration` | CONF | **SUPERSEDED (Pass 11):** 0.5 s (HoverTruck_Physics) |
| Vehicle hover height | **200 UU = 2.0 m** | `…SuspensionRadius` | CONF | **SUPERSEDED (Pass 11):** 1.85 m (HoverTruck_Physics SuspensionRadius 185) |
| Vehicle jump speed | **1200 UU/s = 12 m/s** | `…JumpLinearSpeed` | CONF | APPLIED |
| Vehicle turn rate | **~π rad/s** (180°/s) | `…AiMaxAngularSpeed` | CONF | **SUPERSEDED (Pass 11):** AI-only field; player steering rate PROV |
| Vehicle terminal velocity | 8000 UU/s = 80 m/s | `Default__TnTransformer.TerminalVelocity` | CONF | n/a (fall uncapped) |

---

## STREET COLLISION (RECOVERED this pass)
- **Authored collision participation recovered from props.json:** of 1952 placed static
  meshes, **1693 BLOCK** (CollideActors+BlockActors true) and **259 do not** (decorative:
  deco spheres, etc.). These flags come from the original ART/BASE actor/component properties.
- **UE3 collision model:** the player cylinder (non-zero extent) collides against each static
  mesh's collision. Default `UseSimpleBoxCollision=true` routes the player to the mesh's
  simplified BodySetup collision; architectural meshes commonly set per-poly. We reproduce the
  **per-poly path** (render geometry of blocking props) — faithful for the structural
  floors/walls/ramps that use it, an over-approximation for small simple-collision props.
- **Extraction extended:** new `AssetTools/scripts/wfc/vs_collision.py` regenerates
  `collision.glb` = pristine base (BSP-solid + blocking-volume hulls, kept as
  `collision_base.glb`) **+ 1693 blocking props** (instanced; file 5.46 MB → ~1.85 M world
  collision tris baked at load). Non-colliders excluded; oversize guard (>2000 m) excludes
  skydome/background shells (0 hit).
- **Verified:** player stands on the street floor at the authored FFA spawn (Y −724.5) and at
  4 spawns spanning ~180×164 m; auto-walk is now correctly **blocked by building walls** it
  previously passed through. Collision bounds Y [−800..−329].
- **Remaining:** simple-collision (BodySetup convex/box) extraction for props that use it;
  vehicle RB collision channel; moving-platform (InterpActor) dynamic collision (baked at rest
  position); exact MaxStepHeight/slope (pawn exe defaults).

## MAP / WORLD (investigated)
- `world.glb` is composed from **all three sublevels** (ART/AUDIO/BASE): BSP 2460 tris
  (ART only) + **1952 placed static meshes** + spawns/objectives. It is NOT a thin subset.
- Collision extent spans ~740 m (X) × 451 m (Z) in gltf metres (BSP-solid + blocking-volume
  convex hulls). Auto-walk forward from the FFA spawn is blocked by a wall after ~23 m — a
  building/volume ahead, not the map edge.
- **Known gaps (why it can look smaller/simpler than the original):**
  1. **Static-mesh collision not extracted** (`collision.json` note) — the 1952 prop meshes
     have no collision, so floors/ramps built from static meshes are not walkable and some
     structures are pass-through. Highest-value map task: build collision from prop render
     meshes where `collide/block=true` (data is in `props.json`).
  2. **PrefabInstance (16)** and **HeightFog** actors are not composed into `world.glb`
     (unhandled in `vs_map.py`). Prefabs hold modular set-dressing; missing them thins detail.
  3. Lighting is **baked into lightmaps** (StaticLightCollectionActor) which were **not
     extracted** — the single biggest reason the scene reads flatter than the original.

## LIGHTING / MATERIALS (investigated + partially fixed)
- Washed-out cause identified: the fixed-function renderer used global-ambient 0.35 + light-
  ambient 0.35, flooring every surface at ~0.70 brightness (no contrast). **Fixed [PROV]:**
  ambient lowered (global 0.14 / light 0.10), warm directional key, separate **specular**
  term (metallic highlights).
- **HeightFog RECOVERED [CONF]** from `MP_IAC_Streets_ART_m HeightFogComponent_11010`:
  LightColor **(234,91,116)** warm red-pink, Density **2e-5/UU = 0.002/m**, StartDistance
  2048 UU. Applied (GL_EXP, density softened to 0.0014/m for the play-area scale; clear colour
  warmed to match). This replaced the earlier **guessed blue-grey** fog.
- **BAKED LIGHTMAPS — FULLY RECOVERED, DECODED, AND RENDERED [CONF].** Streets is now lit by
  its original authored baked lightmaps instead of the stand-in directional/ambient.
  - **Serialization CRACKED (file_version 511, licensee 144).** After a component's tagged
    props the native `FStaticMeshComponentLODInfo` block is **big-endian**:
    `… [LightMapType=2] [LightGuids.Num: byte] [LightGuids: Num × FGuid(16)]`
    `[lead int32=0] 3 × ([tex: BE int32 export index][ScaleVector: 3 BE floats][1.0])`
    `[CoordinateScale: 2 BE floats] [CoordinateBias: 2 BE floats]`.
    It is a **directional lightmap** (3 coefficient textures); LightGuids[0] is a shared
    dominant-light GUID. The **texture is a positive export index into the ART package's own
    export table** (a seekfree forward-export whose name matches the `_LM` atlas) — this solved
    the "no imports / GUID" blocker.
  - **Values recovered:** per prop instance — atlas (coeff-0 `LightMapTexture2D`),
    CoordinateScale (e.g. 0.0625 = 1/16), CoordinateBias, and the coeff-0 ScaleVector (HDR).
    `AssetTools/scripts/wfc/vs_lightmap.py` parses all **1793 lightmapped components** and joins
    them to props (**1755/1952 props → lightmap**, 20 atlases used). Atlases decoded to PNG via
    umodel (1024² DXT1) in `…/MP_IAC_Streets/lightmaps/`.
  - **Rendering:** `vs_map.py` now emits TEXCOORD_1 + per-instance lightmap node extras;
    `world.glb` carries them; the loader attaches them to submeshes; the renderer draws
    lightmapped submeshes **unlit** then **multiplies by the atlas** (blend DST_COLOR·ZERO,
    UV1 via a texture matrix = uv1·CoordinateScale + CoordinateBias), with the **HDR ScaleVector
    applied via GL_COMBINE RGB_SCALE 4×**. 1962 submeshes lit. A/B toggle: `WFC_NOLIGHTMAP=1`.
  - **Verified** across spawn 0 / region 6 / region 18: correct per-region baked shadows and
    coloured bounce (purple, green/teal, warm), high contrast, bright lit doorways; no wrong-
    atlas / UV-flip / seam artefacts. Colour space: textures sampled as-is (sRGB-ish), modulate
    then HDR-scale — matches WFC's moody baked look.
  - **Remaining [PROV]:** only coeff-0 of the 3 directional coefficients is used (no per-pixel
    normal reconstruction — a shader refinement); ScaleVector >4 clamps (rare); BSP surfaces
    have no static-mesh lightmaps (BSP lightmaps are a separate path, not yet done).
  - **Pipeline ordering:** run `vs_lightmap.py` → `vs_map.py` (writes world.glb + base
    collision.glb) → `vs_collision.py` (restores full prop collision). vs_map resets
    collision.glb, so vs_collision must run last.
- **Emissive RECOVERED + APPLIED [CONF]:** Optimus's authored emissive textures
  (`textures/*_emissive.png`, the glow mask — blue optics/energon/Autobot vents on black) are
  loaded (derived as the `_basecolor`→`_emissive` sibling) and drawn as an **additive
  self-illumination pass** (GL_ONE/GL_ONE, unlit, depth-write off). Result: Optimus's eyes and
  energon details glow blue (iconic WFC look), from original data, not invented. Applies to
  robot/vehicle/weapon. [PROV] emissive *intensity* scale not recovered (drawn at 1×).
- Still missing: **normal/specular maps** (roles extracted; need a programmable GL path),
  map/environment emissive (different naming), tone-mapping / bloom / DOF.

## CONFIRMED ORIGINAL (authored data)
- Streets map = three sublevels **BASE** (gameplay: 24 FFA + 60 team starts, 58 blocking
  volumes, pickups, objectives) + **ART** (visual: BSP 2460 tris, 34 StaticMeshActors,
  16 PrefabInstances, 25 decals, HeightFog) + **AUDIO** (40 AmbientSound + 30 Hm spatial
  emitters). Source: `maps/MP_IAC_Streets_*_m.json`.
- Gravity, transform blend times, camera FOV, Ion Blaster stats — see table.

## HIGH-CONFIDENCE RECONSTRUCTION
- GLB skinning / animation sampling; material base-colour; collision ground query.

## PARTIALLY CONFIRMED
- Vehicle movement: forms (TnTruckForm/TnCarForm) are **modifier** layers (=1.0) over
  compiled base values; WFC vehicles use RB thruster/suspension/hover physics. Base
  constants live in `default.xex` — NOT yet recovered.
- Map completeness: 1952 props + BSP composed, but **PrefabInstance (16)** and **HeightFog**
  not yet composed; **static-mesh collision not extracted** (only BSP-solid + blocking hulls).

## WEAPON SOCKET (RECOVERED this pass)
- `WeaponSocket_Primary` relative transform (character.json): loc_ue [-40,0,0],
  rot_ue [pitch 0, yaw 31311 = 172°, roll 5461 = 30°]. Converted to a gltf-space socket
  matrix (via `vs_common.ue_rot`/`ue_to_gltf_matrix`) and applied — the Ion Blaster now holds
  with the correct orientation (barrel forward along the arm), not translation-only.

## CAMERA (partial)
- Recovered: pivot/eye height 2.8 m [CONF] (CollisionHeight 200 + BaseEyeHeight 80 UU).
  Flying-cam pitch range ±60°, first-person rotation ±45°, flying AnchorOffset [0,0,220 UU].
- STILL [PROV]: ground third-person **follow distance** — WFC selects it from an orbit-distance
  list (`TnLocationOffsetCameraBehavior.CurrentOrbitDistanceIndex`); the distances live in a
  camera data asset / `DefaultCamera.ini` that was not extracted. Keeping 9 m. Mouse
  sensitivity, ground-cam pitch limits still provisional.

## VEHICLE PHYSICS (RECOVERED this pass)
- WFC ground vehicles **hover**; Optimus's truck uses **`Default__TnHoverCarSimulationBlueprint`**
  (TransGame.xxx). The `TnCarForm`/`TnTruckForm` CDOs only carry `=1.0` modifiers; the base
  values live on the *Simulation* blueprint classes (which I'd missed earlier). Recovered +
  applied: max speed 15 m/s, accel 30 m/s², dash/boost 50 m/s (0.3 s burst), hover height 2 m,
  jump 12 m/s, turn ~π rad/s, terminal 80 m/s. Verified: vehicle hovers 2 m above the street
  and cruises ~13.6→15 m/s. (Non-hover `TnCarSimulationBlueprint` = MaxSpeed 500 UU; unused.)
- Remaining: reverse speed + braking/damping constants (via LinearDamping, not a single value);
  per-axis turn rate; exact dash-as-impulse vs the accel-ramp approximation; velocity transfer
  at transform (currently zeroed).

## PROVISIONAL / APPROXIMATE (rebuild guesses — NOT evidence)
- ~~Camera follow distance (9 m); ground-cam pitch limits~~ → 8 m / ±75° CONF (Pass 11). Mouse sensitivity still PROV.
- ~~MaxStepHeight (kStepUp 0.6 m)~~ → 0.35 m CONF (Gameplay Pass 10, TnRobotForm; Systems also found no override of the Engine.Pawn default 35 UU).
- Audio master level 0.5 and mixer-category / reverb treatment (weapon cue radii now CONF, Systems PASS 7; non-weapon transform/land cues still generic falloff).
- Locomotion crossfade time (0.15 s) for non-moving transitions (idle↔moving is CONF 0.2 s, Pass 10).

## AUDIO (investigated + fixed)
- Original Streets audio = 40 `AmbientSound` + 17 `HmAmbientSoundVolumeEmitter` +
  13 `HmAmbientSoundLineEmitter` (spatial bed) with SoundCues; weapon/transform cues are
  FMOD SoundCues. Exact per-cue attenuation radii/reverb not yet extracted.
- "Too loud / not spatial" cause: the mixer played every cue at full volume, mono, no
  attenuation. **Fixed:** master level 0.5 [PROV], and a 3D path (distance attenuation +
  equal-power stereo pan vs the camera listener) via `IAudio::playAt`/`setListener`. Fire is
  positioned at the muzzle; land/transform/reload at the pawn.
- Remaining: the ambient emitter bed, reverb (MASTER_WET zone presets), interior/exterior treatment,
  occlusion. Cue radii/falloff/variation/concurrency, per-cue SmartPan and owner attachment are done
  (see MILESTONE 03 SYSTEMS).

## NOT YET IMPLEMENTED
- Normal/specular/emissive materials; lightmaps/baked lighting; HeightFog; post FX (bloom/DOF).
- ADS/fine-aim mode; camera recoil/shake; per-shot spread visualization.
- Prefab geometry; static-mesh collision; spatial/reverb audio.

---

## OPEN QUESTIONS → EVIDENCE NEEDED
- Robot GroundSpeed/JumpZ/AccelRate: read `TnPawn`/robot-form class defaults in `default.xex`
  (Ghidra/ReVa) or measure root motion from `Nav_*` clips.
- Camera follow distance/offset: HM camera behavior assets (cooked packages) or exe.
- Vehicle base speed/accel/turn: `TnCarForm`/`TnTruckForm` compiled defaults in exe.
- Lighting: lightmaps not extracted; dominant directional light direction/colour from map.
