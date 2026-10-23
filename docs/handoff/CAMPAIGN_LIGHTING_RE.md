# Campaign runtime lighting: what the campaign does that MP never does (RE, 2026-10-23)

For Rendering's campaign build.
Labels: C = TransGame / Engine bytecode or the WFC binary; data counts are C (authored.db + AssetTools
`next_map/<chapter>_{kismet,matinee}.json`); H = stock UE3 behaviour not traced natively.
Chapter = the chapter whose manifests reference the package (419 packages mapped; script `work/q/pkg_chapter.json`).

**MP comparison:** the 10 MP maps have 1 PointLightToggleable, 1 volume-linked height fog, 2 FloatMaterialParam tracks and none
of the rest below. Everything here is effectively campaign-only.

## 1. Runtime-changeable lights per chapter (C, data)
Only these actors change at runtime: Toggleable lights are switched; Movable lights move or are animated. Plain PointLight /
SpotLight actors are static (baked) and **ignore Toggle** (§2). StaticLightCollectionActor holds baked lights.

| Chapter | PointLight Toggleable | PointLight Movable | SpotLight Toggleable | SpotLight Movable | DirectionalLight Toggleable | SkyLight Toggleable | light comps disabled at start | comps with a LightFunction |
|---|---|---|---|---|---|---|---|---|
| A1_IAC | 5 | 2 | – | 1 | 1 | – | 24 | 9 |
| A2_KON | 5 | 8 | – | 6 | – | – | – | – |
| A3_COR | 10 | 2 | – | 8 | – | – | 5 | – |
| A4_ORB | 18 | 9 | – | – | 1 | – | 9 | 16 |
| A5_TRY | – | 1 | – | – | 3 | – | 1 | – |
| D1_ORB | 5 | 13 | – | 1 | 1 | – | – | 4 |
| D2_COR | 1 | 6 | 11 | 2 | – | 1 | 2 | – |
| D3A_IAC | 22 | 1 | 3 | – | – | – | 14 | 19 |
| D3B_IAC | 10 | 10 | 1 | – | 2 | – | 45 | 31 |
| D5_OME | – | – | – | – | – | – | – | 4 |

- Light-component totals per chapter run 170 (D5) to 2,750 (A2), almost all baked.
- "Disabled at start" = authored `bEnabled false`. These are switched on by Kismet or Matinee.

## 2. Kismet on lights, fog and post-process (C)
**`SeqAct_Toggle` (inputs Turn On / Turn Off / Toggle):**
- **Lights:** `Light.OnToggle` does nothing when `bStatic` (plain, baked lights). Otherwise `LightComponent.SetEnabled(on / off /
  !enabled)`, then bEnabled is mirrored and replicated. Toggleable, Movable and Spawnable lights respond.
- **HeightFog:** `HeightFog.OnToggle` → `Component.SetEnabled` (same three inputs).
- **PostProcessVolume / DynamicPostProcessVolume:** `OnToggle` sets **bEnabled** (on / off / flip). A disabled volume drops out
  of the per-view volume selection (PPV_HEIGHTFOG §1), so its settings and linked fogs transition out.

**Authored Toggle targets** (from the per-chapter kismet link summaries):

| Chapter | Toggle → light | Toggle → PP volume | Toggle → HeightFog | Matinee bound to light actors |
|---|---|---|---|---|
| A1_IAC | PointLightToggleable 2 | DynamicPPV 2 | – | PointLightToggleable 8, SpotLightMovable 1 |
| A2_KON | PointLightToggleable 3, light-energon interact 2 | DynamicPPV 1 | – | PointLightToggleable 5 |
| A3_COR | PointLightToggleable 18, energon 5 | PPV 2 | – | PointLight Toggleable 11 / Movable 1 |
| A4_ORB | PointLightToggleable 18 | DynamicPPV 2 | – | Toggleable 1, Movable 2 |
| A5_TRY | PointLightMovable 2 | DynamicPPV 5 | – | PointLightMovable 1, DirectionalLightToggleable 2 |
| D1_ORB | – | PPV 1 | – | DirectionalToggleable 1, PointToggleable 5, Movable 2 |
| D2_COR | PointLightMovable 2 | DynamicPPV 20, PPV 11 | **1** | SpotToggleable 11, PointMovable 3, PointToggleable 1 |
| D3A_IAC | PointLightToggleable 27, SpotLightMovable 5 | PPV 34 | – | PointToggleable 26, SpotToggleable 2 |
| D3B_IAC | PointLightToggleable 8, PointLightMovable 1 | PPV 16, DynamicPPV 2 | – | PointToggleable 10 |
| D5_OME | – | PPV 4 | – | – |

