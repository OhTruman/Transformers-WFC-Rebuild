# Hang and crash dumps - classes and status on af4d0731 (Experimental, 2026-10-17)

**Scope.** Every `wfc_hang_*.dmp` / `wfc_crash_*.dmp` in the five worktrees: 124 dumps, 2026-10-06 to 2026-10-17. That covers the
4 at the Rebuild root, about 60 in Rendering's work/, 3 in Gameplay's, 7 in Systems', and the rest in Experimental's.

**Tools.** There is no cdb / WinDbg on this PC, so I wrote two read-only tools:

- `tools/fidelity/minidump_scan.py` reads, per dump:
  - the thread list, with each thread's RIP mapped to module + offset;
  - the exception, for crash dumps;
  - "likely callers": a scan of each thread's captured stack for return addresses inside wfc_rebuild.exe;
  - the watchdog lines of the wfc.log beside the dump.
- `tools/fidelity/minidump_classes.py` groups the results into classes.

**Symbols.** Frames get names only when the exe the dump names still has its own `wfc_rebuild.map` (lld map, matched by PE
timestamp); 209 exe copies were indexed. Most dumps come from exes that were rebuilt or deleted since, so they give offsets only.
Even named frames are likely callers, not an exact unwind.

**The deciding facts are the watchdog's render-thread phase and whether the run recovered.**

- The watchdog (src/render/gl/RenderWatchdog.cpp) writes a dump whenever no frame completes for 5 s (kStallSeconds = 5). It
  writes at most 2 dumps per process. Since d1131171 / 2026-10-13 it ends the process only on evidence of GPU loss.
- So most "hang" dumps are long stalls that recovered, not hangs.

**Recurrence check on af4d0731.** 142 windowed runs on 2026-10-17 (110 gate runs and 32 leak arm B runs) produced 1 dump: class F.
No crash dump from any lane is dated after 2026-10-13.

## Classes

