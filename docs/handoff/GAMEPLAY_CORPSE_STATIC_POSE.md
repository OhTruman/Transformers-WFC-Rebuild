# Death bodies: skin only while the pose changes (package 6 respin)

Gameplay, 2026-10-16. The patch is `docs/handoff/GAMEPLAY_CORPSE_STATIC_POSE.patch`, against integration ddea856e (`World.h` / `World.cpp`).

## Why

The package 6 gate regress (RT off) was the death bodies, by Experimental's corpse arms: `WFC_NOCORPSE=1` lands within 0.06 ms of the base on frame, CPU and GPU, and the mines cost nothing.

The cost per body:
- every un-gibbed corpse ran samplePose + blendPose + skinPose on the CPU **every frame**;
- `drawDynamicMesh` then rebuilt the interleaved vertex buffer (about 0.5 MB per robot) and uploaded it, even though most corpses sit in a static end pose for most of their TotalTime;
- the vehicle hull wreck likewise re-uploaded its unchanging bind mesh every frame.

## The change (look unchanged)

**Bodies:**
- A corpse is re-skinned only while its pose changes. Once its death clip has ended (or it has none) and the blend-in is done (`poseStatic`), tickCorpses skips samplePose / blendPose / skinPose.
- drawCorpses draws through `drawDynamicMeshPosed` with a **pose serial**. Each re-skin takes a new serial from a global counter (`corpsePoseSerial_`). The renderer keeps the vertex buffer per MeshData and skips the build / upload while the serial is unchanged.
- **Cache key safety (Rendering's note):** corpses move in `corpses_` when an earlier one is erased, so the mesh address changes. Serials are unique across all corpses, so a moved corpse's address always holds a different serial: it rebuilds once and never draws another corpse's buffer.

**Hull wreck:** the shared bind-pose hull (heap-held in `hullMeshes_`, a stable key) is never re-posed; only its model matrix moves. It draws through `drawDynamicMeshPosed` with a constant serial.

**Harness switches** (env, default off): `WFC_NOCORPSE=1` (no body / gibs / wreck; the death light and sound unchanged) and `WFC_NOMINEPRES=1` (no mine presentation), for the package gate's cost arms.

## Checks

| Check | Result |
|---|---|
| Corpse crops (WFC_CORPSESTAGE, Streets seed 7, the 5 staged deaths: melee Defrag, explosive, single shot, rapid shot, Truck4 hull wreck), frames 280-1560 every 20, camera at 10 m and at 40 m (from down the corridor, the bodies in view), head ddea856e vs head + patch | **Byte-identical: 65 / 65 frames at each distance** |
| BOTTEST, 12 seeds, Systems' env | 48 / 48 on all 12 |
| Seed-7 SIMHASH (18,007 steps) | Identical to the uncapped head run (presentation only) |
| Experimental's timing (static-pose body tree, before the hull change) | Recovers 0.39 of the 1.87 ms RT-off regression |

The sheet is `docs/handoff/deathbody/corpse_static_pose_sheet.png`.

**The remaining cost** is about 0.2 ms per live body while it exists (draws, the shadow pass, gib chunks, trails). It is **package 7 item 1**:
- corpses through `IRenderer::drawSkinnedMesh` / GpuSkin.h, like live pawns, including the shadow pass (a stable key per corpse, palette rows, ≤ 128 bones, fallback on false);
- instanced or static gib chunks;
- measured against the `WFC_NOCORPSE` arm as the floor.

**Dropped:** the 10-unit presentation cap. At most 9 presentations were alive at once at 63 bots, so it never engaged. Its design stays in `GAMEPLAY_CORPSE_COST_PLAN.md` for package 7.
