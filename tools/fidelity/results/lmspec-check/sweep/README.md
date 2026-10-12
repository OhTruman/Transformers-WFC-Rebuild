# Lightmapped world materials vs the original microcode, 8 maps (shared-map pairing; report only, 2026-10-12)

Integration's request: use the shared static-permutation pairing (`../molten/sharemap.py`) to sweep the other maps for world
(non-character) materials that differ from the original. Untimed; it waited on PERF_HOLD before every step (`sweep.sh`). Outputs
are only in Experimental work/. Rendering's emulator copy is as in `../molten/lmspec_geom_local.diff`.

**Coverage:** Rust, Streets, Gorge, Debris, Berth, Seed, Complex, Molten. The two ESC maps (BrokenHope, Remnant) were not reached:
Claude Code stopped the background sweep at ~12:10 because the system was low on memory. It is not restarted until asked
(commit free 16.1 GB, physical 13.0 GB afterwards; leftovers stopped by PID).

## Per map (world materials; characters / weapons / FX excluded): `rank.py`

| map | exact | differs | not emulated |
|---|---|---|---|
| Rust | 56 | 7 | 131 |
| Streets | 59 | 1 | 69 |
| Gorge | 35 | 0 | 73 |
| Debris | 94 | 0 | 31 |
| Berth | 32 | 3 | 93 |
| Seed | 35 | 0 | 122 |
| Complex | 67 | 0 | 53 |
| Molten | 78 | 0 | 65 |

"Not emulated" = no paired original shader (no name-matched sibling with equal switches) or the emulator could not identify the
geometry registers / uniforms. That is a coverage gap of this check, not a finding.

## Ranked suspects (world materials whose lightmapped output differs from the original; residual = relative RMS)

| rank | material | maps | worst residual | fitted scale | kind | specular share |
|---|---|---|---|---|---|---|
| 1 | ENV_IAC_BLDS_2_p Pillar01_MATINST | Berth, Rust, Streets | 0.112 | 1.00 | diffuse / normal | 0.98 |
| 2 | ENV_IAC_Deco_1_p SideSupport_MATINST | Berth, Rust | 0.148 | 1.01 | diffuse / normal | 0.93 |
| 3 | ENV_IAC_Interior_1_p Interior_NewFloor_MATINST | Berth, Rust | 0.056 | 1.02 | diffuse / normal | 0.98 |
| 4 | DES_IAC_p LiftLock_MAT_INST | Rust | 0.274 | 0.97 | diffuse / normal | 0.91 |
| 5 | ENV_IAC_CleanMaterials_p ENV_IAC_CleanDoor_Off_MATINST | Rust | 0.198 | 1.00 | diffuse / normal | 0.97 |
| 6 | ENV_IAC_Interior_1_p Interior_WallsCeilings_MATInstance | Rust | 0.101 | 0.99 | diffuse / normal | 0.93 |
| 7 | ENV_IAC_Interior_1_p Interior_WallsCeilings_AquaLight_MATINST | Rust | 0.101 | 0.99 | diffuse / normal | 0.93 |

Ranked by number of maps, then by the worst residual. Notes:
- No world material differs in the specular colour alone, and none fits the diffuse-only model better. The lightmap-specular code is
  not implicated anywhere. The fitted scale is ~1.0 for all 7, so none is uniformly too bright or too dark.
- All 7 are IAC-family materials (Rust / Berth / Streets). The specular term carries 91-98 % of their lightmap response, so the
  residual most likely comes from the material inputs that feed it, i.e. the normal (normal map, detail normal, mask) or the
  SpecularPower/colour expression, rather than the lightmap code. Rendering's earlier run already listed LiftLock, Pillar01,
  Interior_NewFloor and Interior_WallsCeilings_AquaLight on Rust as "diffuse/normal differs (pre-existing)". SideSupport and
  CleanDoor_Off are new here: their maps were found only through shared-map pairing.
- Not yet done: a per-material breakdown (which translated expression differs). That is a next step for Rendering or me on request.

## After the normal-node fix (integration bf8acad+, materials_glsl.json regenerated; re-run 13:30-14:10)

The same 8 maps were re-run one process at a time (`onemap.sh`), with Rendering's updated gl2py (wfcNormalNode). The before set is
the 11:40-12:10 run above. Compare with `cmpba.py`:

| map | world exact | world differs | all exact | all differs |
|---|---|---|---|---|
| Berth | 32 -> 35 | 3 -> 0 | 155 -> 175 | 40 -> 20 |
| Rust | 56 -> 63 | 7 -> 0 | 178 -> 202 | 44 -> 20 |
| Seed | 35 -> 35 | 0 -> 0 | 159 -> 176 | 37 -> 20 |
| Streets | 59 -> 60 | 1 -> 0 | 184 -> 202 | 38 -> 20 |
| Molten | 78 -> 78 | 0 -> 0 | 202 -> 219 | 37 -> 20 |
| Debris | 94 -> 94 | 0 -> 0 | 219 -> 236 | 37 -> 20 |
| Complex | 67 -> 67 | 0 -> 0 | 191 -> 208 | 37 -> 20 |
| Gorge | 35 -> 35 | 0 -> 0 | 158 -> 175 | 36 -> 19 |
| **total** | **456 -> 467** | **11 -> 0** | **1446 -> 1593** | **306 -> 159** |

- 147 map-material pairs moved from differs to exact (11 world, 136 character / weapon), and no material regressed.
- **No world material differs from the original any more** (`rank_after.txt` is empty).
- The remaining ~20 per map are the same set on every map: bot soldiers (RB_/VH_Soldier*), Frenzy/Rumble, Slipstream, Laserbeak,
  SniperDec and the melee weapons (LongSword, ProngDagger, SerratedBlade, SpikedBat). All are on the diffuse/normal side, with fitted
  scales 0.5-1.9, which suggests a poor pairing or texture assignment more than a single wrong term. They never draw lightmapped, so
  this test is a weak judge for them. Not chased.
- Coverage limits as before: the "not emulated" counts are unchanged, and the ESC maps were not run.
