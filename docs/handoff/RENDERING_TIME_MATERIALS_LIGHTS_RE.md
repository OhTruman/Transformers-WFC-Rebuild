# Time-driven materials and dynamic lights on the MP maps (RE, 2026-10-17)

> **Caveat (applies to every table):** K means our translated GLSL contains the time term (the authored expression was
> compiled with `m.time`), **not** that the speed, phase or time base match the original. C = named / invoked in our
> source, not proven faithful.

Labels: CONFIRMED (C) = from cooked data / script bytecode / binary; HIGH (H) = stock UE3 reading not traced in the WFC
binary; UNKNOWN = not established.

Data: `notes/data/time_material_audit.json` / `.md` (tool `tools/time_material_audit.py`, read-only).

## (1) Time-driven materials

**Method.** Every material in each MP map's `Rebuild/work/render/<MAP>/materials_glsl.json` (world, props, weapons,
characters, FX: what our build translated) plus every material in the map's renderer draw dump (`active_dump.jsonl`).
For each, the master Material's graph is walked from the connected outputs:
- static switches resolved with the values our translation recorded (`info.switches`), else the expression default;
- material functions followed pin-accurately (the call's output index selects the OutputBlock pin; InputBlock pins
  resolve to the call's inputs in the caller frame). A first pass that walked functions whole falsely flagged 119
  `ENV_PhongMain_NS_Mat` instances whose `ENV_Emissive_MF` pulse (Time → Sine) sits behind `Enable_Emissive_Pulse` = off.

Time sources counted only when they reach an output: Panner / Rotator (implicit time, or a Time input that itself depends
on time), Time, FlipBookSample, TextureSampleParameterMovie, Sine / Cosine fed by time.

**Result (C):**

| Map | Time-driven materials | K | D (dropped) | E (error) | ? (drawn, untranslated) |
|---|---|---|---|---|---|
| MP_ESC_BrokenHope | 317 | 317 | 0 | 0 | 0 |
| MP_ESC_Remnant | 322 | 322 | 0 | 0 | 0 |
| MP_IAC_Berth | 299 | 299 | 0 | 0 | 0 |
| MP_IAC_Rust | 313 | 313 | 0 | 0 | 0 |
| MP_IAC_Seed | 316 | 316 | 0 | 0 | 0 |
| MP_IAC_Streets | 311 | 311 | 0 | 0 | 0 |
| MP_KON_Molten | 308 | 308 | 0 | 0 | 0 |
| MP_ORB_Debris | 300 | 300 | 0 | 0 | 0 |
| MP_UND_Complex | 303 | 303 | 0 | 0 | 0 |
| MP_UND_Gorge | 304 | 304 | 0 | 0 | 0 |

- Reverse check: **0** translated materials use `m.time` without an authored time source reaching an output.
- Source mix (Streets): Panner 513, Time 219, Sine 108, Rotator 24, Cosine 11, FlipBook 5. **No** MaterialInstanceTimeVarying
  exists in the game data (0 objects); no Noise / Oscillator expressions in WFC's UE3 build.
- Our compiler (`Rebuild/tools/render/matc.py`) maps each node like stock UE3 (C, read): Panner `uv + t·(SpeedX, SpeedY)`;
  Rotator `rot(t·Speed)` about (CenterX, CenterY); Sine / Cosine with `Period` (2π/Period); FlipBook
  `frame = floor(mod(t·FrameRate, H·V))`, row order.
- FlipBook FBMethod: one texture authors `TFBM_RANDOM` (`PROP_NEU_Monitors_p.Texture.Flipbook_Screen_CLR`); it is **not drawn on
  any MP map**. Ours always uses row order; no MP impact.

**So the graph-level translation is complete. The remaining differences are in the time base.**

### Time source

| | Ours (C, `Rebuild/src/render/gl/WfcPipeline.cpp:8561`) | Original |
|---|---|---|
| Material time | `steady_clock` seconds since the **first rendered frame of the process** (`static t0`); `WFC_LOCKSTEP`: `(frameNo − lockBase)/60` | Panner / Rotator / Time (bIgnorePause unset on **all 216** Time nodes, C data) = **world time** (`WorldInfo.TimeSeconds` via the view family's CurrentWorldTime) **(H, stock UE3)** |
| Resets at map load | no (continues across maps and the front end) | yes (world time restarts per level) (H) |
| Pauses | never | with the world (H) |
| Time dilation | none | × `WorldInfo.TimeDilation` (H) |
| FlipBook | same `m.time` | `UTextureFlipBook` ticks its own frame time from engine DeltaTime (H) |

**Why this is benign in MP (C):**
- MP cannot pause: `TnGame.bPauseable = False` (TransGame CDO); TnMultiplayerGame / TnTeamGame / TnFreeForAllGame / TnVersusGame /
  TnSurvivalGame do not override it, and `bAdminCanPause = false` (Xe-TransGame.ini). (Only the campaign sets bPauseable True.)
- MP never changes TimeDilation: the only script writers are `GameInfo.SetGameSpeed` (cheat / admin) and the stock players-only paths.
- So world time and real time advance at the same rate during an MP match. **The visible difference is phase only**: ours
  starts at the process start, not at the map load. **No speed mismatch is expected.**
- Residual: ours grows across a whole session (float precision on `t·Speed` after many hours: ~1 ms granularity at 10⁴ s;
  negligible). Optional: reset `t0` at map load to match the original phase.

### Material parameters driven at runtime (script / Kismet / matinee)

| Driver | Parameter (material) | Ours |
|---|---|---|
| `TnObjectiveMarkerType*` (C script) | `Neutral`, `Flashing`, `TeamID`, `PingOpacity`, `Health`, `Alpha`, `OnScreenParam` / `FocusParam` / `RotationParam` | C (HudMarkers.cpp / World.cpp) |
| `TnObjectiveMarkerTypeDomination` (C) | **`NodeID`** = point number (`UI_HudMarkers_p.MPMarkerDominationNode_MAT`) | **-** (no hit in src) |
| `TnWeapon` tick → `SetWeaponGlow(Heat)` → MPT_HeatGlow parameter | every weapon mesh authors MPT_HeatGlow name **None**, so it is a no-op on materials (only the WeaponGlowSoundParameter audio parameter is set) | n/a |
| `TnChargeWeapon` → `SetMaterialParameter(1 = MPT_WeaponSpecific)` | `Overheat` (PlasmaCannon_WEPMESH, the only weapon that names it) | C (World.cpp:3733) |
| `HmMaterialEffectData` | post-process overlay transparency (screen effects) | covered in presentation audits 2 / 3 |
| Matinee material-param tracks in MP packages | 2 × `Opacity` + 3 × DrawScale / DrawScale3D, all in `MP_ORB_Debris_BASE_m …Main_Sequence.Old.7_Kill_Streak_(Tryp_and_Omega)` (an "Old" subsequence) and BrokenHope `Megatron` DrawScale3D | not a material time term; no further work |

## (2) Dynamic lights

**Census (C, authored.db, all light components owned by MP map packages + their classes):**
- **2,690 of 2,700** placed MP lights are in StaticLightCollectionActor (baked; 11 of them `bEnabled = false`: they stay off).
- **0 LightFunctions** in any MP package (all 88 are campaign). **0** matinee tracks on light brightness / colour in MP
  (the only InterpTrackFloatProp in MP is DrawScale). **No** SeqAct_Toggle in MP targets a light.
- Berth: 6 PrefabInstances of `ENV_IAC_Turret_p.Pieces.{blue,orange}spot_berth` (PointLight + SpotLight archetypes); their
  placed lights are baked like the rest (H: the prefab archetypes are the only non-collection light objects).

**Runtime lights:**

| Light | Behaviour (C unless marked) | Ours |
|---|---|---|
| Muzzle flash `HmWeaponMesh.MuzzleFlashLight` | On `StartMuzzleFlash`: enabled if `EnableMuzzleLight` (default True) and not `MuzzleFlashes[i].DisableLight` **and `GetGameThrottle() < 2`**; disabled after `MuzzleFlashDuration` (0.1 s) unless the flash loops (`PSLoops`, e.g. Code of Power's WP_Looping: stays on). Per-weapon radius / brightness / falloff from the WEPMESH component | C (`src/game/MuzzleLights.inc`, generated) |
| **Muzzle light colour** | `TnWeaponMesh.Attach` → `ApplyCharacterColorsToWeapon`: **`MuzzleFlashLight.SetLightProperties(, Pawn.EnergonColor)`**: the authored colour is **replaced by the owner's energon colour** (brightness kept) on every attach | **Gap:** ours uses the authored WEPMESH colour (`MuzzleLights.inc` "linear colour × Brightness") |
| Death light (`TnRampLightManager`, service `TnDeathLightService`, component `DeathLight0`) | Triggered by `TnDeathModifierTriggerLight` / `TnAnimNotify_TriggerDeathLight` with RLPs (Radius, FalloffExponent, Translation, Brightness, LightColor, RampInTime, LightTime, RampOutTime). Per pawn tick: `B = min(t/RampIn, 1)·Brightness`; from `LightTime − RampOutTime`: `B = (1 − (t − start)/RampOut)·Brightness`; disabled after LightTime | C (`src/game/DeathLights.inc`, World::spawnDeathLight) |
| Cloak light (`CloakLight0`, `TnCloakLightService`) | Initialised in TnPawn and updated per tick, but **no script caller of TriggerLight on the cloak service** (only the death service is triggered) | n/a (H: never lit in MP) |
| **Remnant `PointLightToggleable_1918`** | A **toggleable (dynamic) point light, always on** (no Kismet toggle, bEnabled default): radius 4096 uu, colour sRGB (64, 187, 234), brightness 1 (default), at UE (12384, 13792, 4461), MP_ESC_Remnant_ART_m. Toggleable lights are not baked into lightmaps; they light dynamically every frame (H, stock UE3) | **Gap:** absent from our `work/render/MP_ESC_Remnant/lighting.json` (the build reads StaticLightCollectionActor lights only) |
| Energon pickups (`TnAbilityResourcePickup` / `DroppedPickup`) | **No light component**: the glow is the particle system (`FX_Pickups_p.FX.Shard_FX`, Shard_PowerUp_FX / Shard_FadeOut_FX) | covered by the FX runtime |
| Pickup factories (ammo / health / weapon / overshield) | particle `PickupEffect` only, no lights | as above |
| Emissive-driven glows | material time terms only (section 1); emissive does not cast light at runtime (no dynamic GI) | K |

**Gaps (actionable):**
1. Muzzle-light colour = owner's EnergonColor (CONFIRMED script).
2. Remnant's always-on toggleable point light is missing (C data; its dynamic-light behaviour H).
3. Domination marker `NodeID` parameter is not set (C script).
4. Optional: reset material time at map load (phase only).

(3) Top-20 visible time-driven surfaces with crop coordinates: section below (separate commit).
