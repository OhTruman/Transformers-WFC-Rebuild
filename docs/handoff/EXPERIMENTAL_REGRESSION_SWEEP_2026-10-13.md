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

## 6. RT stage 1 merge review (integration 5d1aecd0, head 52efcbf7; code read)

| item | status | detail |
|---|---|---|
| F9 bound three times | **FAIL -> fixed e6d61e2d (code-presence PASS: F9 = Button::RenderMode only, Win32Window.cpp:70; Button::Debug = F11, :55; bot overlay = F12, Application_Frontend.cpp:844; no other F9 / F11 / F12 binding)** | Button::Debug (debug wireframes: bounds / capsule / aim ray, World.cpp:3621), the playtest bot overlay (Application_Frontend.cpp:844) and Button::RenderMode (the RT cycle) all fire on F9 in every build. The first two predate the merge. Proposed: RT keeps F9; Debug moves to a dev-only key; the overlay moves to F12. |
| release() keep list / RT objects freed | PASS (minor) | rtMode_ and rtAvailKnown_ kept; WFC_SETTINGSTEST checks the RT mode; GL RT objects and BLAS / TLAS / masked / hit data freed per map. The clearStaticWorld waitIdle-failed branch drops the resources without freeing or logging them (up to ~187 MB per map change); a LOG_WARN was requested. |
| RT Off and never toggled | PARTIAL | Image byte-identical (Rendering's gate), but every launch creates the RT bridge (D3D12 device, queue, allocators, shared fence imported as a GL semaphore) through the frontend's hardware probe (rayTracingAvailable -> Bridge::init). Asked for a throwaway capability probe / lazy bridge, and a regress-8map with RT Off vs 05319349 before the playtest package. |
| RT failure fallback | PASS | g_failed is sticky; the uniform gates give the Original look (within the accepted <= 1 / 255). |
| F9 vs the Ray Tracing row | NOTE | F9 on a map loaded with RT Off arms the next map only; F9 changes neither the profile nor the Settings row. Goes in the playtest guide. |
| Env vars leaking into shipping behaviour | PASS | every WFC_RT_* is read only when set. |

## 7. Build and harness items added tonight

| item | status | detail |
|---|---|---|
| Integration builds without the RT shaders | **FAIL -> fixed 08:27** | Integration's third_party had no dxc / ffx_denoiser, so every integration build had WFC_RT_SHADERS undefined: the Ray Tracing row showed and Faithful / Realistic looked Original ("RT bridge: built without the RT shaders"). Integration fetched dxc 1.9.2609.5 + FidelityFX SDK v1.1.4 denoiser and re-configured. tools/fidelity/rt-build-check.ps1 -BuildDir: integration build/release PASS (WFC_RT_SHADERS, WFC_RT_DENOISER, 10 / 10 generated headers). Rendering (rt2): rtAvailable() false without the RT shaders. Runtime check (-Exe: profile RayTracing=2 builds a TLAS, logs no "without the RT shaders"): PENDING (exe launches after the GPU cap). |
| Settings precedence: profile vs WFC_* env | PASS (pinned) | Since the frontend's Ray Tracing row, the profile's RayTracing= / HDTextures= are applied at boot and override WFC_RT_MODE / WFC_HDTEXTURES. Every harness profile writer now writes RayTracing (0 by default; Get-BotProfile -RayTracing) next to HDTextures + HDTexturesUserSet=1 (M05.ps1, bot-matrix legacy, m07-frontend, map-loss-gate). lockstep-ab writes each arm's WFC_RT_MODE into that arm's profile. |
| lockstep-ab marks a Ray Tracing mismatch UNKNOWN | PASS | New columns rt_req / rt_mode / rt_scene / rt_noshaders (lib/Run.ps1 Get-WfcRtState: the last "renderer: Ray Tracing <mode>" line, else inferred from the RT scene: TLAS = Faithful, + reflection hit data = Realistic). UNKNOWN if the mode differs from the request, or an RT-on arm built no RT scene / has no shaders. Checked on Rendering's s5_mat log (Realistic inferred) and a regress-8map head log (Off). |
| GPU-lost dialog in harness runs | PASS (pinned) | Add-WfcHarnessEnv pins WFC_GPULOST_DIALOG_S=0. |
| Harness exit codes 0 / 1, no 0xC0000409 | PENDING (runtime) | N2 cause found by Gameplay: the watchdog's global std::thread terminated at static destruction on init-false harness paths (WFC_WEAPONTEST, WFC_PARTICIPANTTEST); a static guard now joins it (integration 51260f56, RenderWatchdog.cpp). The self-test sweep on 51260f56 records every test's exit code. |

## 8. TDR diagnostic (Integration's analysis, 2026-10-13 morning)

Five AMD TDRs this morning. The cause was oversubscription of the shared card (several lanes' game exes + the DML ladder + RT builds); no RT work was active at any TDR. **Signature to look for:** GL frames over 250 ms of GPU time with no CPU cause in the same frame (Rendering's pure-GL Off runs logged 260-765 ms GPU frames all morning). That is the driver time-slicing contexts, the step before a TDR. In a wfc.log: "GPU frame spike <ms> (cpu <ms>)" lines with GPU >> CPU, or a "GPU frame time" warning near "Windows TDR resets the driver at ~2000 ms". Mitigation since 09:00: at most 2 game exes on the card (slot A Rendering RT, slot B rotating). The product's answer to an actual reset is N1a's exit path.

### N1 residual, observed 08:54:41 (TDR #6, 2-exe rule active)
The leak soak on 51260f56 hung after the reset with its main thread blocked in wglSwapBuffers (atio6axx <- opengl32 <- gdi32 SwapBuffers <- Win32Window::present <- Application::drawFrontendFrame), in the lobby between matches 2 and 3. Never spinning; never reaching the beginFrame poll. Systems' 23456 dump showed the same signature. So 2 of today's 3 hang dumps are the case the merged fix does not cover; the GetDeviceRemovedReason backstop is needed for players. The soak stopped at 2 of 16 matches (Seed 5778 -> 4689 MB, Berth 5433 -> 4584 MB; audio PCM back to 36.5 MB both times): no leak verdict yet.
**Residual closed (integration 14c3ffca):** covered by the evidence backstop. The watchdog keeps a small D3D11 probe device per hardware adapter; a render thread stalled >= 5 s is ended (after the stall line and the dump) only when a probe's GetDeviceRemovedReason reports the device removed. Never on time alone, and never under a debugger. WFC_SIMHANG=removed tests the path. Verified by simulation only; a real-TDR test needs `dxcap -forcetdr` with the user's OK. Review notes (minor): the probes cover every hardware adapter, not only GL's, so a reset on another adapter during an unrelated long stall (e.g. a minutes-long RT build) would also end the game. Keying the probe to GL's adapter LUID (d3d12AdapterForGL) would close that. This machine shows only the RX 7900 XTX plus the Parsec virtual display.

### N1b: the robust GL context is never created on AMD 26.6.4 (found from Systems' log observation, 09:30)
Every run today logged "OpenGL context: legacy (WGL_ARB_create_context_robustness unavailable: getExt 1 create 1 ext 0)". A standalone WGL probe (work/wglprobe/probe.c) shows:
- the driver omits WGL_ARB_create_context_robustness from both WGL extension strings, while GL_ARB_robustness / GL_KHR_robustness are present;
- the legacy context's GL_RESET_NOTIFICATION_STRATEGY is NO_RESET_NOTIFICATION (0x8261), so the beginFrame reset poll can never report a TDR on this machine. 14c3ffca's device-removed backstop is the protection that actually works here;
- wglCreateContextAttribsARB still accepts LOSE_CONTEXT_ON_RESET (with or without ROBUST_ACCESS) and reports strategy 0x8252.

Proposed fix: try the robust creation whenever WGL_ARB_create_context exists; keep it only if the strategy reads 0x8252; leave out ROBUST_ACCESS (its cost is unmeasured). Status: FAIL (detection gap), owner Systems / Rendering (Win32Window).

rt-build-check: direct boots apply the profile's Ray Tracing row only on frontend boots, so the -Exe check now also sets WFC_RT_MODE=2 (Systems).
Probe result for the TDR section: **without the robust-context fix, the beginFrame reset poll is dead on this driver** (legacy context = NO_RESET_NOTIFICATION). rt-build-check -Exe now requires "OpenGL context: robust ... LOSE_CONTEXT_ON_RESET / 0x8252" (result rtcheck.context). Before the 12:30 timed rows, a lockstep identity capture (tools/fidelity/identity-capture.sh, one map, RT Off, HD off, 25 captures + SIMHASH) compares the final head with 05319349, to show the context change is image-identical.

## 9. Slot-B essentials on f70a46d5 (robust context; 09:46-)

| check | result | detail |
|---|---|---|
| WFC_SIMRESET=300 (direct reset poll) | PASS | exit 3 after 36 s, wfc_gpu_lost report written ("the GL context was lost (reset status 0x8255)") |
| WFC_SIMHANG=removed (evidence backstop) | PASS | exit 3, "the GPU device was removed (reason 0x887a0005) and no frame completed for 7.1 s" |
| WFC_SIMHANG=1, no removal evidence | PASS | NOT ended by the product (still running at the harness' 75 s timeout, no report): no false kill |
| rt-build-check (build + -Exe, Streets, WFC_RT_MODE=2) | PASS 3 / 3 | WFC_RT_SHADERS + DENOISER, 10 / 10 headers; "OpenGL context: robust (GL 4.6 compatibility, LOSE_CONTEXT_ON_RESET; WGL_ARB_create_context_robustness not advertised, created anyway)"; RT scene built |
| map-loss-gate (Streets -> lobby -> Berth -> lobby -> Streets) | PASS (gate false positive on Berth) | Streets both visits PASS against the known-good reference. Berth reported FAIL (black 0.47-0.72), but the frames show the environment drawn and lit: Berth is a dark interior and the gate has no Berth reference (b_1spawn / b_2move inspected). Process clean, no display-driver events, render data root resolved. Gate improvement: a Berth reference frame. |
| m07-renderstate (normal) | PASS 7 / 7 | world frame after every overlay; GL entry state |
| sim-determinism (7200 lockstep frames, 8 v 8, seed 123, 2 threaded runs) | PASS | 0 of 2 diverged |
| identity-capture Rust 504: 05319349 vs f70a46d5 (robust context, RT Off, HD off) | PASS | SIMHASH 3002 / 3002 equal; 25 / 25 captures identical. The robust-context change (and everything merged since 05319349, including RT stage 1 Off) is image-identical on Rust. |