| class | dumps | dates | phase / exception | main thread | outcome | status on af4d0731 |
|---|---|---|---|---|---|---|
| A. load stall after the frontend | 66 | 10-07 to 10-16 (mostly Rendering's 10-14 / 10-16 RT runs) | 'presented outside the renderer (movie / frontend)' | wfc_rebuild.exe (34), ntdll waits (23), win32u (6), amdxc64 (2), KERNELBASE (1) | recovered: the run continued (e.g. x3_a: watchdog at 106 s, log ran to 13,575 lines) | **present.** See A below |
| B. deliberate hang tests | 9 | 10-13 | 'WFC_SIMHANG blocked' (8), 'WFC_HANGTEST stall' (1) | ntdll wait | by design: exits (5) / continues (4) | test switches, not bugs |
| C. GL driver stall in beginFrame | 3 + 2 unlabelled | 10-12 / 10-13 | 'beginFrame' | atio6axx.dll (AMD GL driver) | recovered or killed | **environmental.** See C below |
| D. D3D12 driver work at the first map frame | 6 | 10-12 to 10-16 | 'endFrame: map presentation' | amdxc64.dll (AMD D3D12 / shader compiler) in 3; one named: `render::rtBridgeAddHitData <- wfc::Pipeline::rtBuildHit` | recovered | **present, benign.** See D below |
| E. mesh upload stall at load | 4 | 10-06 / 10-08 | 'uploadMesh' | win32u / ntdll | recovered | **closed in practice.** See E below |
| F. D3D12 presenter exit hang | 1 | 10-17 (arm B run 32) | 'after endFrame (buffer swap / game update)', fatal | win32u.dll in `render::teardownPresenter() <- shutdownD3D12Present() <- GLRenderer::~GLRenderer() <- Application::shutdown()` | unkillable kernel wait; reboot | **live on af4d0731** with WFC_D3D12PRESENT / FG. See F below |
| G. post-frame stalls in soaks | 2 | 10-13 | 'after endFrame' | ntdll wait | recovered | **no recurrence.** See G below |
| H. 10-06 root and early dumps, no log line | 13 | 10-06 to 10-13 | (no watchdog line beside them: logs overwritten by later runs) | wfc_rebuild.exe (7), atio6axx (2), ntdll (2), win32u (1), WindowsCodecs (1) | unknown | **closed by age.** See H below |

### A. Load stall after the frontend (still present)

- **What it is.** A frontend boot loads the match (map, RT scene / BLAS build, HD textures), and the watchdog sees more than 5 s
  without a completed frame.
- **Why the gate never sees it.** Direct-boot gate runs never hit this: the watchdog arms only after the first frame. That is why
  none of the 142 af4d0731 runs dumped.
- **Where it matters.** The user's path, a frontend boot, still has the stall.
- **Suggested polish item (package 7):** measure the frontend -> match load time on af4d0731 (Off / Realistic / Realistic + HD 2).
  Either cut the stall, for example by spreading the BLAS / hit-data builds or prewarming, or have the watchdog skip dumps inside
  a known load phase. Today every long Realistic load writes a dump next to the exe.

### C. GL driver stall in beginFrame (environmental)

- **When.** These dumps come from the 10-12 / 10-13 multi-exe GPU overload: up to 5 GL exes plus a DML job, with AMD TDRs (memory
  gpu-overload-tdr-hang).
- **On af4d0731.** Not reproduced with one windowed exe at a time. It is still possible under GPU overload or a TDR.

### D. D3D12 driver work at the first map frame (present, benign)

- **What it is.** The first Realistic map frame builds pipelines and hit data. In one dump the driver's shader compiler is under
  rtBuildHit.
- **Same class as the long stall Systems measured.** "~310 s building RT reflection hit data" (the 2026-10-13 review) is the same
  class.
- **On af4d0731.** Present. Not a hang; it belongs with A's load-time item.

### E. Mesh upload stall at load (closed in practice)

- **When.** Old exes, before the mesh / HD loader changes.
- **On af4d0731.** No 'uploadMesh' stall since 10-08.

### F. D3D12 presenter exit hang (live on af4d0731)

- **When it appears.** 1 in 32 runs with WFC_D3D12PRESENT / FG.
- **The fix.** Rendering's 9015c4e9 (one persistent event) + 8736608c (the fence fix) are in int_cand15.
- **Verification.** presenter-verify.sh: 64 runs, FG, resize and map switch, 0 hangs needed. Queued for the card.
- **Note.** The presenter is off by default (GL present), so default play doesn't reach it.

### G. Post-frame stalls in soaks (no recurrence)

- **Runs.** Soak runs on 51260f5 and Rendering's r1_r_off.
- **Status.** Not seen since.

### H. 10-06 root and early dumps, no log line (closed by age)

- **What they are.** The four at the Rebuild root plus gptests / m6b / m9 / m5_simlost_w / pass23. Rendering read the root ones
  as "main thread running in game code" on an older exe, and their own maps place the main thread in game code too.
- **Status.** These exes predate the watchdog's phase strings and every fix since 10-06. There have been no matching stalls on
  current heads.

## Crashes (19)

| class | dumps | dates | exception | status on af4d0731 |
|---|---|---|---|---|
| FSR 3 bring-up | 6 + 1 + 1 | 10-12 (Rendering f_fsr3 / f_fsr3d / f_v4-6, combo_fgfsr3, soak_fg) | 0xc0000005 in D3D12Core.dll+0x12accc (6); in amd_fidelityfx_framegeneration_dx12.dll (1); C++ exception 0xe06d7363 (1) | Rendering's FSR 3 / FG integration runs of one afternoon, before the M5 presenter work. No FSR / FG crash since. **Re-check in the presenter verification's FG run.** |
| VehicleFx::draw null mesh | 1 | 10-13 (Systems verify_dlss_laptop rt_on) | 0xc0000005 in `game::VehicleFx::draw(render::IRenderer&) const+0x1ad` (named) | **closed by b1363fca** (2026-10-13: "skip mesh emitters when load() never ran (graybox fallback, no map): indexed an empty meshes_ and read address 0") |
| Unnamed game-code AVs | 9 | 10-07 Rendering crashtest (deliberate); 10-08 Experimental crash60 / fe64 / final-head-realplay (2 x +0x4a8e00, 2 x +0x2a10fa, 1 x +0x278aba); 10-10 Experimental permap 510 (2 x +0x3db619); 10-13 Gameplay pass23 | 0xc0000005 in wfc_rebuild.exe, offsets only (exes rebuilt) | The 10-08 / 10-10 ones are the crash60 / per-map investigations of those days, on exes before the M6 / M7 merges. The repeat pairs show they were deterministic on their builds. **No game-code crash on any lane since 10-13.** |

## Bottom line for package 7

- **Live:**
  - F, the presenter exit hang (fix in verification);
  - A / D, long first-frame / load stalls on frontend boots in Realistic. These are not hangs, but they are user-visible as a
    multi-second freeze and leave a dump next to the exe each time.
- **Everything else** is a deliberate test (B), environmental GPU overload (C), or closed by age or a named fix (E, G, H, the
  crashes).
- **Proposed bug item:** "frontend -> match load stall in Realistic" (A / D). Measure it on af4d0731 in a gap with one frontend
  boot per mode, then decide whether to spread the work or teach the watchdog the load phase.

Raw output: work/dumps/all.txt, all.json, classes3.txt (Experimental worktree).
