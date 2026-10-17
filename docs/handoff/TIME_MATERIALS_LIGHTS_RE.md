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


## (3) Top-20 most visible time-driven surfaces, with crop coordinates

Tool: `tools/time_offenders.py` → `notes/data/time_offenders.json` / `.md` (all 313 ranked candidates in the json).

**Ranking.** Every placed world surface (static meshes incl. StaticMeshCollectionActor members, decals) whose material is
time-driven (section 1); FX sprites are excluded (particle audit).
- Score = Σ over placed instances and mesh sections of (glTF section bbox diagonal × instance scale)² (decals: width × height),
  × a weight: 3 if the time term reaches EmissiveColor (or Opacity of a translucent / unlit material), 2 for Diffuse / Normal, 1 otherwise.
- Only Streets has a renderer draw dump, so screen coverage is a placed-surface proxy, not a measured pixel count.
- Max 3 per map, every map represented.

**Since every row is K** (time term present), these are the surfaces where a wrong speed / phase / scroll direction would
show most, i.e. where Rendering's visual check pays off. The sky / space domes rotate very slowly (0.0006–0.001 rad/s:
one turn in 1.7–2.9 h), so for those, compare the **pan direction** over a ~60 s A/B rather than a single frame.

**Crop coordinates.**
- Target = the first placed instance (UE units; instance count shown).
- Camera = the nearest player start, with the UE yaw / pitch (65536 = 360°) that faces the target.
- For domes centred at the origin, any start works: pitch up.

