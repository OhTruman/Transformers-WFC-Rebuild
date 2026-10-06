# Per-map compile / presentation sweep

build: `fdffa7ff131b137f7e7a355af905046564f38973` (`F:\Transformers Rebuild\Rebuild-Experimental\work\ab\m8c_fdffa7f\build-release\bin\wfc_rebuild.exe`)

One lockstep direct boot per map (600 frames). The world verdict uses the frames (HUD band and player excluded). Warning types per map: mapsweep.csv (warn_types).

| map | class | world_verdict | world_detail | world_draws | materials | noProgram | glErr | glDebug | gpu_ms | material_fallbacks | particle_fallbacks | ribbons_not_drawn | legacy | out_of_bounds | clean_exit |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| MP_IAC_Seed | PLAYABLE - DARK (HUMAN CHECK) | FAIL | 0.039 | 57 | 36 | 0 | 0 | 0 | 1.2 |  |  | 0 | 0 | 0 | True |
| MP_IAC_Berth | PLAYABLE - DARK (HUMAN CHECK) | FAIL | 0.049 | 29 | 22 | 0 | 0 | 0 | 0.2 |  |  | 0 | 0 | 0 | True |
| MP_UND_Complex | PLAYABLE | PASS | 0.337 | 46 | 33 | 0 | 0 | 0 | 0.5 |  |  | 0 | 0 | 0 | True |
| MP_IAC_Rust | PLAYABLE | PASS | 0.244 | 132 | 58 | 0 | 0 | 0 | 0.6 |  |  | 0 | 0 | 0 | True |
| MP_ESC_BrokenHope | PLAYABLE | PASS | 0.373 | 548 | 92 | 0 | 0 | 0 | 1 |  |  | 0 | 0 | 0 | True |
| MP_ESC_Remnant | PLAYABLE | PASS | 0.592 | 155 | 57 | 0 | 0 | 0 | 0.7 |  |  | 0 | 0 | 0 | True |
| MP_ORB_Debris | PLAYABLE WITH VISUAL DEFECTS | PASS | 0.454 | 47 | 22 | 0 | 0 | 0 | 0.4 |  |  | 0 | 0 | 0 | True |
| MP_IAC_Streets | PLAYABLE | PASS | 0.164 | 78 | 41 | 0 | 0 | 0 | 0.8 |  |  | 0 | 0 | 0 | True |
| MP_KON_Molten | PLAYABLE | PASS | 0.516 | 38 | 35 | 0 | 0 | 0 | 1 |  |  | 0 | 0 | 0 | True |
| MP_UND_Gorge | PLAYABLE WITH VISUAL DEFECTS | PASS | 0.358 | 286 | 47 | 0 | 0 | 0 | 1.2 |  |  | 0 | 0 | 0 | True |

## Findings (Experimental, 2026-10-05)
- **All 10 maps:** 0 original-material build fallbacks, 0 particle-material fallbacks, 0 Trail2 / Beam2, 0 LEGACY
  RENDERER, 0 out-of-bounds draws, glErr 0, glDebug 0, clean exit. **No compile fallbacks on any map.**
- **Seed and Berth: PLAYABLE - DARK (HUMAN CHECK).** The geometry is complete and textured, but very dark (black
  0.53-0.84 of the world region, median luma 11) in every captured view.
  - **A/B, identical sweep and start** (`ab_seed_berth_06b_vs_08c.jpg`, frame 320; left 06b 681fd29, right M08c):
    - M08b 175a634 and M08c fdffa7f are **identical**, so this is not an M08c change.
    - Against 06b, at the same draws (Seed 1444, Berth 571): Seed luma 15 → 11, black 0.26 → 0.40; Berth luma
      18 → 11, black 0.00 → 0.37.
    - But 06b's Berth wall was a **flat untextured slab** (the sweep FAILs it), and M08c draws it as detailed
      machinery. 06b's bright red robot was the old Optimus fallback body.
  - So M08b+ is more complete **and** darker. The darkening came between 681fd29 and 175a634: Rendering M20 / M21
    (lightmap records, FColor channel order), M24 (vertex lightmaps on every map) and M25 (PostProcessVolume grades).
  - Whether the original is this dark is **UNKNOWN** without a reference: human check, plus a question to Rendering.
- **Debris: FIXED.** AssetTools 4d7a423 exports the destructible-mesh lightmaps (`lightmaps_destructibles.json`;
  vs_lightmap had joined only props.json components). Debris render data was regenerated and re-swept on fdffa7f:
  **PLAYABLE**, 0 decode failures, world 0.45-0.54. The original note follows. Debris: three lightmaps referenced by
  the render data (`LightMapTexture2D_4181 / _5690 / _587`) are absent from
  the AssetTools export (99 source lightmaps vs 569 in the render data), so those surfaces have no baked light:
  **source-data gap**. `decals.glb` is empty because the source has 0 decals (authentic).
- **Gorge:** 4 vertex lightmaps are not bound (sample count ≠ vertex count, e.g. 1576 samples vs 322 / 1294 vertices,
  StaticMeshActor_15751 / _6134).
- **GPU frame-time spikes:** one warning each on Seed (569 ms), Berth (383 ms) and Rust (345 ms) (TDR watch; the
  limit is about 2,000 ms; the median is 0.2-2.4 ms).
- **RETRACTED: ambient reverb "not in the mixer".** Systems aa15569: the warning came from AssetTools' flattened
  zone list (it stops at SeqAct_Delay / ActivateRemoteEvent links). Every MP map runs its zones through the generated
  Kismet graph, which follows them. All authored REVERB_* presets activate on all 10 maps (now a Systems suite
  check). The warning now fires only when flattened zones are in use. The original (wrong) note follows. Ambient
  reverb zones "not in the mixer" (Systems): Complex, Rust, BrokenHope, Remnant, Debris, Molten. The
  zone's reverb preset is missing, so those rooms play with the default reverb.
- **Map FX `PMI_LocationPrimitiveSphere`:** VelocityScale / StartRadius not decoded, so a default is used (Seed,
  Berth, Gorge; Rendering, PARTIAL).
- **Sweep rule (negative-controlled):** a FAIL that is textured, noise-free and dark but not black (0.25 ≤ black
  < 0.92, detail ≥ 0.02) is "PLAYABLE - DARK (HUMAN CHECK)". The M06b lost-world frames (black 0.08-0.16) and 06b
  Berth's flat slab still FAIL.
