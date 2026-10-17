# Package 7: Set E + transforming doors, data recipe (Rendering)

Apply order on integration 814e7cf5 (each patch is `diff -ru`, LF; `patch -p1` after stripping CR from the touched files):

1. `RENDERING_P7_SET_E.patch` (against 814e7cf5): flash term, haze exposure, Remnant toggleable light + `build_lighting.py --standalone-only`,
   and the Set D follow-up: height fog in-scattering x 1 / ln 2 (RE 302c5e7; all modes; expected change: every fogged view's fog
   term 1.443x - Rust near-floor fill, Gorge far haze; the colour already uses the native 2.2 table).
2. `RENDERING_P7_TRANSACT_DRAW.patch` (against 814e7cf5 + Set E): the door draw (`src/game/TransformingActorsDraw.*`, World member + draw call),
   the interpolated light maps (`IRenderer::setDrawInterpolatedLightmap`, WfcPipeline, SkinnedModel TEXCOORD_1, GpuSkin), and the tools
   (`build_interp_lightmaps.py`, `transforming_materials.py`, `merge_transforming_materials.py`, `build_render_data.ps1`, `build_materials_only.py`).

Both together were compile-checked on 814e7cf5 (work/int_d814).

## Data (Rebuild/work/render), after the patches

Python: `"F:/Transformers Rebuild/AssetTools/bin/py/python.exe" -I`, run from the tree root. Write into a scratch directory, check the
gate, then copy over the live file.

### lighting.json: Remnant (both keys); BrokenHope, Rust (interpolated light maps only)

```
# Remnant: the standalone light first, then the door light maps on its output
py tools/render/build_lighting.py --standalone-only MP_ESC_Remnant <live>/MP_ESC_Remnant/lighting.json <tmp>/a/MP_ESC_Remnant
py tools/render/build_interp_lightmaps.py MP_ESC_Remnant <tmp>/a/MP_ESC_Remnant/lighting.json <tmp>/b/MP_ESC_Remnant
# BrokenHope, Rust
py tools/render/build_interp_lightmaps.py MP_ESC_BrokenHope <live>/MP_ESC_BrokenHope/lighting.json <tmp>/b/MP_ESC_BrokenHope
py tools/render/build_interp_lightmaps.py MP_IAC_Rust <live>/MP_IAC_Rust/lighting.json <tmp>/b/MP_IAC_Rust
```

Gate (after a JSON load): every existing key equal; added only:
- Remnant: one entry appended to `lights` (PointLightToggleable_1918's PointLightComponent, GUID 29405897 / 1214979729 / 77194133 /
  2399548689, `force_dynamic` true), and `interpolated_lightmaps` (12 actors).
- BrokenHope: `interpolated_lightmaps` (18 actors); Rust: `interpolated_lightmaps` (2 actors).

The script prints any light-map atlas missing from the map's `lightmaps/`; expected none (all 6 x 32 atlases are already live).
`--standalone-only` on the other 9 MP maps adds nothing (checked), so they need no regen. Cross-check: the 18 BrokenHope captures match
AssetTools' independent export (`manifests/maps/transforming_actors/MP_ESC_BrokenHope_interp_lightmaps.json`) exactly.

### materials_glsl.json: BrokenHope, Remnant, Rust (6 door / bridge materials)

```
py tools/render/build_materials_only.py <live root> <tmp>/mat MP_ESC_BrokenHope MP_ESC_Remnant MP_IAC_Rust
py tools/render/merge_transforming_materials.py      # reads <live>, writes work/mat_trans_merged/<map>/materials_glsl.json
```

`merge_transforming_materials.py` takes only the transforming-actor materials the live file lacks from the regen, rewrites their tex/
paths to the live map folder, and leaves every other entry byte-identical. Paths in it point at the regen output in
`work/mat_trans` and `work/mat_trans_merged`; edit the two constants if run elsewhere. A full materials regen differs from live in
hundreds of entries (other lanes' translator versions), so do not copy `<tmp>/mat` over live.

Gate: the live file plus exactly:
- BrokenHope: `TPRO_IAC_Doors_p.InteriorDoor_ON_MATINST`, `ENV_NEU_Bridges_p.Textures.Bridge_Neutral_Shared_MATINST`;
- Remnant: `TPRO_ORB_D1_Doors_p.Materials.Orange_InteriorDoor_MATINST`, `TPRO_ORB_D1_Doors_p.Materials.InteriorDoor_Inside_Emissive_MATINST`;
- Rust: `ENV_IAC_CleanMaterials_p.Materials.ENV_IAC_CleanDoor_Red_MATINST`, `TPRO_IAC_Doors_p.InteriorDoor_ON_MATINST`.

Ready-made copies built from today's live files: `Rebuild-Rendering/work/mat_trans_merged/<map>/materials_glsl.json` (gate checked).

## Identity

Maps without transforming actors: byte-identical (the draw has no actors; no lighting / materials change). Off / Faithful on the 3 door
maps change by design (doors drawn; Remnant's light). Eye evidence: work/wobble/door_sheet.png, door_open_sheet.png, rl_sheet.png;
the Remnant versus (open) view and the Rust opening on the merged head in the next slot.
