# Package 7 body lever: one draw per settled body (design)

Gameplay, 2026-10-16. A design only; code follows after the X3 gate.

## Budget

- **Measured** on Berth, 63 bots, RT off, p90: integration 51b55a09 (GPU-skinned bodies) **3.10 ms**, against the `WFC_NOCORPSE` floor of **2.17 ms**. That leaves **≈ 0.9 ms** to recover.
- **Draws:** dynamic draws are 176 with bodies against 126 without. With up to 9 death presentations alive at 63 bots (the measured maximum), that's **≈ 5-6 draws per body**.
- **Target:** within 0.1 ms of the floor, without touching the look. Per the user's bar there is no shorter life, no cap and no shadow removal.

## What a body draws today, per phase

| Phase (per body) | Draws | Shadow caster |
|---|---|---|
| Death animation → TotalTime (0.3-4 s) | 1 GPU-skinned body (queued; Defrag bodies CPU-posed until Rendering's fix) | batched with characters (GPU path) |
| Gib → +5 s | 4 RB_Gib chunks (`drawMesh`, static buffers, one call each) | none (as authored: small debris) |
| Vehicles: gib → +3-7 s | 1 hull wreck (`drawDynamicMeshPosed`, a rigid bind mesh) | **immediate, per draw** (CPU path, not queued) |
| FX (Exp_Death*, TF_Death_GibsTrail × 4, wreck smoke) | particle draws, LOD'd by distance (Systems) | none |

The 5-6 draws per body are mostly **the 4 chunks** plus the body or the hull. The hull is also the only part whose caster isn't batched.

## Levers, in order

1. **Instanced gib chunks** (largest, cheapest).
   - There are 11 RB_Gib meshes, so all chunks of all bodies need at most **11 draws** in total, instead of 4 per body.
   - Step 1: read `gStats.instRejected` / the instance counters on a gib-heavy frame (Gorge or Berth, 63 bots, `WFC_PERFSTATS`). If the chunks already batch through the renderer's internal `queueInstance` (static meshes sharing a program), this lever is already in place and the draw count comes from elsewhere.
   - If they're rejected, find why: per-draw colour, owner state, or a material outside the instancing program. Make them qualify (draw with the neutral colour and owner 0; the chunks have no team tint) or add an explicit instanced call (Rendering).
   - **Expected:** 9 bodies × 4 = 36 chunk draws → ≤ 11. That's about −25 draws, most of the gap.
2. **The hull wreck on the queued path.**
   - Draw it with `drawSkinnedGpu` using a one-joint palette: the hull is rigid, so its world matrix is the palette entry, on the bind mesh from `hullMeshes_` (a stable key).
   - Its caster then batches with the characters instead of drawing immediately. The serial bumps only while it moves, so a settled wreck uploads nothing.
   - **Prerequisite:** the hull glTF carries joint / weight streams, or a one-joint stream is synthesised at load. Same Defrag caveat as the bodies until Rendering's fix.
3. **One draw per settled body: merge after rest.**
   - Once a body's chunks are all at rest (`k.rest`) and its hull has settled, bake the rested chunks (and the hull) into **one static mesh per body**: one draw and, for the hull, one caster, until it ends.
   - Only worth it if lever 1 leaves chunk draws significant. With instancing the chunks already share draws across bodies, and merging would *break* that sharing. So **do lever 1 first, measure, and only then decide on 3.**
4. **Caster batching for the remaining CPU-path draws** (Defrag bodies during their 3 s sweep, any GPU refusals): these disappear once Rendering's energy-death fix lets Defrag bodies use the GPU path. No Gameplay work needed beyond removing the latch (tomorrow's item).

## Not levers (they change the look)

A shorter LifeSpan, fewer chunks, removing trails or casters, a body cap. A cap never engaged anyway: at most 9 bodies at once at 63 bots.

## Measurement and gates

1. **Before:** the 51b55a09 + `WFC_PERFSTATS` instance / reject counters and draw counts on Berth and Gorge at 63 bots.
2. **Per lever:**
   - Experimental's corpse arm: NOCORPSE vs head vs lever, the same match, frame / CPU / GPU p90, plus dynamic and instanced draw counts;
   - corpse crops at 10 m and 40 m (look-back), byte-identical or within the accepted GPU / CPU rounding (Integration 2026-10-16);
   - SIMHASH identical (presentation only); BOTTEST 48 / 48.
3. **Stop when** within 0.1 ms of NOCORPSE.

## Estimate

| Lever | Cost |
|---|---|
| 1 (counters + making chunks qualify) | 1-2 h, + Rendering if an instanced call is needed |
| 2 (one-joint hull palette) | about 2 h |
| 3 (merge after rest) | about 3 h, only if still needed |