| # | Map | Material (master) | Time sources -> outputs | Surface proxy m² (weight) | Target UE (x, y, z) / instances | Camera: nearest start, dist, yaw / pitch (UE units) |
|---|---|---|---|---|---|---|
| 1 | MP_ORB_Debris | ENV_ORB_Spacedome_p.Spacedome_Nebula_MAT (Spacedome_Nebula_MAT) | Panner(0.0001, 0.0001); Rotator(0.0006 rad/s @0.5,0.5); Rotator(0.001 rad/s @0.5,0.5) -> EmissiveColor | 8574368401 (×2) | [0, 0, 0] / 1 | TnTeamPlayerStart_5212 @ [8251, 4417, 11459], 14796 uu, 37895 / -9241 |
| 2 | MP_ESC_Remnant | ENV_ORB_D1_Architecture_p.Materials.Spacedome_NebulaBright_A_MAT (Spacedome_NebulaBright_A_MAT) | Panner(0.0001, 0.0001); Rotator(0.0006 rad/s @0.5,0.5); Rotator(0.001 rad/s @0.5,0.5) -> EmissiveColor | 238176908 (×3) | [-57157, -4857, 18209] / 1 | TnTeamPlayerStart_1834 @ [-1664, -2304, 727], 58238 uu, 33247 / 3180 |
| 3 | MP_IAC_Streets | ENV_IAC_Background_p.Materials.Iacon_Nebula_D_MAT (Iacon_Nebula_D_MAT) | Panner(0.0001, 0.0001); Rotator(0.0006 rad/s @0.5,0.5); Rotator(0.001 rad/s @0.5,0.5) -> EmissiveColor | 238176905 (×3) | [-33885, -28680, 117502] / 1 | TnFreeForAllPlayerStart_9414 @ [9088, -38528, -70572], 193172 uu, 30418 / 13982 |
| 4 | MP_IAC_Seed | ENV_ORB_Spacedome_p.Spacedome_ORBITAL_MAT (Spacedome_ORBITAL_MAT) | Panner(0.0001, 0.0001); Rotator(0.0006 rad/s @0.5,0.5); Rotator(0.001 rad/s @0.5,0.5) -> EmissiveColor | 238176900 (×3) | [-14078, -6501, 55296] / 1 | TnTeamPlayerStart_952 @ [-7296, -8576, 1623], 54139 uu, 29671 / 15014 |
| 5 | MP_UND_Complex | ENV_ORB_Spacedome_p.Spacedome_ORBITAL_MAT (Spacedome_ORBITAL_MAT) | Panner(0.0001, 0.0001); Rotator(0.0006 rad/s @0.5,0.5); Rotator(0.001 rad/s @0.5,0.5) -> EmissiveColor | 238176900 (×3) | [9472, -640, 512] / 1 | TnFreeForAllPlayerStart_9832 @ [11070, 4799, 984], 5689 uu, 46171 / -866 |
| 6 | MP_IAC_Berth | ENV_ORB_Spacedome_p.Spacedome_MAT (Spacedome_MAT) | Panner(0.0001, 0.0001); Rotator(0.001 rad/s @0.5,0.5) -> EmissiveColor | 119088450 (×3) | [-6382, -13994, -3673] / 4 | TnTeamPlayerStart_9199 @ [-4748, -15170, -1033], 3320 uu, 26262 / -9587 |
| 7 | MP_IAC_Rust | ENV_ORB_Spacedome_p.Spacedome_MAT (Spacedome_MAT) | Panner(0.0001, 0.0001); Rotator(0.001 rad/s @0.5,0.5) -> EmissiveColor | 119088450 (×3) | [-6382, -16442, -3673] / 4 | TnTeamPlayerStart_895 @ [-1290, -18966, 1108], 7427 uu, 27967 / -7295 |
| 8 | MP_KON_Molten | ENV_KON_Molten_p.Material.KaonSmokeMolten_MATINST (SmokeCoolDepth_Mat) | Panner(0.02, 0.05) time-in; Time -> EmissiveColor, Opacity | 112703805 (×2) | [-7240, -60010, 71198] / 13 | TnFreeForAllPlayerStart_6312 @ [-7424, -18944, -170], 82340 uu, 49199 / 10938 |
| 9 | MP_KON_Molten | ENV_KON_StarDome_p.Test.KON_newSky_MAT (KON_newSky_MAT) | Panner(0.0001, 0.0001); Rotator(0.0006 rad/s @0.5,0.5); Rotator(0.001 rad/s @0.5,0.5) -> EmissiveColor | 67924420 (×3) | [0, 0, 0] / 1 | TnFreeForAllPlayerStart_11551 @ [768, 1408, -153], 1611 uu, 43944 / 990 |
| 10 | MP_IAC_Rust | ENV_IAC_LightSources_p.LightPlanes.IntroVistaSmoke_MATINST (SmokeCoolDepth_Mat) | Panner(0.02, 0.05) time-in; Time -> EmissiveColor, Opacity | 48356971 (×3) | [67259, -10154, -24881] / 6 | TnTeamPlayerStart_16276 @ [10472, -12754, 2244], 62987 uu, 477 / -4644 |
| 11 | MP_ESC_BrokenHope | ENV_IAC_LightSources_p.LightPlanes.BrightSmoke_MATINST (SmokeCoolDepth_Mat) | Panner(0.02, 0.05) time-in; Time -> EmissiveColor, Opacity | 47678393 (×2) | [-17075, 108685, 50440] / 8 | TnTeamPlayerStart_6416 @ [12650, -5179, 84], 128001 uu, 19048 / 4217 |
| 12 | MP_ESC_BrokenHope | ENV_ORB_Spacedome_p.Spacedome_IaconSunset_MAT_INST (SkyDome_PARENT_MAT) | Panner(0.0001, 0.0001); Rotator(0.0006 rad/s @0.5,0.5); Rotator(0.001 rad/s @0.5,0.5) -> EmissiveColor | 40389113 (×3) | [11840, -374, 8336] / 1 | TnTeamPlayerStart_6416 @ [12650, -5179, 84], 9584 uu, 18126 / 10820 |
| 13 | MP_IAC_Rust | ENV_IAC_LightSources_p.LightPlanes.StarScreamFightSmoke_MATINST (SmokeCoolDepth_Mat) | Panner(0.02, 0.05) time-in; Time -> EmissiveColor, Opacity | 10136176 (×2) | [-37561, 29936, -3729] / 2 | TnTeamPlayerStart_15519 @ [-34816, 6784, 2803], 24212 uu, 17615 / -2849 |
| 14 | MP_ESC_BrokenHope | ENV_IAC_LightSources_p.LightPlanes.BckSillouhetteSmoke_MATINST (SmokeCoolDepth_Mat) | Panner(0.02, 0.05) time-in; Time -> EmissiveColor, Opacity | 9569216 (×3) | [56796, -14182, 39724] / 1 | TnTeamPlayerStart_13338 @ [13297, -5985, 84], 59420 uu, 63593 / 7618 |
| 15 | MP_IAC_Seed | ENV_IAC_LightSources_p.LightPlanes.ChasmDepths_Light_MATINST (SmokeCoolDepth_Mat) | Panner(0.02, 0.05) time-in; Time -> EmissiveColor, Opacity | 4772747 (×3) | [3744, -4640, 42528] / 1 | TnTeamPlayerStart_177 @ [5760, -384, 1620], 41178 uu, 44538 / 15189 |
| 16 | MP_IAC_Seed | ENV_IAC_LightSources_p.LightPlanes.ChasmDepths_MATINST (SmokeCoolDepth_Mat) | Panner(0.02, 0.05) time-in; Time -> EmissiveColor, Opacity | 4595005 (×3) | [-27444, 20620, 11436] / 3 | TnTeamPlayerStart_13609 @ [-24954, 10232, 980], 14948 uu, 18838 / 8080 |
| 17 | MP_IAC_Streets | ENV_IAC_LightSources_p.LightPlanes.BckSillouhetteSmoke_MATINST (SmokeCoolDepth_Mat) | Panner(0.02, 0.05) time-in; Time -> EmissiveColor, Opacity | 2275095 (×3) | [963, -33098, -65408] / 20 | TnFreeForAllPlayerStart_9414 @ [9088, -38528, -70572], 11053 uu, 26623 / 5071 |
| 18 | MP_IAC_Streets | ENV_GLB_EnvEffects_p.Material.LightVolumePurplish_MATINST (LightVolume_Base_MAT) | Time -> EmissiveColor | 1992220 (×3) | [-13761, -28160, -65168] / 3 | TnFreeForAllPlayerStart_12027 @ [9600, -29056, -71340], 24179 uu, 32368 / 2692 |
| 19 | MP_UND_Complex | ENV_GLB_EnvEffects_p.Material.LightRaysV2Cool_MATINST (LightRays_V2_Parent_MAT) | Panner(0.015, -0.025) -> EmissiveColor | 619731 (×3) | [-722, -17107, 30409] / 3 | TnTeamPlayerStart_5189 @ [-768, -11264, -23], 30988 uu, 49234 / 14405 |
| 20 | MP_UND_Gorge | ENV_GLB_EnvEffects_p.Material.LightRaysV2WarmInside_Lighter_MATINST (LightRays_V2_Parent_MAT) | Panner(0.015, -0.025) -> EmissiveColor | 454356 (×3) | [-16627, 9546, 36079] / 2 | TnTeamPlayerStart_11061 @ [-18541, 4232, 3141], 33419 uu, 12777 / 14613 |

