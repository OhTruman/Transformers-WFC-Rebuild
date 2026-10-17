# Gameplay resume note (2026-10-16; updated about 20:05: a reboot may come at any time)

## Current state (update at every stable point)

- **Tomorrow, 2026-10-17, after the user's reboot** (Integration's order):
  1. **Lever 1 of `GAMEPLAY_CORPSE_ONEDRAW.md` (merged 51b64874): instanced gib chunks.**
     - First read the renderer's instance / `instRejected` counters on a gib-heavy 63-bot frame (Berth / Gorge, `WFC_PERFSTATS`).
     - If the chunks are rejected, make them qualify (neutral colour, owner 0) or ask Rendering for an instanced call.
     - Compiles and headless runs are free then; the crops and the corpse arm need a slot.
     - It joins package 6 **only** if it passes before Systems' package build on the final head starts; otherwise package 7.
  2. **Remove the Defrag CPU latch** (`edit_gpu_defrag.js`) once Rendering's energy-death fix for queued skinned draws is in, then re-run the 4 crops.
- **About 21:30: the GPU-corpse patch is MERGED** by Integration.
  - Corpse arm on Berth at 63 bots: p90 3.10 ms, against 3.50 for the head and 2.17 for the NOCORPSE floor. That's −0.41 ms; the 0.1 ms target was missed, with about 0.9 ms left. Dynamic draws are unchanged (176 vs 126 without bodies).
  - **Next (package 7 lever, the user's look bar):** fewer draws per settled body: merge the gib chunks and body parts into one draw, and instanced gibs (check `gStats.instRejected` first). Not a shorter life, a cap or shadow removal.
  - **Tomorrow:** remove the Defrag CPU latch once Rendering's fix lands (the queued skinned draw honouring setDrawEnergyDeath), then re-run the 4 crops.
- **About 21:05**, at agents/gameplay 252e6426: the GPU-corpse crops **PASS** under Integration's decisions (Defrag CPU latch; GPU/CPU rounding accepted; f920 for Rendering's eye). The exes have gone to Experimental for the corpse arm. Waiting for the timing result and the merge.
- **About 20:45**, at c0938c76: the GPU-corpse patch builds.
  - Headless checks pass: BOTTEST 48/48 on 12 seeds; seed-7 SIMHASH identical (null render never draws, so these are sim only).
  - First windowed crops **FAILED** on the Defrag (melee) body: the queued GPU draw ignores setDrawEnergyDeath (`docs/handoff/corpsegpu/defrag_cmp.png`).
  - Fixed: Defrag bodies stay on the CPU path (`edit_gpu_defrag.js`). `work/int_gcn` is rebuilt with the fix; the crops are **not yet re-run**.
  - Other bodies differ only by GPU/CPU rounding (max 1-2, plus 2 edge pixels). Rendering decides whether that meets identity.
  - 16 v 16: 0 GPU refusals, 0 rows-full warnings.
- **Next:** with a slot from Integration, re-run the 4 crops (`node docs/handoff/corpsegpu/mkgc.js crops`, the first 4 steps). Then on PASS, Integration merges and Experimental times the corpse arm (target within 0.1 ms of WFC_NOCORPSE).
- **Trees:** `work/int_gcb` (head 1c60a8ba + the stage hook) and `work/int_gcn` (+ patch + hook). After a reboot, rebuild with `powershell -File work/int_build.ps1 -Tag gcn` / `gcb` if needed.
- **The patch edits, in order** (`docs/handoff/corpsegpu/`): `edit_corpse_gpu.js`, `edit_gpu_refuse_log.js`, `edit_gpu_defrag.js`.

## In progress: GPU-skinned death bodies (moved into package 6 by the user's decision; package 6 is held 2-3 days for volumetric lighting)

