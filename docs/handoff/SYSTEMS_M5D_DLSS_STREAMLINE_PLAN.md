# M5 D prep: DLSS SR + DLSS frame generation via NVIDIA Streamline (design + licence + laptop test plan; docs only)

Systems, 2026-10-12, against 09c 402644e. M5 order (user): AMD first (FSR 3.1 SR shipped as an option, FSR 3 FG in
Rendering's hands), then NVIDIA. NVIDIA checks run on the user's RTX 5090 laptop only. Nothing here is code.

## 1. Licences: what we may ship (read 2026-10-12; not legal advice - the user decides)

**Streamline framework** (sl.interposer.dll, sl.common.dll, sl.dlss.dll, sl.dlss_g.dll, sl.reflex.dll, headers): the repo's
license.txt is an MIT-style grant ("Permission is hereby granted, free of charge ... to use, copy, modify, merge, publish,
distribute ...", copyright notice to be kept). Exception: sl_nvperf.h / sl_nvperf.dll fall under the Nsight Perf SDK
licence; we would not ship them.

**DLSS models** (nvngx_dlss.dll for DLSS SR, nvngx_dlssg.dll for DLSS-G; NVIDIA RTX SDKs License, DLSS repo LICENSE.txt):
- May ship: "Distribute any software and materials within the SDK ... as incorporated in object code format into a software
  application" (1.c) - the DLLs, unmodified, inside the game package.
- Conditions: "An application must have material additional functionality, beyond the included portions of the SDK" (2.a;
  a game is); distribution "subject to the terms at least as protective as the terms of this license" (2.c; our package
  README must carry NVIDIA's terms / notice for those DLLs); not "as a stand-alone product" (4.b).
- Restrictions: no reverse engineering / decompiling, no removal of notices (4.a); "You may not use the SDK in any manner that
  would cause it to become subject to an open source software license" (4.e: keep the DLLs out of any copyleft-licensed
  part; our vgmstream / FFmpeg DLLs are separate files, LGPL, dynamically loaded, so no conflict, but the package licence
  notes must not imply the DLSS DLLs are open source); DLSS is "licensed ... only for ... use in systems with NVIDIA GPUs"
  (Supplement 1: the DLSS rows exist only when an NVIDIA RTX GPU is detected).
- Attribution: "For applications that incorporate the DLSS SDK or NGX SDK ... you must attribute the use of the applicable
  SDK and include the NVIDIA Marks on splash screens, in the about box of the application (if present), and in credits"
  (Supplement 7.1(b) / (c)). Our frontend has an Extras / credits path and a boot splash sequence: an "NVIDIA DLSS" mark on
  the credits screen and in the boot splash when DLSS is enabled is required; no other use of NVIDIA marks (7.2(c)).
- Notify: "You are required to notify NVIDIA prior to commercial release of an application ... that incorporates ... the
  DLSS SDK" (Supplement 4). The playtest builds are private and non-commercial; if the project is ever released publicly,
  the user must re-read this (and the much larger question for a fan remake is the original game's assets, not DLSS).
- Termination is automatic on breach (12).
- Fan / free projects are not carved out: the terms apply as written.

Verdict for M5 D: shipping the unmodified Streamline + nvngx DLLs in the private playtest package is within these terms if
we (1) include NVIDIA's licence / notice text for those DLLs in the package, (2) add the NVIDIA DLSS attribution to the
credits and the splash, (3) offer DLSS only on NVIDIA RTX GPUs, (4) do not modify / reverse-engineer them. Open: the user
confirms (2)'s placement and that the project stays non-commercial.

## 2. Integration design (Streamline on our D3D12 path)

What exists: D3D12Presenter (flip-model swapchain over the GL renderer; worker-thread present with glClientWaitSync),
D3D12Upscaler (FSR 3.1 SR: GL render-size colour RGBA16F (pre-post HDR), depth R32F, velocity RG16F, reactive R8 shared with
D3D12; display-size output RGBA16F; upscale before post), Halton(2,3) jitter with jitter-free motion vectors (WfcMotion),
camera-cut hooks (Rendering), the HUD composited in GL at the end (GfxRendererGL), the Agility SDK selection.

- Device / swapchain: Streamline must see the D3D12 device and the DXGI swapchain it hooks. Load sl.interposer.dll at start-up
  (only when an NVIDIA RTX GPU is present and a DLSS row is selected; never otherwise - the faithful path stays untouched),
  slInit with the features (kFeatureDLSS, kFeatureDLSS_G, kFeatureReflex, kFeaturePCL), then create the presenter's
  D3D12 device / swapchain through the interposer's proxies (or slUpgradeInterface on the native objects). DLSS-G replaces
  presentation inside the hooked swapchain, as FSR 3 FG's frame-interpolation swapchain does on AMD.
- Resources (slSetTag), shared with FSR 3.1's inputs - no new GL work:
  - kBufferTypeScalingInputColor = render-size HDR colour, kBufferTypeDepth = depth, kBufferTypeMotionVectors = velocity
    (mvecScale converts our UV-units prevUV - curUV to what DLSS expects), kBufferTypeScalingOutputColor = display-size
    output (then post at display size, as FSR 3.1);
  - for DLSS-G: kBufferTypeHUDLessColor = the final frame before the HUD (the GfxRendererGL backdrop copy already is that
    image: begin() copies the frame before drawing the HUD), kBufferTypeUIColorAndAlpha = the HUD layer (M5 item 6, the HUD
    split Systems + Rendering own; until it exists, DLSS-G can run with HUD-less colour only and the HUD gets interpolated).
- Constants (slSetConstants per frame): camera view / projection (unjittered + jitter offset in pixels), motion-vector scale,
  depth inverted / near / far, reset on Rendering's camera cuts, frame index.
- Modes: DLSS SR Quality / Balanced / Performance / Ultra Performance and DLAA; DLSS-G 2x and, on RTX 50, multi frame
  generation 3x / 4x (numFramesToGenerate 2 / 3).
- Reflex: DLSS-G requires it. slReflexSleep at the top of the frame replaces our limiter wait when Reflex is on; PCL markers
  (simulation start / end, render submit start / end, present start / end) around the existing frame phases. Frame cap with
  FG: the M5 item 10 pacing (renderer reports frameGenerationFactor() = 2 / 3 / 4; the limiter runs at target / factor) -
  with Reflex on, the cap goes to slReflexSetOptions' frameLimitUs instead of our limiter.
- Settings rows (Frontend + Systems): Upscaling gains DLSS Q / B / P / UP / DLAA only when slIsFeatureSupported(kFeatureDLSS)
  says so; Frame generation: Off / On (2x) / 3x / 4x - backend DLSS-G on supported NVIDIA, FSR 3 FG elsewhere; Latency:
  Reflex (NVIDIA) / Anti-Lag 2 (AMD) when available. Auto-detect never picks DLSS / DLSS-G / FG (as with FSR 3.1:
  explicit user choice).
- Order of work: (1) sl init + device / swapchain proxies with no feature on (byte-identical frames, D3D12 present path);
  (2) DLSS SR over the FSR 3.1 inputs; (3) Reflex + PCL markers; (4) DLSS-G 2x with HUD-less colour; (5) the UI layer from
  item 6; (6) MFG 3x / 4x on RTX 50.

Hybrid-laptop prerequisite (found while writing this): the exe exports neither NvOptimusEnablement nor
AmdPowerXpressRequestHighPerformance, so on an Optimus / hybrid laptop the GL context may be created on the integrated GPU
unless Windows' per-app graphics setting forces the NVIDIA GPU. GL and D3D12 must also be on the same adapter for the
interop (match the D3D12 adapter's LUID to GL's: WGL / GL_EXT_memory_object's device UUID / LUID query). Proposal: export
`NvOptimusEnablement = 1` and `AmdPowerXpressRequestHighPerformance = 1` from the exe (two globals; no effect on desktops
with one GPU), and pick the D3D12 adapter by GL's LUID instead of the default adapter.

## 3. Test plan: the user's RTX 5090 laptop

We cannot reach the laptop, so the plan is a package plus a one-click script the user runs; results come back as a zip.
Plugged in, performance power mode, display at native resolution / refresh, VRR on if the panel has it, Windows' hardware-
accelerated GPU scheduling ON (DLSS-G requires it).

0. Setup check (script, no user input): log the GPU the GL context runs on, the D3D12 adapter, their LUID match, driver
   version, HAGS state, slIsFeatureSupported for DLSS / DLSS-G / Reflex, panel refresh / VRR. Stop with a clear message if
   GL is on the integrated GPU.
1. Faithful baseline: native, no upscaler, no FG; Gorge + Rust 64p real play, 120 s each; present-trace intervals + fps.
2. DLSS SR: each mode (DLAA, Q, B, P, UP) on Gorge: fps + screenshots at fixed seeded positions (the user compares against
   native and FSR 3.1 on the same laptop; side-by-side sheet generated by the script).
3. Reflex: on vs off at DLSS Q: PCL latency markers (render latency, PC latency) logged.
4. DLSS-G 2x, then MFG 3x / 4x: present intervals (pacing: no bunching, frame-time variance), displayed vs rendered fps,
   the M5 item 10 cap check (Frame Rate Limit 120 -> rendered 60 / 40 / 30), HUD clean on generated frames (screenshots
   of generated frames with the kill feed / scoreboard).
5. Stability: alt-tab, fullscreen <-> windowed, resolution change, map travel x3, 20 min soak with DLSS-G 4x: no device
   removed, no interop error, no crash; the log scanned for [error].
6. Return: one zip (logs, traces, screenshots, the setup report) to put on F: for us to read.
Pass / fail per step is written by the script; anything the user must judge (image quality, feel) is a short questionnaire
at the end.

## 4. Sources

- Streamline (licence: license.txt, MIT-style + Nsight Perf exception): https://github.com/NVIDIA-RTX/Streamline
- NVIDIA RTX SDKs License (DLSS SDK LICENSE.txt, quoted sections 1.c, 2.a-c, 4.a/b/e, 12; Supplement 1, 4, 7):
  https://github.com/NVIDIA/DLSS and https://developer.nvidia.com/downloads/nvidia-rtx-sdks-license-23jan2023pdf
- Streamline DLSS-G programming guide (HUD-less colour, UI colour / alpha, Reflex): https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS_G.md

## 5. User decisions (2026-10-12, relayed by Integration)

- Licensing status: "Yes. Treat this project as non-commercial and not intended to be sold. If that ever changes, we will
  revisit licensing requirements before any commercial release." (So the DLSS SDK's notify-before-commercial-release clause
  does not apply now; any commercial plan reopens section 1.)
- DLSS attribution: "Use all three where applicable: startup splash; About screen, if we have one; credits. Keep it clean
  and unobtrusive."

## 6. Credit spec for Frontend (hand over when M5 D starts; only in builds that ship the DLSS DLLs)

- Where: (1) the startup splash sequence - one short, static line or small mark ("NVIDIA DLSS" / NVIDIA's DLSS badge per
  NVIDIA's usage guidelines) on the existing PC-adaptation splash step, never a new full-screen card or a delay; (2) the About
  screen, if the PC build has one (else skip); (3) the credits - a line in the third-party technology block ("NVIDIA DLSS,
  NVIDIA Streamline").
- When: only in packages that contain nvngx_dlss.dll / nvngx_dlssg.dll (a build / package flag, not a runtime GPU check:
  the credit follows what is shipped, not whether the player's GPU uses it).
- Style: the game's existing credit font / size / colour, no animation, no extra sound; the original game's own logos and
  credit order untouched (PC-adaptation additions after the original credits).
- Text and logo: NVIDIA's current trademark / logo guidelines for DLSS (fetched when D starts); no other NVIDIA marks.
- Notices: NVIDIA's licence / notice text for the shipped DLLs goes in the package's licenses folder beside the vgmstream
  ones (bin/licenses/nvidia/), referenced from README.txt.
- D itself waits for the user's RTX 5090 laptop session (section 3).

## 7. Built (2026-10-12, HELD until the laptop test: docs/handoff/SYSTEMS_M5D_DLSS.patch on 09c de6783e)

Compile-tested and absence-tested on the AMD desktop only; nothing NVIDIA has run yet.

- **SDK**: tools/render/fetch_streamline.py pins Streamline v2.14.1. It is fetched, never committed (third_party/streamline/ is git-ignored).
  - Kept: include/ (without sl_nvperf.h); the 8 production DLLs (sl.interposer / common / dlss / dlss_g / reflex / pcl, nvngx_dlss 59 MB, nvngx_dlssg 7.5 MB); licenses/ (Streamline license.txt + 3rd-party-licenses.md, nvngx_dlss.license.txt, reflex.license.txt).
  - CMake builds DLSS only when the headers are present (WFC_STREAMLINE_SDK), links wintrust / crypt32 (the interposer's signature check) and copies the DLLs beside the exe. Without the SDK every call is an "unavailable" stub.
- **Core (render/d3d12/D3D12Streamline.{h,cpp})**
  - A one-time probe reads GL's adapter vendor (d3d12AdapterForGL; else the default adapter). On non-NVIDIA it stops there: no DLL loaded, one info line "DLSS: unavailable (not an NVIDIA GPU (...))".
  - On NVIDIA: verify sl.interposer.dll's NVIDIA signature (sl_security.h), LoadLibrary + GetProcAddress (no import library), then slInit.
    - Settings: no OTA, no downloaded plugins, plugins from the exe directory, frame-based tagging, DLSS / DLSS-G / Reflex / PCL.
    - slIsFeatureSupported runs on GL's LUID.
  - One Streamline D3D12 device on GL's adapter (slSetD3DDevice), plus frame tokens, Reflex options / sleep / PCL markers, and camera constants.
  - The GL matrices are converted to Streamline's convention: y flip and z to [0, 1] for the top-down FG inputs.
  - Latency log: "Reflex: PC latency (simulation start -> GPU render end) X ms avg", from the driver's reports, every 300 frames.
  - WFC_NODLSS=1 never loads anything. slShutdown runs at exit.
- **DLSS SR (render/d3d12/D3D12Dlss.{h,cpp})**
  - Same interface as FSR 3.1 (Fsr3Targets / Fsr3Params) and the same GL copies into shared textures. Pipeline::fsr3Run picks DLSS when a DLSS mode is set.
  - Fallback: DLSS -> FSR 3.1 -> FSR 1, at the same scale.
  - Upscaling modes 7-11 = DLSS Quality / Balanced / Performance / Ultra Performance / DLAA (ratios 0.667 / 0.58 / 0.5 / 0.333 / 1.0; NVIDIA's optimal size logged).
  - Inputs: HDR colour + auto exposure; motion-vector scale converted to normalised units; jitter as FSR 3.1's.
- **DLSS-G (D3D12Presenter.cpp, Rendering's file)**: FG on + an NVIDIA GPU with DLSS-G + Reflex means the presenter runs on Streamline's device with an upgraded DXGI factory, so the swapchain is Streamline's proxy. WFC_FG_BACKEND=fsr keeps FSR 3 FG.
  - Reflex low latency is on.
  - Per frame it reuses FSR 3 FG's three inputs (HUD-less RGBA8, depth R32F, motion RG16F): options when they change (off in menus), camera constants from new FgParams matrices (unjittered view-projection, view, previous view-projection), tags (state COMMON, valid until present), PCL present markers.
  - Factor 2x / 3x / 4x: setFrameGenerationFactorAtStartup / WFC_DLSSG_FACTOR. framegenFactor() feeds the M5 item 10 limiter (cap / factor).
  - The worker-thread present is not used with DLSS-G. slDLSSGGetState's status is logged when it fails.
- **Main loops (Application)**: Reflex sleep + simulation / render-submit markers. These are no-ops unless Streamline runs Reflex or DLSS-G.
- **Menus**
  - Profile Upscaling now goes up to 11.
  - HardwareFacts.dlss / FrontendRuntime::dlssAvailable(): the Upscaling row lists the DLSS entries only when DLSS is available (else only a stored choice, so the row shows what is set). Auto-detect never picks DLSS.
  - The Frame generation row: frameGenerationAvailable() is true with FSR 3 FG or DLSS-G.
  - Log line "graphics: DLSS yes / no". (The optional-runtimes line is unchanged, for verify_slim.)
- **Package**: build_slim.py --m5 --dlss ships the 8 DLLs, licenses/nvidia/, README notes (NVIDIA trademarks; loaded only on NVIDIA GPUs), and DLSS_Laptop_Test.bat + dlss_laptop_test.ps1 in the package root.
- **Laptop test (tools/package/dlss_laptop_test.ps1, one click, about 45 min)** implements section 3:
  - Step 0: setup facts, plus a start-up that stops with the reason if GL is not on the NVIDIA GPU or DLSS is unavailable.
  - Step 1: native Gorge + Rust, 64p.
  - Step 2: DLAA / Q / B / P / UP / FSR 3.1 fps, plus lockstep screenshots at match steps 600-2400 into image_sheet.html.
  - Step 3: Reflex off / on latency.
  - Step 4: DLSS-G 2x / 3x / 4x (presented / rendered), plus the 120 cap check (60 / 40 / 30).
  - Step 5: 20 min soak, then a manual alt-tab / fullscreen / mode check.
  - Step 6: questions, then the zip. PASS / FAIL per step goes to report.txt.
- **Absence proof (RX 7900 XTX, 09c de6783e + the patch, 1080p window, Streets 5v5)**:
  - Default settings: "DLSS: unavailable (not an NVIDIA GPU (AMD Radeon RX 7900 XTX, vendor 0x1002))", "graphics: DLSS no", 0 [error]; NVIDIA / Streamline modules loaded: none (process module list at 46 s).
  - A stored DLSS choice (WFC_UPSCALING=7): "renderer: upscaling mode 7 (DLSS Quality)" -> "DLSS: unavailable (see above): FSR 3.1 instead" -> FSR 1 at 0.67 (this tree has no FFX SDK); 0 [error].
- **To check on the laptop first** (assumptions that cannot be verified here):
  - The jitter sign and the motion-vector scale for DLSS SR (DLSS's debug overlay / ghosting).
  - The FG camera matrices: prevViewProj_ timing within the frame; DLSS-G status bit 8 = constants invalid.
  - That slSetTag with state COMMON is accepted for the simultaneous-access shared textures.
  - Whether Streamline needs the D3D12 device created through the interposer (we use slSetD3DDevice on a native device).
  - Reflex markers in the frontend loop (one span per frame).
- **Not done (later)**: the HUD as a separate UI layer for DLSS-G (M5 item 6; until then the HUD is interpolated with the frame); a Reflex row; a 3x / 4x choice in the FG row (the factor is start-up / env only); the credits / splash attribution (Frontend, §6).

### 7a. Review fixes (Frontend, Experimental, Rendering; same patch, re-committed)

**Frontend**
- tests profile.upscaling_range (5 -> 5, 14 -> 11); the frontend tests pass 133 / 133.
- The "DLSS needs NVIDIA RTX: FSR is used at the same scale." hint (a stored DLSS choice without DLSS) and the "DLSS: best image on RTX GPUs. FSR 1 is nearly free." hint.
- fsr3Missing is limited to modes 4-6.
- New IRenderer::frameGenerationMaxFactor() (1 / 2 / 3 / 4, from NVIDIA's reported maximum) for the 2x / 3x / 4x row; render::setFrameGenerationFactorAtStartup(n) sets the factor.

**Experimental**
1. With DLSS-G, fgRecord (FFX) no longer runs, so dlssgRecord is the only consumer of fgHave_ (no null ffx_.Configure).
2. dlssMode_ / dlssFailed_ survive Pipeline::release(). The test requires the DLSS line after "FLOW level.begin level=Match" and no FSR line there.
3. The DLSS-G factor is clamped to numFramesToGenerateMax (at init, and again once DLSS-G runs); factor() reports the frames actually set.
4. The manual run sets WFC_AUTODETECT=0.
5. The Reflex-only step is dropped: GL presents there, so it measures nothing. Reflex latency is information-only under DLSS-G; no latency claim is made.
6. The per-frame token / sleep / markers run only while Reflex runs (DLSS-G or WFC_REFLEX). DLSS SR makes its own token per dispatch.
7. The FSR 1 fallback sharpness is 0.2. DLSS falls back to FSR 3.1 after 30 failed evaluations in a row, or at once when its device is lost.
8. DLSS-G is turned off and its resources freed (slFreeResources) before ResizeBuffers and before teardown. There is one token per present: loading yields get fresh tokens.
9. The test resolution is the primary display's current mode (EnumDisplaySettings, physical pixels: no mode switch). Error counts are compared against the native baseline.

On the jitter sign, the SL 2.14 guides specify only "pixel space" (and no jitter in the matrices), so the sign stays FSR 3.1's. DLSS's debug overlay on the laptop is the check.

**Rendering**: prevViewProj_ was confirmed to hold last frame's matrix at framegenCapture.
- Device-loss recovery: a presenter that ran DLSS-G and lost its device sets a process flag, and the recovery uses FSR 3 FG on a fresh device.
- streamlineDevice() never hands out a removed device; DLSS SR then fails over to FSR 3.1.
- Laptop test step 4b: WFC_D3D12_SIMLOST=2000 with DLSS-G on must log "recovered with a new device", with frames continuing.

**AMD rerun** (stored DLSS Quality, Streets 5v5): no NVIDIA / Streamline module at 46 s and 91 s; the DLSS choice kept through the match load (FSR 1 at 0.67, sharpness 0.20); 0 [error].

### 7b. Experimental's re-review residuals (same patch)

- **R1**: the DLSS-G factor follows NVIDIA's maximum in both directions once it is known. An unknown maximum clamps the first options to 1 frame; the asked factor is kept (dlssgWanted_) and re-applied after the maximum is learnt.
- **R2**: after a DLSS-G device loss Reflex stops (streamlineReflexLost): no per-frame token / sleep / markers on the dead device.
- **R3**: frameGenerationMaxFactor() creates no device. It is exact once a DLSS-G session has asked NVIDIA; before that, 4 if GL's adapter is an "RTX 50", else 2.
- **R4**: DLSS-G is off across our display-mode switches via a new platform hook. platform::setDisplayModeChangeHook / notifyDisplayModeChange (PresentHook.h) is called by Win32Window before the CDS_FULLSCREEN switch and before the desktop-mode restore. DLSS-G is also off while the window is minimised. It turns itself back on at the next frame.
- **R5**: the token's presented flag is set by the presenter after Present returns, not inside the PCL marker.
- **R6**: two constants sets per token avoided. DLSS SR now runs on its own viewport (1), since its inputs keep GL's row order with different jitter / motion signs; DLSS-G uses viewport 0. Step 4 reports a count of Streamline warning lines.
- Note for Integration: SYSTEMS_DISPLAYMODE_REVIEW.patch also touches Win32Window.cpp near the same lines (the restore); the two hunks are separate lines and should apply in either order with offsets.

