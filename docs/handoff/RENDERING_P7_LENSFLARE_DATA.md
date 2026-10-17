# Package 7: lens flares, data recipe (Rendering)

Code: rt2 87941774 (WfcLensFlare.cpp, the MatIn lensFlare fields, the draw hook after the translucency pass, matc's LensFlare*
nodes, build_lens_flares.py, the build_render_data.ps1 / build_materials_only.py wiring). The patch against the head Integration
names follows the eye check.

Six MP maps carry one LensFlareSource each (RE LENS_FLARE_RE.md s1): BrokenHope, Debris, Remnant, Rust, Seed, Gorge. The other four
MP maps get no file and no material change (identical runtime).

## lens_flares.json (new file per flare map)

```
py tools/render/build_lens_flares.py <map> <tmp>/<map>          # reads AssetTools manifests/maps/lens_flares/<map>_lens_flares.json
```

Gate: the six maps get `<map>/lens_flares.json` (1 flare each; Rust 5 enabled elements, Debris 6 ...); the script prints
"no lens flares" for the other four and writes nothing. Nothing else in the map folder changes.

## materials_glsl.json (the 4 flare materials, on the six flare maps)

```
py tools/render/build_lens_flares.py --materials <map>          # lists ENV_ORB_Effects_p.Materials.ORB_LensFlare_{BASE,REF01,REF02,RING}
py tools/render/build_materials.py <map> <tmp>/mat/<map> <those 4 names>
```

Merge only those keys into the live `materials_glsl.json` (as the door materials: the live file plus exactly the new keys; their
textures are ExtractedAssets paths, no tex/ copies). They need the matc of 87941774 (the LensFlare* nodes; older matc fails them
with "unsupported expression MaterialExpressionLensFlareIntensity").

Gate: live + exactly `ENV_ORB_Effects_p.Materials.ORB_LensFlare_BASE`, `_REF01`, `_REF02`, `_RING` (the set the map's enabled
elements use; `--materials` prints it per map), each with error None.

## Expected change

Only frames with a flare map's sun on screen or its fixed occlusion box in view; the four maps without flares byte-identical.
Off / Faithful / Realistic alike (the original draws them in every mode). `WFC_NOLENSFLARES=1` disables them for A/Bs.
