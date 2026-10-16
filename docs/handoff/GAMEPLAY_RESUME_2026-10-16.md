# Gameplay resume note (written 2026-10-16 18:40, before the 21:30 reboot)

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