**Light gaps, for the same crop pass:**

| Item | Map | Target UE | Camera |
|---|---|---|---|
| PointLightToggleable_1918 (missing always-on dynamic light, r 4096 uu, sRGB 64/187/234) | MP_ESC_Remnant | (12384, 13792, 4461) | TnTeamPlayerStart_1675 @ (−1664, 2304, 727), 18527 uu, yaw 7150 / pitch 2117 |
| Muzzle light colour (owner energon colour vs authored) | any | a firing pawn | any; best a character with a non-default energon colour, at night-lit Debris |

**The time-materials / dynamic-lights audit is complete.**

## Remnant PointLightToggleable_1918: cooked state and world interaction (2026-10-18)

**(3) Cooked flags (C):**
- **Authored on the component:** Radius 4096, RadiusOfInfluence 4055, LightColor (64, 187, 234), **bCastCompositeShadow True,
  bForceDynamicLight True**, LightAffectsClassification LAC_USER_SELECTED, LightGuid 29405897 / 1214979729 / 77194133 / 2399548689.
- **From the class default** (Default__PointLightToggleable.PointLightComponent0): **UseDirectLightMap false, bToggleable true**.
- **From Default__LightComponent:** Brightness 1.0, **bEnabled True, CastShadows True, CastDynamicShadows True**, LightingChannels
  **BSP + Static + Dynamic + CompositeDynamic** (bInitialized).