**Material actions:**

| op | count (campaign) | semantics (C) |
|---|---|---|
| `SeqAct_SetActorMatInstScalarParam` / `VectorParam` | 92 / 62 (D1 79 / 44) | `Target.SetMaterialInstance{Scalar,Vector}Parameter(Name, Value)` → for **every MeshComponent** on the actor → `MeshComponent.Set{Scalar,Vector}Param` |
| `SeqAct_SetMatInstScalarParam` / `VectorParam` | 0 / 9 | stock: sets the parameter **on the MIC asset itself**, so every user of that MIC changes (H, stock native) |
| `SeqAct_SetMaterial` | 52 | `DynamicSMActor / Pawn / SkeletalMeshActor.OnSetMaterial` → `Mesh.SetMaterial(MaterialIndex, NewMaterial)` |

**`MeshComponent.Set{Scalar,Vector}Param`, per material slot (C):**
- skip slots that aren't a MaterialInstanceConstant;
- if the MIC isn't already owned by this component, **clone it** (`CreateInstanceAndSet…ParameterValue`, outer = the component),
  set the parameter, and assign the clone to the slot;
- otherwise set the parameter on the existing clone.
- So the change is **per component** and persists. Plain Material slots are untouched.

## 3. Matinee on lights and materials (C for data; H for the native property write)
**Property tracks on light actors** (all chapters): `InterpTrackFloatProp` on
- `PointLightComponent0.Brightness` 58;
- `SpotLightComponent0.Brightness` 9;
- `DirectionalLightComponent0.Brightness` 3;
- `PointLightComponent0.Radius` 1.

Plus Event / Sound tracks on the same groups.
- The curve is evaluated as in MATINEE_NATIVE §1 (raw-tangent Hermite / linear / constant), and the value is written to the
  component property each frame.
- **H (stock):** the component is re-attached / updated so the scene light changes.
- **No colour tracks on lights.**

**Director tracks** (screen-wide):

| Chapter | Fade | Slomo | ColorScale |
|---|---|---|---|
| A1 | 19 | 4 | 3 |
| A2 | 15 | 4 | 2 |
| A3 | 8 | 8 | – |
| A4 | 5 | 5 | – |
| A5 | 9 | 3 | 2 |
| D1 | 6 | 4 | – |
| D2 | 3 | 6 | – |
| D3A | 10 | 5 | 6 |
| D3B | 11 | 5 | – |
| D5 | 7 | – | – |

**Material-parameter tracks:**

| Chapter | ActorFloatMaterialParam | ActorVectorMaterialParam | FloatMaterialParam (asset) | VectorMaterialParam (asset) | VectorProp |
|---|---|---|---|---|---|
| A1 | 37 | 18 | 24 | – | 10 |
| A2 | 3 | 72 | – | – | 4 |
| A3 | 56 | – | 15 | 10 | 11 |
| A4 | 35 | 3 | 79 | – | 43 |
| A5 | – | – | – | – | – |
| D1 | 31 | 6 | 13 | – | 13 |
| D2 | 7 | 2 | 4 | 5 | 3 |
| D3A | **253** | – | 48 | – | 10 |
| D3B | 44 | – | 45 | – | 16 |
| D5 | 6 | – | 7 | 9 | – |

