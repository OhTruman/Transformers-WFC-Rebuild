# Experimental regression sweep - 2026-10-13 (integration head 05319349; runtime parts resume on 9a492cfa) - IN PROGRESS

User request (07:40, via Integration): "hunt for bugs, memory leaks, and any problems I have ever resolved or caught."
Scope here: every fix recorded on integration, plus the human-playtest findings in STATUS.md / docs/MILESTONES_09c.md.
Gameplay runs its own gameplay-side sweep (GAMEPLAY_REGRESSION_SWEEP_2026-10-13.md); gameplay mechanics are covered
here only through the in-product self-tests.

Exe: head 05319349, Release + PGO (its own tools/pgo profile), FidelityFX / Agility SDK copied in (md5 = integration's),
frozen render data (work/ab/m9c_37eec59 copy). Every run was windowed, with the harness pins (WFC_AUTODETECT=0,
WFC_AUTODETECT_WINDOWED=1, WFC_HDTEXTURES=0, WFC_BOTAI=Classic), and processes were killed by PID.

Status words: PASS = the fix is present and its repro passes (or the code is present where no repro exists);
FAIL = the problem is back, or a new defect was found; UNKNOWN = no verdict yet (run pending or environment-limited).

## 1. New findings tonight

| # | finding | status | owner | evidence |
|---|---|---|---|---|
| N1 | **The game hangs forever after a GPU driver reset (TDR)** instead of detecting the lost context. The AMD driver reset at 07:45:57 and 07:55:02 (System log 4101). The leak-soak exe (match 2, Berth) logged a wrapped GPU timer (18446741330926 ms), then "watchdog: no frame completed for 5.8 s - render thread last at 'beginFrame'", wrote a hang minidump, and spun one core with no response until killed. Systems' verify exe (Debris) hung the same way after the second reset. No reset line was logged, although Win32Window asks for LOSE_CONTEXT_ON_RESET. **Root cause (minidump, lldb):** the main thread is inside atio6axx.dll, called from `Pipeline::pawnOcclusionResults()` (WfcPipeline.cpp:6222) <- `Pipeline::beginFrame`; it reads occlusion-query results on the lost context and the driver never returns. glGetGraphicsResetStatus is polled only in GLRenderer::endFrame (GLRenderer.cpp:376), after that point, so the reset is never even logged. Fix shape: poll the reset status at the top of beginFrame, before any query readback, then exit cleanly. (OBS's graphics-hook64.dll is injected into the process on this machine.) | FAIL (new) | Systems (hang path -> clean exit + display restore, WFC_SIMHANG repro); Rendering reviews | work/leak/soak_05319349/wfc.log lines 4138-4140, wfc_hang_23000_0.dmp; Rebuild-Systems/work/verify_b6b6e38/Debris/bin/wfc_hang_23456_0.dmp |
| N1a | Fix landed: 45a8b9c0 (reset poll at the top of beginFrame -> clean exit, report wfc_gpu_lost_<pid>.txt, exit 3) + 9a492cfa (reset status latched; dialog 0 s in scripted runs). Experimental's review BLOCKED the first version's watchdog backstop (kill 15 s after any stall): legitimate stalls already run longer and recover (Rendering fsr3/runs/s5_mat about 310 s building RT reflection hit data; cold-cache program prewarm + warm-up draw about 15 s; 9 of 14 historical watchdog stalls recovered). Now default OFF (WFC_HANG_EXIT_S -1) until Systems keys it on GPU-loss evidence (GetDeviceRemovedReason). **Residual risk:** a TDR is caught only when the GL poll sees it; a driver call that blocks before the next beginFrame (Systems' 23456 dump: SwapBuffers in the frontend) still hangs. Switch tests: PENDING. | PASS (fix) / residual open | Systems | integration 45a8b9c0, 9a492cfa |
| N2 | WFC_WEAPONTEST ends in `libc++abi: terminating` (exit 0xC0000409) after "WEAPON SUMMARY: 19/19". main() returns 1 when init() returns false (every WFC_*TEST early return) without calling shutdown(); a member's destructor then terminates. The base 0502051 behaves the same, so this is not a regression. Test mode only; it only makes the exit code meaningless to harnesses. | FAIL (pre-existing, low) | Integration / Gameplay (src/main.cpp:28) | work/sweep/selftests_05319349/wfc_weapontest/run.log.err; same on work/sweep/selftests_0502051 |
| N3 | WFC_BOTTEST 38/39: "AI cost per step (avg < 0.04 ms per bot, max < 6 ms): avg 0.077 ms for 15 bots, max 8.94 ms" (phase 2). The per-bot average (0.005 ms) passes; only the single-step max is over the limit. The run overlapped about 5 game exes, 16 + 8 CPU threads of other lanes' jobs and a TDR. | UNKNOWN (quiet-machine rerun pending) | Gameplay if it repeats | work/sweep/selftests_05319349/wfc_bottest/wfc.log:720 |

## 2. Fix commits: code still present at head (git, read-only)

Method (work/sweep/presence.py, issues.py, missing.py): for every non-merge integration commit whose message matches
"fix", take the non-trivial lines it added under src/ and check each one is still in the same file at head. Commits
under 80 % were read by hand.

- 149 fix commits: 99 have >= 80 % of their lines present, and 40 touch no src (docs, STATUS, patches, harness).
- 10 fall below 80 %. Each one was traced; all are superseded or refactored, none lost:

| commit | fix | present | outcome |
|---|---|---|---|
| 4be2ec2d | M06b: render data root, pause menu, Choose Character hold, Extras movies | 22 % | superseded: root search now in WfcPipeline (logs "render data root"); pause close via UIController::onCurrentUIClosed (Frontend 9aa28ea7 / 6fb19f8); the Choose Character hold replaced by UseInGameLobby (Frontend 6fb19f8, CONFIRMED script); Game.PlayMovie + MovieEnded (Frontend 732a5b2). PASS |
| 3c9314a8 | M03 Systems: firing-cost fix (segmentHit grid walk) | 52 % | 2D DDA walk present (Collision.cpp:96 / 169). PASS |
| 2edae8fe | IRenderer::hdTexturesAvailable | 57 % | rewritten as hdRoot() with the AssetTools /pack layout (WfcPipeline.cpp:1712). PASS |
| 57ab7e2a / fe689fe0 / 049f614c | Gameplay passes 11 / 22x / 13 | 64-79 % | large feature commits that evolved since; gameplay side covered by Gameplay's sweep and the self-tests. PASS (code) |
| a2067e5d | matc_pbr M6 contract | 69 % | superseded by the M6 re-issues reviewed tonight (486b3285). PASS |
| 3e7ce286 | HUD weapon class -> crosshair | 70 % | the missing line is the old Ion Blaster default; head takes the equipped class (Application_Frontend.cpp:1495), as STATUS M08b asked. PASS |
| 3bffa0a8 | AMD non-finite draw guards | 71 % | per-position guard moved into drawDynamic's bounds scan (WfcPipeline.cpp:6347); sprite-corner guard into the FX replay's single corner validation (64b3c82a). PASS |
| 359da41b | M10b preview pawn / lobby scene | 71 % | evolved (preview path, persistent level present). PASS (code) |

Fixes delivered as patches (no "fix" commit touching src), checked by symbol at head: fxNextVid_ std::atomic (FX data
race), NvOptimusEnablement / AmdPowerXpressRequestHighPerformance (hybrid GPU), platform::longPath (18 sites; Frontend
reads through frontend::openPath), WFC_LOCKSTEP_EXACT, WFC_DRAWCOMBO, the display-mode restore / crash filter
(d7e34137, 13b27f82, d1131171). All present. PASS.

## 3. Human-playtest and user-caught issues

| issue (first reported) | fix | code at head | repro on head |
|---|---|---|---|
| Black world / malformed menus on a plain Release launch (M06) | 398b732a render data root | 97 % | map-loss / presentation route: PENDING |
| Pause menu over gameplay after Resume (M06) | 9aa28ea7 / 6fb19f8 | 86-100 % | presentation route (pause / resume): PENDING |
| Choose Character soft lock (M06) | 6fb19f8, 3e174831 | 100 % | presentation route: PENDING |
| Extras Movies / Credits soft lock (M06) | 732a5b27 | 96 % | code PASS |
| Account / rename text entry (M06) | 9dc70ec7, 136ac7a1 | 98-100 % | code PASS |
| Intro skipped on second boot (M06) | 3e9db972 | 100 % | code PASS |
| Kill feed overlap / HUD not animating (M06) | HmObjectInterpolator.addInterp (GfxPresenter.cpp:396) | present | code PASS |
| Menu side strips / vignette (M08b) | a6618512 | 100 % | code PASS |
| HUD crosshair always Ion Blaster (M08b) | bec41cd9, equipped class | 97 % | code PASS |
| Team games spawned on the wrong side (M08b) | 9cee8f04 | 100 % | code PASS |
| Scatter Blaster / Ion grey rods, smoke slabs | 5247d14e, ba5e9dae, 736f40e9 | 95-100 % | code PASS |
| Create a Character class-pick freeze (08k) | 50f07421 | 100 % | code PASS |
| Menu / lobby / spawn / music-start hitches (08k-08r, M09c) | 3f001109, dfa1f257, e0e7eaf5 | 91-99 % | code PASS |
| GFx / GL / map memory leaks (M28, killstreak, prefetch) | 1013867d, c2c25d8d, c0678b29 | 97-100 % | leak soak: PENDING (rerun after the GPU cap) |
| Bots hitting team-mates (pawnRayHit) | 5eeb3f79 | 100 % | self-tests (PARTICIPANT) PENDING |
| Radius damage through walls (HurtRadius LOS) | b749a833 | 100 % | Gameplay sweep |
| Stranded bots / rejoin | a7234074, a1ee934a, b1e65868 (8d91c7ea's 25 m superseded by 34 m) | 100 % | Gameplay sweep |
| Settings lost at map change (release() keep list) | 072437aa + WFC_SETTINGSTEST | 100 % | WFC_SETTINGSTEST 6 / 6 PASS (gamma, FSR scale / sharpness, anisotropy, HD, FSR 3.1, Modern); RT row not on head yet (agents/rendering-rt held) |
| HD on by default / auto-detect flips settings (2026-10-12) | b4cbd742, HDTexturesUserSet, hint only | present (refactored) | code PASS: Profile.h hdTextures = 0, rayTracing = 0 |
| Display left at 60 Hz after fullscreen (2026-10-12) | d7e34137, 13b27f82, d1131171 | 96-100 % | not run (no fullscreen mode switches tonight) |
| Package paths over MAX_PATH | 60f99c6b, 87aa7da4 | 100 % | verify_slim on the morning refresh |
| AMD freezes / resets (M09) | 3bffa0a8 guards, 5b72d0fc FX budgets | present (refactored) | see N1: the game does not survive a driver reset |

## 4. In-product self-tests on head (tools/fidelity/selftest-sweep.ps1)

Run so far, before Integration's 08:00 GPU cap stopped the sweep: ASYNCSTEP, BARRIERWALK, BOTNAV 7/7, BOTOBJ 12/12,
BOTTEST 38/39 (N3), BRAWL, CAM, CHARGE, CHASSIS, CLASSCHANGE, CTF, SETTINGS 6/6, WEAPON 19/19 (N2). PENDING: the rest
(the sweep resumes where it stopped).

## 5. Memory-leak soak

PENDING. The first attempt (16 matches over the 8 maps, 7v8 bots, 1080p windowed) hung in match 2 on the 07:45 driver
reset (N1). It resumes when the GPU cap lifts.
