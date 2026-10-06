# Rendering handoff: human-playtest fidelity and AMD stability pass (M41–M45)

Branch `agents/rendering`. Commits: 7e078a5 (M41), b0c2a76 (M42), e15862f (M43/M44), 79388f5 (M45), plus the docs commit.
Validated on a merged preview: `integration/milestone-08b` 175a634 + agents/frontend a661851 (vignette) + these renderer changes.
Render data was regenerated for Standard and all 10 MP maps with these tools.

## Integration steps

1. Merge agents/rendering. Code changes:
   - `src/render/*`;
   - `src/assets/SkinnedModel.cpp` (M45);
   - `tools/render/*` (`weapon_materials.py` is new);
   - diagnostics in `src/core/Application.cpp` (`WFC_FXTEST`, `WFC_MATPARAM`, `WFC_RELOADTEST` period).
2. Merge agents/frontend at a661851 or later, for the title vignette.
3. **Regenerate render data for every map** with `tools/render/build_render_data.ps1`. Required for:
   - weapon materials;
   - template-library beam / trail data;
   - `dynamic_channel` component flags;
   - per-template default colours.
4. Gameplay mirrors `SkinnedModel.cpp` edits. The M45 change is three lines in `skinPose` and must not be lost in the merge.

## Visible fixes (each VISUALLY VERIFIED unless noted)

| Human report | Cause | Owner | Fix |
|---|---|---|---|
| Title buildings / debris solid black | The 51 movable title InterpActors author `LightingChannels = {Dynamic}` with an enabled LightEnvironment. The renderer lit them with the Static set only. | Rendering | M41: Dynamic-channel lights at their current position. Title near-black pixels 9.9 % → 2.5 %. The M08b human frame's black transport is now lit. |
| Bright star-like artifacts on the black objects | Star / particle sprites over unlit silhouettes. | Rendering | Mitigated by M41 (the silhouettes are lit). |
| Vignette not covering the screen, bright side strips | GFx stage is 1120×720. The movie's own `Stage.onResize` stretches vignette I24; the host reported 1120 and only notified noScale movies. RE: no SetViewScaleMode call, full viewport, showAll default (HIGH). | **Frontend** (GFx host) | agents/frontend a661851. Verified at 1280×720, 1920×1080, 2560×1440 windowed and 2560×1440 fullscreen: edge luma 40 vs 64 just inside. |
| Autobot Scientist grey / incomplete | The grey mass is its primary weapon (Burst Rifle), drawn with the glTF fallback. The Air Raid (Jet4) body binds all 6 textures + the Metals cube. Its preview colours are the palette pick (randomised, as the original). | Rendering | M42 |
| Sniper untextured | Only 6 weapon materials (pickups / crates) were ever compiled. Every other held weapon drew the fallback. | Rendering | M42: all 54 exported weapon material slots compile (57 / 57 WEP_ on Streets and Gorge). The Sniper shows authored gunmetal + cyan emissive. |
| Vehicle hover / engine effects absent | Only Systems' hand-made Optimus effects exist. Every chassis' authored HoverFX / BoostFx / JumpFX templates + sockets are in the AssetTools roster, and all 23 FX_Navigation_p templates are in every map's library. | **Systems** wiring, Rendering API | M44: `setParticleEffectParam(handle, "Size" / "Color", rgba)` per RE's rules (Size = min(1, thrust) × socket scale; Color = lerp(EnergonColor, Yellow), alpha 100..255). Hover templates render and scale. |
| Tracer / repair / drain beams absent | Trail2 / Beam2 emitters were skipped. | Rendering | M44: Beam2 ribbons (MaxBeamCount cap: 1 authored), Trail2 ribbons (spawn per distance, path history, segment tracers). Assault Rifle / Sniper tracers and the repair beam render from their own materials. |

## AMD RX 7900 XTX stability

This machine runs the same AMD driver family (26.6.x).

**New diagnostics, always on and cheap:**
- GL debug-output callback: errors, undefined behaviour, high severity; rate limited.
  - `WFC_GLDEBUG=all` logs every message, `sync` gives synchronous stacks, `selftest` runs a harmless proof.
- Context reset status poll: a lost context is logged once.
- GPU frame timer (never stalls): frames over 250 ms are logged; the Windows TDR resets the driver at about 2 s.
- `VISUALCHECK` lines carry `gpu=` and `glDebug=`, and driver errors FAIL the verdict.

**New guards:**
- Every draw's sub-mesh index range is validated (out-of-bounds ones are dropped and logged).
- Vertex-lightmap texture width is checked against `GL_MAX_TEXTURE_SIZE`.

**Real defect found and fixed (M45):** `assets::skinPose` reused a pose output's sub-mesh ranges / UVs from a different model with the same section count. On the release path, after the Optimus spawn:
- 20 sub-meshes would have fetched past their index buffer on the GPU;
- one frame took 364 ms of GPU time.

That is undefined behaviour a driver may legitimately answer with a page fault / reset. After the fix: 0 rejected draws, no long GPU frames.

**Results on the fixed merged build:**
- release_path_check PASS: 0 GL debug errors, 0 out-of-bounds draws, 0 long GPU frames.
- 9-match frontend map chain (Streets, Seed, Berth, Gorge, Complex, Rust, Debris, Molten, Streets; persistent renderer), fixed build: 0 GL debug errors, 0 context resets, 0 out-of-bounds draws, 0 GPU frames over 250 ms, 0 shader failures. Private memory after unload 3.0–4.1 GB, no monotonic growth.
- Streets visual suite: 6 / 6 PASS, Streets cameras refdiff 0.000.
- Renderer-owned GL objects constant across matches. Live textures +6 over the chain are GFx level-thumbnail textures cached once per distinct map (bounded, about +11 per session; Frontend, by design): not a leak.

**Not reproduced:** no driver reset or GL error occurred here, so the human's resets remain UNKNOWN. M45 is the strongest concrete candidate.

**Recommended platform change (not renderer-owned):**
- Create the GL context with `WGL_ARB_create_context` + `WGL_ARB_create_context_robustness` (robust access, `LOSE_CONTEXT_ON_RESET` notification).
- Offer a debug context behind an env switch.

A reset would then be reported and survivable, and debug messages would carry detail. Today the context comes from plain `wglCreateContext`, and AMD reports "No detailed debug message due to a non-debug context".

**Ask the human:** if a reset recurs, keep `wfc.log`. The last `GPU frame time` / `GL debug` / `out of bounds` lines before it are the evidence.

## Remaining rendering discrepancies

- **Beam / trail fidelity:** Beam2 taper, InterpolationPoints and noise, and Trail2 tessellation / tiling distance / bConnectToSource are not applied (PARTIAL).
- **Hover `Size` parameter:** the consuming module decodes as `PMI_Unknown`; it is applied as an effect-wide size scale (PARTIAL).
- **Undecoded modules:** PMI_Gravity / PMI_Unknown payloads; LocationPrimitiveSphere radius on some templates.
- **ColorByParameter DefaultColor:** 16 / 451 Streets emitters not decoded (white).
- **Escalation maps:** BrokenHope / Remnant fail 53 materials. These are characters not cooked into those maps; unchanged from the current Integration data.
- **Title planet's dark regions:** dark in albedo (authored) — not a lighting fault.
- **Gorge:** one vertex-lightmap component unbound (export / cooked vertex count mismatch).
- **RandomSeed** source: UNKNOWN.
- **Hud_GFX post-process chains** `ActivatePostProcessChain` (hurt / downed / scramble): bridge.unhandled (Experimental P2-4). Frontend + Rendering, not started.