- The **Actor\*MaterialParam** tracks (native UpdateTrack; H: the same path as the Kismet action above, so a per-component MIC
  clone on the group actor's meshes) are what make emissives pulse and fade in the campaign.
- **FloatMaterialParam / VectorMaterialParam** (asset tracks) change the **shared MIC asset** (H, stock).
- **Restore:** these track instances have native Save / RestoreActorState. H (stock): restored when the matinee is rewound
  without bNoResetOnRewind.

## 4. Light functions (C data; H rendering)
46 light components use one of 13 light-function materials:
- **IAC:** VentGobo_MAT_INST (34 uses across A1 / D1 / D3A / D3B), RoundLightFunction_MAT (9), Lift_Gobo (11),
  LightGobo_Vent (8), LightFunctionFan (2), PillarSupportLight (2), RoundLight_Flicker_CUSTOM (1);
- **ORB:** LightFunction_Spinning (6), StripeLight_LightFunction (8), StripeLight_Flash_Pan (2);
- **Omega:** LightFunctionOmegaV2 (4).
- H, stock: the light-function material's emissive is evaluated as a projected multiplier on the light (scaled by LightFunction
  Scale). Animated ones (Fan, Spinning, Flicker, Flash_Pan) animate through material time.
- **None in MP**, so this is a new render feature for the campaign.

## 5. Fog and post-process in play (C)
- **Volume-linked height fog** (PostProcessVolumeHeightFog, 169) and its in / out timers: CAMPAIGN_PPV_HEIGHTFOG_RE.
- **HeightFog actors:** A1 5, A3 3, A5 4, D1 3, D2 10, D5 2 (D2 toggles one).
- **Fog volumes:** FogVolumeConstantDensityInfo, A1 2 and A4 8.
- **Post-process volumes:**

| Chapter | PostProcessVolume | DynamicPostProcessVolume |
|---|---|---|
| A1 | 30 | 1 |
| A2 | 20 | 1 |
| A3 | 20 | – |
| A4 | 31 | 2 |
| A5 | 3 | 5 |
| D1 | 14 | – |
| D2 | 67 | 9 |
| D3A | 53 | – |
| D3B | 25 | 2 |
| D5 | 3 | – |

- **DynamicPostProcessVolume** (20 campaign-wide) is **attached to moving actors**: 13 have a Base, 6 to a skeletal bone. The
  volume rides a vehicle, boss or set piece.
- **Settings transition between volumes** (ULocalPlayer 0x82F9A170): on a volume change, each group (bloom / DOF / motion blur /
  scene colour) restarts its interpolation over that group's `*_InterpolationDuration`. **Switching back to the previous volume
  mid-blend resumes from (1 − progress)** instead of restarting.
- **Example:** A1 Thumper's dynamic volume: Bloom 2.0 at threshold 0.5, the desaturation40 CLUT, durations 0.5 s.
- **Matinee camera override:** a CameraActor with bCamOverridePostProcess replaces the settings and freezes the volume-fog list.
- **LightsVisibilitiesVolume** (312) and LightVolume / BeastLightEnvironmentVolume are lighting-bake helpers (octree of which
  lights affect where). They're static data for the light assignment, not runtime switches.

## 6. Checklist for the campaign renderer
1. Toggleable / Movable lights as dynamic lights with enable state; ignore Toggle on static lights.
2. Kismet Toggle on HeightFog and on (Dynamic)PostProcessVolume.bEnabled; dynamic PP volumes follow their base.
3. Matinee FloatProp → light Brightness / Radius; director Fade / Slomo / ColorScale.
4. Per-component MIC clones for SetActorMatInst* and Actor*MaterialParam tracks; asset-level changes for the stock
   SetMatInst* / *MaterialParam tracks.
5. Light-function projection for the 46 components.
6. PP settings blend per group, with the reverse-resume rule; volume fogs per PPV_HEIGHTFOG.
