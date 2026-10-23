# PostProcessVolumeHeightFog: when a volume-linked height fog applies (RE, 2026-10-23)

Labels: C = WFC binary / cooked data; H = inference.
Native: `ULocalPlayer` post-process update 0x82F9A170; per-view fog gather 0x82EA0728; InitFogConstants 0x82EA6380 (all C).

## Data (C, authored.db)

**The fog actor.**
- `PostProcessVolumeHeightFog` (WFC actor) owns a `PostProcessVolumeHeightFogComponent`, a HeightFogComponent subclass.
- **The component fields are the plain HeightFogComponent fields:** Height, Density, LightBrightness, LightColor, StartDistance,
  ExtinctionDistance, bEnabled. Absent fields take the same class defaults: Density 5e-5, LightBrightness 0.1,
  ExtinctionDistance 1e8, StartDistance 0.
- Counts: 172 components, 170 placed actors; 1 on MP_ESC_Remnant.
- They're converted to shader constants by the same per-layer code as plain fogs (GORGE_RUST_HEIGHTFOG_RE §4):
  - log2(1 − Density);
  - LightColor_lin × Brightness × −1/ln 2;
  - Ext;
  - max(Start, 0).
  - So **build_lighting's existing reading maps one-to-one.**

**The link lives on the volume** (WFC additions to `FPostProcessSettings`):
- `PostProcessVolumeHeightFog[]`: an array of `{PostProcessVolumeHeightFogActor, bPriority, bTransitionFromExistingHeightFog}`.
- `PostProcessVolumeHeightFog_InterpolationDuration`: default 1.0; authored 0–5 s.
- `_InterpolateDensity`: default **true**.
- `_InterpolateHeight`: default **false**.
- `_InterpolateColor`: default **false**.
- 142 volumes link fogs. **19 fog actors are linked by no volume.** Nothing in script calls AddVolumeLink / RemoveVolumeLink:
  those natives only maintain an editor back-reference list (fog actor +0x1CC).
- WorldInfo DefaultPostProcessSettings: one entry (D1_ORB) with a null actor, so nothing.

## When it applies (C)

### 1. Volume selection
Each frame the local player picks its post-process settings from the world at the view point:
- the **pawn's location** if the PlayerController has a pawn, else the camera location (H: the per-volume bUsePlayerPosition
  choice is inside the stock lookup 0x82C2D9E8, not traced);
- the highest-priority enabled volume containing that point;
- no blending between overlapping volumes.

Exception: while a Matinee **CameraActor with bCamOverridePostProcess** is the view target, its CamOverridePostProcess is used
and **the fog list is not updated**. Active fogs freeze.

### 2. On a volume change
The local player keeps an **active-fog list** of {fog actor, bPriority, bTransition}.

**Every fog already in the list starts fading out:**
- state OUT;
- timer = the **previous** volume's InterpolationDuration.

**For each link k of the new volume:**
- If the link has **bTransitionFromExistingHeightFog** and active slot k exists:
  - the new fog **takes over slot k**: the old fog is dropped with no fade-out;
  - its current values start from the old fog's **current** values, for each parameter whose Interpolate flag is set
    (Density / Height / Color).
- Otherwise it's appended.
- The new fog's state is IN. Its timer is the **new** volume's InterpolationDuration. It latches the volume's three Interpolate
  flags.

**Leaving all volumes** (no volume at the point) means every active fog fades out with the old duration. That's the unbound case.

### 3. Per frame
For each active fog with timer T > 0: f = dt / T, then T −= dt.

- **IN:**
  - while T > 0: `cur += (target − cur)·f` for Density, Height and the 4 colour bytes (target = the component's values);
  - at T ≤ 0: snap to target and leave the IN state. The fog stays active at full component values.
- **OUT:**
  - while T > 0: `cur −= cur·f` toward 0 (Density, Height **and** colour all go to 0);
  - at T ≤ 0: removed from the list.

### 4. Into the shader
The per-view fog gather (0x82EA0728) runs twice inside InitFogConstants:
1. **bPriority fogs first**;
2. then the level's ordinary HeightFog components;
3. then the non-priority volume fogs.

Up to **4 total**: the 5th and later are dropped (overflow flag). **bPriority only decides who gets a slot.** The layers are then
sorted by height as usual.

For each entry whose component is enabled:
- the FHeightFogSceneInfo is built from the component;
- if the fog is IN or OUT, the **interpolated** values replace:
  - Density (if InterpolateDensity);
  - Height (if InterpolateHeight);
  - LightColor = cur colour (sRGB → linear) × the component's LightBrightness (if InterpolateColor).
- **Parameters without the flag use the component value at once.** So with the defaults, only density fades. Height and colour
  switch instantly, and a fading-out fog keeps full height and colour until it's removed.

**Not in the scene list** (H, strong):
- volume fogs are injected only through this per-view list;
- campaign levels hold up to 4 volume fogs per sublevel and 117 levels have them. If they were also scene fogs, the 4-slot cap
  would overflow constantly.
- The component's Attach override wasn't traced.

## For the rebuild
- **Fog set per view** = level HeightFogs + the active list from the camera / pawn point's highest-priority volume, with priority
  links first and a cap of 4.
- **Layer parameters** = the plain HeightFog ones, unchanged.
- **Transitions:** the timers above.
  - IN: lerp toward target by dt / T.
  - OUT: decay to 0 by dt / T, then drop.
  - Only flagged parameters interpolate (default: density only).
  - bTransitionFromExistingHeightFog hands slot k's current values to the new fog, which replaces it with no fade-out.
- **Unlinked fog actors (19)** never apply.
- **MP_ESC_Remnant's one** applies only inside its linking volume.
