# Per-map compile / presentation sweep

build: `fdffa7ff131b137f7e7a355af905046564f38973` (`F:\Transformers Rebuild\Rebuild-Experimental\work\ab\m8c_fdffa7f\build-release\bin\wfc_rebuild.exe`)

One lockstep direct boot per map (600 frames). The world verdict uses the frames (HUD band and player excluded). Warning types per map: mapsweep.csv (warn_types).

| map | class | world_verdict | world_detail | world_draws | materials | noProgram | glErr | glDebug | gpu_ms | material_fallbacks | particle_fallbacks | ribbons_not_drawn | legacy | out_of_bounds | clean_exit |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| MP_IAC_Seed | STRUCTURAL DEFECT | FAIL | 0.039 | 57 | 36 | 0 | 0 | 0 | 1.2 |  |  | 0 | 0 | 0 | True |
| MP_IAC_Berth | STRUCTURAL DEFECT | FAIL | 0.049 | 29 | 22 | 0 | 0 | 0 | 0.2 |  |  | 0 | 0 | 0 | True |
| MP_UND_Complex | PLAYABLE | PASS | 0.337 | 46 | 33 | 0 | 0 | 0 | 0.5 |  |  | 0 | 0 | 0 | True |
| MP_IAC_Rust | PLAYABLE | PASS | 0.244 | 132 | 58 | 0 | 0 | 0 | 0.6 |  |  | 0 | 0 | 0 | True |
| MP_ESC_BrokenHope | PLAYABLE | PASS | 0.373 | 548 | 92 | 0 | 0 | 0 | 1 |  |  | 0 | 0 | 0 | True |
| MP_ESC_Remnant | PLAYABLE | PASS | 0.592 | 155 | 57 | 0 | 0 | 0 | 0.7 |  |  | 0 | 0 | 0 | True |
| MP_ORB_Debris | PLAYABLE WITH VISUAL DEFECTS | PASS | 0.454 | 47 | 22 | 0 | 0 | 0 | 0.4 |  |  | 0 | 0 | 0 | True |
| MP_IAC_Streets | PLAYABLE | PASS | 0.164 | 78 | 41 | 0 | 0 | 0 | 0.8 |  |  | 0 | 0 | 0 | True |
| MP_KON_Molten | PLAYABLE | PASS | 0.516 | 38 | 35 | 0 | 0 | 0 | 1 |  |  | 0 | 0 | 0 | True |
| MP_UND_Gorge | PLAYABLE WITH VISUAL DEFECTS | PASS | 0.358 | 286 | 47 | 0 | 0 | 0 | 1.2 |  |  | 0 | 0 | 0 | True |

## Findings (Experimental, 2026-10-05)
- **Seed and Berth: world FAIL is UNCONFIRMED.**
  - The frames show complete geometry but very dark (median luma 9-12), and the verdict rests on one frame
    (frame 200). The renderer's own VISUALCHECK passes at frames 120 / 240 / 360.
  - An A/B on M08b 175a634 (same sweep) is queued to decide between "authentic dark spot at the default start" and
    "M08c lighting / exposure change".
  - The sweep now captures several frames per map (`-ShotFrom/-ShotTo/-ShotStep`).
- **GPU frame-time spikes:** one warning each on Seed (569 ms), Berth (383 ms) and Rust (345 ms) (Rendering M45 TDR
  watch; the reset limit is about 2,000 ms). These are single spikes; the frame-time median is 0.2-2.4 ms.
- **Debris:** three lightmaps referenced by the render data (`LightMapTexture2D_4181 / _5690 / _587`) are absent from
  the AssetTools export (99 source lightmaps vs 569 in the render data, none of these in `lightmaps.json`), so those
  surfaces have no baked light: **source-data gap** (AssetTools / Rendering). `decals.glb` is empty: 0 decals in the
  source, which is authentic.
- **Gorge:** 4 vertex lightmaps are not bound (sample count ≠ vertex count, e.g. 1576 samples vs 322 / 1294 vertices
  on StaticMeshActor_15751 / _6134) (Rendering / AssetTools).
- **Ambient reverb zones "not in the mixer"** (Systems): Complex, Rust, BrokenHope (11), Remnant (59), Debris, Molten
  (40+). The zone's reverb preset is missing, so those rooms play with the default reverb.
- **Map FX `PMI_LocationPrimitiveSphere`** has no decoded VelocityScale / StartRadius (Seed, Berth, Gorge), so a
  default is used (Rendering, PARTIAL).
- **All 10 maps:** 0 original-material build fallbacks, 0 particle-material fallbacks, 0 Trail2 / Beam2 occurrences,
  0 LEGACY RENDERER, 0 out-of-bounds draws, glErr 0, glDebug 0, clean exit. This answers the audit's UNKNOWN:
  **no compile fallbacks on any map**.
