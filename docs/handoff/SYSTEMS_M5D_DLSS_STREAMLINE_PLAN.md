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
