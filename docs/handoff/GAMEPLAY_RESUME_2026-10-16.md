# Gameplay resume note (2026-10-16; updated about 20:05: a reboot may come at any time)

## Current state (update at every stable point)

### RELAUNCH: START HERE (2026-10-17 morning, before the user's reboot; agents/gameplay)

- **The card is HELD until Integration messages after the reboot** (Experimental's stuck GPU exe, the TDR pattern). No windowed exe
  until then. CPU, compiles, interpreters and null-render headless runs are allowed.
- **Package 7 order:**
  1. death presentation (patch 2 + the killer cam), **code COMPLETE, waiting only for the windowed crop slot**;
  2. hover / ride height + squad follow reversals, with ONE Classic re-baseline that also gives Classic the AI righting path;
  3. perf levers (visually lossless only);
  4. FaceFX.
- **Patch 2 = 11 edit scripts** in `docs/handoff/corpsep2/`, listed in order in `docs/handoff/GAMEPLAY_DEATH_PATCH2.md`, plus
  `corpsep2/new/*`. Base af4d0731 + Rendering e5fc6d65 (Camera.h roll).
  - The built tree is `work/int_p2`, with the diagnostic stage hooks. The base tree for compares is `work/int_p2b`.
  - Headless PASS at a208d3b0: BOTTEST 48 / 48 × 12, seed-7 SIMHASH identical.
  - Systems' FX-count row PASS (0 mismatches / 230).
- **After the reboot:**
  1. Rebuild both trees if needed: `powershell -ExecutionPolicy Bypass -File work/int_build.ps1 -Tag p2` / `p2b`.
  2. When Integration grants the slot: `node docs/handoff/corpsep2/mkp2.js crops`, then
     `run_guarded.ps1 -Steps work/pass29/p2/steps_crops.txt -Status work/pass29/p2/status_crops.txt -EnvSep '^^'` (add `-OwnWindow`
     if the hold is ours).
  3. **Confirm a wfc_rebuild.exe from the tree appears within 10 s** (the runner needs relative exe paths).
  4. Compare the shots base vs new. Check: the scorch, both ragdoll templates settling, a melee death without gibs, the KillZ
     insta-gib, the robot / vehicle death camera, the roll sign, and the killer cam at 3 s.
  5. Then generate the final patch from a clean af4d0731 archive + e5fc6d65 Camera.h + the 11 scripts + new/* (never from int_p2,
     which has the hooks), via `work/mkpatch.sh`. Byte-check with patch -p1, commit, and send Integration the hash.
- **~09:00: PATCH 2 HANDED TO INTEGRATION FOR MERGE, agents/gameplay 9ac94924** (`docs/handoff/GAMEPLAY_DEATH_PATCH2.patch`).
  - It goes on af4d0731 + Rendering e5fc6d65; the scorch is gated.
  - The death-camera re-crop PASSED.
  - Follow-ups: `edit_gib_materials.js` (after Systems' material compile); the scorch default on (after the DeathDecal_MAT data).
- **Hover result:** `docs/handoff/GAMEPLAY_HOVER_RESULT.md` (decision (b), confirmed at 24 seeds).
- **Squad follow reversals:** the first try, `hover/edit_squad_nobacktrack.js` (hold instead of walking back to an overtaken slot
  within 8 m), did **not** reduce follow reversals at 24 seeds × 4 maps (Smart follow 2.26 / 1.80 / 0.99 / 2.22 → 2.45 / 1.53 / 1.09 /
  2.08 per travel-min: noise). The mechanism is something else. Next: trace a squad member (WFC_NAVQTRACE) to see which goal flips.
  - The edit stays applied in work/int_hv for now; drop it or replace it before the hover + squad patch.
- **Campaign AI layer spec** from RE: `RE-Workspace/work/handoff/CAMPAIGN_AI_LAYER_RE.md` (6ce8158), for the campaign phase (buddies never
  downed / instant respawn, tactical volumes, the downed fire penalties).
- **2026-10-17 ~07:30, the crop slot was RUN** (shots in `work/pass29/p2/crop_*`; sheets in work/pass29/p2). The behaviour all passes.
  Three visual defects block the merge:
  1. the scorch draws opaque white: Rendering fixes the decal blend; if it slips past tonight, apply `corpsep2/edit_scorch_gate.js`;
  2. the gib materials are untextured: Systems compiles DeathSkelGib / DeathGib_Mat_INST, then apply `corpsep2/edit_gib_materials.js`;
  3. the robot death camera sat inside the corpse: FIXED by `corpsep2/edit_death_camera_anchor.js` (860eac04). It needs the 2-min
     re-crop, `work/pass29/p2/steps_recrop.txt`, on Integration's relay.
  - Transforming actors: "not drawn" accepted; the draw is on Rendering's list.
- **Hover: the 24-seed data (`docs/handoff/hover/tip_summary_24seeds.txt`) went to Integration with a decision request:**
  - (a) faithful for both;
  - (b) Classic faithful, Smart keeps its steadier righting (I lean b).
  - Waiting for the answer.
- **Open with other lanes:** none blocking. AssetTools will regenerate the FaceFX reference CSV with the product rule (for the FaceFX
  item). Systems adds the local death sound itself.
- **Hover item notes:** `docs/handoff/GAMEPLAY_RIDE_HEIGHT_DESIGN.md` (RE updates + Integration decisions at the end).
  - The trees are `work/int_hv` (af4d0731 + hover/edit_hover_faithful + edit_navq_tip + edit_hover_ai_path) and `work/int_hvb` (af4d0731
    + edit_navq_tip).
  - NAVQ TIP results: `docs/handoff/hover/tip_summary_6seeds.txt`; the 24-seed run is in `work/pass29/hv` (aggregate with
    `node docs/handoff/hover/aggtip.js <dir> base,new2`).
  - The faithful gate removes Classic's long stuck tips but makes short tips more frequent: decide with Integration.
- **Added to the queue (after hover):** the 32 TnTransformingWorldActor doors / bridges have no collision
  (`docs/handoff/MP_TRANSFORMING_ACTORS.md` + AssetTools `manifests/maps/mp_transforming_actors.json`). Plan: dynamic sets from the
  listed boxes; first check whether they change state in MP.

- **2026-10-17 03:35, the user set the package 7 scope:** package 7 is now the **hand-over build**; package 6 still finishes as the checkpoint.
  - **About 05:10: patch 2 (all six edits + GibRagdoll) BUILDS in `work/int_p2`; headless PASSES.**
    - BOTTEST 48 / 48 on all 12 seeds; seed-7 SIMHASH identical to the base `work/int_p2b` (af4d0731) over 18,007 lines.
    - Logs are in `work/pass29/p2`. The null renderer never spawns gibs, so the ragdoll / scorch / camera need the windowed crop slot
      (about 06:15, after Experimental's arm B and Rendering's Streets run).
  - **Crops READY for the slot:**
    - Both trees are rebuilt with the diagnostic hooks (`corpsegpu/stage_hooks2.py` + `corpsep2/stage_hooks_p2.py`; never in the patch,
      which comes from a clean archive + the edit scripts).
    - Steps: `work/pass29/p2/steps_crops.txt` (`node docs/handoff/corpsep2/mkp2.js crops`; 6 runs: corpse_10 / deathcam_robot /
      deathcam_vehicle × base / new).
    - Run with run_guarded `-EnvSep '^^'`, adding `-OwnWindow` if Integration's hold is ours. Then compare the shots.
  - **HARD RULE until FREEZE LIFTED** (Integration, after my sqlite / node runs in the timed hold forced 3 map reruns): **no interpreter
    process at all** (python, node, sqlite3, py_compile). Only the editor / file tools, git and sed.
    - The ragdoll's GIB_LowBody2_PHYSSYS table (7 bodies, 6 constraints) is read from authored.db **after** the lift.
  - **Patch 2 in progress (code edits only, freeze):**
    - The tree is `work/int_p2`, a git archive of af4d0731 (not built yet).
    - Edits in `docs/handoff/corpsep2/`, in order: `edit_scorch.js`, the DeathDecal_MAT scorch under gibbed robots via
      `renderer_->spawnDecal`. It's Rendering's documented recipe: 300 UU trace down from actorLocation, |n.y| ≥ 0.5, 800-950 UU,
      random roll, 30 s, with random numbers hashed from the body seed so `frand()` is untouched.
    - `edit_momentum_picker.js` (applied after edit_scorch.js): the full momentum picker.
      - A per-victim TearOffMomentum direction is noted at the 8 real hit sites: hit-scan local / bots, HurtRadius, explosions,
        shockwave, melee.
      - It's consumed by applyMatchDamage and read by requestCorpse; it falls back to the interim. Presentation only.
      - **Not built yet.**
    - **The RE answer on gibs:** one ragdoll per death, the ref pose at PositionSocket, from rest; 6-9 h.
    - `edit_death_camera.js` (after the picker): the authored death camera per the RE answer (DEATH_CAMERA_RE).
      - It keeps the live orbit rotation / offset frozen at death, plus the death anchor offset (robot (0, −300, 150), vehicle
        (−100, 0, 50)), on the corpse bone, which freezes at the gib.
      - Vehicles: the wreck yaw, pitch −30. FOV 85 / 0.4; the 1.25 / 1.0 s blend from the live view; the robot wiggle; the existing
        camera collision. Held until respawn. **Not built yet.**
      - **Not in it:** the roll, requested from Rendering (rebuild-rendering-fd) as a render::Camera roll field; I add `cam.roll`
        when it lands.
      - **The killer cam** (PlayerSpectating after 3.0 s, initial target _LastPawnWhoKilledMe) is **its own item, right after patch
        2 and before the ride height** (Integration).
    - **Skeletal gibs are ragdolls** (`corpsep2/SKELETAL_GIBS_AUTHORED.md`, 49714373): a PBD ragdoll, 8-12 h. The initial pose
      needs RE (ReVa is down: headless export into work/).
    - **Next:** the Death camera values and the momentum picker (small), then the ragdoll. Then, after FREEZE LIFTED, build, run headless
      (SIMHASH identical, BOTTEST), and do a crop eye check of the decal.
  - **RE-ORDERED 2026-10-17 (the user, via Integration):** "I just want the game to look beautiful. After it looks like a brand new
    game only then while we optimize for more FPS at no extra cost to the Fidelity."
    - **New order:**
      1. Death-body patch 2 (scorch decal, skeletal gibs, authored Death camera, full momentum picker) + the Defrag latch removal;
      2. ride height + squad follow reversals (one shared Classic re-baseline);
      3. the perf levers (instanced gibs, hull on the GPU path, merge after rest, taking the skeletal gibs in);
      4. FaceFX.
    - **Every perf lever must be visually lossless** (a side-by-side eye check, zero difference) or it doesn't merge.
    - **No true dependency of the gibs on the merge path:** skeletal gibs ship drawn per piece, and lever 3c takes them in later.
  - **Original order and estimates sent to Integration** (estimates still valid):

    | # | Item | Estimate |
    |---|---|---|
    | 1 | The Defrag latch removal + crops | 0.5 h + a slot |
    | 2 | Body perf levers: a. instanced gibs; b. hull on the GPU path; c. merge after rest, if still needed | 1-2 h / 2 h / 3 h, a corpse-arm slot each |
    | 3 | Ride height (the design doc's model, Optimus faithful) | 5-10 h |
    | 4 | Squad follow reversals | about 6 h |
    | 5 | Death-body patch 2 (scorch decal, skeletal gibs, authored Death camera, full momentum picker) | 10-13 h, then re-time |
    | 6 | FaceFX hook | 5-7 h |

  - **Order APPROVED by Integration.** The freeze lifts **only at Integration's "FREEZE LIFTED" message** (after Experimental's
    extra 12-min attribution hold). Then:
    - compiles are OK; exes run with WFC_MUTE=1 and WFC_NOINTERACT=1;
    - crops and corpse-arm slots on request, one timed hold at a time across lanes, queued by Integration.
    - Items 3 and 4 share **one** Classic SIMHASH re-baseline.
    - **First run after the lift:** the PERFSTATS instancing counters (see `GAMEPLAY_CORPSE_ONEDRAW.md`, lever 1 code reading).
  - **Target:** the RT-off cost at 63 bots back toward the WFC_NOCORPSE floor, with 5v5 unchanged. WFC_NOCORPSE / WFC_NOMINEPRES stay.
- **2026-10-17: the package 6 gate is running on af4d0731. FREEZE** (Integration): no compiles and no exes until Integration lifts
  it by message; code edits and docs only.
  - **Both items below move to package 7:** the Defrag latch removal, and lever 1, the instanced gibs. They can't be measured during
    the gate, and Systems' build follows it.
  - **Also package 7:** the FaceFX pose hook, from AssetTools' spec `docs/handoff/CAMPAIGN_FACEFX_RE.md` (integration 181c9d27). The
    plan is in `docs/handoff/GAMEPLAY_FACEFX_HOOK.md`: the LocalPose override before poseGlobals, a bone-space probe, and the unit test
    on DX_OPRIME_LK002649 at t = 1.0 s.
- **Update (late 2026-10-16):** the package 6 candidate head is **af4d0731**, with the X3 volumetrics **and Rendering's Defrag draw-list cache fix**. Base both items below on af4d0731.
  - They may run **in parallel**.
  - They join package 6 **only** if they pass before Systems' package build starts, about 1.5 h after the relaunch.
  - The latch removal: `node docs/handoff/corpsegpu/remove_gpu_defrag.js` in an af4d0731 tree, then the 4 crops (`mkgc.js crops`, retargeted to af4d0731 trees), especially frames 300-360 (the Defrag body).
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
