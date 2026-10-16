# Package 7: death bodies on the GPU-skinned character path (design; Gameplay implements, Rendering reviews)

Written 2026-10-16 after the package 6 gate. With the bodies switched off, the head matched package 5 within 0.06 ms
(Experimental). Each live body costs about 0.2 ms, split evenly between CPU and GPU. The respin's static-pose fix
(drawDynamicMeshPosed with a pose serial) recovers 0.39 of the 1.87 ms. This note covers the rest. Code references are
to integration 4078e47b.

## Why a body costs what a live robot does not

- **CPU skinning and upload:** World::tickCorpses CPU-skins the whole mesh (assets::skinPose) for every body.
  drawDynamicMesh then rebuilds the interleaved vertices (~0.5 MB) and uploads them, with the indices, into the one
  shared dynVbo_ / dynIbo_. The posed fix removes the upload only for bodies that have stopped moving.
- **Its own shadow pass:** live robots go through IRenderer::drawSkinnedMesh. In Pipeline::drawDynamic a GPU-skinned
  draw (skinDraw_ set) is **queued** (dynQueue_, charDeferOn()). Its shadow caster and its draw then run at the next
  pass boundary in flushDynQueue, batched with every other character. A CPU-skinned body never enters that queue: its
  caster, shadow projection and draw run immediately and per draw. That is the GPU half of the 0.2 ms, and the posed
  fix cannot remove it.

## The design: a body is a character draw

1. **The draw.**
   - Per body part (the body, plus the jet hull when present) call drawSkinnedGpu(r, bindMeshOf(model), joints,
     weights, palette, nullptr, 1.0f, world, colour, key, serial) from src/game/GpuSkin.h, the same call Character
     uses.
   - Build the palette with buildPalette(model, c.glob, pal) from the joint globals that tickCorpses already computes.
     Only skinPose's vertex output goes away; keep a globals-only pose step.
   - If it returns false (bone rows full, more than 128 joints, WFC_NOGPUSKIN), fall back to today's
     drawDynamicMeshPosed, unchanged.
2. **The key.**
   - Use a stable pointer for the body's whole life: a heap-held Corpse, or a per-body token. Never &corpses_[i] in a
     vector that can reallocate, because each move takes a new bone-texture row.
   - A row comes back 600 frames after its last draw. With 512 rows shared with live pawns and weapons, keep
     concurrent body parts at about 150 or fewer. The 5 s LifeSpanAfterDeath already bounds them well under that.
3. **The serial.**
   - Bump it only when the pose changes. A settled body then uploads nothing: no palette row write, and bounds come
     from its last palette.
   - The ragdoll / death-entry animation bumps it every step while it plays.
4. **States stay as they are.** drawSkinned ends in the same drawDynamic material path, so these behave exactly as
   today:
   - setDrawOwner;
   - setCharacterColors (the owner colours);
   - setDrawEnergyDeath (the Defrag material: the per-owner runtime parameters reach the queued caster too, so a
     dissolving body casts only what is left of it).

   Keep the set / reset calls around the draw call, as tickCorpses does now.
5. **The shadow pass comes for free.** In the queue the body's caster joins the batched character shadows. No new
   renderer code is needed. The GPU share of the ~0.2 ms per body should fall to what a live robot's caster costs.
6. **Light environment.** A queued draw carries its own envSamples / bounds, the same as a live robot. A body keeps
   the light environment of its owner's form, so there is no lighting pop at death.

## Gib chunks

- World::drawCorpseGibs draws each chunk with r.drawMesh(gibMeshes_[k], ...). These are static GpuMesh buffers: no
  upload, one draw per chunk.
- The remaining cost is draw count: up to ~10-20 chunks per body for 5 s.
- Package 7 option: an instanced draw per gib mesh (one call per mesh type per frame, with a per-instance matrix
  buffer). The renderer's queueInstance path already instances static meshes that share a program. Check its
  gStats.instRejected counter on a gib-heavy frame first: if the chunks already instance, nothing is needed.
- Chunks cast no projected shadow today. Keep it that way, matching the original's small debris.

## Verification

- **Identity:** Off / Faithful byte-identical on the integration export. A body's pixels must match the CPU-skinned
  path. Live robots already switched from the CPU to the GPU path this way (the same transformPoint / weight sum;
  the bounds are bitwise equal); confirm pixels with a body-heavy identity frame, not by assumption.
- **Cost:** Experimental's corpse arm (NOCORPSE vs head vs the change) on the same match. Target: within 0.1 ms of
  NOCORPSE on frame, CPU and GPU p90.
- **Checks:** a WFC_SKINDIAG-style count of body parts on the GPU path against fallbacks, and the "bone-texture rows
  full" warning absent in a 16v16 match.