**Patch:** `docs/handoff/GAMEPLAY_CORPSE_GPU.patch`, written, **not yet compiled or checked**.
- Against integration 78d06b6e; it contains the respin's static-pose code.
- Design: `GAMEPLAY_CORPSE_GPU_IMPL.md` (Gameplay) and `RENDERING_CORPSE_GPU_PATH.md` (Rendering).
- The edit script that produced it: `docs/handoff/corpsegpu/edit_corpse_gpu.js`. Run it from a tree root of the head; it runs on top of the static-pose code, which the head already has.

**What it does:**
- Corpse gets `gpuKey` (heap token, a stable palette key), `palette` and `mutable gpuOk`.
- tickCorpses: `poseGlobals` + `buildPalette` while the pose changes; `skinPose` only on the fallback.
- drawCorpses: `bindMeshOf` / `syncMats` / `drawSkinnedGpu(..., gpuKey, poseSerial)` inside the same owner / colour / Defrag calls. On false: `gpuOk = false`, then the next tick CPU-skins, and the body draws `drawDynamicMeshPosed` as before.
- The jet hull wreck stays posed (constant serial).

**Next steps:**
1. **Build two trees** (compiles allowed only when Integration says):
   - `int_gcb` = head + `docs/handoff/corpsegpu/stage_hooks2.py` (the diagnostic WFC_CORPSESTAGE hook with WFC_CORPSESTAGE_DIST; a negative distance puts the camera 40 m down the corridor, looking back);
   - `int_gcn` = head + `edit_corpse_gpu.js` + the hook.
   
   Extract with `git archive <head> > a.tar; tar -xf a.tar -C work/int_X`, then `work/int_build.ps1 -Tag X`. The `int_csb` hook edit for negative distances was applied by hand; it's in `stage_hooks2.py` only as the base form, so re-apply the look-back branch: `if (camD >= 0) ... else setSpectatorView(spot - f * camD + up 4, yaw0 + pi, 0.3 * 9 / camD)`.
2. **Headless checks** (null render):
   - BOTTEST seeds 1-12 in Systems' env (`docs/handoff/corpsegpu/mkcs.js headless`, retargeted to int_gcn): 48 / 48;
   - seed-7 SIMHASH identical to head;
   - the WFC_CORPSETEST counts of GPU vs fallback bodies (add a log line);
   - `gStats.instRejected` on a gib-heavy Gorge 63-bot frame (gib instancing only if the chunks don't already instance);
   - no "bone-texture rows full" warning in 16 v 16 / 32 v 32.
3. **Windowed crops (slot needed:** the card is Rendering's for the volumetric work; Integration interleaves): `mkcs.js crops` with head = int_gcb, new = int_gcn, distances 10 and −40 (look-back); 65 frames each must be byte-identical. Check the Defrag (melee) body especially: the energy-death parameter on the queued caster.
4. **Then:** send Integration the patch + hash; Experimental times the corpse arm. Target: within 0.1 ms of `WFC_NOCORPSE` on frame / CPU / GPU p90.

## Queued after it (package 7)

- **The ride-height probe model:** `GAMEPLAY_RIDE_HEIGHT_DESIGN.md`. Optimus ships faithful (0.34 m floor probe, no exception); if truck bots stick, the fix is a bot path rule.
- **Ledge guard:** `GAMEPLAY_LEDGE_GUARD_PKG7.md`. v3 (the downward segment cast) still fired at the Streets (110, −510) non-drop and let one trench entry through, so it needs another design pass.

## Standing rules (from Integration / the user, still in force)

- WFC_MUTE=1 and WFC_NOINTERACT=1 on every exe (the scratchpad `run_guarded.ps1` sets them).
- One exe at a time; slot B for windowed runs; no compiles or exes during a PERF_HOLD (`Rebuild-Experimental/work/PERF_HOLD`).
- Kill own processes by PID only.
- Commit with the attribution lines; patches go in `docs/handoff`, made with `work/mkpatch.sh`, and are verified on a clean archive.
- Every reply to the user ends with the open decisions:
  - squad fidget / strength;
  - squad formation;
  - the stray `F:\Transformers` file;
  - the Smart team-mate hold-fire rule.