- **From Default__PointLightComponent:** FalloffExponent 2.0.
- **Not built:** bHasLightEverBeenBuiltIntoLightMap is unset (false).

**(2) Initial state and toggles (C):**
- **On at load** (bEnabled CDO true).
- **Nothing toggles it:** in every Remnant package the only object that names `PointLightToggleable_1918` is the actor itself. None of
  the MP maps' 199 SeqAct_Toggle / 162 SeqAct_ToggleHidden actions targets it, and no MP matinee track drives a light property.

**(1) World interaction:**
- **No precomputed data except its LVV entry (C).** A byte search of MP_ESC_Remnant_ART_m / _BASE_m for its LightGuid finds only the
  component and the **LightsVisibilitiesVolume**. No lightmap LightGuid list, no ShadowMap1D / 2D and no IrrelevantLights entry
  references it.
- **It lights the lightmapped world per pixel (H, stock semantics):** bForceDynamicLight + the BSP / Static channels + not built into
  any lightmap mean the BSP and static meshes within 41 m get it as a dynamic per-pixel light.
- **Occlusion (H / UNKNOWN):**
  - there is no static shadowing (nothing precomputed);
  - stock dynamic shadows for a point light are per-object projected (or shadow-volume) shadows cast by **dynamic** primitives
    (characters), not by the BSP / static walls;
  - bCastCompositeShadow adds it to the characters' composite (DLE) shadow direction.
  - **Whether WFC's LVV culls it per world primitive** (and so stops it leaking through Remnant's walls), or only feeds the DLE
    sample points, was **not traced**. It is the remaining UNKNOWN; the next step is the native callers of the LVV query.

**LVV and the world: answered (2026-10-18, C).**
- The LVV query `LightEnv::QueryLightsVisibility` (**0x82CCAC30**) has **exactly one caller**, **0x82CE4E78**, the light-environment
  full update (headless-Ghidra refs; MILESTONE04_STREETS_RUNTIME_SEMANTICS.md).
- The LVV is a baked visibility lookup **for (Dynamic)LightEnvironments only**: pawns, vehicles, pickups, totems. It has **no effect on
  BSP, static meshes, particles or visibility culling**.
- **So the original does not cull PointLightToggleable_1918 on world primitives with its LVV entry.** Its LVV entry only scales the
  light for characters / DLE owners (direct-light selection, SH overflow, composite-shadow fade).
- **World side:** as a forced-dynamic, unbuilt light it lights BSP / static meshes per pixel within its radius, with **no
  precomputed shadowing** (none exists).
- Dynamic shadowing of the world by static geometry is not part of WFC's MP shadow path: projected shadows come from characters
  (MILESTONE03 light-visibility / shadow notes) (H).
- **So in the original it reaches through Remnant's walls within 41 m**, limited only by its distance falloff (FalloffExponent 2),
  brightness 1 and the surfaces' lighting channels.
- **For Rendering:** draw it on the world unculled (radius / falloff only); apply the LVV only to the character / DLE path.
