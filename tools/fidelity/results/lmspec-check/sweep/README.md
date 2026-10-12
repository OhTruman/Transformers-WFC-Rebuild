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
