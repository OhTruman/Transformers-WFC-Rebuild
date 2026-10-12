# Suspects 1-3 term breakdown: matc's MaterialExpressionNormal is world space in the original, not tangent space (2026-10-12)

Integration's request: find which translated term differs for the sweep's suspects 1-3, and where it comes from in matc. Work was
foreground and single-process (one python at a time, ~0.3 GB), untimed, with outputs only in Experimental work/.

## Finding

`tools/render/matc.py:408-410`:

    def x_Normal(self, c, n, p, o):
        # the material's own (tangent-space) normal, as UE3's normal-dependent expressions see it
        self.uses.add('normal'); return 'm.normal', 3

In the original, the Normal node (`MaterialExpressionNormal`, no properties) gives the **world-space** (UE axes) normal. Replacing
` = m.normal;` with ` = tangentToWorldUE(m, m.normal);` in the translated GLSL makes every affected material exact against the
original lightmapped base-pass microcode:

| material (Rust) | residual now | residual with world-space Normal | shaders exact |
|---|---|---|---|
| Pillar01_MATINST (suspect 1) | 0.112 / 0.196 | 7.7e-08 / 1.4e-07 | 3 / 3 |
| SideSupport_MATINST (2) | 0.148 / 0.183 | 4.5e-08 / 1.6e-07 | 4 / 4 emulated |
| Interior_NewFloor_MATINST (3) | 0.056 / 0.095 | 4.2e-08 / 2.3e-07 | 4 / 4 emulated |
| LiftLock_MAT_INST (4) | 0.274 | exact | 6 / 6 |
| ENV_IAC_CleanDoor_Off_MATINST (5) | 0.198 | exact | 6 / 6 |
| Interior_WallsCeilings(_AquaLight) (6, 7) | 0.101 | exact | 6 of 9 (other 3: a different paired map) |
| WEP_AssaultRifle_MATINST | 0.408 | 6.1e-08 | 6 / 6 |
| WEP_Shotgun_MATINST | 0.341 | 4.1e-08 | 6 / 6 |

Where it shows: ENV_AnisotropicHighlight_MF (the IAC anisotropic sheen in ENV_PhongMain_NS_Mat; Pillar01 lines 31-47 of
`pillar_glsl.txt`) does `dot(CameraVector[CS_World], cross(Normal, Normal.zxy))` -> `pow(sqrt(1 - d^2), 10 * SpecPower)` and adds
`5 * that * colour` to DiffuseColor. With a tangent-space Normal against a world-space camera vector, the sheen fires at the wrong
views: in the emulator trials the diffuse came out 0.22x to 4.9x the original's (`bd3.py`).

## How it was found

1. Per-trial dumps of the original's lightmap response K_i and our D, S, P, N (`../molten` emulator copy + `LMS_DUMP`), at the best
   texture assignment.
2. `bd3.py`: the residual sits in 3 of 48 trials, the same trials for all 3 materials. In each, our spec is 0 and the original's K
   has our diffuse shape scaled 0.22x-4.9x, so the D expression differs, view-dependently.
3. The view-dependent part of D is the anisotropic sheen. Hypotheses substituted into the GLSL (`LMS_SUB`) and re-emulated:
   Normal = (0,0,1): 0.27; CameraVector in tangent space: 0.27; Normal = world (0,0,1): 0.20; **Normal = world material normal:
   1.4e-07.**
4. Cross-check (`reach.py`, Rust): 48 materials that use the node already emulate exact, but they use it only in EmissiveColor,
   which a lightmap-response test cannot see. All 28 materials where the node reaches DiffuseColor were non-exact before; this
   includes the WEP_* weapons from the sweep's excluded character/weapon list. The node has no properties in any of the 6 graphs
   read (ENV_AllShader_p), so nothing else selects a space.

## Scope and limits

- 91-102 materials per map contain the node (all 10 maps). The change affects only those where it reaches lit outputs (28 on Rust)
  and their emissive (e.g. hologram / fresnel rims), which this check cannot verify.
- Verified against the lightmapped base-pass PS only. The dynamic-light (uber) pass runs the same material function, so the same
  fix should apply, but that is not emulated here.
- Suggested fix (Rendering): `return 'tangentToWorldUE(m, m.normal)', 3` with `self.uses.add('tbn')`, then re-run lmspec_geom on
  Rust: Pillar01 / SideSupport / NewFloor / LiftLock / CleanDoor_Off / WEP_* should go exact.
