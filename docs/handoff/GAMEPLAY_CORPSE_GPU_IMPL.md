# Package 7 item 1: GPU-skinned death bodies, the implementation plan (Gameplay)

Gameplay, 2026-10-16. This implements Rendering's `RENDERING_CORPSE_GPU_PATH.md` against the package 6 respin head 9350f19b, which has the static-pose patch.

- **Status:** design. Code starts after the respin gate. The patch is held for package 7.
- **Target:** within 0.1 ms of the `WFC_NOCORPSE` arm on frame, CPU and GPU p90 (Experimental's corpse arm, same match).

## What exists (no new renderer or asset code)

| Helper | Where | Use |
|---|---|---|
| `assets::poseGlobals(model, pose, glob)` | `src/assets/SkinnedModel.h` | joint globals **without** the vertex skin (replaces `skinPose` in tickCorpses) |
| `buildPalette(model, glob, pal)` | `src/game/GpuSkin.h` | palette = global(j) × invBind(j) |
| `bindMeshOf(model)` | `src/game/GpuSkin.h` | the model's bind-pose mesh: its address is the renderer's static-buffer key; kept on the model |
| `drawSkinnedGpu(r, bind, joints, weights, pal, nullptr, 1.0f, world, colour, key, serial)` | `src/game/GpuSkin.h` | the queued character draw, so the shadow caster batches with live robots; returns false → fall back |

## Changes, file by file

**`World.h` (Corpse):**
- **Stable key:** add `std::unique_ptr<uint8_t> gpuKey` (allocated at creation). Its address is the `paletteKey` for the body's whole life and survives moves in `corpses_`.
  - Rendering's rule: never `&corpses_[i]`, because each move would take a new bone-texture row.
  - An alternative is `std::vector<std::unique_ptr<Corpse>>`; the token is the smaller diff.
- **Palette:** add `std::vector<core::Mat4> palette`, plus a `bool gpuOk = true` that latches the fallback once `drawSkinnedGpu` returns false for that body.
- **Mesh only on fallback:** keep `mesh` (the CPU-skinned output), now filled only on the fallback path.

**`World.cpp` tickCorpses:**
- While the pose changes (`!poseStatic`): samplePose / blendPose / head shrink as today, then
  - `poseGlobals(*c.model, c.pose, c.glob)`;
  - `buildPalette(*c.model, c.glob, c.palette)`;
  - `c.poseSerial = ++corpsePoseSerial_`.
- `skinPose` (vertices) runs only when `!c.gpuOk`, i.e. the fallback.
- The socket lookups (the head FX) already use `c.glob` and are unchanged.

**`World.cpp` drawCorpses:**
- Same state calls around the draw as today: `setDrawOwner(c.owner)`, `setCharacterColors`, `setDrawEnergyDeath` (Defrag).
- The draw:
  ```
  if (c.gpuOk && drawSkinnedGpu(r, bindMeshOf(*c.model), c.model->joints, c.model->weights, c.palette, nullptr, 1.0f,
                                c.world, colour, c.gpuKey.get(), c.poseSerial)) { /* queued */ }
  else { c.gpuOk = false; /* skin once now */ ...; r.drawDynamicMeshPosed(c.mesh, c.world, colour, c.poseSerial); }
  ```
  `drawCorpses` is const today. The fallback latch and its one-off skin move into tickCorpses on the next step, so the draw stays const: a body that falls back draws nothing for one frame (it isn't visible yet), then CPU-skinned.
- **The jet hull wreck:** it stays `drawDynamicMeshPosed` with a constant serial (rigid bind mesh). Moving it onto the queued path would need a one-bone palette. Measure first; it's 1 draw per jet death.

**Gib chunks:** first read `gStats.instRejected` on a gib-heavy frame (Gorge, 63 bots).
- If the RB_Gib draws already instance (static meshes sharing a program), nothing is needed.
- If they don't, instance them per gib mesh (11 meshes) through the renderer's `queueInstance`.
- No projected shadow for chunks, as today and as in the original.

## Row budget

Rendering's note: 512 shared bone-texture rows; a row returns 600 frames after its last draw.
- Bodies live TotalTime (0.3-4 s, about 240 frames max) + a gib, so concurrent body rows stay small.
- The measured maximum is 9 concurrent presentations at 63 bots, plus 600-frame row recycling: about 9 + 9 × (600 / 240) ≈ 32 rows, well under 150.

## Verification

1. **Identity:**
   - corpse crops at 10 m and 40 m (WFC_CORPSESTAGE: the camera behind the spot, and 40 m down the corridor looking back, the same frames as the package 6 check) byte-identical to 9350f19b, if the GPU skin matches the CPU skin bit for bit as it does for live robots;
   - if not, the residual diff stays within the same per-pixel bound live robots showed on their switch, reported per frame;
   - plus the Defrag (melee) crop, which checks the energy-death parameter on the queued caster.
2. **Counts:** a `WFC_CORPSETEST` line per body, GPU path vs fallback; the "bone-texture rows full" warning absent in a 16 v 16 and a 32 v 32 match.
3. **Cost:** Experimental's corpse arm (NOCORPSE vs 9350f19b vs this), same match, RT off: frame / CPU / GPU p90 per map, plus dynamic / FX draw counts.
4. **Sim:** SIMHASH identical (presentation only); BOTTEST 48 / 48 on 12 seeds.

## Estimate

- Code: about 2-3 h.
- Checks: about 1 h of headless runs + 4 windowed crop runs (slot B) + Experimental's arm.
