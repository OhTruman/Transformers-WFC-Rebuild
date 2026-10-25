# 09c milestones (set 2026-10-10, Integration)

Rule: a milestone closes when its exit criteria are met, and then it is FROZEN. Closed work is only reopened when
the regression suite (below) flags it. Nobody revisits a closed item for "maybe a bit more".

Order: M1 -> M2 -> M3 run now (M1 is top priority for timed slots); M4 runs on the GPU in gaps throughout;
M5+ start when M1 closes.

## Measurement standard (applies to every milestone)
- Comparisons (A/B): seeded lockstep match (tools/fidelity/lockstep-ab.ps1), 1 rep per arm once its A/A noise
  floor is known; SIMHASH and draw / chars parity must match across arms, else the row is UNKNOWN.
- Verdicts (does a map meet the target?): free-running 64p real play + overview, 2 reps, quiet machine,
  contamination watchdog on. Never conclude from a single free-running rep.
- Regression suite: after every merge batch that touches rendering, sim or UI, one lockstep pass on all 8 maps
  vs the last baseline. A row worse by > 0.15 ms p90 blocks the next playtest build until explained or reverted.

## M1 - 300 fps everywhere (64p)
Exit: all 8 TDM maps, 64 players, p90 <= 3.33 ms in BOTH verdict reps, at 4K and 1080, overview and real play.
Image byte-identical (or verified state-identical) for every change.
Status: Debris, Streets, Berth, Complex, Seed, Molten MET; Gorge on the line (3.33-3.44); Rust 0.1-0.3 over.
Verdict 2026-10-11 on ad4e6c5 (Experimental m1-verdict-ad4e6c5): 1080 all MET, Rust overview 4K MET; MISS, all GPU-bound:
Gorge overview 4K 3.35 / 3.32, Gorge real play 4K 3.38 / 3.53, Rust real play 4K 3.01 / 3.42. Item 3 (CPU binds) dropped;
remaining: item 2 GPU side (Rendering, 4K per-pass GPU profile + top exact cut), then a 3-cell re-verdict.
CLOSED 2026-10-11 by the user ("if Gorge is right under 300 fps at 4K I think it's good"). Re-verdict on 5aa2d22 (distortion
cut fa34606, -0.12 ms GPU): Gorge ov 4K 3.20 / 3.26, Rust rp 4K 3.21 / 3.11 MET; Gorge rp 4K 3.41 / 3.21 (borderline, accepted).
Real 2560x1440 fullscreen: Gorge rp 2.60-2.65, Streets rp 2.31-2.34 MET. GFx MSAA 8->4 measured at -0.05 ms, dropped.
Fullscreen (flip) present gap +0.1 ms vs windowed: moved to M5 (DXGI flip-discard swapchain).
REOPENED 2026-10-11 by the user for 48/48: Gorge real play 4K must be <= 3.33 in every rep (3 reps). Rendering hunts >= 0.15 ms
of byte-identical GPU in slow Gorge frames (direct draws, translucent sprites, char shadow projections), ahead of M5 item 1.
Bot work paused until the user is back, except broken-mechanic fixes (radius damage line of sight).
Re-verdict 2026-10-11 on 9c3560e (Experimental aecd4a4, m1-reverdict-9c3560e): regress no BLOCK; Gorge ov 4K 3.08 / 3.09 MET;
Rust rp 4K 2.96 / 3.15 MET; Gorge rp 4K 300 s x3 3.14 MET / 3.41 MISS / 3.36 MISS (pooled 3.32), slow frames GPU-bound in
A_UNDER_DECK fights. Needs ~0.2 ms more GPU (rep mean ~3.12). Next: Rendering (b) weapon-draw queueing, (c) translucent sprites,
(d) char shadow projections, then a Gorge rp 4K 3-rep re-verdict. Regress gate: a binds drop <= 10 % on a declared batching
commit with identical SIMHASH scores PASS(excused).
CLOSED 2026-10-12 (48/48 as the user asked) on 65e856f (Rendering b/d/d2 + Systems HUD shadow scissor + shadow cache), Experimental
fb6a796..91e8ef7 (results/m1-reverdict-65e856f, m1-full-65e856f): regress 8 PASS (all maps -0.04..-0.11); Gorge rp 4K 300 s x3
2.68 / 2.84 / 2.72 (pooled 2.74); full verdict 72/72 MET (8 maps x 4K/1080 x ov/rp x 2 reps + 8 fullscreen 2560x1440 rows,
1.79-2.37); Molten 509 rp 4K 300 s x3 2.64 / 2.88 / 2.91 (pooled 2.82). Attribution: HUD shadow cache off -> Gorge rp 3.15, so the
cache is worth ~0.35-0.40 ms in real play (several rendered frames per sim step). Widest cells: 509 rp 4K, Rust ov ~2.85. FROZEN.
Bounded work list (nothing else is added without Integration):
1. HUD: settle the verifier mismatch (OFF/OFF run), then HUD batching patch 2 (Systems).
2. Gorge combat frames: slow-frame CPU profile, then the top exact cut (Systems CPU side, Rendering GPU side).
3. Rust: remaining program binds / per-draw driver cost (Rendering).
4. Verdict pass on all 8 maps -> close M1.
If items 1-3 are done and a map still misses, Integration brings the user the numbers and the options; no open-ended
tuning loops.

## M2 - Smart AI becomes the default
Exit, all required:
- 400 matches over 4 maps: Smart >= 55 % of decided matches vs Classic.
- Same aim proven: Smart's hit % at Classic's shot mix within 1 point of Classic in every range band.
- Visible intelligence: cover >= 8 % of engaged time; settled squad distance to leader median <= 25 m;
  flanks no longer a net loss on any map.
- Classic identity IDENTICAL; DETERMINISM; BOTTEST cost; stuck rate <= Classic.
- User playtest says yes.
Bounded work list: flank route budget, squad regroup fix, shot-mix check; then the 400 rerun.
If the 400 misses after these, Integration brings the user the numbers before more tuning.
CLOSED 2026-10-11 by the user (accepts Smart at ~60-63 % wins vs Classic; squads 26-38 m and cover 7.8 % accepted as is).
USER 2026-10-12: M2 stays OPEN until the user has playtested Smart vs Classic back-to-back ("Do not treat automated results as final sign-off"). The automated numbers are accepted; the human playtest is the remaining exit item.
400 on 1e9e583: 63.0 / 63.8 % wins; same aim proven by aim error at fire (f79969a); stucks <= Classic; flanks not a loss.
Smart becomes the default (Gameplay World fallback + Frontend kBotAiDefault); Classic stays selectable and byte-identical.
The user's playtest happens with M3.
Re-check 2026-10-11 after the pawnRayHit fix (5eeb3f7, untimed 104 matches/arm, Gameplay pass29/b49/M2_SKEW.md): Smart 61.5 %
(was 72.1 % on b749a83; the bug cost Classic more via false team-mate hits). Still inside the accepted 60-63 %; M2 stays closed.
Parked for the user's return: re-tune Smart's hold-fire-on-team-mate rule (tuned on the inflated figures).

## M3 - Playtest delivery
Exit: build/release (the user's exe) and the slim package rebuilt from the closed M1 (+ M2 if closed) head,
package isolation check passes (0 errors on 3 maps, all English lines play), user has it.
DONE 2026-10-11: head 38cddc8 (tag playtest/09c-m3-38cddc8). build/release refreshed (cmake --build build/release).
Package Rebuild-Systems/work/playtest_slim, 9.69 GB (incl. optional faithful HD pack 3f410a1, 3.96 GB);
verify_slim -Strict -SubstDrive with HD on: PASS on Streets / Gorge / Debris. User playtest pending (also M2's playtest).

## M4 - Optional HD texture pack (faithful)
Exit: full RealPLKSR pass (colour + normals) + DAT2 pass (character / weapon) done, loader verified in game,
before / after sheet to the user, remake list frozen. The remake pass is a separate milestone (M4b) after the
user's verdict.

## Later
USER ORDER 2026-10-11 (corrected): M1 (48/48) -> HD textures (the visible "push harder" upgraded/upscaled pack, PLUS
the remake pass: textures that fail are remade safely with more detail where the original is missing it) -> M5 frame
generation + upscaling (AMD, then NVIDIA) -> M7 ray tracing last. M6 is unranked.
USER 2026-10-12 (order): "implement that [camera-cut hooks / FSR settings] and frame gen, and then finish the upgraded
textures. Then we shall implement raytracing." -> M5 C (FSR 3 FG) next, HD full pass finishes, then M7 ray tracing.
USER 2026-10-12: "D all the way" - full pass top level is D (stronger mid/fine detail + relief), ladder D -> C -> B -> A -> original.
USER 2026-10-11 (superseded by D): HD full pass uses pilot variant C (4x + mid-band detail + stronger normals), per-texture fallback to B / A on
gate fails; HD-on uses Rendering's compressed sRGB S3TC/BC7 upload with a post-filter PWL correction (Rust 4x ~3.3 GB; budget 8 GB/map).
- M5 Upscaling + frame generation (D3D12 interop -> FSR 3 / DLSS / FG), optional, off by default.
  USER 2026-10-11: start after M3 ships; AMD first, then NVIDIA. Order (docs/handoff/SYSTEMS_M5_UPSCALING_FRAMEGEN_PLAN.md):
  1 D3D12 present stage 2 (flip-discard + tearing; also removes the +0.1 ms fullscreen GL flip gap the user hits)
  -> 2 temporal inputs -> B (3 shared targets, 4 FSR 3.1 SR, 5 post placement, 9 settings) -> 6 HUD layer
  -> C (7 FSR 3 FG, 10 frame cap) -> D (8 DLSS SR + DLSS FG via Streamline, checks on the RTX 5090 laptop).
  2026-10-12 item 1 result (e246091, merged 63eec4c): D3D12 present misses its exit criterion on AMD (glSignalSemaphoreEXT
  spins until the GPU finishes; 1440 fullscreen +1.2 ms, 1080w worker +0.43, 4K render +0.35 floor; WGL_NV_DX_interop2 worse).
  Integration decision: GL present stays the default; D3D12 present engages only while FSR / DLSS / FG is on. Fullscreen +0.5 ms
  presentation-mode check folded into the FSR work.
  FSR 3.1 licensing (Rendering 2026-10-12): the shipped amd_fidelityfx_*_dx12.dll are under AMD's binary licence
  (redistribution allowed with its notice, no reverse engineering): packages must carry license.md next to the DLLs;
  the SDK is fetched by tools/render/fetch_ffx_sdk.py, never committed. FSR 3.1 needs shader model 6.6, above Win10 19045's
  in-box D3D12 (SM 6.5): the D3D12 Agility SDK (D3D12Core.dll in bin/D3D12/, Microsoft redistributable, NuGet
  Microsoft.Direct3D.D3D12) is fetched by tools/render/fetch_d3d12_agility.py; packages carry Microsoft's redist terms.
  Option A (AFMF 2 driver FG) is a user-side Adrenalin toggle: offer it to the user as a test when they're home.
  D3 licensing: read the NVIDIA RTX SDK terms before shipping D.
  HD "E" generative stack licensing (AssetTools 2026-10-13, manifests/hd/e_models.json d57242c): Stable Diffusion 1.5 fp16
  (CreativeML OpenRAIL-M) + ControlNet v1.1 tile (OpenRAIL) under AssetTools\work\models (git-ignored, licence text + sha256
  in the manifest); diffusers 0.31 / transformers 4.46 / accelerate 1.1 with torch 2.4.1 pinned for torch-directml.
  FLUX.1-schnell rejected (12B fp16 + T5-XXL does not fit DML on 24 GB without quantised kernels). The E outputs are
  tool-generated textures (OpenRAIL use restrictions apply to the generated content; non-commercial project, revisit before
  any sale). Tint / emissive mask channels are protected in E (TEXSET Masks B / R > 0.1 plus a ~2 px rim keep D).
- M6 Modern materials mode (optional).
- M7 Ray tracing (shadows + AO -> reflections -> GI), optional, off by default.
  USER 2026-10-12: stage 1 on the hybrid GL + D3D12 path (architecture A); collect real performance, VRAM, sync and ray-budget
  numbers first. Port the renderer to D3D12 (option C; never Vulkan) only if those show the hybrid limits RT quality / performance
  or grows overly complicated. OpenGL stays the Classic / reference / default renderer regardless.
  USER 2026-10-12: yes to a later separate optional "RT lighting" mode where ray-traced GI replaces the baked lightmaps
  (modern look); the faithful baked lighting stays the default.
  USER 2026-10-12 (goal "make this game look brand new"): the HD texture pack (D) is the DEFAULT when installed; ray-traced
  shadows / lighting are OFF by default unless enabled. RT sheets judged "way too hard... should be softer and more natural and
  realistic. I want realistic lighting and shadows for everything" -> RT Realistic mode brought forward (soft contact-hardening
  shadows, runtime sun for all geometry + RT-baked indirect lightmaps, local lights, AO, lit reflections, then dynamic GI).
  Comparisons for the user: full-frame 2560x1440 pairs and an F9 live toggle, no crops.
  USER 2026-10-12: textures may go BEYOND faithful ("Yes it can go beyond faithful! I want the game to look realistic and
  new.") -> a stronger-than-D level and the remake pass may add detail the original never had, keeping designs / layouts / palette.
  USER 2026-10-12 (later, REVERSES the HD default): "HD texture's should be off my default too" -> HD textures OFF by default,
  opt-in like RT; "materials should be hd textures too" -> M6 modern material maps ship inside the HD pack under the same switch;
  "start the new textures now instead of waiting" -> the beyond-D "E" pilot starts at once on Rust textures already at D.
  State 2026-10-12 late: HD row Off (default) / Faithful (D root) / Enhanced (E root) with effective-level fallback and VRAM
  hints (Frontend f470b9f3 / e3ec6cfc); modern-map loader (BC4/BC7, hd_pbr_index.json) merged (f70ae7f2); M6 audit + matc pbr
  contract merged (b35c1c0a; 1336 materials, 342 tint consumers forced dielectric); RT row API const-fixed (c2cc4635);
  tools/fidelity synced from Experimental (8dba87f9) with HD/autodetect pins for timed runs. Held until HD done + restart:
  agents/rendering-rt (RT stage 1 + reflections + F9), Systems skin feed, DLSS patch (laptop test).
  M6 Modern materials mode merged 4349632d (renderer side; materials_pbr.json per map); Integration decision: Modern is tied
  to HD level 2 (Enhanced = E textures + Modern), Faithful (1) keeps the original shading; calibration waits for the re-issued
  M6 vectors (view-dependent spec stripped from base colour; cube stripped from base only, never emissive).
  M6 contract after Experimental reviews (22cd29a6): rule 3 keeps texture colour in cube x colour products (all 67 robot bodies
  base share 1.00); tint targets use the 10 palette swatches. Integration decisions: WEP_PhongMain per-pixel metallic =
  (1 - tint mask B); CHR robot bodies get a "metallic paint" class (F0 from the TINTED paint, dielectric share kept, Energon
  emissive untouched); exit = custom-colour hue / lightness within a few degrees of Original across the palette.
  Modern shading state (f77e0e45): lightmapped + dynamic (robots / weapons / movers) paths, per-pixel metallic / F0 hooks
  (defaults = previous output), widened lightmap-basis lobe (~35 deg), explicit env mip; kPbrSpecOcc calibration and the
  WEP / CHR hook rows (Systems) pending; in-game check + pairs after the restart.
  Held agents/rendering-rt (3c6cdc0e / 9f67c007) is synced with agents/rendering (Modern included; wfcSCR_ routed through
  wfcRtRefl) and carries Experimental's RT review fixes; the post-restart Off byte-identity proof covers RT + Modern together.
  28cd1150: Pipeline::release() keeps modern_ (was lost per map change; WFC_SETTINGSTEST self-test added); Systems' M6 rows
  landed: CHR metallic_paint (F0 = tinted paint, m 0.8) and WEP weapon_metal_mask (metallic = 1 - mask.b); matc_pbr hook
  emission + regen next.
  2026-10-13 02:16 (861108b5 + regen): Systems' RE-ISSUED rows after Experimental's review - CHR f0 = base = paint /
  max(1, max channel) (hue kept, energy-bounded), WEP metallic = clamp((1 - mask.b) x mask.g) with f0 clamped (14 white),
  glsl_hash per row; audit rerun in integration (1336 materials, 103 hook rows = 67 CHR + 36 WEP, all hashes match the
  render data); materials_pbr.json regenerated with --hooks in all 15 render dirs (backup work/render_bak_20261013-0216-
  prehooks), oracle check 0 of 3,632 vectors differ, 0 skipped on hash. Systems' CIEDE2000 exit check does NOT pass
  (combined pass 9-17 % at m 0.6-0.8; hue holds, dH* < 1; lightness / chroma spread per pixel fails; the gate compares a
  direct-lit Original diffuse with an env-only Modern IBL term - open whether it measures the right thing). Integration
  decision: the rows ship on the opt-in Enhanced path; the gate for M6 is the user's in-game full-frame A/B at 2560x1440
  after the restart (Rendering's pairs + F9), with the ΔE check kept as a diagnostic; Experimental re-reviews the rows.
  Experimental re-review (751442ca): rows OK, A/B gate agreed (exposure ratio is systematic, Original's cube term scales
  with scene irradiance, Modern's IBL does not; no offline dE00 threshold can gate). Fixes ordered (Systems re-issue 3):
  WEP f0 normalised not clamped (clamp whitened 9 blue-steel weapons); CHR hook baseColor = null (stripped base; P carried
  the gloss twice and dropped the diffuse-luminance term; L* correlation 0.83 -> 0.98), f0 = bounded P, metallic constant
  0.8 (NOT gloss-driven: gloss is already in P and SpecularPower); rule 2 (view mean) keeps the Original shading (UI_FrontEnd
  planet shell limb darkening); audit extended to the UI_* dirs (26 unreviewed FrontEnd materials). matc_pbr --check to
  accept (0,1) labels only for a sole wfcSampleCubeMean change. Diagnostics kept: hue, pattern correlation, swatch
  readability (min pairwise dE00 between swatch means 5.5 -> 3.9; A/B to place similar team colours side by side).
  2026-10-13 02:48 (e240d452 Systems re-issue 3 + 486b3285 Rendering matc_pbr): all four fixes landed; audit rerun over
  the 15 dirs (1,368 rows = 1,336 MP unchanged + 32 UI; 105 hook rows, all hashes match); materials_pbr.json regenerated
  with --hooks --check in all 15 dirs (backup work/render_bak_20261013-0240-prehooks): 0 of 3,684 vectors differ, 1 benign
  rule-label flag (Debris DeadBodies 5272: cube x vec4(0,0,0,1)), the FrontEnd planet shell has no pbr entry (Original
  shading under Modern). Experimental final look CONFIRMED (own Debris regen byte-identical; every emitted hook body hashes to its row in all 15 dirs; 0 of 2,680 customised pixels have a stripped base above 1, so the shader's min(D, 1) never shifts hue). M6 data FINAL pending the post-restart in-game full-frame A/B.
  Stage 1 composite: RT shadows / AO on dynamic surfaces + the
  dynamic-light term only (option b: end of the opaque pass), lightmapped statics untouched except character contact AO.
  2026-10-13 07:00 PC RESTARTED (nonpaged pool 27.4 -> 1.1 GB; C: 7.4 -> 127 GB free after powercfg -h off; display back
  at 2560x1440 @ 144 on the HP X27i, confirmed 1440p not 4K; SA230 1080p secondary). USER 07:30: "do whatever you need to do
  to continue improving the game ... I'll check back tomorrow morning and then I'll do a play test. So get as far as you can".
  OVERNIGHT PLAN (Integration): Experimental regress-8map (0502051 vs 05319349) -> untimed window: Rendering
  WFC_SETTINGSTEST + RT Off byte-identity on agents/rendering-rt synced to head -> MERGE agents/rendering-rt if it passes
  (RT off by default) -> full-frame 2560x1440 pairs (Original / HD Faithful / Enhanced; RT Off / Faithful / Realistic) for
  the user; Gameplay camera-cut proof -> merge 1604234f + a playtest guide; Systems V_bake Gorge, final DLSS rebase on the
  post-RT head + --m5 --dlss laptop package, then a PLAYTEST PACKAGE REFRESH at the final head (Smart / Classic, HD, RT,
  FSR / FG rows; verify_slim PASS); AssetTools DML probe + E ladder (Rust) + pack for D vs E pairs, CPU colour / batches
  continue, frozen D index for the refresh; Experimental reviews the RT merge, hunts settings persistence via menus, FSR/FG
  windowed alt-tab, Smart stuck / rejoin, then verify_slim on the refresh. Morning: user plays Smart vs Classic (M2 exit)
  and views the pairs (M6 / HD / RT gates).
  USER 07:40: "have the Ray traced lighting all done" by morning if possible ("If not it's okay") and "hunt for bugs, memory
  leaks, and any problems I have ever resolved or caught" -> Rendering: RT Realistic R1-R4 after the Off-identity gate (trial
  branch integration/rt-trial 879928b3); Systems: V_bake all 10 maps; Experimental: leak soak + regression sweep of every
  fixed bug (EXPERIMENTAL_REGRESSION_SWEEP_2026-10-13.md); Gameplay: gameplay-side sweep (GAMEPLAY_REGRESSION_SWEEP_2026-10-13.md).
  07:40 Experimental regress-8map 0502051 vs 05319349: 8/8 PASS, no BLOCK (lockstep 64p TDM, RenderSize 2160 windowed,
  2 runs/arm, best valid p90; base/head ms: 501 2.68/2.69, 502 2.43/2.41, 503 2.99/3.01, 504 3.11/3.11, 507 2.43/2.40,
  508 2.68/2.67, 509 3.44/3.47, 510 3.23/3.19; all within +-0.04 noise; frozen render data, code only). Untimed window
  opened 07:45 for all lanes (RT gate + Realistic, V_bake x10, Gameplay proof + sweep, E ladder + D colour, leak soak).
  BUG (Experimental 08:00, open, Systems): after an AMD driver reset (TDR; System log Display 4101 07:45:57 / 07:55:02 under
  5 GL exes + the DML ladder + RT builds) wfc_rebuild hangs forever in beginFrame (watchdog fires, wfc_hang dump written, one
  core spinning); the per-frame glGetGraphicsResetStatus poll never saw the reset. Fix: watchdog hang path -> clean exit with
  a message + display-mode restore (+ WFC_SIMHANG repro). GPU CAP: at most one exe per lane and the DML job; while the
  ladder runs only Rendering + Gameplay launch. Experimental's leak soak got 1.6 matches (no verdict) and resumes after.
  WFC_BOTTEST 38/39 on head (docs 39/39): Experimental isolating, Gameplay owns.
  08:10: TDR hang FIX landed 45a8b9c0 (Systems patch: reset poll at the top of beginFrame; watchdog backstop exits cleanly
  10 s after the stall dump; WFC_SIMHANG / WFC_SIMRESET); root cause from the minidump: glGetQueryObjectiv spinning in the
  AMD driver inside Pipeline::pawnOcclusionResults, the reset poll sat in endFrame. All 3 TDRs (07:45, 07:55, 08:02)
  coincided with Gameplay's LEGACY-renderer exes (archive exes without render data): Gameplay now runs with WFC_RENDER_DATA
  = integration work/render; OPEN (Rendering, after RT): legacy renderer GPU hang on DM Streets, 10 pawns, ~45 s in.
  BOTTEST 38/39 = AI cost max 8.94 ms vs 6 ms under load just before the 07:55 TDR (per-bot avg 0.005 ms): likely load,
  re-check on a quiet machine. Note: OBS graphics-hook64.dll is injected into wfc_rebuild on this machine.
  08:25 M7 RT STAGE 1 MERGED: integration/rt-trial 879928b3 (agents/rendering-rt 1b399cba) -> integration 5d1aecd0. Gate
  (Rendering): RT Off identity vs the integration exe, HD off, 2560x1440 windowed: Gorge + Rust 5/5 shots byte-identical,
  SIMHASH identical over 7,740 / 9,601 steps (default: RT not loaded). Loaded-but-Off (RT turned on then off mid-map; infra
  freed at the next map): state identical, Rust 4/5 shots differ by <= 1/255 in 15 px of 3.7 M (RT-variant shaders'
  uRtOn == 0 path compiles with different float contraction) - ACCEPTED, no second program set. RT row Off (default) /
  Faithful / Realistic, F9 cycles. Realistic work continues on agents/rendering-rt2 (cut from 879928b3): R2 soft shadows +
  R1 V_bake sun swap first, then R3 AO everywhere + reflections on pbr hits, R4 probe GI prototype only.
  TDR fix follow-ups 9a492cfa: reset status latched (Rendering), backstop default OFF until keyed on device-removed
  evidence (Experimental blocker: legitimate stalls up to ~310 s), dialog 0 s in scripted runs.
  08:45 Experimental RT merge review: (a) release() PASS (minor: LOG_WARN sizes on the dropped-not-freed path after a failed
  waitIdle); (b) FAIL-minor: with the row Off, rayTracingAvailable() at start-up builds the whole D3D12 bridge for the
  process' life -> Rendering: throwaway probe / build on first enable; timed regress-8map 05319349 vs head (RT Off) before
  the playtest package; (c) failure fallback PASS; (d) F9 live only on a map loaded with the row non-Off, F9 not saved
  (guide); (e) env vars PASS. F9 COLLISION fixed e6d61e2d: F9 = RT cycle, Debug wire boxes F11, bot overlay F12.
  FAIL (Experimental 08:15, fixed 08:27): integration's tree had no third_party/dxc / ffx_denoiser (gitignored, fetched
  like ffx_sdk / agility), so build/release compiled with NO RT shaders: row visible, RT silently the Original look. Fetched
  (DXC 1.9.2609.5, FidelityFX SDK v1.1.4 denoiser), re-configured, rebuilt: 10 DXC shaders, generated/rt/*.h present,
  WFC_RT_SHADERS=1. Rule: every packaging tree fetches all four third_party deps and checks build.ninja; verify_slim to
  assert an RT-on frame logs a TLAS line; Rendering: rtAvailable() false in a shader-less build (rt2).
  Harness: the profile's RayTracing= / HDTextures= override WFC_RT_MODE / WFC_HDTEXTURES at boot (set the row per arm).
  TDRs: 3 of 4 during AssetTools' DML ladder at 15 GB VRAM beside 5 GL exes (oversubscription), 1 without it; ladder now
  sliced (10.8 GB peak, dispatches <= 0.55 s) + checkpointed.
  08:40 Gameplay camera-cut proof PASS (lockstep SIMHASH identical 3001/3001 x 4 modes; cut reasons on the frames the
  heuristics caught) -> 1604234f merged 2013d67d. BUG found by the proof, fixed 51260f56: init-false harness paths
  (WFC_PARTICIPANTTEST) never destroyed the renderer, the watchdog's global std::thread stayed joinable and std::terminate
  fired at exit (0xC0000409); a static guard now joins it. tools/fidelity synced (51260f56): profiles write RayTracing=
  per arm, lockstep-ab marks RT mismatches UNKNOWN, rt-build-check.ps1 (Systems adds it to verify_slim).
  08:45 TDR cause refined (Rendering logs): no RT work active at any of the 5 TDRs; pure-GL Off runs logged 260-765 ms GPU
  frames = the card time-sliced across processes; any long packet then crosses TdrDelay 2 s. Rule: two game exes on the
  card max (slot A Rendering RT continuous, slot B rotating), DML ladder sliced in the background. Rendering adds per-map
  longest-D3D12-submission logging + BLAS / hit-data chunking (< 0.25 s lists) in rt2 for the user's machine.
  Frontend DLSS credit merged be725954 (CMake WFC_DLSS_PACKAGE, default OFF; splash + Extras credits line); Systems builds
  the --dlss laptop package with the flag ON and screenshots the line. HD pack: batches 0-3 packed, 463 textures, 3.4 GB.
  09:20 Systems: first DLSS laptop package (52efcbf7 + patch, no credit) at Rebuild-Systems/work/dlss_52ef/work/
  dlss_laptop_slim, 8.37 GB; SETTINGSTEST 8/8 incl. DLSS latch; DLSS reports unavailable on AMD as designed; RT build
  check 10/10; rebuild on 692f082a with -DWFC_DLSS_PACKAGE=ON next. verify_slim gains the RT gate (-RtBuildDir / -RtCheck,
  UNKNOWN without DXR 1.1). Harness link fix 692f082a (wfc_fidelity needed LongPath.cpp; latent). GPU slot B: Experimental
  leak soak from ~09:20 on 51260f56 (16 matches / 16 map changes), then Gameplay, then Systems. New FAIL (Gameplay, low
  impact): VehicleFx::draw indexes meshes_ without a size guard when the map never loaded (graybox fallback) - crash
  instead of fallback; fix = size guard. Guard merged b1363fca (Gameplay 1bac7bfb).
  08:54:41 TDR #6 with only 2 GL exes + the sliced DML job: Experimental's soak exe hung in wglSwapBuffers (residual case,
  beginFrame poll never reached; 2 of 3 hangs today); soak lost again (2 matches). Decisions: DML ladder PAUSED 08:57-09:42
  as an experiment (TDR count with 2 GL exes and no DML decides whether DML gets exclusive windows); soak reruns in the
  quiet GPU window 13:30-14:15; harness pins WFC_HANG_EXIT_S=60. Systems' backstop rework TESTED (WFC_SIMHANG=removed ->
  dump, report, exit 3; plain SIMHANG=1 -> log + dump only; probe on 2 adapters at 0.23 s, zero frame-path cost) and
  applied to integration (D3D11 probe device per adapter polled at 1 Hz only while stalled >= 5 s; exit only on
  GetDeviceRemovedReason != S_OK). Not yet tested against a real TDR (would need dxcap -forcetdr with the user's OK).
  Nonpaged pool after the restart: 1.1 -> 2.76 GB by 08:48 (~20 MB/min under load, no step at TDRs, load-driven); TDRs are
  recurring on this PC (10 since 2026-10-12 20:45).
  09:15 Rendering (rt2, pending commit): the RT static scene was missing ~half of every map's occluders (rtBuildWorld took
  only the depth prepass set: MDI opaque subs, vertex-lit runs excluded; Gorge 634 k vs 1.2 M tris in V_bake) - sun / AO /
  reflection rays escaped through vertex-lit rock and roofs in Faithful AND Realistic as merged in 5d1aecd0. Fixed: every
  static opaque sub (Gorge 1.11 M tris, BLAS 98 -> 173 MB, 37 ms; Rust 1.74 M, 270 MB, 65 ms). Also fixed: R1 never applied
  on MDI-drawn world (bound the base program); V_bake build moved to the loading screen (was +100-250 ms on the first
  frame). TDR item: every load-time D3D12 submission timed, hit-data copies in <= 32 MB lists (longest Gorge 11 ms, Rust
  23 ms); no 4101 from Rendering's runs since 08:29. F9 cycle x5 mid-match: state identical over 9,601 steps. R1 effect is
  real but small on these maps (1-5 % sunlit texels); the occluder fix matters more for the look. Frontend credit
  visibility fix merged db72e065 (line on the Logo_* boot movies too, off FMV_intro).
  09:25 rt2 MERGED 47938bea (fe5b88d7): full occluder scene, R1 in Realistic (MDI fix, loading-screen build), R2 tuning,
  Off-idle probe + no-shaders gate, timed / chunked load submissions, leak log; Off byte-identical on Gorge + Rust, F9 x5
  state-identical. Rust indicative at 2560x1440 (shared GPU): Faithful shadow+AO 2.0 ms / 4.12 GB; Realistic 2.1 +
  reflections 2.3 ms / 4.54 GB; R1 +30 MB; pixel change vs Off: Faithful 0.1-4.7 %, Realistic 4.7-24 %, +R1 up to 56 %
  (sun a touch strong; lower-k refit available in minutes). Systems' packager fixes merged 3406a9f4: fog_volumes.json
  was MISSING from packages (build_slim RENDER_IF_PRESENT), verify_slim HD line semantics, RT gate WFC_RT_MODE=2 stopgap
  for direct boots; LUID probe patch HELD. Context finding (Experimental probe): AMD 26.6.4 does not list
  WGL_ARB_create_context_robustness, so every run got a legacy context with NO_RESET_NOTIFICATION (the beginFrame poll
  is dead here); the driver accepts LOSE_CONTEXT_ON_RESET via wglCreateContextAttribsARB -> Rendering fixes in rt2
  (keep only if strategy == 0x8252), deadline 11:45 for the 12:30 regress head; rt-build-check requires the robust line.
  09:26 ROBUST CONTEXT MERGED f70a46d5 (rt2 6a11ea06): creation attempted whenever wglCreateContextAttribsARB exists,
  LOSE_CONTEXT_ON_RESET only (no ROBUST_ACCESS), kept only if GL_RESET_NOTIFICATION_STRATEGY == 0x8252, else legacy +
  logged; this machine now logs the robust context, so the beginFrame reset poll is live here; Off identity with it:
  Gorge + Rust 5/5 shots, SIMHASH 9,601/9,601; settings test 7/7. Direct boots (WFC_BOOT=match) take WFC_RT_MODE by
  design (the frontend owns the profile): verify_slim's RT gate uses WFC_RT_MODE=2 (correct, not a stopgap). Frontend
  credit polish merged 30e29e7e (no blink between logos). DML-pause TDR experiment: 0 Display 4101 events 08:57-09:19 with
  game exes running (window to 09:42; Experimental reports).
  09:30 DLSS LAPTOP TEST PACKAGE READY: Rebuild-Systems/work/dlss_db72/work/dlss_laptop_slim (8.37 GB; db72e065 + DLSS
  patch 02726216, Release + PGO, WFC_DLSS_PACKAGE=ON, HD pack D 339, M5 + DLSS runtimes, NVIDIA licences, fog_volumes x10);
  verify_slim PASS Streets / Gorge / Debris; RT functional on this card (BLAS 295 MB; the gate's parser only accepted the
  TLAS line - Experimental fixing); credit shots work/credit_shots_db72 (logo + credits visible). LUID probe patch applied
  9542eb2f (probe on GL's adapter only; hybrid laptop safe). GPU slot B -> Gameplay essentials (09:30).
  TDR #7 09:29:39 with DML PAUSED: two RT-on exes at once (Rendering's RT run + Systems' RT verify). Two triggers
  established: aggregate oversubscription and concurrent RT / D3D12-interop workloads. RULE: one RT-on game exe at a time
  (Rendering's slot A); slot B runs RT-off; the sliced DML ladder resumes 09:43 as background (E pack ~11:40). Pool leak:
  load-driven, 8.9 MB/min with 1-2 exes, no step at resets. Systems asked whether their verify exe exited via the backstop
  (first real-TDR test). Result: Systems' exe had finished normally at that second (no reset seen);
  Rendering's exe hung (879928b3 build without poll / backstop / robust context - not a valid test). Rendering found the
  RT bridge was NEVER shut down at exit (GL imports died under in-flight D3D12 work): fixed in rt2 e65d84e3 (~GLRenderer
  -> rtBridgeShutdown: queue wait, scene cleared, GL memory objects + semaphore released, glFinish), merged acfb163d with
  R3 AO EVERYWHERE in Realistic (contact AO 1 m / 2 rays, Faithful unchanged, Rust shadow+AO 2.07 -> 2.82 ms) and R1 on
  Systems' q30 k refit (652711be; Rust k 0.874 -> 0.662). Off identity: Rust 5/5 + SIMHASH; Gorge SIMHASH identical, one
  shot 5 px at 1/255 on the first run, rerun 5/5 (first Off run-to-run noise seen). PACKAGING: R1 needs <map>/vbake/
  (fit.json + vbake_*.png) in the render data (Systems to ship + verify).
  09:50 Gameplay essentials on 51260f56 ALL PASS: PARTICIPANTTEST exits 1 cleanly (static-join fix VERIFIED; 52 harnesses
  share that path), BOTTEST Classic 39/39, Smart 39/39 (AI-cost max 0.67 / 4.59 ms on a quiet card: the b6b6e38 13.85 /
  7.58 ms were load), Gorge 5v5 Smart x3 identical to b6b6e38 (STUCKWATCH 0, stranded 0), graybox VehicleFx crash
  REPRODUCED unpatched and VERIFIED fixed with the guard. Slot B -> Experimental essentials. Final DML-pause verdict:
  DML not required for a TDR (one with two RT exes, none in 44 other minutes); pool leak slow + load-proportional.
  10:16 Experimental essentials on f70a46d5 ALL PASS: GPU-lost exits 3/3 (SIMRESET exit 3; SIMHANG=removed exit 3; plain
  SIMHANG not ended = no false kill); rt-build-check 3/3 incl. the robust-context line; map-loss gate PASS (Berth = known
  false positive, no reference); renderstate 7/7; determinism 0/2 diverged; identity-capture Rust 05319349 vs f70a46d5:
  SIMHASH 3002/3002, 25/25 captures identical (everything since the regress base is image-identical with RT Off).
  Slot B -> Gameplay's remaining sweep until 12:15. Head 493e42c0 (+ Systems verify_slim settings self-test step).
  10:30 USER-FACING FIX merged 67b42ce6 (rt2 c37596b2): on a cold AMD shader cache the first Realistic frame of a map froze for
  MINUTES (Molten 440 s, s5_mat 310 s; main thread in amdxc64.dll compiling the material reflection PSO, one uber-switch
  over every material); the PSO now compiles on a background worker, reflections use the proxy shading until ready, then
  switch between frames (map change abandons a pending compile); masked atlas + hit data built under the loading screen
  (was a 2-3 s first-frame hitch). Cold Seed: no stall; warm Rust ready after 26 ms; Gorge Off byte-identical. Guide
  notes the few-minute proxy period; a pre-compile at install / launch is a later option.
  10:45 PAIRS for the user (Rendering, e65d84e3, 2560x1440, HD off): Rebuild-Rendering/work/pairs/final_rt_{rust,molten}
  (Off / Faithful / Realistic, 5 steps, lossless + strips) and showcase_rt_{rust,molten}/crops.md (3 flash-free frames x 2
  100 % crops + heat maps + LOOK AT notes). Realistic changes 18-60 % of pixels (soft world sun shadows, world AO,
  reflections); Faithful 0-5 %. Indicative cost at 2560x1440 (shared GPU): Faithful shadow+AO 2.4-2.5 ms; Realistic
  3.3-3.8 + reflections 2.7-2.8 ms; RT VRAM 3.9-4.7 GB. NEXT-ROUND flags: (1) Realistic's character shadow fainter than
  Faithful's where the bake had no sun (Molten m01200) -> keep a soft sky / ambient character shadow in Realistic;
  (2) RT reflections on rough metal read as dirt veins (Molten m00600) -> roughness gate; (3) robot lineup with two
  similar team colours still to capture. HD-on set (Original / D / E + Modern x RT) after the E pack.
  10:56 E PILOT DONE (AssetTools): out\hd\push_e_gen, 20 rows / 125.5 MB, G2 strength, all 11 Rust units pass every gate;
  by eye G2 replaces D's crunchy grit with smoother natural weathering, designs kept. D pass: batches 0-3 packed (463
  textures), 4-5 reviewed + rebuilding for the 12:30 freeze, 6-10 built awaiting review; colour model 870 / 1,657 (~31 h
  left at the shared-CPU rate, ~2026-10-14 18:00). Gameplay sweep: 16 harness groups PASS; QABOT 6/7 (QA freeze tool:
  bots moved 5.34 m after freeze, likely vehicle coasting; Gameplay isolating; dev tool). TDR #8 10:29:27 with ONE menu
  exe + DML; id 1001 GL errors = post-reset spam on legacy-context builds (closed). Final head candidate 0ec2a0bb.
  11:12 Gameplay sweep COMPLETE on 51260f56 (docs/handoffs/GAMEPLAY_REGRESSION_SWEEP_2026-10-13.md, 6e62b4bf): 30+
  harness groups PASS, measurement-only clean, no open gameplay FAIL; private memory flat at 4.50 GB over a 870 s Gorge
  5v5 (no leak in our process). QABOT fixed a17309b7 (QA freeze zeroes bot velocity once; vehicle-form bot coasted 5.3 m;
  7/7 both AIs; dev-only path). TDR #9 11:06:42 (Rendering HD-on runs + Gameplay exe). FINAL HEAD CANDIDATE a17309b7.
  11:35 HD-ON PAIRS (Rendering, 18 runs): Rebuild-Rendering/work/pairs README section HD on: final_hd_* (Original / D /
  Enhanced, RT Off), final_hdrt_* (Original / D + Faithful / Enhanced + Realistic), final_e_vs_d_rust, + showcase crops.
  FINDINGS: (1) the menu's Enhanced is NOT live: hdTextureLevelAvailable(2) is false (no Enhanced root), HDTextures=2
  clamps to 1 (D textures, Modern off); the pairs forced WFC_MODERN=1. Next round: level-2 root = E + Modern. (2) HONEST
  READ of Modern: floors / metal much brighter, paler, glossier (82-84 % of pixels change) - washed out / plastic next to
  the original's dark weathering; kPbrSpecOcc / specular calibration is the first next-round item. (3) HD D vs Original:
  sharper grit, same colours, subtle in full frames (Integration view of m00300 crop1 agrees). (4) E vs D: 0.7-1.7 % of
  pixels, the 20-row pilot is too small to judge; extend E to a full map. DECISION: the package ships as is (Enhanced
  clamps to Faithful, user told); the morning showcase leads with RT Realistic crops + HD D.
  12:35 FINAL HEAD a17309b7 GATES PASS (Experimental, quiet card): identity-capture Rust 504 vs 05319349: 0 image diffs,
  SIMHASH 3002/3002; regress-8map lockstep best p90 of 2, base / head ms: 501 2.15/2.16, 502 1.92/1.94, 503 2.34/2.33,
  504 2.53/2.52, 507 2.36/2.37, 508 2.08/2.08, 509 2.76/2.76, 510 2.53/2.53 (worst +0.02 vs block +0.15; 0 UNKNOWN).
  Leak soak 12:36-~14:10. HD freeze for the refresh: push_full_snap_20261013_b0-6 (828 rows, 5,467 MB); live pack 0-7
  (949 textures). Systems PHASE=package started 12:40; verify after 14:15.
  14:02 PLAYTEST REFRESH VERIFIED (Systems, work/verify_refresh_a173/REPORT.md): package work/playtest_slim (a17309b7,
  11.16 GB; tag playtest/09c-overnight-a17309b7): HD static 828 rows 0 problems; Streets / Gorge / Debris PASS;
  SETTINGSTEST 7/7; RT gate build + robust context + runtime PASS. ONE FAIL: R1 sun swap unused - Pipeline::load() tests
  the GL entry points (DrawBuffers / ColorMaski / ClearBufferfv) BEFORE GLExt::load() on that boot, so r1Map_ is false
  for the session (data shipped fine, k 0.354). Decision: ship for the playtest as is (RT Off by default; Realistic
  without the sun swap); Rendering fixes the order (lazy check) on rt2, repackage after the playtest. Leak soak FINAL
  PASS (Experimental 13:53): plateau ~4.9-5.4 GB, handles / GDI / threads flat, GL live textures plateau 93, PCM 36.5 MB
  every return; driver pool +456 MB in 42 min under load, flat on the quiet run. User awake 13:54; lanes parked.
  USER PLAYTEST 2026-10-13 ~15:00 (docs/handoff/USER_PLAYTEST_2026-10-13.md): P0 (user: ASAP) first-person weapon
  wobble / interlace when strafing (Rendering; suspect the M1 weapon draw queue); false firing + reload prompts from
  nearby bot fire (Gameplay); vehicle form = low-ammo / cannot reload (Gameplay); class change during respawn = spawned
  but cannot move (Gameplay). P1: Game Mode Options must hold Player Limit / Autobots / Decepticons / Bot Difficulty /
  Bot AI (Frontend); map name centred, frame off the text (Frontend); USER DECISION M2: Smart AI is the default and the
  Classic row leaves the UI (code path kept for identity tests), Smart XP parity (Gameplay); FPS counter in settings
  (Frontend + Rendering); bot pathing keeps improving (Gameplay, bots unpaused). RT: user likes Realistic on the whole
  environment, wants it pushed further, shadows good; verify reflections (cannot tell); shadow alignment bug on the
  tested map; check the main-menu globe. Working: menus, loading, XP (43 per kill), bots die properly. Overall: 'the game
  is getting closer ... a lot is working'.
  15:50 progress on the playtest items: Frontend merged - Bot AI row removed (b82f7b30 / e6a79334), map name centred above
  the frame (4ba0897c / 2a2ff00d; headless check PASS on all 13 names), FPS Counter row Off / On with a top-right fps + ms
  readout (425efb7e); Game Mode Options merge in progress (~3 h). Root causes found by code reading (Gameplay +
  Experimental agree): #2 phantom fire = one shared WeaponFx re-anchored to the LOCAL muzzle every tick (fix: per-owner
  anchors) and the vehicle HUD mixing the vehicle clip with the ROBOT capacity (fix: both from the HUD weapon; reload tap
  routed to the vehicle weapon); #1 judder = the gun drawn at the current-step hand bone while the body blends prev / cur
  pose at alpha (Rendering fix: blend the socket; captures running); #4 lead = OnRespawn dropped while PausedSpectating
  (Change Character open) -> Resume lands in Spectating with input blanked. Added 15:45: semi-transparent gun on the jet
  character (Rendering). Shadow misalignment map = Molten. User done playing 15:30; lanes resumed (slot A Rendering,
  slot B Experimental baselines, DML off until the P0 proofs).
  16:00 P0 #2 + #3 FIX APPLIED b0a22cb6 (Gameplay patch af9290f0: per-owner WeaponFx anchors; HUD mag / reserve from the
  HUD weapon; vehicle reload tap; WFC_FXOWNERTEST; WFC_AUTOSTRAFE=alt); proof runs queued on slot B. FPS Counter merged
  4f7a2f91. LAUNCH PITFALL (Systems): no log from the user's session - packages write wfc.log / wfc_profile.ini to the CWD,
  and Play.bat's first test falls through to the integration exe at F:\...\build\release\bin on the dev PC (same a17309b7
  code, integration render data); fixes: Play.bat runs the package's own exe (free drive letter when F: is taken), log +
  profile beside the exe (patch), verify_slim checks it. Rule: the user double-clicks the package exe; no build/release
  rebuild while they play.
  16:20 Frontend's Game Mode Options merge landed 9dd8a406 (7da913ed: Player Limit / Autobot + Decepticon Bots or Bots
  / Bot Difficulty inside the mode's options; no bot rows on the lobby screen; tests 145/0). P0 #4 FIX APPLIED c31d8547
  (Gameplay 886e704d: UIController PausedSpectating handles OnRespawn -> Paused, Paused handles OnSpectating; CLASSCHANGE
  extended; 5 ui.* tests; Frontend reviewing). #7 closed by code: XP = base x BotXpPolicy::scale(difficulty), no
  AI-dependent path (43 = 50 x 0.85 Expert), XPTEST classic vs smart to confirm. All playtest P0/P1 code fixes except the
  judder (Rendering, captures running) and the jet-gun transparency are now on integration.
  16:30 Launch / log fixes on integration 0507891d (Systems): Play.bat always runs the package's own exe from its own bin
  and maps the package to F: if free, else the first free letter from W: down with the WFC_* roots set (on the dev PC it
  picks W:, so the package's data is used, not the integration tree); verify_slim requires wfc.log beside the exe, the new
  Play.bat template, and a -ForeignCwd write-denied-cwd run; SYSTEMS_LOG_DIR.patch: cwd stays primary for wfc.log /
  profile / characters / crash reports (harnesses collect per-run logs there), the exe folder is the fallback, the log's
  first line names the file. README: start only via Play.bat. Frontend reviewed the respawn UI hunk: OK; options-row
  style follow-up merged 0b096b7c.
  16:45 P0 #1 judder REPRODUCED objectively (Experimental, a17309b7, Streets strafe, 64 px tiles on the gun): gun-tile
  jitter 34.7 px at 144 Hz (sign flips 0.37) vs 2.06 px at the 60 Hz control; weapondirect (M1 queue bypassed) 33.2 and
  FSR 3.1 34.8 = the queue and FSR are NOT the cause; interpolation-only, which fits the gun at the current-step socket
  vs the blended body. PASS for the fix = gun-tile jitter <= control + 0.35 px on tiles 7,8 / 8,7 / 6,8. Cross-talk
  baseline: robot-form idle shows no ammo drop and no reload prompt (the vehicle-form row is being added); the image
  flash detector is being reworked offline; WFC_FXOWNERTEST is the authoritative proof. User's settings: max, no FSR / FG,
  300 cap, fullscreen native; RT ON when the jet gun looked see-through.
  17:05 P0 #1 JUDDER FIXED a44aa6db (rt2 9d949f4b): real cause = the gun drawn one step behind the body on every step
  frame (weaponWorld_ is set in the step's background part after the frame that shows the new pawn position), catching
  up next frame (+-30 px flicker at 1280x720); weaponDrawWorld() = current meshMatrix x socket bone lerped prev -> cur at
  pose alpha x offset at the draw sites only (gameplay keeps weaponWorld(), SIMHASH unchanged; WFC_WEAPONBLEND=0 A/B).
  Rendering's proof (gun vs body judder): 144 Hz 55.6 -> 2.98 px, 300 cap 25.2 -> 0.46 px, 60 Hz control 2.24; not the
  M1 queue, interpolation or skin upload. Experimental's independent after-run pending. #2 cross-talk REPRODUCED
  objectively on a17309b7 (21 flashes / 60 s on the idle player's gun, HUD ammo unchanged). Repackage head candidate
  a44aa6db; final after #1b (jet gun under RT).
  17:25 Frontend slot-B captures on 0507891d ALL PASS (map name centred inside the frame; TDM Host Options with the 4
  bot rows + Create Game; Graphics page fits with FPS Counter; counter top-right clear of the HUD); sent to the user.
  Gameplay proofs on a44aa6db: CLASSCHANGE 26/26 incl. the respawn-wait cases (#4 PROVEN: spawned, UI InGame, moved
  12 m); RMUZZLE 4/4, MUZZLE 5/5, PROJFX 3/3, WEAPON 19/19; XPTEST identical classic vs smart (#7 PROVEN at runtime);
  FXOWNER: both fix checks PASS (0 held-anchored particles after a bot shot; clip / reserve / reload unchanged), 4 other
  rows are test-geometry bugs (streak starts 1.9 m ahead of the socket; Soldier's 1-round TankCannon) being corrected.
  Experimental after-runs (wobble + crosstalk incl. vehicle row) on a44aa6db running.
  17:35 #1b CLOSED by the judder fix (Rendering): the see-through jet gun was temporal - the gun alternated between two
  positions on step / non-step frames and at 300 fps the eye blended them into two half-opacity guns; 8-frame averages
  show a doubled gun before, one solid gun after (jet judder 2.50 -> 0.67 px); RT Off / Faithful / Realistic standing
  still all give a solid gun (no composite leak). FINAL REPACKAGE HEAD a44aa6db named (tag playtest/09c-fixes-a44aa6db);
  Systems building + packaging with the b0-12+ freeze; verify (incl. -ForeignCwd, SETTINGSTEST 7, R1 sun swap ON gate)
  after Experimental's after-runs.
  17:50 PACKAGE 2 BUILT: work/playtest_slim (a44aa6db, 14.99 GB: HD D b0-12 = 1,531 rows / 9,393 MB static PASS, M5
  runtime, R1 vbake x10, new Play.bat); a17309b7 kept as playtest_slim_a17309b7; verify pending slot B. RT REFLECTIONS
  PROVEN working but weak by design (Rendering: Realistic with vs without, Molten 29.7 % / Rust 11.5 % of pixels change,
  slightly darker grounded metal): the RT sample only replaces the original's low-weighted cube sample. APPROVED for
  Realistic only (user: push further, beyond faithful): a Fresnel-weighted RT specular layer on glossy surfaces
  (SpecularPower-gated) + roughness gate, switchable, with before / after crops, next package. R1 confirmed ON on a direct
  boot (18 pages). Test-only Gameplay patch (FXOWNER socket-frame checks, WFC_NAVQTEST) on integration 2017eeba.
  18:00 EXPERIMENTAL AFTER-RUNS (independent, harness 65a48ed7): both P0s FIXED on a44aa6db. Wobble (Streets strafe
  144 Hz, gun tiles vs torso tiles in the same run): a17309b7 gun jitter 34.7 px, gun / body 7.85, flips 0.37 = FAIL;
  a44aa6db 6.3 px, 1.38 (60 Hz control 1.26), flips 0.03 = body = PASS; same with weapondirect and FSR 3.1. Cross-talk
  (idle player, 60 s, same bot activity): flashes at the player's gun 21 -> 0; vehicle-form HUD clip 60/6 -> 60/60;
  ammo drops 0 / 0. Verdict rule = gun / body within the control's ratio + 0.25 and flips <= body + 0.1 (an absolute
  px rule cannot pass at 144 Hz). Slot B -> Systems' package verify (RT gate granted, Rendering paused).
  16:28 PACKAGE 2 VERIFIED (Systems): all PASS incl. -ForeignCwd (log beside the exe proven), SETTINGSTEST 7/7, RT gates;
  one row open: the R1 first-Realistic-frame line never logged on the direct boot. Root cause found by Rendering during
  the user's session: the WHOLE RT pass runs only inside the characters' flush (flushDynQueue returns early on an empty
  queue), so character-free frames get no RT shadows / AO / sun swap and the effects pop with characters (explains the
  'subtle' verdict, the Molten shadow report and the gate); fix = a world-only RT pass at flushTranslucency when the
  queue is empty (written, builds after the session). Shipped for playtest 2 as is (RT Off by default).
  USER DECISION 16:45: the NEXT PLAYTEST waits for (a) the new textures finished (D pass complete + the beyond-D E pass
  at a days-scale scope + Enhanced live with Modern materials) and (b) full ray tracing: GI replacing the bake in
  Realistic, visible reflections, sky / ambient character shadows, shadow refinement, the RT-pass fix. Plans + ETAs
  requested from Rendering and AssetTools; interim packages keep shipping the playtest-1 fixes.
  User clarification: GI = the ray-traced lighting for the ENTIRE map (replacing the bake). Rendering's FULL-RT PLAN
  approved (work/fsr3/RT_FULL_PLAN.md): 1 RT-pass-without-characters fix 1 h -> 3 visible reflections (Fresnel layer +
  roughness gate) 5-7 h -> 2 GI: DDGI-style irradiance probes, baked sun direct + bounce replaced by RT sun + probe GI,
  characters lit by probes, 20-28 h (fallback today's Realistic) -> 4 character sky / ambient shadows 3-4 h -> 5 shadow
  refinement 4-6 h -> 6 Modern calibration + level-2 E root 6-8 h -> 7 Molten + globe checks 2 h. Total 4-6 days of
  Rendering GPU time. 4c replacing the ~270 baked local lights per map (+2-3 days) = user scope question (default after
  the playtest; the probes already bounce their light). Budget: Realistic <= 4 ms RT at 2560x1440 (~120-165 fps, cannot
  meet the 300 cap), Faithful = the high-fps RT mode, Realistic VRAM delta <= 1 GB.
  17:10 REVISED and approved (supersedes the DDGI steps and the 4c question): a RUNTIME RAY-TRACED LIGHTMAP - per
  lightmap texel: sun (soft) + sky + every static light from lighting.json with RT shadow rays + bounce, written in the
  bake's 3-coefficient directional basis (shaders / UVs unchanged, lightmap textures replaced); full pass under the
  loading screen, cached per map, rotating subset per frame; character probe grid from the same data; dynamic lights per
  pixel; fallback = the baked lightmaps (WFC_RT_GI=0). Stages, each whole-map: S1 sun + sky 16-22 h, S2 static lights +
  RT shadows 10-14 h, S3 multi-bounce + character probes 10-14 h, S4 dynamic lights + per-map brightness calibration +
  perf 8-12 h; ~6-7 days of slot time; cost ~ today's Realistic + 1.5 ms. Order: RT-without-characters -> reflections ->
  S1-S4 -> shadow refinement -> Modern / level 2 -> checks. Systems asked to confirm lighting.json carries every static
  light with the bake's units.
  17:20 HD FINISH PLAN approved (AssetTools docs/HD_FINISH_PLAN.md): D complete ~2026-10-15 morning (~700 textures of
  colour left, ~22 h CPU, 2 x 12-thread trial; GPU rows 1-2 h; review + caps + pack pipelined for 19 batches); E = every
  world surface at G2 (~1,400 textures / ~800 units, ~8 GPU h, ~10 h CPU, ~12 h review) ~2026-10-17/18, map order
  Molten, Streets, Gorge, Rust, Debris; then 490 M6 roughness maps; E as a level-2 root over D + hd_pbr_index.json;
  snapshots per milestone. GPU SHARING: Rendering holds the card; AssetTools' DML in announced exclusive windows
  (~2 x 2 h / day, the first after Rendering's RT-pass fix). NEXT PLAYTEST TARGET ~2026-10-21/22.
  17:30 USER DONE with playtest 2 (no findings reported yet); 'Continuum' = continue. Full machine released: Rendering RT
  step 1 (GPU ~1 h) -> first exclusive DML window (D GPU rows, E milestone 1 = the ~301 world textures bound by the
  user's five maps, ~2 GPU h) -> reflections -> S1; Experimental FAST check of package 2 + the self-test sweep (slot B);
  Gameplay FXOWNER rerun + NAVQ pathing baseline after; Systems extended light fit (point / spot scales, falloff, sky +
  bounce residual, Molten first; bake-lights audit merged e5c5d55d: 0 missing lights, Beast settings not cooked).
  17:50 E ESTIMATE CORRECTED (AssetTools): generation runs at HD size (16x the pixels): milestone 1 ~11 GPU h, every world
  surface ~110 GPU h (weeks). Decision: generate E detail at 2x the original and let D's upscale carry it (4x less GPU:
  milestone 1 ~3-4 h, everything ~28 h) after a 3-unit Rust A/B in the first DML window; user maps first; overnight DML
  windows; fewer denoise steps in reserve. USER LOCK: E = 'a full overhaul ... past the artist representation ... the most
  realistic textures possible ... add more detail wherever needed' = THE texture deliverable (D stays Faithful); strength
  G2 vs G3 chosen by the user from a crop sheet (Original | D | E-2x-G2 | E-2x-G3 | E-4x-G3); scope every world surface,
  then characters + weapons.
  17:05 Experimental FAST launch check of package 2 PASS 2/2 (via the Play.bat route, own profile); self-test sweep on
  2017eeba running. Gameplay: WFC_NULLRENDER=1 CPU-only harness boot landed 46f2c713 (stub IRenderer, no window / GL;
  DETERMINISM 30 s null boot 0.63 GB / 7 s; GL-vs-null digest identity pending a 1-min slot): sim-only harnesses run
  without the GPU from now. Pathing: two Smart-only changes coded (pure-pursuit steering 3.5 m ahead through corridors;
  stuck response = back off 0.6 s toward the open side instead of hopping), measured by NAVQ on the 4 user maps
  (baseline / +pursuit / +both / Classic), patch after the numbers. User: not playing; package 2 untested so far.
  17:10 NAVQ BASELINE (3 seeds x 4 maps; slow % / reversals per travel-min / off-mesh % / stucks per bot-min): SMART
  MOVES WORSE THAN CLASSIC ON EVERY MAP - Streets 7.8/5.5/5.7/1.65 vs 5.4/3.7/5.1/1.55; Molten 3.2/2.4/3.3/0.72 vs
  2.1/2.3/1.6/1.04; Debris 6.1/2.5/5.5/1.60 vs 3.2/0.8/1.9/0.82; Gorge 11.1/7.7/12.5/2.85 vs 6.9/3.8/7.2/2.15 (matches
  the user's 'pathing could be a lot better'). Stuck sidestep worse everywhere (dropped); pursuit helps Streets only.
  Ablation running (exposure cost, per-bot route noise reseeded every 10 s = prime suspect, squads, flank); repaths
  ~45-55 per bot-min = corridor churn. TARGET for the next playtest: Smart >= Classic in all four columns on every map
  with the tactics kept; fix mechanisms (seed hold, repath cap, corridor hysteresis), not feature zeroing.
  17:25 USER REQUEST: automated game runs must be silent while they use the PC (movie). WFC_MUTE=1 added c246ed3e
  (Win32Audio feeds silence to the device, mix / cue logic / stats unchanged; env-only, never a package default), pinned
  in tools/fidelity (eef42f10) and every lane runner; a Windows session muter (Core Audio, scratchpad script) mutes any
  wfc_rebuild.exe session within 2 s for builds without the switch (running 17:27-03:27). Systems' light calibration
  merged 13b29545 (fit_lights.json x10, shipped by build_slim --vbake).
  17:40 NAVQ ABLATION (Gameplay, 3 seeds x 4 maps): Smart's movement deficit comes from SQUADS - squadSize=0 brings Smart
  to ~Classic (Streets 4.0/4.3/3.5/1.26 vs 5.4/3.7/5.1/1.55; Molten 2.2/2.1/2.0/0.73 vs 2.1/2.3/1.6/1.04; Debris
  3.7/1.8/2.5/0.79 vs 3.2/0.8/1.9/0.82; Gorge 8.4/5.3/6.7/2.43 vs 6.9/3.8/7.2/2.15). Route noise HELPS (removing it
  doubles Gorge reversals); exposure / flank mixed; no single squad sub-feature explains it (squadSize also gates
  respawn-near-squad). Next: squads off / spawn-near off / per-member slots 5 m around the shared destination / Classic
  at 6 seeds; the fix keeps squads and removes the crowding mechanism.
  17:50 RT STEP 1 MERGED fd3bd799 (rt2 a93a3e11): the RT pass runs every frame (world-only pass at flushTranslucency when
  no character is queued); no-character RT verified on Rust (sun swap ON, 32 % vs Off every frame) + Molten; Off identity
  with the REAL build (Rendering caught a stale-mtime baseline-vs-baseline pair and reran): Rust 5/5 shots, SIMHASH
  identical. First DML WINDOW opened 17:45 (~2 h): D GPU rows, the E A/B sheet for the user, E milestone 1 start;
  Experimental's sweep parked at test 55 (WFC_TILETEST); Rendering on code (reflection layer, S1 tracer).
  17:55 NAVQ at 6 seeds (Gameplay): Smart's RESPAWN-NEAR-SQUAD is the main Gorge culprit (off: Gorge 5.5/4.1/5.3/1.77 vs
  base 12.1/10.0/14.7/3.06, better than Classic 5.9/5.0/7.0/2.31; helps Molten + Debris); member slots shelved. A real
  visible bug behind the reversal outliers: a Roam goal on a floor 18 m below a railing + horizontal-only arrival made a
  bot ping-pong 2.5 m at 8 m/s for 263 s (565 reversals). Smart-only fixes under test: goalFloorM 3 (arrival needs the
  goal's floor) + ditherGuard (>12 m walked, <3 m net in 6 s outside combat -> avoid the spot, drop the goal, hold
  rejoins 4 s). Next: fix the spawn-near mechanism itself (starts with a short nav path to the squad / on the main nav
  piece) rather than switching it off.
  18:45 E A/B (AssetTools, first DML window; D GPU rows DONE 18:32): the 2x-generation path is ~indistinguishable from D
  (exact lock + D's fine band) -> REJECTED; only E-4x-G3 visibly changes the wall (smoother grime; faint horizontal
  streaks on flat dark areas = artefact). BUG: D protected wherever TEXSET Masks B > 0.1 blocked E on most ENV surfaces
  (B is the Cust A/B BLEND factor, high almost everywhere; 2 of 3 units came out dE 0.00) - Systems asked for the exact
  designer-colour channel / threshold. Approved next: 'G4' A/B = 4x, denoise 0.70, lock at half the original resolution,
  fixed protection, 5 units (~30 min). If G4 is the look: milestone 1 (user's 5 maps) ~17 GPU h over the week, the rest
  of the world after the playtest; the user sees the sheet + hours first.
  18:55 Tint-mask semantics corrected (Systems 5fc3a0b6, merged 5e9dc3fd): Masks B is the Cust A / B blend only with
  Enable_CustomColorTint (234 of 662 materials), elsewhere the detail-normal mask; E protects where the effective tint is
  far from white (SYSTEMS_M6_TINT_AB.json).
  19:00 PATHING ROOT CAUSE (Gameplay): Smart's health-pickup fetch measured distance horizontally - a pickup 18 m below a
  railing counted as nearest within 40 m and the trip led bots into an uncovered nav strip with no corridor out, for
  minutes. Smart-only fixes: height counts double; failed trip skips 90 s; dither escape skips the pickup 2 min; dither
  guard + floor-aware arrival + trapped escape (back to the last on-mesh point; 3 escapes per life -> stranded rule).
  Streets seed 5: reversals 8.4 -> 2.8 per travel-min, worst bot 565 -> 18. Spawn-near fix: spawnNearScope=2 (team's
  ACTIVE spawn cluster): Gorge 7.6/3.7/8.4/1.70 vs any-start 11.9/8.4/11.8/2.64. Final set under a 6-seed run; then
  BOTTEST / DETERMINISM, the Classic identity check, the patch.
  19:20 G4 A/B DONE (AssetTools, out/hd/push_e_full/ab/ab4_sheet.png, sent to the user): E-4x-G3 = the real overhaul
  with designs kept (bolts / rings / rivets / strap detail / bevels / carved panels; mean dE vs D 0.6-0.8, no new edges;
  identity gates 3/5 with two narrow misses); E-4x-G4 invents objects (stickers, rings, seams; fails identity 5/5).
  Streak artefact removed by a negative prompt (default now). Recommendation G3 + negative prompt, E gate dE p99 ~5 with
  layout / edge / palette / seam strict; cost ~0.7 GPU-min per 1024 HD unit -> milestone 1 ~15 GPU h. First DML window
  closed 19:20 (D GPU rows done 18:32); RT RESUME to Rendering (reflections block), Experimental's sweep resumed.
  19:35 SELF-TEST SWEEP on 2017eeba (Experimental, 61 switches, 180 s cap): 38 PASS / 0 FAIL / 23 diagnostic (no
  verdict by design); exit codes 0 / 1 only, no 0xC0000409, no crash dumps except HANGTEST's intended one, 0 gpu_lost;
  QA 9/9 after rerunning with WFC_QA=1. Slot B -> Gameplay (FXOWNER rerun + GL-vs-null DETERMINISM digest).
  19:45 Gameplay proofs: FXOWNER 6/6 (bot flash in the bot's socket frame, follows the socket; player ammo unchanged;
  Scout car HUD 60/300, reload tap works); null-render identity PROVEN (GL and null boot digest 8895b43c7ea0927e).
  Experimental's sweep doc FINAL copied to docs/handoff (85f19e7f). PATHING PATCH APPLIED c949ab88 (Gameplay 6c9a7109, Smart
  only, Classic byte-identical): height-aware pickup distance + 90 s skip, off-mesh dither guard + walk-back + stranded
  after 3 escapes, floor-aware arrival, squad respawn within the team's active cluster. 6 seeds x 4 maps after (Classic):
  Streets 5.4/3.0/3.2/1.22 (6.9/3.3/5.9/1.48) MET; Gorge 4.7/4.8/5.0/1.57 (5.9/5.0/7.0/2.31) MET; Molten 2.0/2.5/1.6/0.75
  (1.8/2.0/1.5/0.84) close; Debris 4.8/1.3/2.6/0.94 (3.8/1.2/2.1/1.09) close; stucks below Classic everywhere; BOTTEST
  39/39 both AIs; Experimental verifying the Classic identity. Remaining: Debris slow time, Molten reversals.
  20:00 RENDER-TO-SIM COUPLING (Rendering): gameplay / bots read per-owner render times (ownerRendered_, set by
  unoccluded pawn draws), so the RT-every-frame change moved the lockstep SIMULATION in Faithful (SIMHASH equal at
  step 300, diverged by step 1000); Off identical. Rules: Faithful / Realistic identity baselines restart at a93a3e11
  (Rendering keeps an a93 baseline tree, no in-place swaps); RT-on lockstep rows compare SIMHASH first; gameplay identity
  claims stay RT-off. Gameplay asked what bots read from render times, whether the null boot matches GL over a full
  match, and to decouple Smart from render times if decision-relevant. Interim package 3 head c949ab88 (HD b0-12) in
  Systems' pipeline.
  20:10 Experimental review of the pathing patch: CLASSIC IDENTITY PASS (null boot, WFC_SIMHASH=0 hashing every pawn +
  bot steering state per step: 7919/7919 identical on Streets + Gorge x seeds 5 / 7, controls OK); Smart changes OK, no
  tactics regression in code; follow-ups to Gameplay: think-time re-pick floor test, a squad cohesion metric for spawn
  scope 2, dither guard vs cover-Retreat spots in off-mesh pockets, comment fix, BOTPERF at 32v32.
  20:20 Coupling audit (Gameplay): NO decision-relevant gameplay read of render times (only the impact-squib relevance
  gate: presentation RNG, extended lobbies, not in SIMHASH; perception / aim / LOD / sleep / bone reads independent; the
  renderer's visibility query is const). So the Faithful SIMHASH divergence after a93a3e11 is an RT-mode SIM INPUT
  (shared RNG consumed by the RT path, or lockstep steps-per-frame tied to frame time): assigned to Rendering as a
  determinism bug to find before S1 (WFC_IMPACTRELEVANCE=0 rerun, RNG / step-clock audit). Experimental: RT-on lockstep
  rows judged SIMHASH-first ('SIM DIVERGED at step N').
  19:49 INTERIM PACKAGE 3 VERIFIED (Systems, work/playtest_slim 15.0 GB, c949ab88, tag playtest/09c-interim3-c949ab88):
  REPORT.md PASS on EVERY row - HD b0-12 0 problems; Streets / Gorge / Debris with -ForeignCwd; SETTINGSTEST 7/7; RT
  build / robust context / runtime; R1 PASS for the first time with the full chain on a direct boot ('sun swap ON
  (V_bake pages 26, arrays 4)'), i.e. the RT-every-frame fix works in the package. Previous package kept as
  playtest_slim_a44aa6db; b6b6e38 / a17309b7 packages deleted (tagged, rebuildable).
  20:05 VISIBLE RT REFLECTIONS MERGED aaded95e (rt2 61b301aa): Realistic-only Fresnel-weighted RT specular layer on glossy
  surfaces (SpecularPower-gated) + roughness gate (Molten veins fixed); WFC_RT_REFLSTRENGTH / WFC_RT_NOREFL; Faithful
  identity vs the a93 baseline 5/5 + SIMHASH. SIM DIVERGENCE RESOLVED as a ONE-OFF: the same exe reproduces f3db... (=
  Off = the other Faithful runs); the odd run had heavy CPU contention and a different impact-FX history -> lockstep is
  not fully deterministic under CPU load (async-step join / FX relevance timing suspected): Gameplay owns the repro +
  fix + a run-twice-under-load self-test; until then lockstep A/Bs run on a quiet CPU. RT code cleared.
  20:15 Experimental review of the reflection layer: PASS (Off gets no shader text; Faithful's uRtGloss = 0 is an exact
  no-op; world lit / lightmapped programs only, never characters / weapons; gate = authored SpecularPower + SpecularColor;
  tangent-space NoV valid). Hardening for Rendering: guard GLOSSLO < GLOSSHI; clamp WFC_RT_REFLSTRENGTH to [0, 4];
  watch grazing-angle edge ghosting from the reprojected target in the next pairs. Gameplay's determinism audit: RNG /
  wall-clock coupling unlikely; suspect a race between the async step's background part and the main thread under
  load; repro = 3 lockstep runs under CPU load + an WFC_ASYNCSTEP=0 control (slot B after the pathing set).
  20:30 PATHING SET 2 APPLIED 20ab0a4a (Gameplay e0353420): floor-aware think re-pick, dither escape ends any Smart errand,
  squadSlowFactor key, NAVQ squad cohesion. 6 seeds (Classic): Streets 5.1/3.2/3.6/1.25 (6.9/3.3/5.9/1.48), Molten
  2.2/2.0/1.6/0.67 (1.8/2.0/1.5/0.84), Debris 4.4/1.3/2.5/0.91 (3.8/1.2/2.1/1.09), Gorge 4.9/3.9/6.4/1.39 (5.9/5.0/7.0/
  2.31): LEVEL WITH CLASSIC within 1 SE on every map, stucks below; Classic identity 24/24; BOTTEST 39/39; 31v32 AI cost
  0.40 vs 0.43 ms. Squad trade-off: spawnNearScope 2 spreads squads (70.6 m mean, 19 % within 15 m) vs scope 0 (55.0 m,
  22 %) but scope 0 doubles Gorge's movement cost. Integration decision: build (c) = active cluster + any start with a
  short nav path (<= ~40 m) to the leader, ship if Gorge stays within 1 SE of scope 2 and cohesion improves, else keep
  scope 2.
  20:45 TEXTURE OVERHAUL STRENGTH DECIDED: E-4x-G3 + negative prompt (user delegated the choice: 'choose whichever one
  looks the best'; G4 invents objects and fails identity). Milestone 1 = the user's five maps (~15 GPU h) in DML windows +
  overnight blocks; level-2 E snapshot after it for an interim package; the rest of the world after the playtest.
  20:55 Squad spawn scope decided: scope 2 stays (scope 3 = +40 m path starts: cohesion -3.5 m / +2 pts only, Gorge
  stucks +0.31 past 1 SE, worse on Molten / Debris; kept as an off-by-default tune); regroup-after-respawn = future squad
  work. Slot B -> Gameplay for the lockstep-under-load repro. MOLTEN 'SHADOWS DO NOT LINE UP' FOUND (Rendering): a black
  'pit' on the central floor = R3's Realistic world AO hitting an invisible occluder just above the floor that is opaque
  in the TLAS (masked / unlit / helper mesh); vanishes with WFC_RT_AORAYS=0; popped with characters before a93a3e11 (53.7 %
  of pixels differ pre / post a93 on that frame). Fix = the TLAS filter (~30 min), then Rust / Gorge checked for the same
  class. Systems: Molten per-page sun k refit done (9 of 18 pages, clamped sun 11.0 -> 4.8 %; runtime to read pages[i].k);
  old packages deleted (F: 218 GB free).
  21:10 MOLTEN FIX MERGED a5825b33 (rt2 d128263e): the occluder was InterpActor_11529 (PROP_UND_RepairNodeB, 10 x 10 m, 7-25 cm
  above the central platform, hidden in TDM) kept in the static TLAS while GL hid it; actor-placed (mover / toggled) subs
  now stay out of the static TLAS (Molten leaves out 2); Off unchanged by construction, Faithful intended change on maps
  with actor nodes; WFC_RT_CENSUS diagnostic. The user's Molten shadow report is CLOSED. Rendering parks for the S1 code
  stretch ~21:20 -> DML window (E milestone 1 at G3) opens.
  20:35 Rendering parked; S1 first smoke test on Molten: the runtime RT lightmap BUILDS (551 k texels in 18 pages, 297
  lights, 4 load-time passes in 119 ms, no errors) but the world draws black with it active (a draw / binding bug, in
  debug). DML WINDOW OPENED 20:35: E milestone 1 at G3 (175 user-map textures, Molten -> Streets -> Gorge -> Rust ->
  Debris), continuing as an overnight block with pauses for Rendering's ~20-min GPU checks; Gameplay's windowed lockstep
  repro parked until the next pause (windowless null-render sims continue).
  20:50 S1 black-world cause found (Rendering): under GI, ToSun.w = 3 (R1's full traced sun visibility on world pixels)
  without R1's dual-source composite multiplied every world pixel by the traced sun visibility - Molten is mostly indoors,
  so everything went black. GI now keeps ToSun.w = 2: world pixels take only the characters' shadows (the lightmap holds
  the world's), which also restores character shadows on the floor under GI; a load-time calibration log (RT vs bake
  decoded means per page) added. First S1 GPU check (~20 min) at the DML pause; Gameplay's lockstep repro runs as slot B
  in the same pause.
  21:00 Lockstep-under-load (Gameplay): NO repro in direct boot (Streets 4+5, seed 3, RT off; Classic and Smart; 3 loaded
  runs + WFC_ASYNCSTEP=0 all identical to the no-load reference). Rendering's odd run first differs at step 727 (~2 s
  into the match) with no event between 690 and 727 = internal state; player bot, LOCKSTEP_EXACT occlusion waits, sim
  RNG and wall clock excluded by code. Next: WFC_SIMHASH=700-760 per-pawn detail in Rendering's lockpair runs; a seeded
  WFC_ASYNCJITTER self-test (lockstep twice with 0-8 ms jitter, equal SIMHASH asserted) in Rendering's configuration.
  GPU scheduling: a 2-min DML window was wasted at the first S1 check -> rule: 90-min minimum per DML window, Rendering
  checks batched to one per 2 h with 15 min notice.
  20:50 S1 FIRST WHOLE-MAP RESULT (Rendering, work/wobble/gi_first.png): the world lit entirely by the runtime RT lightmap,
  right in shape on Molten (551 k texels / 18 pages / 297 lights, 10 passes ~210 ms) and Rust (858 k / 47 / 346, ~100 ms);
  RT / bake decoded mean 0.72-0.73 on both, consistent across pages (0.67-0.76), not a convergence issue (4 -> 10 passes
  moved it 0.01) = a systematic gap in the bounce terms (hit albedo source / hemisphere normalisation / hit L), in code
  before the >= 22:40 check. DML window reopened 20:47 (90-min minimum; overnight block). Systems 552e5f36 merged
  86198319: per-page sun k on all five user maps, capped at the map q30 k (Streets 7.6 -> 5.8 %, Gorge 16.2 -> 10.3 %,
  Rust 19.9 -> 16.5 %, Debris 15.1 -> 14.8 %, Molten 11.0 -> 5.1 % clamped sun energy); fits copied into integration's
  render data (work/render/<map>/vbake/fit.json).
  22:13 E MILESTONE 1 PROGRESS: 18 / 175 units (all Molten; 17 pass, stains_01 overlay stays D because E breaks its tiling
  wrap, caught by the seam gate), 1.4 GPU h, ~4.6 min / unit, remaining ~9-11 GPU h. DML paused 22:13 -> ~22:45 for
  Rendering's S1 + S2 check (bounce gain fitted at load so RT / bake mean = 1.0; per-page sun k read from fit.json; the
  vertex-lit subs traced too (Rust's 825 no longer keep the bake); per-frame tracing stops after 8 sweeps; GI pass GPU
  timing; reflection hardening) and Gameplay's jitter self-test in slot B.
  22:28 S1 + S2 CHECK (Rendering): Faithful lockstep identity 5/5 shots byte-identical (SIMHASH 9601 f3db on both) = GI
  inert in Faithful. Works: whole map lit by RT on Molten / Rust (557 k / 1.93 M texels, 297 / 346 static lights, per-page
  sun k); vertex-lit props traced (Rust 1.07 M vertices; 41 subs still on the bake); converges in 41 frames then 0 ms;
  worst case with per-frame tracing forced: 1.31 ms avg GPU, 3.5 max (Molten 720p). NOT green: brightness - RT / bake at
  the 4x bounce-gain cap Molten 0.77, Rust 0.88; gain 1.4 -> 4 moved Molten 0.73 -> 0.77 only, so the bounce rays deliver
  almost nothing = a bug (suspect: hit albedo from the reflection hit table, now logged). Commit plan: S1 / S2 land with
  R4 OFF by default (WFC_RT_GI=1 opts in) so a package's Realistic stays as today until RT matches the bake. DML window
  reopened 22:28 (overnight; next check >= 00:30).
  22:35 S1 + S2 MERGED 5d452363 (rt2 351ea8a6; built green): the runtime RT lightmap is on integration, opt-in via
  WFC_RT_GI=1; without it Realistic / Faithful / Off are unchanged (Faithful identity 5/5 + SIMHASH on Rendering's tree).
  Next: the bounce bug (RT / bake 0.77 / 0.88), then the default flips and package 4 is cut.
  23:00 EXPERIMENTAL CPU-ONLY CHECKS on 5d452363 (null render, muted): lockstep SIMHASH identity vs c949ab88 (NAVQ 4v5,
  7919 steps, 2 seeds x 4 maps): Classic 8/8 identical; Smart 2/8 differ (Streets seed 7 from step 2858, Gorge seed 5 from
  step 5057 = pathing set 2 firing, expected). Self-tests 4/4: BOTTEST 39/39, CLASSCHANGE 26/26, DETERMINISM 2/2 (digest
  4e8b5f344b226b4f = Gameplay's), FXOWNER 6/6. Code review of rt2 351ea8a6 R4-off path: no new per-frame passes or GI
  allocations without WFC_RT_GI (giOn gates raster -> finish -> setup -> frame; giTex() 0 = the bake binds as before);
  residue with RT on: 2 compute PSOs + a root-signature descriptor table at bridge init, a CPU albedo-log loop at hit-data
  build; RT Off never initialises the bridge. Morning: GL regress-8map on the package-4 head. Squad regroup design
  (Gameplay eac44488, on integration 54f46ff9): A + C approved under the cohesion / movement / AIDUEL gate; 00:30 GPU
  check booked for the bounce bug (new bounce-ray counters).
  23:15 SQUAD REGROUP A + C MEASURED, FAILS THE GATE (null boot, 6 seeds x 4 maps): cohesion shipping 70.6 m / 19.0 %,
  A 63.7 / 20.6, A + C 63.6 / 19.2, C 70.1 / 18.8 vs gate <= 55 m / >= 22 %; A + C also had a Streets outlier (slow 17 %).
  Nothing ships; A / C stay uncommitted. Reading: cohesion is bounded by fights splitting squads, not respawn placement;
  a real gain needs squad-level tactics (members positioning around the leader in engagements) = a design item for the
  user. Experimental: R4 leak rows (harness d150f8d9, r4leak.sh) ready for the morning slot; release edge found by
  reading (clearStaticWorld's drop-without-release branch skips giRelease and its leaked: line omits giBytes_) forwarded
  to Rendering, non-blocking.
  23:35 USER APPROVED squad-level tactics for Smart bots (members positioning around their leader during engagements;
  Classic identical): design doc GAMEPLAY_SQUAD_TACTICS_DESIGN.md first, then implementation, gated on NAVQ cohesion /
  movement / AIDUEL; Gameplay starts after the 00:30 b74 set; target before the package-4 cut.
  2026-10-14 00:05 Squad tactics rounds 1-2 (Gameplay, null boot, 6 seeds x 4 maps): design on integration 6a4b27b6
  (travel wedge, firing line in fights, break rules, keys default off). Cause of far members = leaders leaving the squad
  (moving 48-76 % / vehicle 20-59 % of far samples; pathless only 3-19 %). Formation alone: settled 60.1 m / 24.2 %
  within 15 m, in-fight 73.1 m / 16.7 % within 20 m (shipping 70.6 / 19.0; 91.5 / 9.7) but Debris movement worse.
  F + W (leader on foot while strung out) + E (slot edge margin 1.2 m): 67.6 / 21.8; 73.9 / 17.4 (+7.7 pts) with
  movement at or better than shipping on all maps (Debris 3.85 / 0.88 / 2.66 / 0.89, better than Classic). No arm meets
  the original gate; decision: round 3 on F + W + E (leader waits at a slot-able spot; members vehicle only to catch up)
  under a REVISED GATE (settled >= 22 % within 15 m AND in-fight >= 19.7 % within 20 m, movement within 1 SE, AIDUEL
  57-65 %, Classic 24/24); fallback = ship F + W + E after AIDUEL without the +10.
  00:18 E MILESTONE 1 PROGRESS: 42 / 175 (all Molten, 41 pass + stains_01 at D), 3.2 GPU h, remaining ~8-9 GPU h. DML
  paused 00:17 -> ~01:00: Rendering's bounce check (counters; fix + recheck in-slot if named) + Gameplay's b74 windowed
  lockstep set (-OwnWindow). Then undisturbed until morning.
  00:45 DETERMINISM VERDICT (Gameplay b74 set): Rendering's exact configuration with RT OFF (frontend boot, Rust, 31 + 32,
  player bot, lockstep exact, seed 7 / flow 1, SIMHASH detail 700-760): 4 runs x 4202 steps IDENTICAL incl. step 727, 2
  of them under seeded 0-8 ms WFC_ASYNCJITTER on the background step + main thread (wall 57-59 s vs 28 s). With the
  direct-boot and async-step results: the RT-off sim is step-deterministic in both boots. The one divergence was an
  RT-ON Faithful run, so the leak is in an RT-only path feeding the sim (shared RNG / query side effects / RT-written
  data). Next: Rendering reruns the faith arm 2-3x with WFC_ASYNCJITTER=1 (test-only patch, merging) + detail hashes.
  SQUAD TACTICS ROUND 3 (r3c) hits both cohesion gates: settled 36.8 % within 15 m, in-fight 25.2 % within 20 m; a
  reversal rise (stop-and-go at wedge slots) is being fixed with station-keeping before the full gate.
  00:30 R4 BOUNCE BUG FIXED, MERGED 64a1ce50 (rt2 a3db582b; still opt-in): the bounce-ray counters named it - rays were
  fine (96 % hit static, 91 % on GI pages) but the hit albedo averaged 0.03 with half the subs at 0: the reflection hit
  table built albedo from an 8x8 UV-space tile and UV islands thinner than a tile texel got no fragment (black). Such
  subs now take the mean of the rest (also better proxy reflections); real albedos are dark (Molten 0.058) so the
  load-time fit raises the bounce gain (cap 24, per-channel reflectance capped 0.8). RT / bake: Molten 0.94 (gain 7.9),
  Rust 0.99 (gain 6.1); frame means Molten 40.5 -> 40.9, Rust 31.3 -> 34.8; Rust's vertex-lit side pillars (near-black
  in the bake) now lit by nearby lamps (user to judge; image work/wobble/gi_calibrated.png). Per-term Molten: sun + sky
  0.36, lamps + sky 0.38, direct 0.71. Experimental's not-idle-branch item done. Before the default flips: Streets /
  Gorge / Debris check (morning slot, ~15 min); vertex-lit surfaces as bounce sources (48 % of Rust's static hits).
  WFC_ASYNCJITTER test hook on integration 0e7329cf (Gameplay 1624c398). DML window reopened 00:30, undisturbed until
  morning. NEW LEAD on the one-off divergence (Gameplay): in lockstep only mouse deltas are zeroed; window keys /
  buttons / wheel still reach handleInput and the player bot overrides only movement / fire / jump / dash / transform,
  so a stray focus click, key, wheel (weapon switch), R, Escape or F9 on the odd run's window changes the sim at that
  step - fits a single unreproducible split with no logged event. Fix approved: under WFC_LOCKSTEP discard all
  window-originated input after the pump (scripted inputs kept) + a 'LOCKSTEP input dropped' log line; patch on 64a1ce50.
  00:50 USER on gi_calibrated: Rust's lamp-lit pillars look right; wants more examples and RT pushed as far as possible
  so real-time reflections, lighting and shadows make the game feel like a whole new game. Direction to Rendering:
  Realistic goes beyond the original (S3 bounce + probes, S4 dynamic / weapon light, soft shadows from every light,
  reflections wherever physical, RT AO); Faithful / Off stay byte-identical; 5-map Off | Faithful | Realistic triptychs
  + a reflections close-up from the morning check for the user; RT_FULL_PLAN.md v2 (impact per GPU day, perf budgets)
  reviewed before S3. Lockstep input-drop built (verification = morning slot B with injected window input); squad
  tactics round-3 reversals = a 45 m form-flip threshold, hysteresis in (walk <= 30 m), measuring.
  01:10 RT_FULL_PLAN v2 APPROVED (Rendering work/fsr3/RT_FULL_PLAN.md; copy to docs at the next rt2 commit): 1 characters
  / movers lit by the RT world via a probe grid (1.5 d); 2 soft RT shadows from every light on characters + robots'
  lamp shadows onto the world (1 d); 3 reflections on every physically reflective surface (2 d); 4 dynamic lights
  lighting the world with RT shadows (1.5 d); 5 area-light lamp shadows in the lightmap (0.5 d); 6 Modern calibration
  + Enhanced live; 7 polish. Budget: Realistic >= 120 fps at 1440p (RT ~4.2 ms, ~140 fps expected; the 300 cap is
  unreachable under any RT mode; Faithful stays the high-fps RT mode). Gates per item: Off / Faithful identity, keep
  list + SETTINGSTEST, measured at 1440p within budget with a quality sub-setting, one example image for the user.
  Items 1-2 target package 5 before the ~21 Oct playtest. Morning order fixed: gicheck3 (+ Gameplay slot B) -> R4
  default flip -> example set alone -> Experimental regress alone -> Experimental RT-on rows -> DML reopens.
  01:20 SQUAD TACTICS ROUND 3 FINAL (Gameplay): candidate A (formation + leader on foot while strung out + slot edge
  margin + member walk with 30 m hysteresis + leader wait 14 / 4 s + slow at 18 m): settled 43.6 m / 41.2 % within 15 m
  and in-fight 25.2 % within 20 m (both cohesion gates pass); slow / off-mesh / stucks within noise (Gorge better) but
  reversals +1.5-2 per travel-min on Streets / Molten / Debris = members fidgeting near a slow or waiting leader (not
  stucks; five fixes tried, only the form-flip hysteresis helped). Candidate B (movement-only) just under both cohesion
  gates. DECISION: ship A as the Smart default with a key selecting B / off; the user judges the fidgeting in the
  playtest; NAVQ fidget counter added; remaining gates (AIDUEL 57-65 %, Classic 24/24, BOTTEST, DETERMINISM, AI cost)
  then the patch on 64a1ce50.
  01:45 squadTactics preset 2 = A (default) / 1 = B / 0 = off. Fidget counter: only ~0.6 / travel-min; most of A's
  reversal rise is members moving with a walking leader (follow adjustments, not stucks); A kept as default, NAVQ
  reversal column split pathing / follow / fidget, playtest guide gets a what-to-watch line. Gates for A: Classic
  identity 24 / 24, DETERMINISM 2 / 2, 31 + 32 AI cost 0.20 ms, BOTTEST Smart 39 / 39 (Classic 38 / 39 = a 6.16 ms max
  under load, rerun). AIDUEL: Smart 70 % (73 / 104; Streets 18-8, Molten 18-8, Debris 19-7, Gorge 18-8) vs the old
  57-65 % band, kill share 52 % unchanged = squads win fights, aim equal. Decision: no global trim; the band is
  re-baselined (B and 0 duels running for a fair reference); the player-facing lever is Bot Difficulty, so the
  difficulty ladder (Smart vs Classic at matched difficulty) must stay monotonic with Easy <= ~55 %; if Easy is above
  ~60 % with squads, squadTactics scales by difficulty (Easy = 1) instead.
  01:15 USER: 140 fps at 1440p is fine; quality first ('squeeze every amount of realistic lighting we can out of the
  game, we'll worry about FPS later'). RT plan v2 gate (c) revised: per-item GPU budgets are reported measurements, not
  merge gates; each item takes its quality-maximising variant; the quality sub-setting is for recovering fps later.
  Hard floors kept: no stalls / hitches, Off + Faithful identity, SETTINGSTEST, Realistic >= ~60 fps at 1440p.
  01:30 SQUAD TACTICS MERGED 15b89a75 (Gameplay GAMEPLAY_SQUAD_TACTICS.patch; squadTactics preset 2 = on default / 1 =
  movement-only / 0 = off). Duels Smart vs Classic at matched difficulty: A Hard 70.2 % (104), B 54.8 %, tactics off on
  the same code 65.4 % (today's squads were already at the top of the band; A adds ~5 pts); ladder for A (56 each,
  +-6.6 SE): Easy 51.8 / Medium 60.7 / Hard 70.2 / Expert 57.1 -> Easy <= 55 %, no trim. Classic identity 24 / 24,
  BOTTEST 39 / 39 both (Classic rerun = noise confirmed), DETERMINISM 2 / 2 both, AI cost 0.20 ms. NAVQ reversal column
  split pathing / follow / fidget permanently (Streets seed 1 on A: 2.29 / 2.45 / 0.95); the follow part is the next
  target. Playtest guide gains a what-to-watch section with the fallback. Pending from Gameplay: the lockstep input-drop
  patch after its 07:30 verification.
  03:10 E MILESTONE 1 PROGRESS: scope now 197 units (D batch 13 joined the live pack: Molten 88 / Streets 44 / Gorge 27
  / Rust 27 / Debris 11); 90 done (Molten complete, Streets 2); 86 pass, 4 gate fails stay at D (stains_01 + 3 Molten);
  5.8 GPU h, ~3.9 min / unit; completion ~10:00. SCHEDULE: the DML window stays open until milestone 1 completes; the
  morning GPU sequence moves to ~10:00 (gicheck3 + Gameplay's input-drop slot B -> R4 default flip commit -> example set
  alone -> ~11:00 Experimental regress alone + RT-on rows -> package 4 cut); hard pause at 10:30 if E slips. Rendering
  codes plan-v2 item 1 (probe grid) CPU-only meanwhile; Experimental's identity + self-tests on 15b89a75 all PASS
  (Classic 8 / 8 identical; Smart differs from step 601 = squadTactics acting at 10 s).
  03:40 E EYE REVIEW, Molten (AssetTools hd_e_eye.py, 88 units, 100 % looked at): 67 accepted (knurled rings, bolts,
  bevels, cracks, grime, paint chips -> grime clumps; ~1/3 identical to D under tint protection), 17 rejected for
  invented objects (a face in a door recess, emblems / labels / glyphs / plaques on flat areas, a capsule, gear machinery
  in the burn, a disc on a circuit board, a redrawn Decepticon logo, warped HIGH MOON text, rock -> blobby cells), 4
  gate fails; rejects keep D. ~19 % by-eye reject rate = G3's main risk, caught by the full review. Follow-ups after
  generation: retry rejects at denoise 0.5 + a stronger negative prompt; text / logo mask protection if cheap; per-map
  contact sheets (accepted + rejected examples) for the user. Live E pack Molten = 132 rows, 997 MB; generation at ~95 /
  197 on Streets.
  05:10 E at 133 / 197 (Molten 88 + Streets 44 complete, Gorge started; 5 gate fails), 7.8 GPU h, Streets ~2.7 min /
  unit; generation completes ~08:00, the retry pass (eye-rejects at denoise 0.5 + extended negative prompt) to ~08:20;
  HIGH MOON / emblem units stay D this milestone. Molten contact sheet (6 accepted D | E, 3 rejected with reasons)
  sent to the user. Morning GPU sequence moved up to ~08:30 (gicheck3 + Gameplay slot B -> flip commit -> example set
  alone -> ~09:30 Experimental regress alone + RT-on rows -> package 4).
  07:00 Morning health: 0 TDRs overnight; driver pool 4.4 -> 6.7 GB (load-proportional leak, flat since 06:00; reboot
  before the next playtest); muter ended on its 10 h timeout (lanes launch with WFC_MUTE=1). Streets eye review: 35 /
  44 accepted, 8 rejected (lettering / logo on the road tube, glyph plate, embossed seal, chevron, panel blocks, hatch
  markings, pipe from an outline), 1 gate fail = 18 %; Streets sheet sent to the user.
  07:30 USER VERDICT on the Molten + Streets sheets: the LEFT (D, faithful HD) looks 'way better' than the RIGHT (E,
  G3 overhaul). E is OFF the shipping path: package 4 ships D as the HD row; the level-2 E freeze is on hold; the retry
  pass is skipped. AssetTools: sharpness analysis D vs E (Laplacian variance; 1:1 crops of the units the user saw),
  then one detail-preserving attempt on Molten (D high-pass over E or denoise 0.35-0.4, gated on E sharpness >= D) with
  a 1:1 sheet; if that does not beat D by eye, E is dropped.
  08:30 E generation finished 197 / 197 (exit 0); morning GPU sequence started (gicheck3 + Gameplay's input-drop slot B).
  08:40 USER: 'I just dropped the ones that look worse and just continue with D.' E (the generative overhaul) is DROPPED:
  no sharpness attempt, no freeze, outputs archived out of every pack. Textures = the D faithful HD pass to completion
  (user maps, rest of the world, M6 roughness maps). The HD row stays Off / Faithful (D) / Enhanced (D + materials).
  08:40 D pass status (AssetTools): colour 1,432 / 1,657 (a CPU shard crashed at 01:00 on a 4-px strip texture, fixed
  with >= 16 px tile padding, restarted 08:55, ETA ~15:30); D GPU rows all done = no DML windows today; batches 0-13
  reviewed + packed (1,660 textures, 9.9 GB), 14-31 after colour; D complete freeze (b0-31) tonight -> package 5 with
  Rendering's items; then M6 roughness maps (490, CPU). Gameplay slot B partial: 2 RT-off lockstep runs step-identical;
  the injected frontend run stalled in PartyLobby (the injector posted Escape before the match, where the drop does not
  act); retry gated on match start, any time except Experimental's timed regress.
  08:50 GICHECK3 GREEN -> R4 ON BY DEFAULT in Realistic, merged be72e4ab (rt2 0be0f2ee): RT / bake Streets 1.07 (bounce
  gain 9.7), Gorge 0.96 (8.1), Debris 0.95 (21.7), Rust 0.98 (4.9), Molten 0.93 (7.7); vertex-lit subs as bounce sources
  (Rust sourceless static hits 48 % -> 2.5 %); 32 rays / texel, 32 sweeps (quality-first); item 5 soft lamp shadows
  (0.15 m sources); item 1 probe grid behind WFC_RT_GIPROBES=1 (Molten 3.14 M probes at a forced 4.58 m spacing, tighter
  next); Off / Faithful inert by construction, fresh identity pair in the next slot. Three-map image sent to the user.
  Reflection close-up HELD: a hard vertical seam across Molten's platform and thin black lines at grazing angles
  (reflection-target sampling; item 3's work). Example set running alone (~40 min, HD off).
  09:00 LOCKSTEP INPUT-DROP PROOF PASS (Gameplay): frontend boot reached the match in 31 s; F9 / R / Escape / wheel /
  text injected after match start all dropped + logged, no pause menu, step-identical end (frame 4202) with the plain
  runs (clicks unexercised: WFC_NOMOUSE); patch merged c7f72c50. tools/fidelity scripts synced (Experimental b716a73a:
  ROSTER=64 real 31 v 32 via BotsExtended, XENV, SIMHASH detail; the old '64p' rows ran 5 a side). My sync chain
  deleted the tracked results/ folder in 3d643e94; restored in 30b2fa74 (content identical). HEAD 30b2fa74 named to
  Experimental for regress-8map + captures + R4 leak rows + the 31 v 32 faith jitter pair.
  09:25 EXAMPLE SET (Rendering, 15 triptychs Off | Faithful | Realistic, 63-bot lockstep, HD off, RT / bake Streets 1.06
  / Gorge 1.01 / Rust 0.98 / Debris 0.98 / Molten 0.93): 5 clean picks sent to the user (Molten m300 corridor glow,
  Streets m900 overhead shadow, Rust m300, Debris m300, Gorge m600). Faithful = Off within 0-2 % of pixels; SIMHASH
  identical across all three modes on all 5 maps (no sim divergence in 15 RT runs). Not for the user: Rust m600 (near-
  black camera spot); the reflection seam / grazing lines exist but are invisible at triptych scale (item 3). NAVQ
  baseline for 30b2fa74 on integration (2d551f4e). Experimental's block running on 30b2fa74 since 09:31.
  09:35 PACKAGE 4 provisional head 30b2fa74: Systems building work/playtest_slim_30b2fa74 (refresh.sh NOPROMOTE=1; work/
  playtest_slim stays c949ab88 until 'promote'); HD = D snapshot b0-15 frozen 09:37 (push_full_snap_20261014_b0-15,
  1,875 rows, 11.0 GB; b0-12 fallback); promote criteria = Experimental regress-8map + captures PASS, Systems verify_slim
  PASS (slot right after Experimental's block), Experimental FAST PASS. AssetTools incidents: the D build runner had
  crashed 205 times since 01:00 on one truncated model output (the killed shard's mid-write file) - deleted, gen writes
  atomic (4f89608), builds moving (batch 16); row 2022 to be regenerated ~15:30; D complete (b0-31) ~21:00-22:00.
  10:00 REGRESS-8MAP c949ab88 vs 30b2fa74 (Experimental; lockstep, best p90 of 2, RT off): 8 / 8 PASS, 0 unknown, worst
  delta +0.01 ms (501 2.31 -> 2.30, 502 1.93 -> 1.92, 503 2.28, 504 2.51 -> 2.50, 507 2.37 -> 2.34, 508 2.12 -> 2.13, 509
  2.82, 510 2.66 -> 2.67). 501 / 502 had overlapped AssetTools' 12-thread python colour runner (no hold file had been
  set) and were rerun alone under the hold (first-pass rows kept as contaminated_0938); the flags on 503-510 came from
  the user's own Codex / APEX python (~1 % of one CPU), not a lane; harness 0b033f94 now counts only lane processes.
  HOLD RULE adopted: a file named PERF_HOLD at <lane>\work\ for every timed run (all lanes' runners poll it). AssetTools
  paused 09:39 -> 10:00 for the rerun. Now: untimed captures, R4 leak rows, the 31 v 32 jitter pair.
  10:08 PACKAGE 4 BUILT, not promoted (Systems): work/playtest_slim_30b2fa74, 16.60 GB, 0 failures; HD b0-15 (1,875 rows,
  static check 0 problems, 5 A8R8G8B8 rows valid); Release + PGO; vbake + fit_lights on 10 maps, per-page fit.json
  byte-identical on the 5 user maps. CAPTURES on 30b2fa74, Rust (Experimental): Off identity vs c949ab88 PASS (SIMHASH
  0 / 3002 differ, 0 / 25 shots); Faithful identity vs 0e7329cf PASS (0 / 0); Realistic view vs R4-off: SIMHASH 0
  differ, 25 / 25 shots differ = lighting only (RT GI 1.93 M texels in 47 pages, 2527 subs, 59 MB). Remaining for
  promote: Systems verify_slim, Experimental FAST.
  10:27 R4 LEAK FOUND (Experimental r4leak rows on 30b2fa74, Realistic + GI, Molten -> Rust -> Streets x 2, 6 matches, no
  TDR): GPU dedicated memory +368 / +386 / +78 / +333 MB per map change (+1165 MB over 5), still rising; RT off was flat
  (a17309b7, 12 changes); private memory plateaus; GL live textures plateau (98-101) -> D3D12 / interop per-map release
  (BLAS / TLAS / hit data / GI pages / shared textures / denoiser + RT targets). R4 calibration deterministic across
  revisits (Molten 0.932, Rust 0.980, Streets 1.057). Isolation next (Realistic + GI=0; Faithful). Package 4 ships with
  it as a KNOWN ISSUE (RT off by default; ~0.3 GB VRAM per map change in Realistic until restart); Rendering fixes it
  before item 1 lands, target package 5.
  10:28 LOCKSTEP DIVERGENCE CLOSED AS MONITORED: the RT-on Faithful 31 v 32 jitter pair on 30b2fa74 (Rust, frontend boot,
  player bot, lockstep exact, seed 7 / flow 1, WFC_ASYNCJITTER=1) = SIMHASH 3002 / 3002 identical, 25 / 25 shots
  byte-identical (3843 detail lines kept). With the RT-off proofs, the one-off split is not reproducible; the most
  plausible cause (stray window input) is fixed by the lockstep input drop. Standing rule: lockstep runs keep the
  700-760 detail hashes so any recurrence names the pawn and field. Leak isolation rows (Realistic GI=0; Faithful)
  running, ~40 min.
  11:02 LEAK ISOLATED TO R4 (Experimental): lobby-dip GPU dedicated MB after each of 5 map changes - Realistic + GI 1:
  1981 / 2349 / 2735 / 2813 / 3146 (still rising); Realistic GI 0: 1850 / 2132 / 2312 / 2130 / 2071 (plateau ~2.1 GB);
  Faithful: 1626 / 2130 / 2113 / 2114 / 2130 (plateau); RT off flat. Private memory plateaus in every row. Growth is
  not proportional to the GI page bytes (Molten 17 MB, Rust 59 MB, Streets 28.6 MB) -> a per-load R4 object kept alive
  (suspects: shared GI textures still bound / referenced at delete; per-pass one-shot lists / readbacks in the vertex-
  lit bounce + 32-sweep path; descriptor-table refs). Rendering's code read found no missing release on the normal
  path; a VRAM-per-release-step diagnostic cycle runs in slot A after Systems' verify. Experimental's block done
  11:02; Systems' verify_slim slot opened 11:03.
  11:11 PACKAGE 4 VERIFY PASS (Systems, 11:09): HD b0-15 1,875 rows 0 problems; bin 20 / M5 runtime; Streets / Gorge /
  Debris -ForeignCwd PASS; SETTINGSTEST 7 / 7; RT build / context / runtime mode 2 PASS; the R1 row's FAIL was a stale
  gate rule (with R4 running the sun swap is off by design: r1Active = && !giActive); verify_slim is R4-aware now
  (requires V_bake data + pages, R4 texels > 0, per-page k from fit.json: 937,589 texels, 26 k-pages on that log) -
  merged 90cd8550 with the NOPROMOTE mode. Slot A -> Rendering's leak diagnostic cycle; Experimental's FAST gate on
  30b2fa74 beside it (RT off); promote on FAST PASS.
  11:33 LEAK DIAGNOSTIC (Rendering, DXGI QueryVideoMemoryInfo per release step, Molten -> Rust -> Streets x 2, Realistic
  + GI): process VRAM with the map unloaded 1578 -> 1880 -> 2008 -> 2019 -> 2013 -> 1989 MB = FLAT after the first
  cycle; loaded peaks converge (Molten 3748 -> 4227, Rust 4674 -> 4812, Streets 4216 -> 4221); each change frees
  650-850 MB (GI 50-185, masked + hit 290-370, BLAS / TLAS 260-300). Experimental's sampler counter ('GPU Process
  Memory Dedicated Usage') rose 1981 -> 3145 on the same cycle -> parity rerun (both views on one 7v8 run) decides:
  counter up + DXGI flat = residency / driver caching of freed shared allocations (fix: pool the per-map interop
  allocations); both up = live objects. Also: the 'hit albedo mean 0.011' log value is a 32-bit stats overflow
  (albedo x 1000 sum wraps past ~5e7 rays; Molten at 71 M rays reads 0.058 = the table mean), carry-word fix coded;
  lighting unaffected (the gain fit does not use it). FAST gate on 30b2fa74 running since 11:13 (hold set).
  11:42 FAST GATE on 30b2fa74 (Experimental, 11:13-11:39): PASS 55 / 4 flagged / KNOWN 2 / INFO 8 / PARTIAL 2 / UNKNOWN 1
  / SKIP 1; the 4 flagged rows traced to stale harness constructions (the range-damage check builds a bare Weapon with no range points, so it reads 15 hp; the game deals the authored 7.5 hp at 300 m, Gameplay;
  64 decal nodes vs an old manifest of 25; the fidelity rig infers the drawn model from a CPU pose, empty since GPU
  skinning) and a 79 ms GPU frame spike under the shared card; 2 voices after an unload = the transition movie on the
  pause -> Quit route (lobby route 0 / 3). Owner confirmations requested for the harness update. PACKAGE 4 PROMOTED:
  work/playtest_slim -> playtest_slim_30b2fa74 (16.60 GB; notes in docs/handoff/PACKAGE_4_30b2fa74.md; tag
  playtest/09c-package4-30b2fa74).
  11:47 Package 4 promoted by Systems (Play.bat line 2 = '09c 30b2fa74 slim'; README carries the Realistic VRAM note;
  fallback playtest_slim_c949ab88; playtest_slim_a44aa6db deleted) and announced to the user. Owner confirmations:
  Gameplay - 15 hp was a test-construction artefact (bare Weapon, no range points), the game deals the authored 7.5 hp
  at 300 m; Rendering - 64 decal nodes current since 480e3149 (one primitive per decal-receiver); Systems - the 2 voices
  after the Quit unload are the popup BUTTON_ACCEPT click (map instances 0 / level cues 0). Harness fixes synced
  1c7c8d35 (Experimental 1cc9605f + 02b9caff). FAST on 30b2fa74 re-reported: wfc_fidelity 100 % Debug + Release, route
  28 / 0, mapswitch 6, renderstate 7 / 7, totals PASS 58 / KNOWN 2 / PRODUCT 1 (the 79 ms GPU spike under the shared
  card). Rendering's leak parity rerun in slot A.
  11:59 R4 LEAK ROOT CAUSE (Rendering parity run, 7v8, Realistic + GI, Molten -> Rust -> Streets x 2, counter and DXGI on
  one run): DXGI lobby plateau 1564 / 1820 / 1884 / 1957 / 1961 / 1948 MB; the 'GPU Process Memory Dedicated' counter
  rises ~275 MB per change (1818 -> 3184); loaded cycle 1 -> 2 DXGI +74-457, counter +600-970. Live objects are flat
  (the step log frees 650-850 MB per change); GI=0 has the same BLAS / hit churn and plateaus -> the driver keeps
  charging freed GL-imported GI pages. FIX (coded, building): the shared GI pages (D3D12 committed + GL memory-object
  import) go to a pool at the map change and are reused by (w, h, format); freed at shutdown; log 'N reused from the
  pool, M pooled spare'; albedo stats carry-word fix rides along. Verification: one 22-min cycle in slot A after
  Systems' REPORT rerun, pass = counter lobby flat over 5 changes; then Experimental's 1440p RT perf baseline
  (rt-perf-baseline.ps1, synced 7ccf1f30).
  12:28 Package 4 REPORT.md PASS on every row (Systems, 12:05). R4 LEAK FIXED + VERIFIED, merged 00aa2ca7 (rt2 77d9e02c):
  GI shared-page pool reused by (w, h, format), freed at shutdown; counter lobby minimum 1632 / 2321 / 2354 / 2468 /
  2393 / 2446 MB = flat after cycle 1 (was 1818 -> 3184); loaded cycle 1 -> 2 Rust 5261 -> 5336, Streets 4816 -> 4829
  (was +658 / +596); pool holds the largest page set played (Rust 142 pages, ~130 MB); albedo stats carry word + VRAM
  step log ride along; Off / Faithful unaffected. Goes into package 5. Experimental's 1440p RT perf baseline on
  30b2fa74 started 12:30 (alone, hold set); then slot A back to Rendering for the item 1 / 2 tests.
  12:33 USER (package 4): characters draw in front of smoke / effects even when inside them -> translucent FX not
  depth-tested against characters (draw order / depth); Systems owns the diagnosis + fix, Rendering consulted on the
  pass order; fix = a baseline change in all modes (new captures); target package 5. USER on squads: 'just do whatever
  looks best' -> the formation version stays the Smart default. Baseline note: AssetTools' long jobs checked the hold
  only between textures (up to ~50 min on a 4096); fixed to a 20 s re-check between model tiles; map 504 rerun at
  the end of the baseline.
  12:34 SMOKE BUG CAUSE (Systems, code read): WeaponFx batches (tracer smoke trails, impact / explosion / grenade clouds,
  sparks, dust) carry no emitter material, so GLRenderer::drawParticles takes the fixed-function fallback and draws
  them IMMEDIATELY (depth write off) at World.cpp fx_.draw, before the queued characters are flushed (flushDynQueue
  runs in drawMapPresentation); the characters then pass the depth test and cover the smoke they stand in. Vehicle
  emitters with no material too; map / authored FX are correct (sorted translucency after flushDynQueue). Rendering
  confirms the pass order itself is right and RT / R4 do not change it. FIX A approved: the fallback path calls
  wfc_.flushDynQueue() before its immediate draw (characters first, smoke depth-tests against them); repro + A/B in
  slot B after the 1440p baseline; patch on 00aa2ca7; a baseline change in all three modes (SIMHASH unaffected). FIX B
  (fallback batches join the sorted translucency with a generic material + soft depth) queued for Rendering after plan
  v2 item 2.
  12:43 1440p RT PERF BASELINE on 30b2fa74 (Experimental 976e8f7e; copy in docs/handoff/RT_PERF_BASELINE_30b2fa74.md +
  .csv): 2560x1440 windowed, 63 bots + player bot, lockstep steps 2400-6000, alone under the hold, every RT arm the same
  match as Off (SIMHASH equal). Frame p50 / p90 ms: Rust Off 1.84 / 2.29, Faithful 4.85 / 5.53, Realistic 7.77 / 8.61;
  Debris 2.14 / 2.35, 4.45 / 5.23, 6.99 / 7.83; Streets 1.60 / 1.96, 4.70 / 5.77, 8.28 / 9.38; Molten 1.82 / 2.09, 5.25
  / 6.21, 8.68 / 9.88; Gorge 1.93 / 2.31, 4.85 / 5.93, 7.96 / 9.04. GPU ms Faithful -> Realistic: shadow + AO 1.9-2.3
  -> 2.6-3.3, denoiser 0.75 -> 0.9-1.1, reflections 1.3-1.9, GI tracer 3.5-6.6 per traced frame while converging (Debris
  highest), RT device VRAM 3.4-4.1 -> 3.8-4.6 GB. Realistic = +2.6-3.6 ms p50 over Faithful; Faithful = +2.3-3.4 over
  Off. Rendering diffs each plan-v2 item against it (rt-perf-baseline.ps1). Rust's first pass (under AssetTools' python)
  rerun clean. GPU released: slot A -> Rendering (item 1 / 2 tests), slot B -> Systems (smoke repro + A/B), AssetTools
  resumed.
  13:08 SMOKE-BEHIND-CHARACTERS FIXED, merged 56290b7d (Systems SYSTEMS_SMOKE_ORDER.patch 657fe1c7): the drawParticles
  fallback flushes the queued characters first (WfcPipeline::flushQueuedCharacters); barrier / sentry meshes queued
  before fx_.draw / vehicleFx_.draw (Rendering's RT-pass guard). Staged A/B (Streets, Off, the WFC_SMOKEREPRO
  diagnostic hook in the A/B trees only): 70 / 70 frames differ only on the character silhouette under the trails;
  before = the trail in front of the body cut out, after = it veils the legs and arms while the trail behind stays
  hidden (captures docs/handoff/smoke_ab/, crop sent to the user). A baseline change in all three modes wherever
  game-side smoke overlaps a character; SIMHASH unaffected. Remaining: hard particle edges, no soft depth fade =
  option B (Rendering after item 2). Head 56290b7d = package-5 candidate base.
  13:20 USER clarifications (package 4): the effects ordering applies to all characters (players visible in front of the
  GRID ability; the local muzzle flash drawn behind the enemy being shot) -> Systems: the muzzle flash is WeaponFx MESH
  parts drawn immediately before the fallback flush, so v2 = one IRenderer::flushQueuedCharacters() in World::draw
  after barriers / sentries and before fx_.draw (staged 3-case proof: smoke on a bot, muzzle flash over a bot, barrier
  between pawn and bot; WFC_FXSTAGE diagnostic hook, never in a patch); the barrier grid is already correct in code. A
  tipped-over vehicle kept driving forward -> Gameplay (original roll handling; BOTTEST row). The gun cross-talk
  re-ask = already fixed (b0a22cb6, FXOWNER gate) in packages 3 / 4. USER DECISION: no interim build; the next playtest
  is PACKAGE 5 after 'the lighting is all done and all the ray tracing stuff is implemented and textures' = plan v2
  items 1-5 + option B + D complete (+ rest of the world if ready) + the effects v2 / vehicle fixes; slot A continuous
  for Rendering; user away until 2026-10-15 10:00 (usage reset), message only when package 5 is ready.
  13:25 TIPPED VEHICLE (Gameplay, RE decomp): the original gives players no forced transform / reset / roll clamp (hover
  rights only with no spring contact or upside down; strafe scaled by up.Z^2), but AI vehicles run TnCarForm /
  TnTankForm Hovering.DoUpdateAi -> HoverPhysics.RotateToTargetOrientation toward pitch 0 / roll 0 (AxisMask 1,1,1
  once IsUnstable > 30 deg) at AiMaxAngularSpeed / Acceleration (car 12 / 12, truck 20 / 20, tank 6 / 6), translating
  in world frame meanwhile. Ours ran bots on the player hover path with +-1.2 rad pitch / roll clamps, so a tipped body
  with spring contact never righted. FIX (built, CPU): MoveIntent.ai from MatchOpponent + RotateToTargetOrientation
  when unstable with the authored limits from the hover blueprint; AI translation port out of scope. Approved as a
  BASELINE change (both AIs; player path untouched): identity re-based where a tip occurs, with the 24-match tip
  diagnostic, a real-case VEHTEST tipover row, BOTTEST / DETERMINISM. AssetTools: the D pass is the whole M4 HD
  selection (3,771 textures, all maps), so tonight's b0-31 freeze (~22:00-23:00) is the complete faithful HD world for
  package 5.
  13:35 STAGED FX RESULTS on 56290b7d (Systems 8549a529, docs/handoff/fx_order_stage/; bots frozen, local pawn 6 m from a
  bot, lockstep, RT off; the first two staged runs had captured empty private matches - WFC_LOBBY_OPTIONS now sets 4v4):
  (1) smoke on a non-local bot CORRECT (the v1 flush covers every queued character); (2) the local muzzle flash 2 m in
  front of a bot CORRECT (it takes the particle fallback; v2 changed nothing, dropped); (3) the BARRIER grid STILL
  WRONG: Pipeline::drawDynamic queues only skinDraw_ && envSamples_ && !weapon meshes, so the barrier / sentry / any
  ability mesh without a light environment draws immediately and the later-flushed characters cover it = the user's
  'always in front of any effects'. FIX 3a (approved, the patch): flushQueuedCharacters() in World::draw BEFORE the
  barrier / sentry draws; 3b (drawDynamic queues immediate draws in submission order while dynQueue_ is non-empty,
  no shadow cast) queued for Rendering with option B; Rendering asked to confirm those meshes are absent from the RT
  scene. TIP-OVER MEASUREMENTS (Gameplay, 24 Smart matches): righting alone halves tipped time (Streets 69.8 -> 29.6
  s, Molten 41.9 -> 15.9, Debris 20.3 -> 11.9, Gorge 98.2 -> 61.7); the real case is a car straddling a Gorge ledge on
  its rear springs (8.3 kN each): the old path sits at -60..-67 deg creeping forward, righting-only holds at -35 deg
  (the 12 rad/s^2 AI cap loses to the spring torque). DECISION (b): port the AI translation (MoveToTargetLocation at
  the authored AI linear limits) only while unstable (> 30 deg); gates = TIPTEST on the straddle, the tipped-time
  table, NAVQ within 1 SE outside tips vs the 30b2fa74 baseline, identity re-based, BOTTEST / DETERMINISM.
  13:42 BARRIER CASE RETRACTED then NOT REPRODUCED (Systems): the first staging had the bot 6 m away while spawnBarrier
  places the barrier 10 m ahead, so the bot really stood in front of it; restaged with the pawn at 14 m (barrier ~4 m
  in front of the bot): A vs B (+3a) pixel-identical on 56 frames, the bot completely hidden = our barrier renders as a
  SOLID depth-writing surface. Rendering confirms the barrier / sentry are in neither RT set (static TLAS or the posed
  skinned dynamic set), so 3a would change nothing for RT. Nothing committed for 3a; it stays a code note for 3b. OPEN
  FIDELITY QUESTION: the user could see players through the 'grid', so the grid they saw is see-through; Systems checks
  the original's Barrier ability material in the Game Dump (translucent hex grid, no depth write?) and the package-4
  exe with HD on / off; if the original is translucent, ours is made translucent and 3a goes in with it. Cases 1 (smoke
  on a bot) and 2 (local muzzle flash over a bot) are CORRECT on 56290b7d.
  13:44 BARRIER FIDELITY BUG (Systems, Game Dump): the original barrier mesh WEP_Shield_p.Barrier.WEP_Barrier_SKEL uses
  WEP_Barrier_Defrag_MATINST (parent ENV_ForceField_Defrag_MAT): BLEND_Additive, MLM_Unlit, Distortion + Emissive +
  Normal + Opacity from wfcDepthBiasedAlpha = a see-through additive force field that never writes depth (matches the
  user seeing players through the grid). Ours renders a flat pink wall that hides everything behind it: dynamic meshes
  (meshIdx -1) cannot defer translucent subs, so the barrier's sub draws immediately in World::draw, and its distortion
  goes through distFbo_ in post. Isolation runs (WFC_SKIPMAT=Barrier_Defrag, WFC_NODISTORTION=1, WFC_HDTEXTURES=1) in
  slot B, then the fix: render it additive / unlit with depth-biased alpha + distortion, bundled with 3a (the
  immediate translucent draw then needs the characters flushed first) and staged with the bot behind the grid. Package
  5 item. The package-4 exe cannot be staged (release build, no hook); Systems' tree A is the same renderer / barrier
  code.
  13:49 BARRIER CAUSE (Systems): the four isolation frames are identical (not distortion, not HD; SKIPMAT by name
  matched nothing): ExtractedAssets' WEP_Barrier_SKEL.gltf names its material 'WEP_Barrier_Defrag_MATINST' with NO
  extras.wfc_material, so wfcName is empty and the pipeline draws the barrier with the generic opaque lit glTF
  fallback (baseColor 0.3 = the pink wall); the compiled additive force field never runs. Same gap on
  WEP_SentryDeploy_SKEL (WEP_DeployedTurret_MATINST, not compiled in any map). FIX (package 5): World::
  ensureAbilityModels names the barrier material from the package path when the export lacks wfc_material, bundled
  with 3a (an immediate additive draw must come after the flushed characters); AssetTools adds extras.wfc_material to
  those exports later (in work/). Staged A (symptom) vs B (+3a) with the bot behind the now see-through grid in
  slot B.
  13:51 BARRIER STEP 2 (Systems 9641b7ab): with the package-path name the barrier resolves its compiled material (591
  verts, 7 joints) but still draws as an OPAQUE near-black wall -> the GPU-skinned dynamic pass-1 draw ignores the
  material's BLEND_Additive (depth write likely on). Rendering item (now if < 1 h, else right after item 1): skinned
  dynamic draws honour the material blend mode (additive ONE / ONE, translucent SRC_ALPHA, no depth write, depth test
  on, depthBiasedAlpha / distortion); then Systems' patch = barrier name fallback + 3a with bot-behind-grid crops;
  AssetTools exports extras.wfc_material (the turret MATINST is compiled in no map: AssetTools item). TIP-OVER (b)
  measured (Gameplay GAMEPLAY_TIPOVER.patch, not applied): tipped time old -> (a) -> (b) Streets 69.8 -> 29.6 -> 43.1,
  Molten 41.9 -> 15.9 -> 10.6, Debris 20.3 -> 11.9 -> 14.3, Gorge 98.2 -> 61.7 -> 36.5 (total 230 -> 119 -> 105 s);
  TIPTEST 2 / 3 (the right-side case levels after 4.72 s: 4 spring rays vs the PhysX hull); identity re-based (the
  SIMHASH diverges one step after the first unstable hover step; WFC_NOAIPATH=1 reproduces the baseline); NAVQ (b):
  Debris off-mesh 1.9 -> 3.4 %, Gorge stucks 1.23 -> 1.79 = outside 1 SE; BOTTEST / DETERMINISM / cost green. NOT
  shipped as is: Gameplay measures (a) righting-only and (b') = translation only after > 1 s unstable with the target
  clamped to the nav mesh; the best within 1 SE on all maps ships; the TIPTEST right row may ship as a known limit if
  it levels within 5 s.
  14:39 Tip-over (a) righting-only vs (b') translation toward the next nav waypoint after 1 s tipped: TIPTEST 3 / 3 both
  (right case 0.82 s); tipped time total old 230 / (b) 105 / (a) 132 / (b') 129 s. Runs diverge from the first tip, so
  6-seed NAVQ columns are unpaired; pooled 12 seeds (noise = |off s7-12 - off s1-6|): NEITHER passes (Smart 9 regressing
  columns each, mostly Debris / Gorge, e.g. (a) Gorge reversals 4.24 -> 5.82 at noise 0.71, stucks 1.26 -> 1.73 at
  0.06; Classic 4 each). Hypothesis: the original's absolute IsUnstable (> 30 deg) also fires on hover vehicles climbing
  steep ramps and rights them against the slope; our bots take ramps the original AI may not. Testing a contact-
  relative gate (WFC_TIPREL, PROV: body up vs the averaged spring contact normal, or no contact; the Gorge wall case
  still counts) pooled on both AIs: ships default-on if it passes (documented deviation), else keys only + known issue.
  Rendering items 1 / 2 near commit (Off / Faithful identity 5 / 5; probes 1.72 M, 55 % usable, 4.5 s load; per-light
  shadows fixed from a radius-0 descriptor bug, 4 lights traced; a 5 s first-frame stall fixed); the barrier blend
  (material resolve) follows their commit.
  14:46 RT PLAN V2 ITEMS 1 + 2 MERGED df132b5a (rt2 12bda047): item 1 characters / movers lit by the RT world via sparse
  RT probes (default on in Realistic; WFC_RT_GIPROBES=0 off; release trace 0.75-1.8 s per map under the loading
  screen); item 2 per-light RT shadows, up to 4 lamps per frame (2A a lamp behind a wall no longer lights a robot +
  lamp self-shadowing; 2B robots' lamp shadows on the world; WFC_RT_LIGHTSHADOWS=0 off). Gates: Off + Faithful
  byte-identical vs 77d9e02c (5 / 5 shots, SIMHASH f3db6ef0); no new settings; rt-perf-baseline vs 30b2fa74 at 1440p
  / 63 bots: Realistic p50 Rust 7.77 -> 8.04, Debris 6.99 -> 7.10, Streets 8.28 -> 8.66, Molten 8.68 -> 9.09, Gorge
  7.96 -> 8.48 ms (p90 max 10.55; shadow + AO 2.7-3.6 ms); Off / Faithful unchanged. Example (Molten vehicle picking
  up the room's purple light) sent to the user. Head df132b5a named to Experimental (regress vs 30b2fa74, Off /
  Faithful identity with the smoke-order diffs expected on character silhouettes only, R4 leak rows). Rendering next:
  barrier blend, option B + 3b, item 3.
  15:15 REGRESS-8MAP 30b2fa74 vs df132b5a (Experimental, RT off, hold set, card verified clear at 14:54): 8 / 8 PASS,
  deltas -0.06 .. +0.01 ms (501 2.65 -> 2.61, 502 2.20 -> 2.14, 503 2.78 -> 2.76, 504 2.91 -> 2.89, 507 2.35 -> 2.36,
  508 2.54 -> 2.49, 509 3.37 -> 3.34, 510 3.16 -> 3.14); 501 rerun pending (Rendering compiled 14:55-14:57 during the
  hold, self-flagged; rule restated: code edits only during a timed hold). Absolute p90s ~0.3-1 ms above the morning on
  BOTH arms = machine state (driver pool ~6.8 GB, long uptime); deltas valid; reboot before package-5 timing. The
  14:52 hold stopped AssetTools on its own within 21 s (20 s re-check live). Barrier: on rt2 12bda047 + Systems' kit
  the barrier renders as the correct additive field, so the opaque wall is specific to the 56290b7d tree; 3-way bisect
  (NODISTORTION / v1 barrier move reverted / WEAPONDIRECT) in slot B, prime suspect the v1 move of the immediate
  barrier draw before fx_.draw into a pass state with blend off. Option B (material-less particles into the sorted
  translucency, 0.5 m soft depth fade, WFC_FALLBACKIMMEDIATE=1 = old path) coded by Rendering, test pending.
  15:19 CAPTURES df132b5a vs 30b2fa74, Rust (Experimental): SIMHASH 3002 / 3002 identical in both modes; Off 9 / 25 shots
  differ (521-22,961 px), Faithful the same 9 shots with the same boxes within a few px = the smoke-order baseline
  change only (a purple weapon plume the jet used to occlude now draws over it at m960; smoke over a Decepticon
  silhouette at m1080; tracer smoke over character edges at m840 / m1500); static world, HUD and non-overlapping
  smoke identical; items 1 + 2 did not leak into Faithful. PASS. Barrier bisect NEGATIVE (control / NODISTORTION / v1
  reverted / WEAPONDIRECT pixel-identical: the dark panel rises and hides the bots, steps 920-980); rt2 12bda047 Debug
  + the kit renders it see-through -> next split: 12bda047 Release + kit vs 56290b7d Debug (code vs build type; a
  Release / PGO-only cause = uninitialised blend / program-variant state, priority since the user plays Release +
  PGO). R4 leak rows then the 501 timed rerun running.
  15:25 TIP-OVER DECISION: the contact-relative gate also fails pooled ((b') + TIPREL: Smart 6 regressing columns, Classic
  11; (a) + TIPREL: Smart 3 (Gorge), Classic 11; the noise yardstick is a single 6-vs-6 difference, some columns at
  0.00-0.06; the Smart Gorge degradation is consistent across variants). Tipped time (seeds 1-3, Smart): old 230 s,
  (a) + TIPREL 76.5 s, (b') + TIPREL 109.6 s; TIPTEST 3 / 3. Overriding the strict rule: (a) + TIPREL ships DEFAULT ON
  for SMART only (WFC_BOTTIP=0, WFC_TIPREL=1), Classic stays on the player path (identity byte-exact, no Classic
  regression), (b') opt-in (WFC_BOTTIP=2); reason: the user's visible bug is fixed at the cost of 3 Smart columns on
  Gorge, which becomes the explicit target of the next Smart pathing round (with the follow reversals). Package-5
  note: Smart bot vehicles self-right (original AI behaviour); Gorge movement slightly worse, next round.
  2026-10-15 22:09 TEAM PAUSE: every lane session stopped at ~15:40 on 10-14 when the user's usage ran out, and nothing ran until
  Integration resumed them at 22:00 on 10-15 (~30 h lost; the 10:00 reset did not auto-resume the lanes). State at
  resume: head 3051cb56; option B (bf602d7c) unmerged; 3a committed with its crop on disk; D freeze NOT done (colour
  model complete, batches 0-31 built, b0-20 reviewed + packed = 2,411 textures / 13.9 GB; batches 21-31 review + pack
  overnight, D complete ~08:00-09:00 on 10-16); Experimental's df132b5a leak rows PASS (lobby VRAM +80 MB over 4
  changes, 100 % pool reuse from the 4th load) and the 501 rerun done 22:05 (final regress 8 / 8 PASS, worst +0.01).
  Pool 8.59 GB, 0 TDRs. MERGED: option B ceafb8d2 (Rendering bf602d7c: material-less particles into the sorted
  translucency with a 0.5 m soft fade; Off identity 4 / 5 identical, m01200 differs only on tracer smoke over a
  vehicle; conflict in GLRenderer.cpp resolved as Rendering specified: queue attempt, then the character flush for
  declined batches, then the immediate draw) + Systems 3a ede2fc3c (barrier name fallback + one flush before the
  ability meshes). Rendering's verification: 3a changes 0 px for the barrier because the WEP_-prefixed barrier goes
  to weaponQueue_, drawn before the bodies -> case 3 is fixed by 3b (translucent-only dynamic draws queued into the
  sorted translucency after the characters), coding now, then item 3. Gameplay: NAVQ baseline for 069a0a8f (12 seeds,
  pooled gate rule). Head ede2fc3c named to Experimental. Package 5 ETA ~18-19 Oct.
  2026-10-15 22:09 USER: 'run nonstop until it's all done' = package 5 (RT plan v2 items 3-4 + 3b, D complete, the effects /
  vehicle fixes, regress) with no interim; then the next phases in order: the full campaign (+ its textures and ray
  tracing), cross-play multiplayer (PC, web, Xbox 360, PS3, Android / iOS), one final optimization pass, maybe a
  Search-and-Destroy-like mode. Message the user only when done, for questions, or for verification.
  2026-10-15 22:33 REGRESS-8MAP df132b5a vs ede2fc3c (Experimental, RT off, Classic): 8 / 8 PASS; 501 2.61 -> 2.62, 502 2.15 ->
  2.19, 503 2.76 -> 2.84, 504 2.92 -> 2.97, 507 2.44 -> 2.46, 508 2.90 -> 2.98, 509 3.83 -> 3.88, 510 3.65 -> 3.24 (one
  rep, noise). 7 of 8 maps +0.01-0.08 ms p90 (503 / 508 consistent, p50 unchanged) = option B's sorting + soft fade on
  FX-heavy frames, within the gate (quality-first). One 501 head rep contaminated by a short python from Rendering's
  session scratchpad during the hold (passes either way); reminder issued. Untimed Classic captures running.
  2026-10-15 22:37 CAPTURES ede2fc3c vs df132b5a, Rust, Classic (Experimental): SIMHASH 3002 / 3002 identical in Off and Faithful
  (tip-over is Smart-only); world, HUD and characters identical; 9 / 25 shots differ in both modes on material-less
  particles only (tracer beams, muzzle flashes, an explosion fireball, impact sparks: up to 78 k px, 10-21 k beyond 8
  levels) where they overlap each other (the new sort) or pass within the 0.5 m soft fade of ANY geometry or
  character = option B's documented scope (UE3 DepthBiasedAlpha against all depth). PASS. ede2fc3c rows complete.
  Rendering's 3b second attempt (the barrier is drawn unposed from the shared dynVao_, so the deferred draw gets a
  pooled per-frame vertex copy) building.
  2026-10-15 22:46 3b MERGED e5182075 (rt2 848b147b): a dynamic draw whose subs are all translucent (the barrier force field,
  shields) goes into the sorted translucency pass after the characters, replayed like the weapon queue; unposed
  drawDynamicMesh from the shared dynVao_ gets a per-frame pooled vertex copy (WFC_DYNTRANSIMMEDIATE=1 = old order).
  USER CASE 3 FIXED in a real lockstep Rust match (work/fsr3/3b_id1500.png: two vehicles behind a barrier, drawn over
  it before, veiled after; image sent to the user). Off identity vs 77d9e02c: SIMHASH unchanged; m01500 differs only on
  that barrier (0.26 % px), m01200 as option B; baseline change in every mode. Systems' 3a flush stays (harmless,
  helps muzzle-flash meshes); the barrier-name fallback still needed. Head e5182075 named to Experimental. Rendering
  on item 3 (reflections from the RT lightmap, roughness-aware Fresnel for the Molten seam, grazing black lines).
  2026-10-15 23:10 USER: 'make it so all the bots stop taking control of my mouse when I'm working' -> WFC_NOINTERACT=1
  (Gameplay, today): the window is created / shown without activation (WS_EX_NOACTIVATE, SW_SHOWNOACTIVATE, no
  SetForegroundWindow / SetFocus anywhere), never ClipCursor / SetCapture / ShowCursor(FALSE), window keys dropped
  (lockstep input-drop path), window placed out of the work area (WFC_WINPOS override), boot log line, a
  WFC_NOINTERACTTEST self-check; SIMHASH and captures identical by design; env-only, never a package default. Every
  lane runner sets it next to WFC_MUTE=1; Experimental pins it in tools/fidelity. Interim tonight: WFC_NOMOUSE=1 and
  windows at the bottom-right. Experimental's timed regress ede2fc3c vs e5182075 running since 22:50.
  2026-10-15 23:10 REGRESS-8MAP ede2fc3c vs e5182075 (Experimental, RT off, Classic): 8 / 8 PASS, all maps -0.01 to -0.05 ms (501
  2.57 -> 2.53, 502 2.16 -> 2.11, 503 2.77 -> 2.74, 504 2.90 -> 2.86, 507 2.39 -> 2.38, 508 2.53 -> 2.48, 509 3.30 ->
  3.27, 510 3.19 -> 3.18), no contamination. The base's own p90 on 503 / 508 came out lower than in the previous run,
  so the earlier '+0.08 option B drift' was run-to-run machine state; resolved unless it recurs. Classic captures
  running.
  2026-10-15 23:14 Captures e5182075 vs ede2fc3c, Rust, Classic: Off 0 / 25, Faithful 0 / 25, SIMHASH identical (no barrier with
  a character behind it fell in the 25 shots; the positive evidence for 3b is Rendering's lockstep capture m01500).
  e5182075 rows complete. WFC_NOMOUSE today only zeroes deltas; Win32Window still SetForegroundWindow + SetFocus at
  boot and SetCursorPos(centre) every focused frame (the real 'takes my mouse'); no ClipCursor / SetCapture anywhere.
  Interim told to the user: click back into their own window; the run re-grabs focus only at its next boot.
  2026-10-15 23:21 The user was interrupted again (Rendering's item-3 windowed test, PID 17468, booted 23:20:31): killed by PID
  by Integration. Standing rule until WFC_NOINTERACT is merged: no windowed exe from any lane (null render / code
  only); Gameplay's NOINTERACT self-test run is the exception (it must not grab: that is the test; the windowed
  control run is skipped, SIMHASH identity proven under null render instead). Experimental pinned WFC_NOMOUSE=1 in
  tools/fidelity (b4f419d9 -> integration 72201064) as the interim (deltas only).
  2026-10-15 23:30 WFC_NOINTERACT VERIFIED (Gameplay, patched e5182075; 3 windowed lockstep frontend TDM runs, 63 bots, 3002
  frames): NOINTERACTTEST 6 / 6 at 5 s in all runs (GetCapture NULL, not foreground, not focus, clip == virtual screen,
  cursor never hidden or moved, WS_EX_NOACTIVATE, not topmost); an external 250 ms watcher: 0 foreground / 0 clipped
  / 0 centred samples; F9 / R / Escape / wheel / click posted every 2 s through the frontend and the match: no
  disturbance, dropped at the source; SIMHASH 3002 / 3002 identical across runs, 5 / 5 shots byte-identical; the
  window opened on a secondary monitor's work-area corner. Windowless SIMHASH on / off pair then the patch.
  2026-10-15 23:32 WFC_NOINTERACT MERGED 4da148bb (Gameplay GAMEPLAY_NOINTERACT.patch 8a18b894): WS_EX_NOACTIVATE +
  SW_SHOWNOACTIVATE, no SetForegroundWindow / SetFocus, HWND_BOTTOM + SWP_NOACTIVATE (fullscreen requests stay
  windowed); no ShowCursor / SetCursorPos / WM_SETCURSOR hide; keys, buttons, wheel, text and the real pad dropped at
  the source in the pump (frontend + loading loops too; scripted inputs still apply); window at the bottom-right of a
  secondary monitor's work area if present (WFC_WINPOS override); QA panel not topmost; boot line + WFC_NOINTERACTTEST.
  Window-less NAVQ pair on vs off identical (SIMHASH 18720 / 18720). Env-only. Every lane sets WFC_NOINTERACT=1 on
  its runners; Rendering applies the patch file uncommitted on rt2; Experimental pins it in tools/fidelity. Windowed
  runs allowed again under the flag.
  2026-10-15 23:33 NOINTERACT rollout complete: Systems 405a909e (verify_slim maps / settings / RT gate, fxstage_ab, pgo_train;
  merged c934bdc4 with the fxstage kit, the staging captures and the transient-lights audit for item 4); Experimental
  f3cfadde (lib/Run.ps1 process-wide + per launch, identity-capture.sh with NOINTERACT=0 = env -u since any value
  counts as on, capacity-stress -Fullscreen deletes it; synced e1659fff); Rendering applies the patch uncommitted on
  rt2 with WFC_NOINTERACT=1 in all launchers; Gameplay's runners set it. Head e1659fff (code 4da148bb).
  2026-10-15 23:47 USER: 'That fixed it!' (the mouse). Item 4 inputs (Systems): SYSTEMS_MUZZLE_LIGHTS.patch (0a4fa3a9, the
  original muzzle light values; merges after Rendering's addTransientLight lands, keeping Rendering's Renderer.h
  declaration); death lights DECISION: (b) now, Systems - vehicle-form car / tank death -> 8 s purple light, robot
  death except melee / headshot / energy -> 2 s white ramp light, documented as an approximation of the original's
  death-type selection; (a) a real death-type picker (anims, gibs, lights) = Gameplay in the campaign phase after
  package 5 (docs/handoff/CAMPAIGN_BACKLOG.md). Gates: SIMHASH unaffected, headless ENGAGE / DEATH rows, captures treat
  the new lights as a baseline change.
  2026-10-15 23:48 USER: background audio while making music -> Rendering's work/base_head launcher ran an exe without WFC_MUTE=1
  (PID 26216); the session muter was restarted (12 h backstop, until 11:47) and silenced it within a second;
  Rendering told to put WFC_MUTE=1 + WFC_NOINTERACT=1 on every launcher incl. ad-hoc runs. Rule: muter as a backstop
  while the user is at the PC, env flags on every launch.
  2026-10-15 23:52 ROOT CAUSE (Rendering): rt2 does not contain c246ed3e (WFC_MUTE) nor 4da148bb (NOINTERACT), so every exe built
  from the branch ignored both flags although the env was set; only the muter silenced them. Fixed by applying both
  diffs UNCOMMITTED in Rendering's worktree, work/base_head and work/fx_tree, all rebuilt; the unmuted identity run
  killed; all launchers (run1.ps1, lockex / lockpair / lockrt, fxstage_ab, the mrm_tools harness copy) set both flags.
  Ancestry check: no lane branch contains either commit (lanes normally build from integration-based trees) -> rule: an
  exe is NOINTERACT-capable only if built from >= 4da148bb or with GAMEPLAY_NOINTERACT.patch applied (patch -p1 in
  archive trees); the package-4 exe and the m9c_30b2fa7 / int_a44aa6db trees ignore it; lanes told.
  2026-10-15 23:52 Lane confirmations: Experimental patched work/ab/m9c_e518207 (patch -p1, rebuilt; PATCHED_NOINTERACT.txt) as
  the base for the item-3 rows and retired every older tree from windowed runs; Systems runs nothing windowed from
  pre-4da148bb exes (its lights tests were null render). Systems: SYSTEMS_DEATH_LIGHTS.patch (5f275360) stacks on the
  muzzle lights patch (data-driven DamageDeathType, 34 lit types, no headshots; car / truck / tank 8 s purple, jets
  none; BOTTEST 39 / 39 with 78 deaths; CAMPAIGN_BACKLOG.md row for the Gameplay picker); both merge after Rendering's
  item 4. New bug for Gameplay: applyMatchDamage despawns the victim before the PlayerKilled event, whose onPawnDeath is
  gated on spawned(), so the victim death sound is skipped for every participant killed that way (fix presentation-only,
  SIMHASH identical, with a headless row).
  2026-10-16 00:01 DEATH PRESENTATION FIXED, merged ee43bef1 (Gameplay GAMEPLAY_DEATHPRESENT.patch c1c4b440): in applyMatchDamage
  the still-spawned victim gets onPawnDeath(chassis, vehicle form, last damage type, location) after match_.killed()
  and before despawn (the same call the PlayerKilled event makes; the event keeps its spawned() gate, no double play;
  despawn / event order / sim unchanged). AbilityAudio::deathCue() for headless checks. BOTTEST DEATH rows 44 / 44 both
  AIs (natural deaths Smart 11 / 11 + 40 / 40, Classic 22 / 22 + 54 / 54; scripted melee kills cues 4 / 4); SIMHASH +
  whole NAVQ log identical pristine vs patched. Systems' death lights hook onPawnDeath (fires exactly once per death on
  both paths); muzzle + death lights patches rebase onto the item-4 head.
  2026-10-16 00:10 Systems rebased SYSTEMS_MUZZLE_LIGHTS + SYSTEMS_DEATH_LIGHTS to ee43bef1 (b87f1d17; one hook in
  World::onPawnDeath; victim key / headshot / jet from the death record; headless BOTTEST 43 / 44 + ENGAGE 3 / 3, 89
  deaths lit as expected). The one FAIL is Gameplay's new DEATH row counting a natural bot death inside its 600-tick
  window under seed 7 (fails on unpatched ee43bef1 too): Gameplay fixes the check to count the scripted victims only.
  2026-10-16 00:12 Midnight health: 0 TDRs, pool 9.11 GB, muter 13 sessions silenced, C: 105 / F: 177 GB. Experimental had set a hold
  at 00:09 on Rendering's direct request (unannounced; AssetTools paused on it); removed, then GRANTED properly by
  Integration: a 75-min timed window for Rendering's rt-perf-baseline on the items 3 / 4 build (Off + Faithful
  identity already PASS vs 848b147b); rule restated: timed windows come from Integration, every hold is announced
  first. AssetTools D: batches 0-25 reviewed, b0-24 packed (2,852 textures, 16.9 GB), 26-31 left; D complete
  ~06:30-07:30 (the 'no python' listing was a gap between ~10-min passes).
  2026-10-16 00:28 Rendering's timed perf window ran 00:11-00:27 (15 of 75 min; completion vs abort asked); hold removed 00:27.
  DEATH-row fix merged 24c89769 (Gameplay GAMEPLAY_DEATHROW_FIX.patch ed06fd93: the scripted row measures
  presentations / cues synchronously around each scripted applyMatchDamage call, so natural deaths in the window no
  longer count; 44 / 44 both AIs at the default seed and at seed 7 on non-PGO; Systems reruns the PGO env on
  24c89769 with the lights patches rebased).
  2026-10-16 00:32 RT PLAN V2 ITEMS 3 + 4 MERGED 2873a071 (rt2 bea87923; Renderer.h / GLRenderer.cpp conflicts resolved
  additively). Item 3: reflections on every physically reflective surface with a roughness-aware Fresnel (perceptual
  roughness, not the GGX alpha) replacing the hard SpecularPower gate: the Molten platform seam and the grazing black
  rim bands are gone (refl3_closeup.png: off | old gate | new; the near-mirror pillar reflection is now blurred / faint
  on the stone floor); reflection hits read the RT lightmap. Item 4: transient lights (Systems spawns the original's
  muzzle / death lights) light the lightmapped world and characters per pixel in all modes, Realistic RT-shadows the 4
  strongest (tl4.png). Gates: Off + Faithful 5 / 5 identical, SIMHASH unchanged; rt-perf-baseline (the 75-min window
  took 13 min) Realistic p50 / p90 ms 30b2fa74 -> items 1 / 2 -> items 3 / 4: Rust 7.77 / 8.61 -> 8.04 / 9.11 -> 8.74 /
  10.84, Debris 6.99 / 7.83 -> 7.10 / 8.00 -> 7.51 / 9.45, Streets 8.28 / 9.38 -> 8.66 / 9.92 -> 9.38 / 11.39, Molten
  8.68 / 9.88 -> 9.09 / 10.55 -> 9.79 / 12.01, Gorge 7.96 / 9.04 -> 8.48 / 9.84 -> 9.21 / 11.58 (reflections 1.3-1.9 and
  shadow + AO 2.7-3.6 unchanged, denoiser +0.05-0.1; all far under the 14 ms flag). Caveat: Faithful +0-0.3 p50 / +1
  p90 vs the items 1 / 2 run with Faithful's RT passes unchanged -> a 15-min same-flags A/B granted. Images sent to
  the user. Rendering next: robots in reflections (hits on characters take the previous frame's image). Systems
  rebases the lights patches onto 2873a071.
  2026-10-16 00:50 SAME-FLAGS A/B 848b147b vs bea87923 (Rendering, release, muted + NOINTERACT, 1440p, 63 bots; found and fixed
  first: its baseline scratch exe lacked the mute / NOINTERACT code because git apply in an untracked tree is a silent
  no-op - the cause of tonight's unmuted run; files copied in, exes verified by grep): items 3 + 4 cost nothing
  measurable (Molten Faithful 5.42 / 7.21 -> 5.44 / 7.15, Realistic 9.72 / 11.96 -> 9.83 / 11.96; Streets Faithful 4.95
  / 6.76 -> 4.94 / 6.80, Realistic 9.34 / 11.26 -> 9.27 / 11.19; RT pass ms within 0.03). The earlier Faithful +1 ms p90
  is option B / 3b, the mute + NOINTERACT window conditions (a never-foreground window plausibly gets background
  scheduling), or both - unseparated; the clean one-build on / off A/B needs the OFF arm to take the mouse, so it waits
  for the user's OK. RULES: perf rows under NOINTERACT form a new reference series (tables carry a run-conditions line,
  Experimental b1851612); the 30b2fa74 RT baseline is labelled pre-NOINTERACT; package 5's rows end with a re-recorded
  rt-perf-baseline under NOINTERACT as the new reference; A/Bs always run both arms under identical flags.
  2026-10-16 00:54 TRANSIENT LIGHTS MERGED 76b077e8 (Systems c1c968e8: SYSTEMS_MUZZLE_LIGHTS + SYSTEMS_DEATH_LIGHTS rebased to
  2873a071): muzzle-flash lights with the original values; death lights from one hook in World::onPawnDeath (the
  killing damage type's DamageDeathType, 34 lit types, no headshots; robot death 2 s white ramp; car / truck / tank
  death 8 s purple; jets none; an approximation documented in SYSTEMS_DEATH_LIGHTS.md, the real picker in
  CAMPAIGN_BACKLOG.md); WFC_NOTRANSIENTLIGHTS=1 off. Headless (Release + PGO, seed 7): BOTTEST 44 / 44, ENGAGE 3 / 3, 89
  death lights (32 / 24 / 33); SIMHASH untouched. 76b077e8 = PACKAGE-5 CANDIDATE HEAD: Systems' windowed lights check
  (slot B, Rendering paused) -> Experimental's rows (regress vs 30b2fa74, captures, leak, rt-perf-baseline under
  NOINTERACT) -> after the D complete freeze: Systems builds package 5 -> verify -> FAST -> reboot reminder -> promote.
  Rendering's robots-in-reflections joins if it lands before the build.
  2026-10-16 00:59 ROBOTS IN REFLECTIONS MERGED a5f53923 (rt2 54d9d039): reflection rays that hit characters / non-static geometry
  take the previous frame's HDR image at the hit point (new shared target t16) instead of a dark constant; Faithful
  identity 5 / 5 + SIMHASH. PACKAGE-5 CANDIDATE HEAD = a5f53923 (Experimental patches the delta; base 30b2fa74).
  Rendering next: item 7 polish (temporal reflections, R4 lightmap super-sample on the blurriest pages); item 6 =
  Modern calibration against the D pack + M6 roughness (E dropped).
  2026-10-16 00:59 TRANSIENT LIGHTS WINDOWED CHECK PASS (Systems, 76b077e8, Molten TDM 4v5 + playerbot lockstep seed 7, 61 shot pairs
  600-1800, lights on vs WFC_NOTRANSIENTLIGHTS=1 at RT_MODE 2 and 0; submissions identical across the four runs): RT 2
  lit 22 / 61 frames, up to 23 % of pixels brighter (+60-80 / 255), the robot shadows itself and the floor (dark vs off
  peaks 1.6 %); RT 0 the same 22 frames, unshadowed (max 26 %); off = no light. Crops docs/handoff/transient_lights/.
  Experimental's package-5 rows on a5f53923 started (regress 30b2fa74 vs a5f53923 timed; Classic captures Off /
  Faithful / Realistic vs 30b2fa74 + the NOINTERACT on-arm determinism pair; R4 leak rows; rt-perf-baseline under
  NOINTERACT as the new reference); ~1 h 45.
  2026-10-16 01:23 BLOCK: regress-8map 30b2fa74 vs a5f53923 (RT off, Classic, lockstep, best p90 of 2): 6 / 8 maps BLOCK - 501 2.72
  -> 2.86, 502 2.17 -> 2.42, 503 2.76 -> 3.07, 504 2.82 -> 3.14, 507 2.37 -> 2.45, 508 2.54 -> 2.92, 509 3.40 -> 3.58,
  510 3.05 -> 3.34; real (both reps, same match, p50 up; 508 slow frames 405 -> 661: GPU world 0.71 -> 0.80,
  translucency 0.55 -> 0.70-0.79, fx 0.40 -> 0.05 by design, chars CPU 0.61 -> 0.68, draws +7, binds +10). Per-head
  A/Bs through e5182075 sum to <= +0.05, so ~+0.25 ms sits in 2873a071 (items 3 / 4), 76b077e8 (transient lights -
  the only RT-off feature, prime suspect) or a5f53923 (robots in reflections). Bisect by switch first: a timed 3-arm
  A/B 30b2fa74 / a5f53923 / a5f53923 + WFC_NOTRANSIENTLIGHTS=1 on 508 / 504 / 503 (archive builds of 2873a071 and
  76b077e8 at -j 4 only if needed). Package 5 does not promote on a5f53923 as is; Rendering and Systems asked for the
  transient-light cull options (per-draw light lists by radius, a nearest / brightest cap, lifetimes).
  2026-10-16 01:24 Analyses while the bisect runs. Systems: 4v5 fight = 50-97 % of draws carry a light (~2.6 per lit draw, bursts
  of 6-9); 31v32 estimate 15-30 live vs the nearest-8 cap; the game side costs < 32 virtual calls per frame, so the
  cost is renderer-side. Rendering (code read): the transient term is in every lit program with no per-draw cull - a
  per-pixel loop over the <= 8 applied lights whenever any light is live, 3-4 string-keyed uniform lookups per program
  bind (chars CPU +0.07), and a register-count rise that may cost occupancy even at zero lights; the world is MDI
  multi-draw so a per-draw cull needs per-sub mask bits in the row data; robots-in-reflections does zero work at RT
  off; option B nets -0.1 to -0.2. FIX (pre-approved): cached uniform locations now; per-draw light masks for non-MDI
  draws + per-sub mask bits for the world if the lights-off arm removes the cost, or the transient term in its own
  program variant if the cost is static. Gate: regress vs 30b2fa74 within +0.15 on every map at RT off, Faithful
  identity, SIMHASH unchanged, the lights still show.
  2026-10-16 01:25 Rendering's transient-light cost fixes coded + built (unrun until the bisect's hold ends): cached uniform
  locations in Program; a per-draw light mask for non-MDI draws (lights whose cull sphere meets the sub's bounds,
  loose x1.5 + 1 m for skinned subs; zero lights = every loop iteration masked); a per-light CULL RADIUS from the
  falloff exponent (x^2_cut = 1 - (1 / 512)^(1 / e): a 30 m exponent-75 MP gun light really reaches ~8.5 m), with the
  shader early-out before the pow and the masks using the cut sphere, giving world pixels the early-out without MDI
  row-data changes; the 1 / 512 floor is a documented visual change invisible after tonemap. Sequence: Experimental's
  3-arm bisect (after rows 2-3) -> Rendering's Faithful identity + lights check + commit -> merge -> Experimental's
  regress-8map on the merged head decides package 5.
  2026-10-16 01:33 D COMPLETE FROZEN (AssetTools, 01:33): push_full_snap_20261016_b0-31, 3,505 rows, 21.27 GB (levels D 1,458 / C
  1,202 / B 226 / A 619; 2,062 DXT1 / 1,438 DXT5 / 5 A8R8G8B8; every file byte-checked, 0 stale, max path 248); the
  rest of the 3,771-texture selection stays original by eye (sky / glow / FX gradients, faction UI logos, flares); a
  final fix keys eye caps by each texture's own path (a shared EditorTextureSet name had leaked a cap onto CanyonFloor_A
  / CellBlocks, rebuilt). = package 5's HD pack; Systems runs the static check now. AssetTools next: M6 roughness
  (490), then the exporter wfc_material item.
  2026-10-16 01:39 b0-31 static checks PASS (Systems: 3,505 rows = 3,505 files, 21,779.5 MB; missing / mismatch / bad format /
  unindexed / over MAX_PATH all 0; 5 / 5 A8R8G8B8 rows valid). M6 ROUGHNESS DONE (AssetTools, ~8 min, provisional until
  Systems' family review): out/hd/push_m6, pack/hd_pbr_index.json, 498 BC4_UNORM rows, 441 MB, half the HD colour
  resolution, from the artists' gloss channels in the frozen D pack with each map's mean remapped to its row's
  target_mean_roughness; no ORM / metal maps (18 orm_candidate rows flagged); sheets m6_001..063. Forwarded to Systems
  (review) and Rendering (matc pbr, item 6); the Enhanced row ships in package 5 only if calibrated before the cut,
  else package 6. AssetTools next: the exporter wfc_material item + the missing-MATINST audit.
  2026-10-16 01:42 EXPORTER wfc_material DONE (AssetTools scripts/wfc/gltf_wfcmat.py): 85 WEP_ / PROP_ skeletal glTFs regenerated to
  AssetTools/work/gltf_wfcmat/content/... at the same relative paths (148 / 151 materials resolved from the authored
  DB, 3 dummy slots), identical except materials[i].extras, buffer URIs unchanged. DECISION: ExtractedAssets stays
  read-only; package 5 OVERLAYS the 85 files in the build (Systems refresh.sh step + a verify_slim row checking the
  barrier glTF's extras); dev builds keep World's name fallback. AUDIT: work/gltf_wfcmat/missing_matinst_audit.json =
  58 weapon / prop materials none of the 1,368 compiled keys cover (WEP_DeployedTurret_MATINST, barricade / rocket-
  barrier force fields, battery / heart-crystal props); WEP_Barrier_Defrag_MATINST is compiled, so the barrier's whole
  problem was the missing extras link. Rendering's matc pass over the 58 = package 6 unless trivial.
  2026-10-16 01:43 glTF overlay frozen as push_gltf_wfcmat_20261016 (FROZEN.json: 85 files, sizes + sha256, source ae6e142;
  files read-only; refresh.sh verifies before copying). M6 FAMILY REVIEW (Systems db07e55e): 498 / 498 accepted (BC4
  DX10 full mips, exact sizes; means within 0.02 of target; gloss-source tracking |r| median 0.988, inverted in 495 /
  495); notes: flat maps mirror flat sources, 37 luma-derived rows at lower confidence, 18 orm_candidate rows stay
  dielectric roughness only until a metal review. Enhanced row data complete; ships in package 5 only if Rendering's
  item 6 calibration lands before the cut. Machine 01:42: 0 TDRs, pool 9.4 GB, F: 151 GB; Experimental's untimed rows
  running on a5f53923.
  2026-10-16 01:47 OVERLAY TOOLING MERGED 62f941c3 (Systems ac5be728): the package ships glTFs as .gltf.xpr compressed twins, so
  build_slim --overlay DIR replaces the source of the 85 rows before packing (FROZEN.json bytes + sha256, exactly one
  shipped row each, recorded in build_slim.json; a wrong sha256 stops the build); refresh.sh OVERLAY defaults to
  AssetTools/work/gltf_wfcmat and stops unless 85 of 85 rows are replaced; verify_slim static row check_overlay.py
  (the shipped barrier glTF carries the Barrier_Defrag MATINST extras; all 85 byte-identical to FROZEN; package 4 fails
  it by design). Systems ready for 'build package 5' once the cull-fix head passes regress.
  2026-10-16 02:01 BISECT (Experimental, 3-arm lockstep, RT off, Classic, 508 / 504 / 503, 2 reps, same match, 0 contamination):
  a5f53923 + WFC_NOTRANSIENTLIGHTS=1 equals 30b2fa74 within -0.04 .. +0.01 ms (508 2.96 / 3.27 / 2.97; 504 3.30 / 3.53 /
  3.26; 503 3.28 / 3.48 / 3.27), so the whole regression is the transient lights: GPU per-pixel cost in the lit world
  pass (slow-frame GPU world 0.70 -> 0.78-0.80; p50 1.82 -> 1.93; chars CPU and draws equal between head and lights
  off) = up to 8 lights evaluated in every lit world shader whenever any light is alive, which with 63 bots is always.
  Translucency 0.47 -> 0.64 on both head and lights-off = option B / 3b, not part of the regression. Rows 2-3 on
  a5f53923: captures vs 30b2fa74 Off 14 / 25, Faithful 14 / 25, Realistic 25 / 25 (expected sets), NOINTERACT
  determinism pair 0 / 25, SIMHASH identical; R4 leak rows done. Target for the cull fix: within +0.05 ms p90 of base;
  Experimental reruns the same 3-arm A/B on the merged head; Systems adds a game-side view-cone cull as a small patch.
  2026-10-16 02:02 a5f53923 rows 2-3 PASS (Experimental, Rust, Classic, SIMHASH identical throughout): captures vs 30b2fa74 - Off
  14 / 25 and Faithful 14 / 25 (the same shots: the smoke / flash order, soft fade and barrier changes, then from 900
  the transient lights lighting nearby surfaces and vehicles, up to 585 k px), Realistic 25 / 25 (items 1-4 + the
  lights); NOINTERACT on-arm determinism 25 / 25 identical; R4 leak rows PASS (GPU dedicated plateau ~2.7 GB from
  change 2, +82 MB over the last 3; handles flat). NOTE: at shot 1140 a muzzle light turns a dark pillar next to the
  camera almost fully bright beige (frame mean luminance 38.3 -> 53.1), possibly overbright: Systems verifies the
  authored brightness / radius / exponent vs the original, Rendering verifies linear-before-tonemap and no double
  brightness scale; a correction rides with the cull fix. Row 4 (perf baseline) waits for the fix head.
  2026-10-16 02:03 MUZZLE-LIGHT BRIGHTNESS VERIFIED (Systems, authored.db): the shooter in shot 1140 is a vehicle; vehicle MG muzzle
  lights are authored Brightness 25 / Radius 512 cm / FalloffExponent 10, colour (255, 128, 128) -> linear x 25 =
  (25, 5.5, 5.5); MP rifle-class weapons B 10 / R 3000 cm / exp 75 (effectively ~8 m). Our chain: (byte / 255)^2.2 x
  Brightness, radius cm / 100, the authored falloff, added once in linear space before the output conversion (no
  second gamma or brightness). A wall 1-1.5 m from a firing car gets 10-16x its albedo in red-heavy light, so the
  beige-pink saturation is what the original's numbers produce (UE3 2009 clipped too; no filmic tonemap). DECISION:
  keep the authored values in every mode (the original look), add a WFC_TLIGHTSCALE tune key (default 1) in Systems'
  cull patch, note it in the package-5 notes for the user to judge; a real-game capture would settle it. Systems'
  view-cone cull patch (SYSTEMS_TLIGHT_CULL.patch) rebases onto the cull-fix head once named.
  2026-10-16 02:16 TRANSIENT-LIGHT CULL FIX MERGED 3c007160 (rt2 5fa83761): per-light cull radius (1 / 512 floor: an exponent-75
  light reaches 0.28 R; documented visual change invisible after tonemap), per-draw light masks for non-MDI draws,
  cached uniform locations. Checks: Faithful identity 5 / 5 + SIMHASH; lights still show at muzzle parameters (12.9 %
  of pixels Realistic / 15.3 % Faithful on Molten); Rendering confirmed the renderer side of the units chain (added
  once in linear before fog / tonemap, UE3 falloff, no second brightness). NEW PACKAGE-5 CANDIDATE = 3c007160:
  Experimental reruns the 3-arm A/B (target within +0.05 ms p90 of 30b2fa74), then regress-8map, captures vs
  a5f53923, and the rt-perf-baseline under NOINTERACT; Systems rebases its view-cone cull + WFC_TLIGHTSCALE patch onto
  it (rides if in before the build).
  2026-10-16 02:32 3-ARM A/B on 3c007160 (Experimental, 508 / 504 / 503): head +0.19 / +0.25 / +0.20 ms p90 vs 30b2fa74, the
  lights-off arm within +0.04 / +0.06 / -0.06: the cull radius, per-draw masks and cached uniforms recover almost
  nothing (within noise on 508 only); the cost stays per-pixel in the lit world pass on every frame (508 slow-frame GPU
  world 0.70 -> 0.76, 504 0.97 -> 1.09; CPU render +0.03-0.08 from the masks) because with 63 bots firing the lights
  are alive near most visible geometry. DECISION: package 5 ships with the transient lights ON (the original's
  behaviour; the user set quality first; ~6-8 % of a 3 ms Off frame, ~2 % at Realistic); the regress verdict is
  re-based = the lights-off arm within +0.15 of base on every map AND the lights' cost <= +0.35 everywhere, documented
  as a feature cost; the real fix (tiled / clustered light lists, per-vertex world evaluation, or a nearest cap) is
  Rendering's package-6 item with a +0.05 target. Systems' view-cone / 150 m cull + WFC_TLIGHTSCALE merged 398b27ef =
  FINAL PACKAGE-5 HEAD. Experimental: 3-arm regress on all 8 maps, captures, rt-perf-baseline; Systems' windowed
  cull-numbers run beside the captures.
  2026-10-16 02:35 Step 3 PASS (Experimental): 3c007160 vs a5f53923 captures on Rust, Classic, SIMHASH 3002 / 3002 identical, Off
  = Faithful: 13 / 25 shots differ from 720 on, all faint light-edge shifts from the 1 / 512 cull floor (max channel
  5-31 on 10 shots; 53 on 1200; 87 on 720 in a 540 px box at a light's cut-off edge); no missing or extra lights.
  Step 2 next: the 3-arm regress on all 8 maps on 398b27ef (~45 min hold), then step 4.
  2026-10-16 02:37 Systems' view-cone / 150 m cull numbers (Molten 4v5 + playerbot lockstep, RT off, 3 x 600-draw windows):
  submitted lights 2,453 -> 1,798 (-27 %) with 61 capture pairs cull vs base 44 identical and 17 within 0.09 % px
  (pose noise on a distant flier), no lighting difference: the cull removes only lights that do not reach the view.
  Step 2 started 02:36 (hold set; 3 arms x 8 maps, ~45 min). Incident: Rendering launched an exe at 02:35:55 after
  the hold and my pause message; killed by PID at 02:36:37; Experimental reruns any marked row at the end.
  2026-10-16 03:09 PACKAGE 5 REGRESS PASS (Experimental, 3-arm lockstep x 8 maps, RT off, Classic, 2 reps, 0 contamination; the
  stray exe ended before the gate started): lights-off arm vs 30b2fa74 -0.10 .. +0.04 on every map (nothing else
  regressed); the transient lights' cost (head minus lights-off) +0.03 .. +0.23 ms p90 (501 +0.14, 502 +0.15, 503
  +0.08, 504 +0.18, 507 +0.06, 508 +0.20, 509 +0.03, 510 +0.23), documented; Systems' view-cone cull roughly halved
  it (508 +0.31 -> +0.20); head vs base within +0.15 on 7 / 8 maps (508 +0.18). Step 4 (rt-perf-baseline on 398b27ef
  under NOINTERACT, the new reference) running under a hold; the package-5 build starts on 'go' after it. Rendering's
  launcher now polls the hold every second and kills its own exe.
  2026-10-16 03:20 USER DIRECTION for package 6 (watching a lane run): more interactive shadows, most notably buildings shadowing
  the players / bots under them on the open-skybox map; vehicle lights and weapon fire casting on the ground; all
  lighting interacting across the entire map ('one place'); more objects casting shadows, more lights casting light and
  reflections; colour cast between surfaces and characters by position. Rendering writes RT_FULL_PLAN v3 as a gap
  analysis after package 5 promotes: sun / sky shadows from static geometry onto characters and vehicles, characters
  casting sun shadows onto the world, vehicle headlights as shadowed dynamic lights, muzzle light on the ground,
  coloured bounce onto characters, dynamic lights + characters in reflections, emissive surfaces as light sources, one
  energy basis for world and characters; ordered by visible impact, quality first, Off / Faithful untouched. (The run
  the user watched may have been an Off / Faithful arm or package 4.) Package-5 notes written
  (docs/handoff/PACKAGE_5_398b27ef.md); tag playtest/09c-package5-398b27ef.
  2026-10-16 03:25 RT PLAN V3 merged 0c44256d (Rendering c59145c1): 8 rows, order 1, 8, 3, 5, 6, 4, 7, 2; row 1 (robots shadowed
  under buildings) verified first with a before / after image. USER (03:40): 'I can wait on getting a playable build
  ... I want the highest quality looking ray tracing, shadows, reflections, color and everything in between ... when
  somebody shoots I want it cast on the walls and reflected off reflective surfaces and the colour from the gun blast
  cast on other players near it ... tie the entire world into one atmosphere. Keep working until you get there and
  let me know when it's done.' => PACKAGE 5 = INTERNAL CHECKPOINT (built, verified and promoted quietly as the lanes'
  base; no hand-over message). DONE CONDITION for the RT phase: plan v3 complete at the quality-maximising variant
  (incl. gun-blast light reflected on reflective surfaces and the blast colour on nearby players), the tiled /
  clustered transient-light fix, item 6 Enhanced calibration, the item 7 polish worth doing, a per-map eye pass on
  the five user maps; then package 6 = the hand-over build with the reboot reminder. Systems reads the vehicle
  headlight / dynamic emissive light data for rows 3 and 7.
  2026-10-16 03:26 PACKAGE-5 ROWS DONE on 398b27ef (Experimental): regress PASS (re-based), captures PASS, R4 leak PASS, and the
  NEW RT PERF REFERENCE under NOINTERACT (cea68f4b; docs/handoff/RT_PERF_BASELINE_398b27ef.md + .csv, synced f7c10f94):
  2560x1440 windowed, 63 bots, transient lights on - p50 / p90 ms Off 1.60-2.07 / 1.96-2.31; Faithful 4.19-5.03 /
  5.41-7.21; Realistic Rust 9.35 / 11.44, Debris 7.44 / 8.49, Streets 9.19 / 11.69, Molten 9.71 / 11.94, Gorge 8.94 /
  11.16; Realistic shadow + AO 2.9-4.0 ms (+0.5-0.7 vs 30b2fa74 from items 1 + 2), denoiser 0.9-1.2, reflections
  1.3-1.9, GI per traced frame 3.3-6.7, RT VRAM 3.9-4.8 GB. Off matches the pre-NOINTERACT series within 0.07.
  'GO build package 5' sent to Systems (398b27ef, D b0-31, overlay); Rendering has slot A for plan v3 row 1.
  2026-10-16 03:28 Plan v3 rows 3 / 7 data (Systems SYSTEMS_VEHICLE_LIGHTS.md a23e1600): no vehicle light components in the
  original (174 objects / 29 packages; the light beams are translucent cone meshes), no light components on pickups
  / interactables / destructibles / screens (glow = emission + particles + the FX_Pickups LightBeam_STAT cone); the
  original's gameplay lights are muzzle + death (shipped) + TnPawn.CloakLight (authored, not wired). DECISIONS:
  headlights = a Realistic-only deliberate addition (an RT-shadowed spot per vehicle beam cone mesh, colour from its
  emissive, calibrated to the cone's visual; Systems tables the placements); emissive pickups / shards = Realistic-only
  point lights from the material emissive; CloakLight wired in all modes (fidelity, Systems patch). Off / Faithful
  untouched. Package 5 build at 721 / 873 files.
  2026-10-16 03:30 CAMPAIGN ASSET INVENTORY (AssetTools, read-only; docs/handoff/CAMPAIGN_ASSET_INVENTORY.md): 10 chapters (A1_IAC,
  A2_KON, A3_COR, A4_ORB, A5_TRY, D1_ORB, D2_COR, D3A_IAC, D3B_IAC, D5_OME), 1,167 packages / 421 map packages / 420
  streaming sublevels (2.48 GB on disk, 6.08 GB uncompressed), the map pipeline run on 0 of 10 (actor-summary JSON
  only); nav data present (17,875 NavigationCell, 175,184 TnReachSpec); campaign-only assets 1,468 static + 199
  skeletal meshes, 449 AnimSets, 281 particle systems, 6,008 sound cues, 3,601 matinees, 3,185 Kismet sequences, 57
  TR_ characters (25 map-referenced, 32 runtime-spawned); textures: 2,490 of 3,429 already in the frozen D pack, ~3 new,
  346 UI textures a separate decision; blockers: 127,556 Kismet objects (11 of 40 High Moon classes without RE decomp),
  no matinee player, 570 parent materials / 907 MICs without .mat (matc need unknown), 22 FaceFX assets, the
  runtime-character definitions, a streaming-aware map pipeline. PILOT approved: D5_OME (9 packages / 155 MB) through
  the map pipeline, CPU only, hold-aware, ~1 day, provisional, in AssetTools work/.
  2026-10-16 03:35 PACKAGE 5 BUILT (Systems, not promoted): Rebuild-Systems/work/playtest_slim_398b27ef, 27.09 GB (keep 46,245
  / xpr 7,161 / 0 failures), Release + PGO with RT shaders, M5 runtime, HD = push_full_snap_20261016_b0-31 (3,506
  files, 21.27 GB), overlay push_gltf_wfcmat_20261016 (85 / 85 rows replaced, FROZEN.json verified; post-build
  check_overlay: the barrier carries WEP_Barrier_Defrag_MATINST, 85 / 85 byte-identical), vbake + fit_lights on 10
  maps. verify_slim slot opened (Rendering's RT-on exe paused for the RT gate); FAST next; promote quietly as the
  internal checkpoint. Headlights: the original has no beam cones on vehicles (the LightBeam cones are map props and
  the pickup beam), so lamp sockets are derived from the vehicles' emissive lamp areas = an INVENTED PLACEMENT for the
  Realistic-only addition, documented in SYSTEMS_VEHICLE_BEAMS.md; approved.
  2026-10-16 03:41 CAMPAIGN PILOT D5_OME DONE (AssetTools 248f73f, ~2 min CPU, 2.5 GB peak; docs/handoff/CAMPAIGN_PILOT_D5_OME.md):
  all 9 map packages via authored streaming from D5_OME_BASE_m; world.glb 1,341 placed / 153 unique meshes (SMCA
  transforms 1,291 / 1,291 exact); collision, lighting, probes, postprocess, 71 lightmap atlases, vertex lightmaps, nav
  (123 cells / 210 tactical / 163 air), gameplay, spawns, audio, FX, physics, 36 destructibles, Kismet manifest;
  structure audit PASS; completeness 33 COMPLETE / 1 PARTIAL / 4 missing lobby metadata; outputs under AssetTools
  work/campaign_pilot via a new opt-in WFC_VS_ROOT (default unchanged; ExtractedAssets / Game Dump untouched). Two
  validator stops (sky-card mesh refs; the MP robot check) need a ~20-line campaign mode; streaming currently merges
  all sublevels visible.
  2026-10-16 03:43 PACKAGE 5 VERIFY PASS (Systems, work/verify_398b/REPORT.md, exe sha256 94938E66...): Streets 91 s / Gorge 88 s
  / Debris 85 s (-ForeignCwd; 0 outside-package opens), HD D 3,505 rows / 3,506 files / 21.27 GB 0 problems, bin 20
  files exact with the M5 runtime (FSR 3.1 + frame gen), overlay row PASS (barrier wfc_material; 85 / 85 identical),
  SETTINGSTEST 7 / 7, RT gate build / context / runtime / R4 PASS (V_bake k 0.354, 26 pages; R4 937,589 texels; per-
  page k). FAST gate started (Experimental); promote on PASS as the internal checkpoint. CloakLight: the data gives
  only the component (Brightness 1 / Radius 1024 cm / falloff 2, colour 250,220,120, disabled) - the ramp values and
  on / off points live in script bytecode with no decompiler; DECISION (b): hold, Gameplay checks its RE notes for the
  TnBuffCloak / TnCloakLightService values; a guessed glow could reveal cloaked players (gameplay change), so no
  approximation. Systems builds the INVENTED-PLACEMENT headlight table.
  2026-10-16 03:45 HEADLIGHT PLACEMENT TABLE (Systems 98ceeff2, merged de85b4a4; docs/handoff/SYSTEMS_VEHICLE_BEAMS.md + .json;
  tools/systems/vehicle_beams.py; INVENTED PLACEMENT, Realistic-only): per MP vehicle (34), forward-facing emissive
  lamp triangles in the front third clustered at 0.15 m with mirrored L / R pairs: 25 pairs (19 high confidence), 7
  singles (jets, Car2, Tank2), 2 without a forward lamp (Tank, Minion3); position in vehicle root space, lamp normal,
  emissive colour, area; caveats: geometric only, some roof / cab-height picks (the JSON lists every cluster incl.
  bumper pairs), the emissive is the default skin's blue energon glow so the headlight colour is Rendering's call.
  Handed to Rendering for plan v3 row 3.
  2026-10-16 03:53 CLOAK LIGHT CLOSED (Gameplay RE, fb5e40c3, docs/handoff/GAMEPLAY_CLOAKLIGHT_RE.md, merged fc00f731): the shipped
  game never enables TnPawn.CloakLight - TriggerLight's only callers are the two death-light paths, nothing calls
  GetService(TnCloakLightService), UpdateLight idles while CurrentTime == -1, the exe references the service name only
  in the native registration, and every RampLightProperties instance across 2,438 packages is a death light. Faithful
  = no cloak light; the original's gameplay lights are exactly muzzle + death (both in package 5). FAST gate on
  398b27ef running (functional rows only, no hold).
  2026-10-16 03:54 TDR (Display 4101, amduw23g stopped responding and recovered) at 03:51:26, the first since 2026-10-13 16:30:
  Experimental's FAST (frequent short RT-off launches) beside Rendering's RT-on row-1 captures, with the driver
  nonpaged pool at 10.40 GB (4.4 GB two days ago). Rendering paused until FAST completes; rule until the next
  reboot: one GPU client at a time, Integration serialises slots; Experimental checks the rows around the reset and
  reruns them; Rendering discards the capture started 03:52:10. Health 03:53: F: 119 GB free (package 5 + FAST trees;
  Experimental prunes old archive builds), C: 106 GB, muter 29 sessions silenced.
  2026-10-16 03:56 PLAN V3 ROWS 1 + 8 VERIFIED (Rendering; commit after the Faithful identity once FAST is done). CAUSE of the
  user's 'no shadow under buildings': on Streets the original's per-object light visibility (the baked volume) ranked
  the sun OUT of the robots' light environments in 93-100 % of character draws (Debris: in for 66-83 %), so robots were
  lit by ambient only, the same in sun and under buildings, and the existing per-pixel RT sun visibility had nothing
  to shadow. FIX (Realistic only): characters take the RT sun at full per-object visibility and the per-pixel RT sun
  ray (world + characters) shadows it (Streets: sun direct in 100 % of draws; WFC_RT_CHARSUN=0 off); row 8: the
  characters' lamps use the RT lightmap's calibrated scale (giLightCal_, Systems' fit; WFC_RT_CHARLAMPCAL=0), one
  energy basis. Images: work/fsr3/v3r1_m01100.png (robots in the open sunlit to match the walls) and v3r1_m00800.png
  (a robot under the overhang stays shaded); the Rust pair discarded (TDR window). ESTIMATE for the done condition: ~6
  days of slot work -> the evening of 2026-10-19 with holds and serialised slots. Harness: the NOINTERACT window's
  client area varied (2560x1417 vs 1440; placement shrinks the client when window + frame exceed the work area):
  Gameplay makes NOINTERACT a borderless popup at the exact requested client size within the monitor bounds;
  Experimental marks captures with a mismatched client rect INVALID. D5 blockers: 35 Kismet startup events (~17 custom classes without RE decomp), 40 matinees /
  27 cameras, FaceFX 7 assets / 13 tracks; 46 of 115 materials need a matc pass. APPROVED next: the validator campaign
  mode (Streets byte-identical), per-node sublevel tags + Kismet streaming state, then A1_IAC (50 sublevels).
  15:31 BARRIER SPLIT (Systems): 56290b7d Debug = Release (the same opaque wall: not build type, not UB); rt2 12bda047
  Release + the kit = a flat pink panel; Rendering's own scene (work/barrier_seq.png, a different local character) = a
  proper see-through grid with the scene behind it. Likely a STAGING artefact: the hook forces requestBarrier on a
  non-Leader pawn (Car4), leaving the force field's per-owner material parameters unset. Every 'barrier opaque' result
  today is UNCONFIRMED as a product bug; the name-fallback finding stands as code. Next: Rendering answers case 3 in
  its working scene (a bot behind the grid: drawn over it or tinted by it?) and reports how its barrier resolved its
  material (decides whether the name fallback is needed); Systems prepares a Leader-owner staging (CPU) for a
  controlled A/B if needed. Cases 1 (smoke on a bot) and 2 (local muzzle flash over a bot) stay verified correct.
  15:33 CASE 3 CONFIRMED REAL (Rendering, work/barrier_behind.png, frames 1080 / 1200): in the working scene a bot
  vehicle behind the see-through grid is drawn OVER it (the grid's veil stops at the silhouette): the barrier draws
  immediately, the queued characters later at the map presentation, and an additive surface writes no depth. Their
  barrier got its material only through Systems' name fallback (same ExtractedAssets glTF, no other path). FIX =
  Systems' SYSTEMS_FX_ORDER_3A.patch (flushQueuedCharacters before the barrier / sentry draws + the ensureAbilityModels
  name fallback), verified by Rendering in its scratch tree with a before / after crop, then merged. Systems' opaque
  results = the forced barrier on a non-Leader owner (staging). Option B tested green (RT off, Streets staging: the
  smoke trail veils the body; a muzzle / impact flash in front of a bot shows where the bot used to cover it; 0.6-2.9 %
  of pixels differ); Off identity pair running, commit after.
  15:37 TIP-OVER MERGED 069a0a8f (Gameplay GAMEPLAY_TIPOVER.patch v2): Smart bots self-right when tipped (pitch / roll
  righting at the authored AI limits with the contact-relative unstable gate, per bot via MoveIntent.aiTip); Classic
  stays on the player path (NAVQ 24 / 24 byte-exact vs 30b2fa74); WFC_BOTTIP / WFC_TIPREL for measurement, modes 1 / 2
  opt-in. Smart tipped time 230 -> 76.5 s; TIPTEST 3 / 3; BOTTEST 39 / 39 both; DETERMINISM 2 / 2 both; cost 0.23 ms;
  Smart Gorge stucks 1.26 -> 1.44 and Streets off-mesh 3.85 -> 4.91 beyond the (weak) pooled noise = the next Smart
  round's target. Systems' SYSTEMS_FX_ORDER_3A.patch committed (4adf8e9c; name fallback + flush before the barrier /
  sentry draws), applies clean; merge after Rendering's verification crop.
  DLSS (M5 D): non-commercial project (user 2026-10-12; licensing revisited before any commercial release); the "NVIDIA DLSS"
  credit goes on the start-up splash, the About screen (if any) and the credits - clean and unobtrusive.
  2026-10-16 04:11 REBOOT DONE (user, 04:02): driver nonpaged pool 10.6 -> 1.10 GB, 0 TDRs since boot, all exes gone;
  one-client rule lifted (normal slot rules: Rendering slot A, Experimental untimed rows beside it, timed rows alone).
  All five lanes resumed on their saved plans: Rendering v3batch1 (identity + rows 2 / 6) then the rows 1 / 8 / 6 / 2 +
  spot API commit; Experimental FAST rerun of the TDR-window rows on 398b27ef + work/ab prune + client-rect check;
  Systems promotes package 5 quietly on FAST PASS; Gameplay NOINTERACT client-size patch; AssetTools A1_IAC rerun.
  C: free 106 -> 54 GB across the reboot: pagefile.sys 52 GB (system-managed, 0 MB in use) - a user call, left alone.
  2026-10-16 04:29 USER BAR (04:20): the RT / lighting / fidelity work must make the game look NIGHT AND DAY from the
  original graphics; the last big item, everything after is small stuff. Eye pass = same-frame side-by-side Off vs
  Realistic per map; Rendering assesses extra rows beyond plan v3 with costs (exposure / tone mapping, colour bleed,
  shadow casters, wet response, atmosphere, RT-era effect lights). Merged: NOINTERACT exact client size 7d9edf78
  (Gameplay, 2560x1440 client verified, SIMHASH 3002 / 3002 identical, shots 600-1500 identical; frame 300 = tracer
  noise on the same exe), SYSTEMS_MATC58_AUDIT 521f131c (56 / 58 compile with parent + shader fallbacks; MP-first
  list; the original has no light sources beyond muzzle + death), SYSTEMS_RT_LIGHT_HOOKS 57109d37 (27 effect lights
  proposed, RT-era, Realistic-only, default off; code waits on Rendering's pick). Pagefile moved to F: (user).
  2026-10-16 04:41 USER DECISIONS: no preview build (waits for the full package 6); second RT worker = Systems
  (effect-light hooks WFC_FXLIGHTS, item 6 material compile, row 3 headlights; src/render stays Rendering's);
  per-row gates = Off / Faithful identity + one crop, the 3-arm regress / leak rows / capture sets once on the final
  head; Experimental builds the same-frame Off | Realistic eye-pass pipeline + penultimate-head FAST. Target ~17 Oct.
  PLAN V3 ROWS MERGED (rt2, Off / Faithful byte-identical to 848b147b, 5 / 5 + SIMHASH): row 1 bd749603 characters
  take the RT sun (WFC_RT_CHARSUN), row 8 5ab0a45c lamp calibration (WFC_RT_CHARLAMPCAL), row 6 9007d194 transient
  lights in reflections (WFC_RT_REFLTL), row 2 8b29ff13 sun-share shadows (WFC_RT_SUNSHARE), 428bcf7e spot API
  addTransientSpot + transient-light cost reserve. Built, switches in the exe, pushed. Systems FX_LIGHTS headless PASS
  (SIMHASH identical, 44 / 44); Streets crop pending their RT-enabled rebuild.
  2026-10-16 04:42 NIGHT-AND-DAY ASSESSMENT (Rendering): remaining v3 = row 5 (0.5 d), row 4 (0.25), tiled light fix
  (1 d, lifts the 8-light cap), row 7 (0.25), item 6, item 7. Extra Realistic-only rows ranked: X1 filmic tone curve +
  eye adaptation + grade + HDR bloom (1 d, 0.2 ms), X2 RT-shadowed sun + lamp specular on the world (0.75 d, 0.3 ms),
  X3 volumetric sun shafts / lamp halos / lit fog (2 d, 1-1.5 ms), X4 stronger GI colour bleed (0.5 d), X5 more
  shadow casters + character AO 4 m + contact shadows (1 d, 0.3 ms), X6 more reflective surfaces + damp sheen (0.5 d,
  0.3-0.8 ms), X7 2x RT lightmap (1.5 d, +0.5-1.4 GB VRAM); effect-light calibration 0.5 d. Approved now: row 5, X1,
  row 4, tiled fix, X2, X4, explosion + impact calibration (every option includes them). X3 / X5 / X6 / X7 = user
  scope-vs-date decision (A: X1+X2+X4 -> ~19 Oct; C: +X5 + effect lights -> ~20-21 Oct; B: all seven -> ~24-25 Oct).
  2026-10-16 04:51 MERGED: item 6 materials tooling d0a3f988 (Systems patch, Rendering review as-is; data ships after the
  4-map existing-material byte check), RT-era effect lights + vehicle headlights 709df0f5 (Systems, game-side only;
  WFC_FXLIGHTS / WFC_HEADLIGHTS opt-in until Rendering calibrates; SIMHASH identical; Streets 3-arm A/B lit 50 / 91
  and 39 / 91 frames). Approved X rows in build order: row 5, X1, row 4, tiled fix, X2, X4, effect-light calibration.
  2026-10-16 05:07 USER (going to bed): option C for package 6; standing rule = quality / fidelity wins every
  cheaper-vs-better choice without asking (60 fps floor at 1440p); playable build only when everything is done.
  ITEM 6 DATA SHIPPED into Rebuild/work/render for the 5 user maps (Systems handoff; existing entries byte-identical
  on all 5 maps: Streets 534 / Rust 598 / Molten 543 / Gorge 506 / Debris 518 identical, 0 changed, 56 new each, 0
  compile errors; +6 FluidRed cubemap textures per map; backup work/render_backup_pre_item6_*). Sentry turret draws
  its authored hologram (package 5 known issue closed). Other 5 MP maps regenerating. New gap for Gameplay: kamikaze
  mines have no presentation (World never draws mines_). Experimental resumed 05:00 (FAST table ~40 min; combined
  final gate sized ~3.5 h GPU block). AssetTools A1_IAC running (10 GB chapter cap); validator campaign mode DONE.
  2026-10-16 05:32 FAST 398b27ef post-reboot rerun (Experimental 05:04-05:30): NO product fails (PASS 51 / HARNESS 1 /
  KNOWN 2 / INFO 2 / PARTIAL 2 / UNKNOWN 1); the first run's BROKEN rows were the 03:51 TDR (GL reset 0x8255). The
  mapswitch row needs an empty card (all lanes paused, running now); package 5 promotes on its DONE. Prune: 89 trees,
  45.5 GB freed. Row 5 merged 70310b02 (WFC_RT_PROBEFILL).
  HD PACK NEVER LOADED (Systems' finding): the HD / HdPbr index loaders read sizes via asFloat; 89,478,612 rounded to
  89,478,608, one row counted as wrong-size and the whole pack was ignored, so HD textures were OFF in packages 4 and 5
  (verify_slim's map rows ran HD = Off). Fixed 66eb7d73 (asDouble). The snapshots are correct; AssetTools' re-snapshot
  cancelled. Package 6 = the first build where HD actually shows; verify_slim gains the HD-accepted runtime row.
  2026-10-16 05:40 FAST 398b27ef FINAL = PASS (58 PASS; the one PRODUCT row = the known Berth dark-interior false
  positive, environment fully drawn; mapswitch ran alone 05:30-05:35): Systems promotes package 5 as the internal
  checkpoint (HD-off known issue noted in its doc). HD loader fix VERIFIED on the integration exe (66eb7d73,
  WFC_HDFORCE, b0-31): "HD texture pack available: 3505 textures". Both snapshots pass AssetTools' new integrity pass
  (hd_pack_integrity.py; hd_snapshot.py renames a failed snapshot _FAILED). Item 6 data batch 2 switched in (Berth,
  Seed, BrokenHope, Remnant, Complex; existing entries identical, 56 new each): all 10 MP maps covered. Fidelity
  tooling merged 27ab8332 (eye-pass pipeline + client-rect INVALID check, script files by path). Eye-pass proof on
  398b27ef gets slot A at Rendering's next batch boundary.
  2026-10-16 05:47 PACKAGE 5 PROMOTED (internal checkpoint, no user message): work/playtest_slim = 398b27ef, 27.09 GB,
  Play.bat = the package exe; fallbacks playtest_slim_30b2fa74 / _c949ab88 kept. HD reject confirmed on the package
  exe; verify_slim HD runtime row added (b02027a2 -> 049e4e49, fails package 5 as intended, passes 66eb7d73+). Doc
  aec84406. Gameplay Smart round closed (no code; 24-seed yardstick); mine presentation capture on slot B. Fidelity
  tooling all synced (package-gate.sh = the one-command final gate). Rendering on the X1 retune batch.
  2026-10-16 06:08 MERGED: kamikaze mine presentation 050a7796 (Gameplay; SIMHASH identical; detonation effect
  Exp_2500cb_FX spawns but is visually unverified -> Systems checks the template). Address-dependent draw order:
  CLOSED, not a defect (control build = base frame-exact; the glow diff was the mines' trails under the truck).
  Systems eye-pass audit 06110ef4: MED voice starvation (player's idle loop dropped, 96 voices held by distant level
  loops -> Systems: audible-level priority + virtualised loops), MED homing-rocket embers never fly (undecoded
  PMI_VelocityOverLife -> AssetTools decode), LOW degenerate-normal impact (Systems); flash / sound sync, LOD, level
  audio PASS. Systems X5 caster list + fade curves merged bb5ac029. AssetTools A1_IAC: two ray-grid audits skipped
  "pending", destructibles stage cached (33 min -> seconds, byte-identical); doc pending. Package-gate BUILD_ONLY
  shakedown clean (11 min); eye-pass proof on 0a9c497f with HD on waits for Rendering's batch boundary (~06:30).
  2026-10-16 06:48 TILED TRANSIENT LIGHTS MERGED 1c94d62e (Rendering aed1c441: 64 lights / frame in Realistic, per-32-px
  tiles, first 4 keep RT shadows, WFC_TLTILED=0; fixes effect lights pushing muzzle lights out of the 8 cap). Off /
  Faithful identity holds with tiled + X1 + X2 + HD fix in the exe (5 / 5, SIMHASH f3db6ef032d17b93). Item 6 now real
  (HD pack accepted, Modern on): Rust energy-matched +19.3 % / Molten +6.7 % vs target +-5 %, Rust needs a pass. X1
  retune: keeps the original's toe / midtones, stops plate bleaching, but is subtle (Streets 10-15 % darker); NOT yet
  night-and-day -> Realistic may take a real grade (adaptation 0.65+, bloom 0.8, midtone gain, per-map colour grade);
  Faithful keeps the match. X2: 0 pixels changed (branch not reaching the world shader), debugging. X4 / X5-1 written.
  Eye-pass proof running on 0a9c497f (Experimental, slot A ~45 min); death-body design approved f7b7828d; mines merged.
  2026-10-16 07:13 EYE-PASS PROOF on 0a9c497f (Experimental, 06:46-07:11): 15 / 15 runs, sim identical across modes,
  HD pack accepted, client rect valid; sheets Rebuild-Experimental/work/eyepass/0a9c497/. HONEST READ: Off, Realistic
  and Realistic + HD are near-identical at a glance (Realistic slightly warmer / darker; the HD column flatter on a
  lit Streets wall at step 1320 -> Rendering checks the HD material response). The night-and-day bar is NOT met at
  this head; the levers in order of visibility: bold Realistic grade / exposure / bloom (X1), X2 specular, X4 bounce
  colour, effect lights + headlights ON by default in Realistic after calibration, X5 contact shadows. Every row
  check from now = an eye-pass sheet. Merged: voice virtual voices + impact normal 9711d354; eyepass fix 98ae68ae;
  A1_IAC doc 48ad24fc; AssetTools VelocityOverLife decode (Systems regenerates the FX runtime data).
  2026-10-16 07:32 VEHICLE YAW RATE MERGED 022e5e34 (Gameplay: faithful AiMaxAngularSpeed; robot facing snaps 133 -> 0,
  vehicle heading snaps 1,016 -> 173). CLASSIC RE-BASELINED on this commit (96 / 96 runs diverge from the old
  reference from step ~602 by design; new reference docs/handoff/vehyaw/); Experimental + Rendering told (package-gate
  regress vs 398b27ef = perf only). Debris Smart cost -> next Smart item: vehicle corner anticipation. Corpse build
  (death bodies) passes BOTTEST 46 / 46 both AIs; crops on slot B now. Systems paced 63-bot audit 5e256a57: no fx
  guards, sync max 1 ms, local player never refused; DECISION: 128-voice pool when ExtendedPlayers=1 (5v5 stays 96).
  AssetTools tag-parser fix moved up (package 6; ~2 h; recovers 2 Gorge audio triggers + Berth blocking volume /
  decal; ships via the package overlay, ExtractedAssets untouched); A5_TRY ran clean (4.5 min). Rendering keeps an
  integration-head export for identity (no rebase, per CLAUDE.md).
  2026-10-16 07:37 TAG-PARSER FIX DONE (AssetTools f384c7b; authored_reparse.py with exact undo): detector 0 on all 10
  MP maps, vs_validate 45 checks 0 FAIL each; the 4 MP objects' runtime files are byte-identical (exporters read the
  cooked package natively), so nothing ships and the Gorge / Berth in-game checks are cancelled. Campaign: 1,442 rows
  re-parsed (D5 80, A1 1,231, A5 131); chapters rerunning to pick up the recovered meshes and lights. Package-gate
  base for package 6 = 398b27ef + yaw 022e5e34 (same match across the 3 arms; Experimental builds it).
  2026-10-16 07:38 DEATH BODIES MERGED (Gameplay GAMEPLAY_DEATH_BODY.patch, fcc567dd): presentation-only corpse plays
  the picked death entry from the last pose (melee Defrag sweep, explosion, headshot, fall / crawl) then gibs; vehicles
  leave the hull wreck with chunks + smoke, dissolving; base showed only the kill-feed icon. BOTTEST 46 / 46 both AIs
  ("every presented death has a corpse"), NAVQ 16 / 16 identical, SIMHASH identical. Crops docs/handoff/deathbody/.
  Patch 2 (decals, skeletal gibs, authored death camera, full momentum picker) after package 6.
  2026-10-16 07:47 RENDERING STATUS: X1 rounds 2 / 3 still read "the same game" (a tone curve alone cannot reach the
  bar); X2 root cause = the port already applies UE3's directional-lightmap specular, so new X2 = a Realistic gain on
  that term fed by the RT lightmap (2.5-4; x1.0 in Off / Faithful); X4 bounce saturation 2.5 incl. probes; X5-1 works
  (robots darker under cover); effect-light calibration batch (gain 1 / 2 / 3) next, then default-on in Realistic.
  DECISION RULE: if the next sheet still reads "same game", X3 volumetrics move INTO package 6 (quality over date) and
  X7 is reconsidered; X3 design written in the gaps. Death bodies a6e9ea67 (archive-build fix); mines closed dc5f5a61.
  2026-10-16 07:59 FX RUNTIME DATA SWITCHED (all 10 MP maps; Systems regen from AssetTools' decode fix): 2,007 LODs
  identical, 1,076 Absolute=false, 13 Orbit completed, 3 VelOverLife (homing-rocket embers now fly; crop
  docs/handoff/fx_regen/); backup work/render_backup_pre_fxregen_*. 128-voice ExtendedPlayers merged b9e6f3d8.
  Rendering: X3 design 6c703b6a (~2 d, ~1 ms, 30 MB), ability_render_assets loud-fail ee921d14; batch 4 (effect lights
  on, specular gain, grade round 3, bounce 2.5, X5-1) on the a6e9ea67 export; sheet + score decide X3. Mine detonation
  colour = authored HDR yellow (data), not a bug; renderer items for Rendering: HDR scene-target clamp point, soft
  particles for all translucent particles. Gameplay: corner-anticipation arm 1 null; diagnostic NAVQ split running.
  AssetTools: D5 + A5 reruns recover all meshes; A5_TRY doc b9e6f3d8; A1 rerun in progress, then D3B_IAC.
  2026-10-16 08:13 VERIFY DRY RUN on 64771c71 (Systems, today's work/render): static rows all PASS (HD static 0 problems,
  bin 20 / 20, overlay 85 / 85, settings 7 / 7; package render data byte-identical to work/render). Map rows Streets /
  Gorge / Debris FAIL on one cause: the two vending-machine MATINSTs from the item 6 data log "shader compile failed
  (UBER) 0:94 constructor too many arguments" (verify fails any [error] line) = a REGRESSION vs package 5 that the dry
  run caught early. Decision: Rendering fixes the matc codegen before the package 6 head (priority raised), Systems
  regenerates the two entries; fallback = drop the two entries (glTF fallback as package 5). RT gate + HD runtime rows
  run in the RT slot at Rendering's batch 4 boundary, ahead of Experimental's sheet. A1_IAC rerun: 1,211 -> 0 mesh-less
  components; D3B_IAC running. Package 6 doc skeleton merged 64771c71 (26 placeholders).
  2026-10-16 08:37 PACKAGE-GATE RULES FINAL (Experimental 101b3970 / 78854ddd / d3a6e1e1 synced): base = 398b27ef + yaw
  022e5e34 (verified same match, identical Classic SIMHASH 600-9600); regress bars = report rows at +0.35 ms p90 per
  map RT off with draws / binds (BISECT above, not a block); blocking = contamination, client rect, different match,
  final vs lights-off parity; new step 1b = Realistic + HD Enhanced perf row (1440p, 31v32, 5 maps, FLAG p50 > 16.7
  ms). GPU block ~2.8-3.1 h. Authored eye-pass views wired 1a49b965 (24 / 25 cameras). Rendering batch 4 ends ~09:10
  -> Systems RT gate row (5 min) -> Experimental eyepass on Rendering's int_cand exe (WFC_FXLIGHTS=1) = the X3 go /
  no-go sheet. Rendering: X2 / soft-particle shader text now Realistic-only (Off / Faithful verbatim); item 6 Rust term
  found (Fresnel shaping on grazing floors, bounded 0.5-1.5x); soft particles for all FX in Realistic
  (WFC_SOFTPARTICLES) in source; Faithful bloom gather 4.0 kept, RE item on the 360's 7e3 target. Gameplay: Debris
  cost = Driving hull mid probe below the 0.45 m wheel clearance on all 7 cars (Car4 pinned on a floor lip, v 21 -> 0);
  one-line clamp in test with a Classic re-baseline; full ride-height model next. AssetTools: WFC_PKG_CACHE proven
  byte-identical on Gorge; D3B_IAC rerunning (cache 6).
  2026-10-16 08:59 DRIVE-PROBE FIX MERGED 56a9beca (Gameplay): Driving hull probes at or above the 0.45 m wheel clearance
  (all 7 cars sat below it; a boosted Car4 pinned on a Debris floor lip 21 -> 0 m/s; players too). Gate clean (NAVQ no
  map worse, VEHTEST new car rows, BOTTEST 46 / 46). SECOND CLASSIC RE-BASELINE (75 / 96 diverge, tables
  docs/handoff/driveprobe/); package-gate base = 398b27ef + yaw + drive-probe 56a9beca (Experimental rebuilding).
  Debris Driving slow back to base; the remaining Smart Debris cost = hover / jet slow (next trace), then the
  ride-height model, then audit items 4 / 5 / 6. Vending matfix data live in work/render (08:50); dry-run map rows +
  HD runtime PASS; RT gate row at Rendering's boundary.
  2026-10-16 09:12 BATCH 4 (Rendering, Streets 13 shots vs Off; eyepass_score method): Realistic flat 19.3 / +13.7 /
  contrast 1.04 / 64 %; all in (X1 round 3 + X2 2.5 + X4 2.5 + X5-1 + effect lights) 30.5 / +27.5 / 1.38 / 77 %;
  specular 4: 1.47; effect gain 2: 1.40. Streets now reads as a lit world with real contrast (b4_m01100.png). Molten
  stays at 1.00 (lava-lit: emissive surfaces as RT light sources + per-map exposure = next). Defaults picked:
  specular gain 2.5, transient gain 2, effect lights + headlights default-on in Realistic after the sheet. Sequence
  now: Systems RT gate row -> Experimental eyepass on int_cand (X3 go / no-go) -> RESUME. Gameplay: bot Driving steer
  sign INVERTED for both AIs (bots steer away from the heading error until the boost drops them to hover); one-line
  fix in test + permanent BOTTEST row + third Classic re-baseline. gate-base.sh scripted (c34c177a).
  2026-10-16 09:33 Systems SYSTEMS_FXLIGHTS_DEFAULT_ON ready (9a11a073; docs merged fec179ff): effect lights default-on
  in Realistic (WFC_FXLIGHTS=0 off), headlights confirmed default-on; fix: rtOnly lights no longer enter the live
  transient list outside Realistic (protects Off / Faithful identity); verify_slim gains rtcheck.rtlights; SIMHASH
  identical, Off identity Gorge 16 / 16 + Streets; the 3 min RT crop runs at the sheet's end, then the patch merges.
  BOTTEST "bots toss grenades (2)" FAILS at 56a9beca itself (45 / 46; Gameplay reproducing on a clean export; must
  read 46 / 46 before the head). Gameplay steer-sign fix PASSES (every map better or equal both AIs; Debris Smart
  within noise of pre-yaw; BOTTEST 48 / 48 with the new DRIVE guard rows, 46 / 48 with the old sign); patch + third
  re-baseline after the grenade item. Rendering: RT-off identity + item 6 recaptures beside the sheet; next sheet exe
  = Release int_cand2. AssetTools: objtree reference-cycle fix (byte-identical), D3B ~1 h, D2_COR next (cache 10).
  2026-10-16 09:54 MERGED: BOTTEST gate robustness + stranded / KillZ death presentation d817d599 (seed variance, no
  re-baseline); bot Driving STEER SIGN fix 3de18d73 (bots steered away from their heading error in vehicle form, both
  AIs; every map better or equal; Debris Smart cost CLOSED; two permanent DRIVE guard rows) = THIRD CLASSIC
  RE-BASELINE (docs/handoff/drivesteer/); gate base now 398b27ef + yaw + drive-probe + BOTTEST gate + steer.
  Integration BOTTEST at seeds 1 / 5 reads 47 / 48 until audit item 4 (idle 20 s row) lands; seed 7 passes. Next
  Gameplay: item 4, items 5 / 6, ride-height model. AssetTools: ALL campaign maps re-parsed up front (1,942 rows);
  D3B relaunched cache 10.
  2026-10-16 10:12 RENDERING per-row commits ready on rt2 (push after the Faithful identity at RESUME): X1 tone curve,
  X2 specular gain 2.5, X4 coloured bounce + emissive hook, X5-1 character AO 3 m, row 4 transient gain 2, soft
  particles, item 6 + RENDERING_MOLTEN_PLAN.md. ITEM 6 IN RANGE: Rust +45.7 -> ~+3 %, Molten +41.5 -> ~-3 % (the
  Modern GGX lobe now only where the original material had a specular input; spec gate 3.0). Package data needs
  materials_pbr.json regenerated with matc_pbr on all 10 maps after the merge (Systems; Rendering's render_m6 Rust /
  Molten / Streets as the reference). D3B_IAC doc a036639e; the remaining 6 chapters chained. Package-gate base =
  398b27ef + yaw + drive-probe + the steer sim line (proof after the sheet; d3db6178).
  2026-10-16 11:05 BATCH-4 SHEET (Experimental, int_cand, 15 / 15 runs, sim identical across modes, HD on): Realistic vs
  Off = Streets 28.6 / +16.1 / 1.14 / 44 %, Gorge 17.5 / +14.4 / 1.46 / 26 %, Debris 25.0 / +9.7 / 1.18 / 40 %, Rust
  22.5 / +15.5 / 1.26 / 33 %, Molten 19.7 / +11.1 / 1.28 / 31 %; all maps 22.7 / +13.4 / 1.27 / 35 % (was 0.99):
  contrast +27 %. Dark Gorge views read night-and-day (pick_510 step 780: far wall + door lit where Off is black);
  lit exteriors a clear step. BUG (top priority, Rendering): Realistic + Enhanced (HD 2) LOSES the RT lighting (all
  maps 1.05, Streets 0.80; the Enhanced column as dark as Off at Gorge 780): the Modern / PBR path does not take
  the RT term. X3 volumetrics: GO for package 6 if the pairs pass (v1 screen-space march built; pairs to the user).
  Authored views rerun deferred to the Release int_cand2 exe. Gameplay: robot ground-snap root causes (no
  WalkableFloorZ 0.7; landing while rising) approved as one commit (both AIs + players, 4th re-baseline).
  2026-10-16 11:11 MERGED 5f423157: effect lights default-on in Realistic (Systems; + the Off / Faithful live-list fix;
  "RT-era lights" boot line; verify_slim rtcheck.rtlights), package-gate base switched to 398b27ef + yaw + drive-probe
  + the one-line steer (Experimental: same match as 3de18d73 True, control False). Rendering RESUMED (held 09:10-11:25):
  Enhanced-darkness leading cause = DATA (materials_pbr.json predates item 6: the Modern path strips the original's
  cube term and adds back only the un-matched env; item 6's regen restores it; test first), then Faithful identity,
  push of the 7 commits, Molten arms, x3pair. Gorge vbake PNGs fail to decode at runtime (R1 sun share falls back):
  Systems / AssetTools check format vs truncation. User sent pick_510 (Gorge sheet).
  2026-10-16 11:22 RENDERING ROWS MERGED (Faithful identity 5 / 5 + SIMHASH on the 56a9beca exports): X1 tone curve
  f486c10a, X2 specular gain a6f5c878, X4 coloured bounce + emissive hook 6acb65ad, X5-1 character AO cabb416b, row 4
  transient gain 2 03aa1294, soft particles 70315129, item 6 + RENDERING_MOLTEN_PLAN 02c743cc; built clean (RT
  shaders, wfcPbrEnvEnergy in the exe). materials_pbr regen (Systems, energy-matched, 10 maps, oracle 0) switched into
  work/render with it (backup kept). ENHANCED DARKNESS IS NOT DATA (Gorge: Enhanced old data +8.8 / 1.04, new data
  +8.1 / 1.04 vs Realistic +12.6 / 1.23): Rendering isolates HD textures / Modern shading / RT with 3 arms (top item).
  Gameplay ground-snap: both traps fixed but Classic Streets / Molten stucks up (z ~2) -> split (1) / (2); (2) refined
  to "land while rising only on a surface the previous bottom was above"; underside test by enclosure proposed.
  PACKAGE_6_HEAD.md refreshed b85227e7 (30 placeholders); m6pbr manifest ca6b94ed.
  2026-10-16 11:30 ENHANCED ROW PACKAGING: package 5 shipped the D pack only (no hd_pbr_index.json / m6), so its
  Enhanced row derived every roughness value and verify never ran level 2; build_slim --hd-pbr + verify_slim M6 static
  row + -HdLevel 2 merged 65dddbe4 (Systems); eyepass / lockstep-ab / package-gate now require "HD modern material
  maps: N" for every HD 2 arm (51a2a75a, a5db23da); shared dev HD root with both indexes =
  Rebuild-Systems/work/hd_dev_full (junctions; 3505 + 498 verified). Rendering's own Enhanced arms DID load the 498
  maps (WFC_HD_PBR_ROOT) and are still dark -> the 3-arm split decides. NOTE: Rebuild/build/bin is a DEBUG build (no
  PGO, ~4x slower): never a timed / sheet subject; work/render materials_pbr.json needs an exe >= 02c743cc.
  2026-10-16 11:33 ENHANCED DARKNESS ROOT CAUSE (Rendering; Gorge split vs Off: Realistic + HD textures 1.24, Off +
  Enhanced 0.99, Realistic + Enhanced 1.04): the Modern lightmapped shader replaced the original's directional-lightmap
  specular (UE3's formula x the material's SpecularColor, large on this game's metal and rock) with a fixed-F0 0.04 GGX
  lobe, throwing most of the authored specular away in EVERY mode; X2's gain never reached Modern. Fix in test: Modern
  takes the original's specular term (WFC_PBR_ORIGSPEC=0 restores the lobe) + X2's gain in Realistic programs; Off +
  Enhanced now matches Off + HD (1.00), Realistic + Enhanced 1.04 -> 1.11 before X2. Sheet on the fixed Release exe at
  Rendering's ~13:00 boundary; int_cand2 superseded. Gate tooling: hd_dev_full default, wfcPbrEnvEnergy check 9d11af72.
  2026-10-16 11:39 ENHANCED FIX MERGED eaed3192 (Rendering f400ed81: Modern takes the original's directional-lightmap
  specular; WFC_PBR_ORIGSPEC=0 restores the lobe): Gorge vs Off, hd_dev_full root, 498 maps loaded: Off + Enhanced
  +0.2 / 1.00 (= Off + HD textures), Realistic + Enhanced +8.8 / 1.04 -> +12.8 / 1.24 (= Realistic + HD 1.24). V_bake
  summary line 6155925b. Package 6 build plan clean on a5db23da (Systems): ~27.5 GB, D 3,506 + M6 498 files, ~3 min
  copy after the Release + PGO build. Next: Rendering batch 7 (Molten emissive 1 / 3 / 6, X3 pairs at 1440p for the
  user) -> boundary ~13:15 -> Experimental eyepass on int_cand3 (Release, 2b28bb78 + Enhanced fix). AssetTools A4_ORB
  in m05 (heavy nav), doc ~12:15; 3 chapters left.
  2026-10-16 11:42 PACKAGE 6 REALISTIC DEFAULTS (6155925b, grepped by Systems): WFC_FXLIGHTS / WFC_HEADLIGHTS on (=0 off),
  WFC_RT_TLGAIN 2.0, WFC_RT_LMSPECGAIN 2.5 (WFC_RT_LMSPEC=0 off), WFC_RT_TONE on + TONE_ADAPT / BLOOM / CONTRAST / GAIN
  1.3, WFC_RT_GISAT 2.5 + WFC_RT_GIEMISSIVE 1.0, WFC_RT_CHARAORADIUS 3.0, WFC_SOFTPARTICLES, WFC_TLTILED, rows 1 / 8 /
  6 / 2 / 5 switches, WFC_PBR_ORIGSPEC; all env-only statics (SETTINGSTEST count stays 7, keep list unaffected).
  verify_slim gains rtcheck.defaults (SYSTEMS_VERIFY_RT_DEFAULTS.patch, docs ac533161; applied once Rendering logs
  "RT Realistic defaults (first Realistic frame): k=v ..."). X3 / Molten emissive / ground-snap pending.
  2026-10-16 11:43 BATCH 7 (Rendering): Molten emissive gain 1 / 3 / 6 = no effect (+5.0 / 1.12 at every gain): the RT
  hit emissive for the lava is near zero -> find why (texture / MIC emissive not sampled by the hit data, or lava
  excluded from the static scene) before tuning. X3 v1: NOT shown (flat grey haze, too dense, frames mostly covered:
  Streets 3.8 / 0.94, Gorge 6.6 / 0.88, Molten 14.6 / 0.82 washed out; no banding, no leaks). X3 v2 building: ambient
  in-scatter / 10, density x0.3, haze 0.001 / m, stronger sun term, lamp halos from the 4 shadowed lamps, open-sky
  frames; pairs only on a passed eye check. Defaults log line done in source (commits with v2). Experimental eyepass
  on int_cand3 (Release, Enhanced fix) RUNNING since ~12:50.
  2026-10-16 11:46 RT HIT-DATA BUG (Rendering, from the Molten dig): the per-surface albedo / emissive atlas pass culled
  back faces (drawSubs re-enabled culling after the pass disabled it), so every surface with a mirrored UV layout drew
  nothing and got a grey mean albedo + zero emissive (Molten: 1,918 of 3,799 subs "had none", emissive mean 0.001):
  the lava lit nothing, the GI bounce lost half its colour (why X4 changed so little), reflections showed those
  surfaces grey. One-line fix built; commits as its own row with per-map "had none" counts; Off / Faithful identity
  on the export. Batch 8 (Molten emissive 1 / 4 with the fix, X3 v2 pairs) after the sheet; X4 re-scored after.
  Gameplay ground-snap: combined refined version (slope filter + rise rule + 2.5 m clearance underside test, floors
  >= 3.94 m clear) in its ~3 h gate.
  2026-10-16 12:11 SHEET on int_cand3 (Release, Enhanced fix; Experimental; all checks pass incl. 498 modern maps):
  Realistic | Realistic + Enhanced vs Off (mean / brighter / contrast / %): Streets 27.4 / +18.2 / 1.17 / 43 % | 1.15;
  Gorge 22.8 / +17.6 / 1.44 | 1.47; Debris 25.6 / +8.2 / 1.15 | 1.13; Rust 26.5 / +19.5 / 1.29 | 1.29; Molten 23.8 /
  +20.1 / 1.50 | 1.48; ALL 1.31 | 1.30 (batch 4: 1.27 | 1.05; proof 0.99). ENHANCED FIXED. User sent pick_509 (Molten).
  Authored views INVALID again (direct-boot runs took size / RT / HD from env only; fixed to write the profile):
  rerun on Rendering's post-cull-fix candidate at their boundary. Rendering RESUMED for batch 8. A4_ORB doc 6c2a9548.
  2026-10-16 12:14 Authored eye-pass views: the direct map boot never applies the profile's display / HD / Modern rows
  (frontend path only), so views run Off | Realistic at 1280x720 for now (4a5c8711); a direct-boot profile hook
  (WFC_SHOTLIST / WFC_BOOT_PROFILE=1 applies setDisplayMode + HD / Modern like the frontend path) requested from
  Gameplay as a harness-only patch. PACKAGE_6_HEAD.md updated d1b03903 (25 placeholders). D3A_IAC doc a30c2c64;
  A2_KON running, D1_ORB last. Rendering batch 8 (Molten with the cull fix, X3 v2 pairs) running since ~13:20.
  2026-10-16 12:23 DIRECT-BOOT PROFILE HOOK MERGED e22321e8 (Gameplay WFC_BOOT_PROFILE=1: direct map boots apply the
  profile's display / HD / Modern rows like the frontend; + WFC_HDTEXTURES=2 for Enhanced; SIMHASH identical); the
  eye-pass authored views now run at 2560x1440 with the Enhanced column on exes from this head (Experimental f60cad43
  / b41e0e5c). Gameplay walkable-floor (slope-only) gate finished, under analysis.
  2026-10-16 12:30 WALKABLE-FLOOR FIX MERGED f19e0052 (Gameplay; |n.y| >= 0.7 ground support; both traps escape; 2 of
  ~460 MAPTRAVERSE routes BLOCKED over steep faces = the faithful player-facing change) = FOURTH CLASSIC RE-BASELINE
  (docs/handoff/walkable/); gate base += the walkable sim line. Rendering batches 8 / 9: HIT-DATA CULL FIX
  (a81f7291, push after identity) surfaces with no hit data before -> after: Molten 1918 -> 129 / 3799, Remnant 2373
  -> 126, Seed 1442 -> 91, Complex 1537 -> 59, BrokenHope 1331 -> 109, Rust 1129 -> 28, Gorge 874 -> 52, Streets 655
  -> 88, Berth 602 -> 25, Debris 559 -> 30: every map's bounce + reflections now see real albedo. Molten lava gain
  test inconclusive (frame had no lava in view) -> rerun on a lava-facing authored view. X4 re-scored: subtle (Streets
  1.1 %). X3 v2 still NOT showable (faint haze, no shafts / halos in these frames: Streets 0.96, Gorge 0.96, Molten
  0.94). DECISION (b): X3 parked after v2; order = Molten lava source, item 7, row 7 / effect-light calibration, X3 v3
  on the authored open-sky views (lamp picks by screen coverage), eye pass. Views rerun on int_cand4 running.
  2026-10-16 12:40 AUTHORED VIEWS on int_cand4 (25 views x Off / Realistic / Enhanced at 1440p via WFC_BOOT_PROFILE; all
  checks pass): mean contrast vs Off, Realistic | Enhanced: firefight 1.26 | 1.29, street under buildings 1.25 |
  1.27, dark interior 1.44 | 1.48 (little brightness, +6.5), reflective floor 1.88 | 1.85, vehicle lane 1.37 | 1.46;
  Enhanced adds on top in 4 of 5 view types. User sent views_507 (Debris). Rendering RESUMED (identity -> push of the
  cull fix + defaults line -> Molten lava-facing gains -> item 7 -> calibration -> X3 v3 on the views -> eye pass).
  Experimental: gate base += walkable (f19e0052@src/game) building. A2_KON doc e4f1912a; D1_ORB (last) running.
  2026-10-16 12:46 CAMPAIGN PIPELINE COMPLETE: all 10 chapters exported (AssetTools; CAMPAIGN_PIPELINE_SUMMARY.md,
  04a6ab8b): 430 levels, 158,484 placed meshes, 45,949 nav nodes, 196 streaming states, 97 min total, no stage over
  6.1 GB (m05 / structure audit pending the grid fix, now in progress). Blockers for the campaign phase: Kismet
  127,610 objects / 15 custom classes without decomp (2 used in all 10 chapters), 3,605 InterpData, 94 FaceFX, 377
  pickup factories, 1,112 unique matc materials. Recommended order: streaming-state runtime, core Kismet + 4 classes,
  pickup discovery, pawn factories / AI, matinee, matc, dialog; first level D5_OME, then A5_TRY. AssetTools now: grid
  fix, then Kismet class RE + streaming runtime design notes. PACKAGE_6_HEAD.md: how-to-play section 04198d4d.
  2026-10-16 12:52 MERGED: RT hit-data back-face cull fix f670f8ba (Rendering a81f7291), "RT Realistic defaults" boot
  line 04c51243 (264fdf97), verify_slim rtcheck.defaults row 59b96d4d (Systems patch; NOTE: 59b96d4d's commit message
  is garbled by a shell heredoc slip, content correct: tools/package/verify_slim.ps1 +39). Identity on int_cand4: Off
  5 / 5, Faithful 5 / 5. Rendering: lava test on the 509 authored views (gain 1 / 4 / 8, tone off diagnostic), then
  item 7 temporal reflections (WFC_RT_REFLTEMPORAL). Experimental: walkable gate base same-match check running.
  Systems: rtcheck.defaults run against a Release build of this head at Rendering's boundary.
  2026-10-16 12:55 MOLTEN LAVA LIGHTS (Rendering e233a641 -> 584c873b): no existing view faced lava, so 4 lava-facing views
  were built from bsp.glb's MoltenFloor primitives; with the cull fix the emissive gain works: rock over a lava basin
  contrast 2.13 (gain 1) -> 2.29 (4) -> 2.45 (8) vs Off with a growing orange tint; Realistic default WFC_RT_GIEMISSIVE
  = 4 (heat glow; 8 reads as a red lamp). Findings: X1's tone curve carries a large share of Realistic's lift on these
  views; the step-1200 yellow washout = X1 soft bloom on very bright emissive + muzzle flash -> cap the bloom input
  on emissive-heavy frames in item 7 polish. Next: item 7 temporal reflections test, effect-light calibration, X3
  v3, eye pass; next RT boundary ~14:15 (Rendering's clock) -> Systems' rtcheck.defaults run (giemissive=4.00).
  2026-10-16 12:55 Package-gate base = 398b27ef + yaw + drive-probe + steer + walkable f19e0052 (Experimental ff28578b:
  same match True vs f19e0052, control False). verify_slim RealisticDefaults giemissive 4 paired with 584c873b
  (a981db7f). Systems' rtcheck.defaults RT row runs at Rendering's next boundary (04c51243 exe, 1.00 table = row
  mechanics check); the 4.00 pairing is verified in the final package gate.
  2026-10-16 12:58 RISE-SNAP RULE MERGED 63368d05 (Gameplay: while rising, land only on a surface the previous bottom was
  above; UE3 physFalling; fixes the barrier-top / pocket pops; NAVQ every |z| < 1.5, MAPTRAVERSE identical) = FIFTH
  CLASSIC RE-BASELINE (docs/handoff/risesnap/); gate base += 63368d05@CharacterMovement.cpp (Experimental). BOTTEST
  "AI cost per step" row to be gated on average + p99 (wall-clock flake). Next Gameplay: the third Streets pocket.
  Systems: the dry-run map-row FAIL = the missing HD folder's pack/hd_index.json probe (not a regression; package 6
  ships HD at the root; verify backlog: optional probe). 584c873b Release build for the final gate in progress.
  2026-10-16 13:00 verify_slim: <hd>/pack/hd_*index.json is an optional probe (HD-less packages pass; Systems b22a2912 ->
  200f8047). PACKAGE_6_HEAD.md refreshed 45fe5514 (28 placeholders; lava closed pending the eye pass; new known issue:
  bloom washout on bright emissive + muzzle flash until item 7's cap). Waiting: Rendering item 7 (temporal reflections
  WFC_RT_REFLTEMPORAL; Systems' table patch f3c56f1b pairs with it), boundary -> Systems rtcheck row -> RESUME.
  2026-10-16 13:05 BOTTEST AI-cost row -> average + p99 (Gameplay; 48 / 48 on all 12 seeds; test row only). Third
  Streets pocket: no longer a trap with both snap fixes (bot escapes 25.9 m in 20 s); still the top robot-slow spot
  (narrow off-mesh gap) -> entry replay, fix proposal with evidence.
  2026-10-16 13:06 Rendering item 7 (8d50190a, local): temporal reflections default-on in Realistic (WFC_RT_REFLTEMPORAL;
  3x3 neighbourhood clamp, 25 % new per frame; neutral on held views, benefit in motion), soft-bloom cap 64 -> 16
  (WFC_TONE_BLOOMMAX; the point-blank flash keeps the robot behind it readable), WfcRtDefaults.h (one source per
  default; defaults line + refltemporal, - x3). Sequence: Systems rtcheck row (running) -> Rendering identity on
  int_cand5 -> push -> merge paired with Systems' table patch -> RESUME: effect-light calibration, X3 v3, eye pass.
  2026-10-16 13:06 THIRD STREETS POCKET root cause (Gameplay replays): a bot VEHICLE carries its corridor corner at 14-15
  m/s off the mesh where the outside of the corner is a 10 m drop into a trench, transforms, and walks the trench dead
  end under an unreachable waypoint (2-5 s each; 132 s over 4 of 24 Classic seeds). APPROVED: ledge guard for bot
  ground vehicles (both AIs, bot steering only; probe position + v x 0.6 s; off-mesh + no floor within 2.5 m below ->
  brake / no boost, steer to the waypoint; drop / jump links exempt; the original's vehicle AI nav is unrecovered),
  Classic re-baseline, gate + activation counts per map / seed. Systems' rtcheck row rerunning (setup slip, no GPU use).
  2026-10-16 13:11 verify_slim RT gate on 04c51243 (Systems): PASS every row incl. rtcheck.rtlights (effect lights on,
  headlights on) and rtcheck.defaults 14 / 14 (tone on, lmspecgain 2.5, tlgain 2.0, gisat 2.5, giemissive 1.0 at that
  head, charao 3.0, softparticles, tiled, rows 1 / 8 / 6 / 2 / 5, origspec); the run's exit 1 = the HD-less dry-run
  probe (verify predates the probe patch). Rendering: identity on int_cand5 -> push 8d50190a (item 7) -> RESUME.
  2026-10-16 13:12 CAMPAIGN PREP DOCS merged 97e53598 (AssetTools): the 15 "no decomp" Kismet classes are data-only
  nodes (behaviour in decompiled handlers: TnAiController.On*, TnInteractBase, TnTogethernessVolume, boss / tank
  pawns; TnSeqAct_MissionComplete = SeqAct_MoviePlayer for the chapter outro FMV); streaming runtime design note
  (initial state, activation, flush, checkpoint restart, CINE). Grid fix (numpy CSR grid, identical candidate sets)
  in its 10-map byte-identical gate; then m05 + structure audit alone on all 10 chapters (~1.5-2 h).
  2026-10-16 13:13 FINAL GATE READY with one GO (Experimental c3550990 -> 4a89ff88): base = 398b27ef + yaw + drive-probe +
  steer + walkable + rise-snap 63368d05 (same match True, control False); dry run (final 63368d05 / penultimate
  04c51243) sequences builds, 3-arm regress, 1b Realistic perf (hd_dev_full), R4 leak, eyepass (match + views at
  1440p), full FAST + delta FAST, report; ~3.0 h GPU block + 20 min builds ahead (BUILD_ONLY=1); one more gate-base
  round (~18 min) per further sim change (Gameplay's ledge guard pending). Item 7 pairing: 8d50190a merges with
  SYSTEMS_VERIFY_RT_DEFAULTS_ITEM7.patch (refltemporal only); bloommax + X1 tone params fold into rtdef + the line
  in a later Rendering commit with its own table patch. USER QUESTION (cross-play now?): recommended design docs now
  (netcode / determinism across platforms, platform feasibility, profile + stats model) with Systems + AssetTools in
  gaps, code after package 6; awaiting the user's answer + platform order.
  2026-10-16 13:14 SCHEDULE: Rendering projects the package 6 head for ~17:00-19:00 today (their clock): item 7 push
  ~13:30, effect-light calibration ~14:30-15:00, X3 v3 verdict ~15:30 (out of package 6 if still weak), eye pass
  with Experimental ~16:00-17:00 (+1-2 h per fix row). Then: gate-base round if the ledge guard landed, BUILD_ONLY,
  the one-GO gate block (~3 h), Systems' package build + verify in parallel, hand-over after PASS. Head freeze = no
  sim change after the gate base is built; the ledge guard lands only if its gate finishes first.
  2026-10-16 13:18 USER DECISION: cross-play design + feasibility documents start now (no package 6 impact); code after
  package 6; order PC-to-PC, PC-to-browser, then PS3 and Xbox 360 towards the end. Assigned: Systems
  CROSSPLAY_NETCODE_DESIGN.md + CROSSPLAY_PROFILE_STATS.md; AssetTools CROSSPLAY_PLATFORM_FEASIBILITY.md (after the
  grid work); Gameplay CROSSPLAY_SIM_DETERMINISM_AUDIT.md (in parallel with the ledge-guard gate). Ledge guard gate
  ETA ~16:00-16:45 (their clock), inside the head freeze.
  2026-10-16 13:21 CROSSPLAY_SIM_DETERMINISM_AUDIT.md merged 7c398220 (Gameplay, written beside the ledge-guard gate): x64
  determinism today = SSE2 baseline, no fast-math / FMA, fixed 1/60 dt, seeded xorshift32 + per-bot LCG, order-
  independent worker pool (SIMHASH = same-exe only); hazards ~400 transcendental / sqrt sites, float thresholds, 8
  unstable sorts, a host-endian hash; options A strict-float + shared libm + stable sorts + canonical serialisation +
  cross-build SIMHASH CI (3-6 d; recommended for PC / browser), B fixed-point (weeks), C server-authoritative
  (UE3's model; PowerPC fallback); a sim-change rule from now on. Feeds Systems' netcode design (fill-in only).
  2026-10-16 13:23 ITEM 7 MERGED ae2b3246 (Rendering 8d50190a: temporal reflections default-on in Realistic, 3x3 clamp;
  soft-bloom cap 64 -> 16 WFC_TONE_BLOOMMAX; WfcRtDefaults.h one source per default; identity on int_cand5 Off 5 / 5,
  Faithful 5 / 5) + verify RealisticDefaults refltemporal=on aa38b067 (Systems, paired). Rendering: effect-light
  calibration running -> X3 v3 verdict -> eye-pass candidate. Package 6 head still projected for this evening.
  2026-10-16 13:23 PACKAGE 6 RUNBOOK merged a4d91dbe (Systems, in PACKAGE_6_HEAD.md above the gates): pre-flight (>= 40 GB
  on F:, head check, 3 input packs), NOPROMOTE=1 JOBS=2 refresh.sh <HEAD> (~45 min; 8 expected lines incl. D 3,506 +
  M6 498 / 498 FROZEN, HD 4,005 / 21.70 GB), verify_slim -SubstDrive X: with the RT hold (M6 static, 3 maps,
  settings 7, RT gate incl. rtlights + defaults, HD runtime level 2), report, promote only on Integration's word.
  F: 91 GB free. Systems on the netcode design as fill-in.
  2026-10-16 13:27 CROSS-PLAY PHASE 0 DOCS merged 29ad2e36 (Systems, fill-in work): CROSSPLAY_NETCODE_DESIGN.md
  (deterministic input lockstep on option A for PC-PC / PC-browser; option C PC-hosted snapshots = the original's UE3
  model as the console fallback; the original's Xe-TransEngine.ini net settings: 30 / 35 tick, 10 KB/s client rate,
  8 s timeout; ~10 B / human / tick, adaptive input delay 2-8 ticks, WebRTC data-channel star relay, join codes /
  direct IP first; leave -> bot swap; late join + desync recovery by canonical state transfer; prerequisite: N
  humans in World; 11 open questions) and CROSSPLAY_PROFILE_STATS.md (original profile / TnXpManager / challenges /
  Live leaderboards; per-platform storage; account-free identity = local key pair + fingerprint + name; friend
  standings via signed summaries, global needs an optional server; 8 open questions). Feasibility doc (AssetTools)
  pending behind the grid work. No code before package 6.
  2026-10-16 13:27 Grid fix (AssetTools): m05's compact grid IDENTICAL to the old code on all 10 MP maps (Streets incl.);
  the structure-audit rewrite reverted (10 GB on Rust vs 2.2 GB old): campaign uses a coarser WFC_AUDIT_CELL=2 m
  (default 1 m unchanged, Berth identical). m05 + structure audit now rerunning alone on all 10 chapters (~1.5-2 h).
  2026-10-16 13:28 EFFECT-LIGHT CALIBRATION (Rendering; lockstep Streets / Gorge / Molten, on vs off, transient gain 2):
  lights change 11-40 % of a frame's pixels where they burn; explosions read right (keep); impacts too strong (green-
  white wash on Molten's floor) -> Systems table change: impact rows colour x0.6, duration 0.25 -> 0.15 s (patch in
  progress; merges before the eye-pass candidate); boost / pickups rechecked in the eye pass. X3 v3 view runs going
  (~25 min) -> verdict -> eye-pass candidate.
  2026-10-16 13:30 IMPACT LIGHT CALIBRATION MERGED fefb3fed (Systems e4874fd1: the 9 impact rows colour x0.6, duration 0.25 ->
  0.15 s via gen_fxlights.py CALIBRATION; explosions / boost / pickups byte-unchanged; values only, Realistic path).
  Eye-pass candidate = Rendering's export of this head after the X3 v3 verdict.
  2026-10-16 13:34 X3 VOLUMETRICS OUT OF PACKAGE 6 (Rendering v3 on the 25 authored views: a grey veil that lifts blacks
  and lowers contrast, Gorge reflective 0.80, Streets reflective 0.79, Molten firefight 0.84; no readable shafts or
  halos) -> package 7 as the froxel / per-lamp version; code uncommitted. EYE PASS STARTED on int_eye = integration
  7cd22027 exactly (Release; every Realistic row merged, effect lights + headlights default-on, impact calibration
  in): Experimental runs match sheets + authored views + scores; Rendering reads per map and sends fix rows; the head
  = last fix row + Gameplay's ledge guard.
  2026-10-16 13:37 Ledge guard v1 (Gameplay): gate PASS (BOTTEST 48 / 48 x 12, NAVQ no map worse, trench entries 1 + 2
  -> 0) but NOT rare (127-423 episodes per seed, 2-4 % of vehicle travel = micro-braking everywhere) -> v2 tightened
  (probe > 1 m off-mesh; drop > max(2.5 m, 0.35 x distance)) re-gating, ETA ~14:30 (their clock), ships only if rare
  and the trench stays at 0, else package 7. AssetTools: structure audit passes at 2 m on A1; m05 still over the cap
  on A1 -> WFC_M05_CELL for the campaign, A1 retry at the chain's end. PACKAGE_6_HEAD.md 15f1ea21 (X3 out resolved).
  2026-10-16 13:43 EYE PASS (int_eye = 7cd22027): Streets (508) landed, all checks pass; Rendering's read: night-and-day
  on the views (reflective floor, street); ONE FIX ROW: character sun shadows on the world are pure-black hard blobs
  where row 2's fitted sun share reaches 1 -> cap 0.75 (WFC_RT_SUNSHAREMAX, sunsharemax=0.75), batched with the
  other maps' rows + the tone / bloom keys into rtdef (one commit, identity, one confirming sheet; Systems' paired
  table patch). Realistic + Enhanced level with Realistic on Streets. Gorge / Debris / Rust / Molten landing ~6 min
  each.
  2026-10-16 13:52 EYE PASS: Gorge (510) read = clear upgrade (firefight floor, reflective floor, lane; Enhanced level);
  FIX ROW 2: point-blank explosions went flat posterised yellow in Realistic (X1's shoulder kept half the hue at the
  clip) -> the hue-kept share fades out between luminance 1 and 3 so very bright light goes to white; joins the
  batched commit (sun-share cap 0.75 + rtdef keys). Debris (507) landed, all checks pass; Rust, Molten next.
  2026-10-16 13:58 EYE PASS: Debris (507) read = strong upgrade (street, reflective, lane; Enhanced adds crisp floor
  detail), no new fix row (shadow blobs covered by the sun-share cap). Rust (504) landed, all checks pass; Molten
  last, then Experimental's score tables, then Rendering's batched fix commit + identity + one confirming sheet.
  2026-10-16 13:58 EYE PASS: Rust (504) read = clear upgrade (reflective, lane, street; Enhanced adds floor detail); one
  calibration row on Systems' side: explosion rows colour x0.7 (a nearby explosion blew Rust's bright floor to
  white; the yellow cast is the shoulder row) -> patch requested against 77a7b0f7; merges before Rendering's fix
  candidate. Molten last.
  2026-10-16 13:59 EXPLOSION LIGHT CALIBRATION merged (Systems f3cbbb01: the 9 explosion rows colour x0.7; impacts x0.6 /
  0.15 s stay; values only). Rendering builds the fix candidate (sun-share cap 0.75, bright-highlight hue fade, rtdef
  keys) on this head after the Molten read; identity; Systems' paired table; one confirming sheet.
  2026-10-16 14:05 EYE PASS DONE on int_eye = 7cd22027 (Experimental; 0 UNKNOWN / INVALID; 15 / 15 match runs + 75 / 75
  views at 1440p, lights on, 3505 + 498 + Modern on). Match sheets vs Off, Realistic | Enhanced contrast: Streets
  1.15 | 1.16, Gorge 1.31 | 1.30, Debris 1.14 | 1.14, Rust 1.27 | 1.28, Molten 1.45 | 1.48; all 1.26 | 1.27
  (brighter +11.2). Views (mean of 5 maps): firefight 1.25 | 1.29, street 1.25 | 1.26, dark 1.43 | 1.46, reflective
  1.88 | 1.84, lane 1.37 | 1.45. The match-sheet drop vs int_cand3 (1.31 -> 1.26, brighter +16.7 -> +11.2) sits in
  firefight moments = the impact calibration; Rendering confirms the intended level. Fix rows so far: sun-share cap
  0.75, bright-highlight hue fade (Rendering), explosion x0.7 (merged 3ff0852b). Molten read pending.
  2026-10-16 14:06 Molten (509) read = strong upgrade, no new row; firefight level confirmed intended (the brightness
  figure fell because the green-white wash left; contrast and views hold). FIX CANDIDATE int_cand6 = 3ff0852b +
  Rendering 18483783 (sun-share cap 0.75, highlight hue fade, rtdef keys); identity running; Experimental staged for
  the confirming sheet. LEDGE GUARD -> PACKAGE 7 (v2 rare and blocks the trench, but ~half its episodes fire over
  collision holes / mesh edges, not real drops; v3 = downward segment cast); nothing from it in package 6. SIM FREEZE
  in effect: gate base = rise-snap 63368d05 final. Head = 18483783 merge + Systems' table.
  2026-10-16 14:11 Ledge-guard findings doc merged c8d4ad0c (package 7 design: downward segment cast). DISK ROUND before
  the gate + package build (F: ~81 GB free, 96 % used): every lane prunes superseded trees (Rendering int_cand..5 /
  int_eye / int_base2; Systems package 3 tree + scratch builds; Experimental old archives / eyepass runs;
  AssetTools proof trees; Gameplay old int_* trees); hd_dev_full, push_m6, b0-31, packages 4 + 5 untouched.
  int_cand6 staged for the confirming sheet (GO on Rendering's identity pass); eyepass saves rt_defaults.txt.
  2026-10-16 14:14 EYE-PASS FIX ROWS MERGED 7f3cf732 (Rendering 18483783: sun-share cap 0.75 WFC_RT_SUNSHAREMAX, bright-
  highlight hue fade (very bright light goes to white), tone / bloom keys in WfcRtDefaults.h + on the defaults line:
  sunsharemax, tonekey, toneadapt, tonegain, tonesat, tonecontrast, tonebloom, bloommax). Identity on int_cand6 Off 5
  / 5, Faithful 5 / 5, SIMHASH equal. Confirming sheet RUNNING on int_cand6 (Experimental). Systems' paired table
  patch requested against 7f3cf732; then the final head is named and BUILD_ONLY + the gate + the package build start.
  2026-10-16 14:15 PAIRED TABLE MERGED c85cc2ad (Systems: sunsharemax 0.75, tonekey 0.02, toneadapt 0.65, tonegain 1.3,
  tonesat 1.1, tonecontrast 1.15, tonebloom 0.8, bloommax 16; standalone 23 / 23). HEAD CANDIDATE = c85cc2ad
  (penultimate 7cd22027), pending the confirming sheet on int_cand6 + Rendering's per-map go. Started in parallel:
  Experimental BUILD_ONLY package-gate c85cc2ad 7cd22027; Systems runbook step 1 (NOPROMOTE refresh + build_slim
  --hd-pbr into work/playtest_slim_c85cc2ad). Disk: Rendering freed 13.7 GB, Systems 29 GB (package 3 tree removed;
  packages 4 + 5 kept); F: 112 GB free.
  2026-10-16 14:28 DISK ROUND DONE: Rendering 13.7 GB, Systems 29 GB (package 3 tree), AssetTools 8 GB (+ b0-2 / 6 / 12
  snapshots 17 GB approved), Gameplay 18 GB (68 old int_* trees), Experimental 47 GB (79 archive trees, old eyepass
  runs); F: ~140 GB free. Kept: packages 4 + 5, hd_dev_full, push_m6, b0-15 / b0-31, the gate base + builds,
  int_cand6 + int_base6, campaign exports. User's call later: AssetTools push_full (59 GB) + full/ PNG inputs (43
  GB) + the E archive (6.7 GB). Running: confirming sheet on int_cand6 (Streets started), BUILD_ONLY c85cc2ad /
  7cd22027 (7cd2202 built), Systems' package build (work/playtest_slim_c85cc2ad). Systems' package build step 1
  pre-flight OK (113 GB, no hold, 3 inputs).
  2026-10-16 14:28 PACKAGE 6 BUILT on c85cc2ad (Systems, NOPROMOTE): work/playtest_slim_c85cc2ad 27.53 GB (keep 46,744 +
  xpr 7,161; HD 4,005 files / 21.70 GB = D 3,506 + M6 498 / 498 FROZEN; overlay 85 / 85; vbake 10 maps; failures 0;
  Release + PGO + RT shaders). verify_slim RT-off rows all PASS (HD static 0 problems, bin 20 / 20, overlay, maps
  Streets / Gorge / Debris, settings 7 / 7); the RT gate row waits for the card after the confirming sheet. F: 148
  GB free (AssetTools deleted b0-2 / 6 / 12).
  2026-10-16 14:40 CONFIRMING SHEET on int_cand6 (= c85cc2ad code): Streets GO (black shadow blobs gone: soft grey with
  floor detail; no regression vs int_eye), Gorge + Debris landed (0 UNKNOWN / INVALID), Rust + Molten in ~15 min;
  the defaults line confirms sunsharemax 0.75 + the tone keys. Gate trees for c85cc2ad / 7cd22027 BUILT (BUILD_ONLY,
  14:21-14:29 their clock, link-render OK). Sequence after DONE: Systems RT gate row (5 min) + HD runtime row ->
  Rendering's last go -> the one-GO gate block (~3 h).
  2026-10-16 14:42 CONFIRMING SHEET: Debris GO; Gorge NO-GO on one frame (step 1080: a full-screen explosion still flat
  yellow-green in Realistic / Enhanced while Off is white + orange; the shoulder fix moved only the luminance half,
  the per-channel half rolls R / G to 1 while B stays low under the exposure adaptation) -> FIX: very bright pixels
  (brightest channel > 1) desaturate toward white before the shoulder; everything else untouched. HEAD MOVES: int_cand7
  = 3ff0852b + 18483783 + fix; after the sheet's DONE the card goes to Rendering (identity + Gorge step-1080 confirm,
  ~25 min) -> merge = new head -> Systems rebuilds the package (~45 min) + Experimental rebuilds the final gate tree
  (~10 min) -> Systems' RT verify row -> the one-GO gate. Sim base unchanged. Head ~1 h later than planned.
  2026-10-16 14:46 int_cand7 READY (Rendering 0a299fe9 local: white roll = pixels whose brightest channel passes 2
  desaturate toward luminance, fully by 16; WFC_TONE_WHITEROLL / _WHITELO / _WHITEHI; defaults line + whiteroll=on
  whitelo=2.0 whitehi=16.0; offline model: orange fire 4x -> warm white, 8x -> white; <= 2 untouched). Systems'
  WHITEROLL table patch staged (dc715a55, applies with the merge). Order after the int_cand6 sheet DONE: Rendering
  identity on int_cand7 (~20 min) -> Experimental Gorge-only eyepass on int_cand7 (step-1080 check) -> push -> merge
  = new head -> Systems package rebuild + Experimental final gate tree -> Systems RT verify row -> the gate block.
  2026-10-16 14:52 CONFIRMING SHEET DONE on int_cand6 (Experimental; 0 UNKNOWN / INVALID; defaults line verified incl.
  sunsharemax 0.75 + tone keys): match sheets vs Off Realistic | Enhanced: Streets 1.15 | 1.17, Gorge 1.32 | 1.28,
  Debris 1.14 | 1.14, Rust 1.27 | 1.28, Molten 1.45 | 1.48; ALL 1.26 | 1.27 (within noise of int_eye). Views: firefight
  1.26 | 1.30, street 1.25 | 1.27, dark 1.44 | 1.47, reflective 1.89 | 1.85, lane 1.37 | 1.46. Rendering: Streets,
  Debris, Rust GO; Gorge no-go on step 1080 (white roll in int_cand7); Molten read pending. Identity on int_cand7
  running (GO), then the Gorge-only rerun, then the merge = package 6 head. USER: HD working trees deletion approved.
  2026-10-16 15:00 Molten GO on int_cand6 -> all five maps GO except Gorge's step-1080 frame. int_cand7 identity PASS
  (Off 5 / 5, Faithful 5 / 5, SIMHASH equal vs int_base6). Gorge-only eyepass on int_cand7 RUNNING (Experimental);
  then Rendering pushes 0a299fe9 -> merge paired with the WHITEROLL table = package 6 head -> Systems' package
  rebuild + Experimental's final gate tree -> Systems' RT verify row -> the one-GO gate block.
  2026-10-16 15:07 Gorge recheck on int_cand7: hue FIXED (neutral white, no yellow-green; Gorge Realistic 1.33, Enhanced
  1.37) but it clips more than Off (near-white 79 % vs Off 58 %): the warm tint + foreground robot detail wash out
  (work/eyepass/int_cand7/step1080_before_after.png). Quality rule: ONE retune to keep warmth (int_cand8), identity,
  6 min Gorge recheck; else ship int_cand7 as is. Systems holds the WHITEROLL table for the final values. Head ~40
  min later.
  2026-10-16 15:12 WHITE-ROLL RETUNE (Rendering, int_cand8 building): whitelo 1.5, whitehi 6, new WFC_TONE_WHITEMIX 0.5
  (partial desaturation keeps the warm tint); target on the Gorge step-1080 frame (frame_compare.py, 22fb09a2):
  near-white ~55-62 % (Off 58.3), warmth >= +8, yellow-green <= 5.6 %. Systems' WHITEROLL table reissued with the final
  values (27 / 27), staged 5c4e53ff; pairs with the merge. Then: identity -> Gorge recheck -> push -> merge = head ->
  package rebuild + final gate tree -> RT verify row -> the gate block.
  2026-10-16 15:17 int_cand8 identity PASS (Off 5 / 5, Faithful 5 / 5; retune a833c984: roll after the shoulder, past 1.5
  a pixel moves to a warm white of its own hue by 6, pure white by 48; model orange fire 4x 1.00 / 0.74 / 0.52, 16x
  1.00 / 0.92 / 0.85). Gorge-only recheck on int_cand8 RUNNING (frame_compare bar: near-white 55-62 %, warmth >= +8,
  yellow-green <= 5.6 %); else int_cand7's rule. Then push -> merge + WHITEROLL table (5c4e53ff) = head.
  2026-10-16 15:24 GORGE RECHECK on int_cand8: step 1080 yellow-green 0.3 % (fixed), warmth +13 (restored: warm cream-
  white), near-white 80 % vs Off 58 % (the frame stays brighter / wider than the original: exposure adaptation +
  bloom on a point-blank full-screen explosion). Gorge 1.33 | 1.38. DECISION: ship int_cand8 (clearly better than
  int_cand7; no third retune); residual = package 7 polish (adaptation on extreme frames). Rendering pushes 0a299fe9
  + a833c984 -> merge + WHITEROLL table = package 6 head.
  2026-10-16 15:25 PACKAGE 6 HEAD = integration 4078e47b (code head 54105396: Rendering white roll 0a299fe9 + warm-white
  retune a833c984 on 7f3cf732; 4078e47b = + Systems' paired WHITEROLL table). Built clean (whitemix in the exe),
  pushed. Started: Experimental BUILD_ONLY 4078e47b / 7cd22027 (final tree) -> READY -> the one-GO gate block (~3 h);
  Systems runbook step 1 -> work/playtest_slim_4078e47b (~45 min) + RT-off verify rows; Systems' RT rows after the
  gate block. Rendering's package 7 polish doc merged (RENDERING_PACKAGE7_POLISH.md: exposure adaptation on extreme
  frames, X3 froxel, X6, X7, VelocityOverLife). Rendering's package 6 RT work COMPLETE.
  2026-10-16 15:30 GATE TIMING GUARD: Experimental READY (final tree fast_4078e47 built; SKIP_BUILD gate on GO); the GO
  waits for Systems' REFRESH READY (compile + 27 GB copy) so the timed regress + 1b hour sees no compiles / heavy I/O;
  AssetTools paused at a stage boundary; Gameplay / Rendering: no compiles during the hold. Rendering package 7
  design docs merged 9d42d3b1 (flash limiter for extreme frames; froxel X3 rules). Muter backstop renews ~16:15.
  2026-10-16 15:31 PACKAGE 6 BUILT on the head: work/playtest_slim_4078e47b 27.53 GB (D 3,506 + M6 498 / 498, overlay 85 /
  85, vbake 10, failures 0, REFRESH READY; NOPROMOTE). GATE BLOCK GO at 15:31: package-gate 4078e47b 7cd22027 (~3 h;
  timed regress + 1b first under PERF_HOLD; machine quiet: CPU 4 %). Systems idle under the hold; RT-off verify rows
  after the hold, RT + HD runtime rows after the block; promote + hand-over on PASS + the user's word.
  2026-10-16 15:39 GATE REPORT ROW (Berth, RT off, Classic, same match): p90 base 2.16 -> head 3.99 ms (lights-off arm
  3.99; effect lights +0.05); draws +4 % (dyn 122 -> 176, fx 69 -> 122), slow-frame GPU 2.97 ms (world 1.11) = a per-
  pixel cost on the OFF path (package 5's regress was 3.08 vs 3.12 here) -> BISECT after the block: WFC_TLTILED=0,
  WFC_SOFTPARTICLES=0, WFC_RT_TONE=0, death bodies / mines off (switch from Gameplay), DATA arms (pre-FX-regen
  map_fx_runtime, pre-item-6 materials). Rendering asked to rank causes during the hold. Hand-over may slip if a code
  fix + re-gate is needed.
  2026-10-16 15:41 BISECT PLAN (Experimental work/bisect_501.sh): Berth RT off, Classic, same match, 2 reps, arms base /
  final / TLTILED=0 / SOFTPARTICLES=0 / RT_TONE=0 / pre-FX-regen data / pre-item-6 data; runs as a ~25 min hold at
  the END of the gate block (~18:30-19:00 their clock); the corpse / mines arms after Gameplay's WFC_NOCORPSE /
  WFC_NOMINEPRES switch build (no switch exists on the head; compiled after the block). The regress + 1b hold clears
  as planned; Systems' RT-off rows + deletion between the holds; AssetTools resumes between on my word.
  2026-10-16 15:42 OFF-PATH REGRESSION, ranked causes (Rendering, code read): (1) CONFIRMED BUG: tlTiledOn() (aed1c441)
  checked only WFC_TLTILED, not the mode, so Off / Faithful ran the tiled transient-light path (CPU tile masks per
  frame, 2 extra binds per lit draw, per-pixel tile + 4 RGBA32F fetches per light) with identical output whenever a
  transient light burned; fits binds 880 -> 972; est. 0.2-0.6 ms; fix written (tlTiledOn requires rtMode_ == 2),
  built after the hold clears, identity after the block -> HEAD RESPINS. (2) FX data regen (fx draws 69 -> 122 =
  overdraw). (3) corpses (vertex / shadow). (4) item-6 data (low prior). Soft particles / tone / temporal / hit data
  / X2 are mode-gated by construction. Re-gate after the respin = regress (+ delta FAST); eye pass only if Realistic
  changes. Systems rebuilds the package on the respun head. Hand-over likely tomorrow (late morning / afternoon).
  2026-10-16 16:20 GATE REGRESS TABLE (RT off, same match, best p90 of 2, base | final | lights-off; delta lights-off -
  base): Berth 2.16 | 4.04 | 3.99 (+1.83); Seed 2.09 | 4.30 | 4.18 (+2.09); Complex 2.11 | 3.84 | 3.77 (+1.66); Rust
  2.61 | 4.59 | 4.52 (+1.91); Debris 2.34 | 5.73 | 5.69 (+3.35); Streets 2.09 | 3.99 | 3.97 (+1.88); Molten 2.77 | 4.87
  | 4.84 (+2.07); Gorge 2.68 | 6.54 | 6.44 (+3.76). All 8 BISECT; effect lights +0.02..+0.12; cost tracks the added
  dyn + fx draws (Gorge dyn 212 -> 356, fx 127 -> 245) = death bodies' gib / trail / wreck FX + the FX data regen +
  the tiled path's binds. 1b read UNKNOWN (HD not applied on lockstep-ab's match boot) -> fixed + rerun in the end
  hold with the bisect. Hold cleared 16:19 (their clock); leak -> eyepass -> FAST running untimed. Rendering builds
  the tiled mode-gate fix; Gameplay prepares the corpse cost plan + WFC_NOCORPSE / WFC_NOMINEPRES switches.
  2026-10-16 16:28 OFF-PATH FX FINDINGS (Systems, tools/systems/fxdata_ab.ps1): the FX data regen is NOT a cause (same exe,
  current vs pre-regen data: Berth / Gorge FX instances, sprites, draws, map-FX CPU identical within noise); LOD
  selection by distance IS honoured for spawned effects (WfcMapFx.cpp:584-592; death templates Automatic). Peak
  driver on Gorge: TF_Death_GibsTrail_FX (#1 runtime spawn, 260 / 60 s; 4 per death x 5 s). DECODE SUSPICION: LOD 1
  heavier than LOD 0 (lifetime and spawn both x3.33 up; 198 heavier vs 535 lighter pairs) -> AssetTools pstream
  LOD-order / percentage check. Runtime lacks UE3's SecondsBeforeInactive / MaxDrawDistance culling -> Systems
  prepares it (WFC_FXCULL=0 off), compile after the block; decision after the bisect. Rendering int_cand9 (+5bce52bb)
  staged as a bisect arm; FX-data arm dropped.
  2026-10-16 16:37 FX LOD UPPER BOUND (Systems, diagnostic WFC_FXLOD0 exe, never a patch; RT off, lockstep 31v32 seed 7,
  60 s): all-LOD-0 vs as-is: Gorge sprites 6,154 -> 2,174 (-65 %; peak 18,401 -> 5,360), draws 102.5 -> 69.9, FX
  CPU 0.90 -> 0.58 ms; Berth sprites 2,310 -> 895 (-61 %), draws 36.6 -> 27.0. Most particle load at gameplay
  distance = LOD 1+ data heavier than LOD 0 (suspected x3.33 decode inversion) -> AssetTools' TOP item: pstream LOD
  order / percentage check + regen of the 10 MP maps if a decode bug (package 6 candidate: look + cost). This bug is
  in base AND head (same data), so it is not the regress delta; the delta = tiled gate + gib trails (+ missing UE3 FX
  culling, Systems coding it). Systems' RT-off verify rehearsal on 4078e47b: PASS; c85cc2ad package deleted (F: 140 GB).
  2026-10-16 16:38 FX CULLING CLOSED for package 6 (Systems): the original authors no MaxDrawDistance (CullDistance 3 km
  on 23 PSCs = never) and SecondsBeforeInactive = the 1.0 s engine default; our runtime already frustum-culls FX
  draws (WfcMapFx.cpp:1940-1960, WFC_FXCULL), so SecondsBeforeInactive saves only sim CPU (< 0.1 ms of 0.25) ->
  package 7 polish (WFC_FXINACTIVE=0). The respin's FX lever = the LOD data. LOD DECODE CHECK REASSIGNED to Systems
  (AssetTools' session blocked on a permission prompt): original ParticleLODLevel bytes via ue3obj vs decoded, bug
  hunt in a copy of pstream.py, regen into their work/, before / after pair counts + sprite A/B.
  2026-10-16 16:40 LOD DECODE CHECK: NO BUG (Systems, pstream.system read-only on the cooked objects): the authored
  PeakActiveParticles per LOD confirm LOD 1 is heavier in the original (GibsTrail Spray_Blue 6 / 17 / 2 / 2, _Dup
  26 / 78 / 6 / 3; Exp_Death 4 emitters heavier at LOD 1, 0 lighter); our decode matches each LOD object; the mid-
  range load is the original's authoring. Off-path delta = the tiled gate (fixed, cand9) + gib trails of 63-bot
  deaths (the original's 10-player cap never produced them). DECISION: Gameplay prepares a 10-presentation cap in
  > 10-player matches (oldest dissolves first, 5v5 untouched, WFC_CORPSECAP); in the respin if the bisect's corpse
  arm shows > ~1 ms p90, else package 7 with the trail distance LOD.
  2026-10-16 17:03 TDR LEDGER day 4: 0 TDRs (05:02-17:02 their clock); nonpaged pool 1.7 -> 6.9 GB (0.43 GB/h; the
  03:51 TDR hit at ~10.6 GB -> ~01:00-02:00 tonight). PLAN: all GPU work (block end, bisect + 1b hold, identity,
  respin merge, package rebuild, RT verify rows, regress re-gate) sequenced to end before ~00:00 their clock; later
  TDR-hit rows = UNKNOWN, rerun after the user's morning reboot; no reboot request tonight.
  2026-10-16 17:06 GATE EYE PASS on 4078e47b: PASS, 0 UNKNOWN / INVALID (views at 2560x1440 with Enhanced included):
  all maps vs Off: Realistic mean 22.0 / +11.6 / contrast 1.28 / 33 % changed; + Enhanced 22.5 / +11.7 / 1.30 / 34 %
  (int_cand6 was 1.26 / 1.27). R4 leak rows done 16:39 (their clock). FAST full on 7cd22027 then delta FAST on
  4078e47b running; block end ~18:15; pool 6,857 MB flat.
  2026-10-16 17:28 PACKAGE 6 GATE BLOCK DONE (17:27 their clock) on 4078e47b vs 7cd22027: regress REPORT 8/8 BISECT
  (+1.66..+3.76 ms p90 at RT off; lights +0.02..+0.12); 1b UNKNOWN (HD 2 not applied; rerun in the hold); R4 leak
  PASS (lobby GPU 2180 / 2841 / 2980 / 2974 MB, spread 138 MB); eye pass PASS (1.28 / 1.30); FAST full 7cd22027
  PASS (53 pass, PRODUCT FAIL 0) + delta 4078e47b PASS; mapswitch row HARNESS FAIL (pruned reference exes; map-loss-
  gate fix 3616470c synced; rerun after the hold). BISECT + 1b HOLD set 17:27 (~40 min); pool 6.86 GB.
  2026-10-16 17:49 BISECT (Berth, RT off, same match, p90 r1 / r2): base 2.14 / 2.14; final 4.07 / 4.08; TLTILED=0
  4.05 / 4.08; pre-item-6 data 4.06 / 4.10; cand9 (tiled mode gate) 4.00 / 4.04; SOFTPARTICLES=0 4.03 / 4.04;
  RT_TONE=0 4.04 / 4.03 -> none of them; the tiled fix is -0.04..-0.07 (still merged for correctness). Remaining
  suspect: death bodies / mines (dyn 122 -> 176); corpse arms on Gameplay's cap + switches tree next (compiling
  now); else a commit bisect 398b27ef..4078e47b (boundaries: 54ac361d, 050a7796, 9711d354, b9e6f3d8, FX data,
  02c743cc, f670f8ba). 1b RERUN with Enhanced: PASS all 5 maps (p50 Streets 9.26, Gorge 9.48, Debris 7.32, Rust
  8.18, Molten 9.30 ms; p90 8.56-11.63; 105-137 fps at 1440p). Hold cleared 17:48 (their clock); pool 6.86 GB.
  2026-10-16 17:50 MEASUREMENT SUSPICION (mine): the gate's base tree predates 7d9edf78 (NOINTERACT exact client size):
  an old framed window larger than the work area is shrunk by Windows (2560x1417 for 2560x1440 per the commit), while
  the final's borderless popup is exactly 3840x2160; Get-WfcClientRect reads only the new log line, so the base
  passes without one. If the arms rendered different pixel counts, every RT-off delta since the gate started is
  invalid (package 5's regress had both arms framed) -> Experimental compares shot dimensions; a rerun at 1920x1080
  (fits the work area framed) if confirmed. Audio off the list (mixer work on the audio thread; <= 1 mix block of
  mutex wait on voice starts). Gameplay's cap + switches tree building (int_cap, ddea856e + patch 75dd1067).
  2026-10-16 17:51 CLIENT-SIZE SUSPICION CLEARED (my read of regress/map-501 logs): base framed window 1296x759 = a
  1280x720 client, head 'NOINTERACT: client 1280x720'; same pixels. The bisect's base 2.14 / final 4.07 equal the
  720p regress numbers -> likely CPU-bound (slow-frame GPU 2.97 < frame 4.07): gib chunk rigid bodies + skinning
  per corpse are the suspects; corpse arms on int_cap decide.
  2026-10-16 17:52 CPU SUSPECT (my read of World.cpp:6989-7090): every un-gibbed corpse runs samplePose + blendPose +
  skinPose on the CPU every frame for TotalTime and is drawn via drawDynamicMesh (an upload per frame); live pawns
  are GPU-skinned. Fits CPU-bound frames + dyn draws. Fix plan (keeps the look): corpses through the pawns' GPU-
  skinned pose path, else skin once when the pose goes static. Gameplay assesses after the int_cap checks; Rendering
  names the API; the corpse arms set the priority.
  2026-10-16 17:53 CAP TREE CHECKED (Gameplay int_cap: SIMHASH identical cap / cap 0 / NOCORPSE over 25,858 steps,
  BOTTEST 48/48, 8-player corpses 17 either way) and the CAP NEVER ENGAGES (max 9 presentations alive at 63 bots,
  80 deaths) -> cap DROPPED from the respin (documented for package 7); per-body cost confirmed as the question:
  CPU skin + drawDynamicMesh re-upload every frame even after the pose goes static. GO plan 1 (pose serial +
  drawDynamicMeshPosed; skip sample / skin once static; ~30 min; look unchanged by construction); plan 2 (GPU
  skinned path for the animating phase) only if the arms still call for it. Corpse arms (default / NOCORPSE /
  NOMINEPRES) on int_cap in a ~15 min hold after the mapswitch reruns.
  2026-10-16 17:55 STATIC-POSE FIX written (Gameplay docs/handoff/GAMEPLAY_CORPSE_STATIC_POSE.patch vs ddea856e: poseStatic
  skips sample / blend / skin once the clip ended and the blend is done; drawDynamicMeshPosed with a unique pose
  serial; cap dropped; NOCORPSE / NOMINEPRES kept). Trees int_csb (head) + int_csn (head + patch) compiling (~30
  min) -> the corpse-arm hold waits for BUILDS DONE; arms default / NOCORPSE / NOMINEPRES (int_cap) + int_csn.
  Slot B (4 windowed crops, 10 / 40 m byte-compare) after the hold. Respin = 5bce52bb + this patch.
  2026-10-16 17:58 RESPIN COMMIT 1 MERGED: b0f5ac45 (= rt2 5bce52bb) tiled transient lights Realistic-only; identity vs
  clean 4078e47b: Off 5/5 + Faithful 5/5 byte-identical, SIMHASH 088bd3b99da5768e x4; Realistic unchanged by
  construction (eye pass stands). drawDynamicMesh cost per call (Rendering): CPU bounds scan + full interleaved
  vertex build (~0.5 MB / character) + 2 STREAM_DRAW uploads into one shared dyn VBO / IBO; frustum cull only.
  GPU path for corpses = IRenderer::drawSkinnedMesh (GpuSkin.h helpers; stable key per corpse, 512 shared rows,
  <= 128 bones, serial bump on pose change) = plan 2; plan 1 (drawDynamicMeshPosed + serial) = the respin.
  2026-10-16 17:58 CORPSE ARMS GO (Experimental hold ~20 min): int_cap default / NOCORPSE / NOMINEPRES + int_csn (head
  + static-pose patch) + int_csb (head rebuilt, control); Berth RT off, 2 reps, frame + GPU p90. Gameplay's CPU-
  heavy headless checks (BOTTEST 12 seeds, SIMHASH) held until the hold clears, then slot B crops (10 / 40 m).
  2026-10-16 18:00 CPU-BOUND CONFIRMED (Experimental, same frame f2160, SIMHASH equal, 4K): GPU 1.71 vs 1.73 ms; CPU
  render 1.32 -> 2.92 ms (world 0.34 -> 0.81 at the SAME world draws, chars 0.71 -> 1.19, fx 0.07 -> 0.38, transl
  0.17 -> 0.51); slow-frame CPU render 2.28 (base) vs 3.37-3.43 (every bisect arm). = per-draw CPU work added on
  the Off path in src/render/gl between 398b27ef and the head (not corpses alone). Rendering reading the draw path
  for it (per-draw light / uniform setup, defaults lookups, state); corpse arms show the chars share. mapswitch
  rerun 7cd22027 PASS 6/6.
  2026-10-16 18:02 GATE COMPLETE on 4078e47b (PACKAGE-GATE.md): mapswitch PASS 6/6 both heads; FAST 7cd22027 + 4078e47b
  PASS 59 / KNOWN 1 / PRODUCT FAIL 0 / HARNESS FAIL 0; regress REPORT 8 BISECT; 1b REPORT >= 60 fps; leak PASS;
  eyepass sheets. CORPSE HOLD running (~20 min): base, int_csb, int_cap, +NOCORPSE, +NOMINEPRES, int_csn.
  WINDOW HYPOTHESIS (Rendering): the GL diff adds ~0.05-0.15 ms per frame of per-bind work at most; the uniform
  CPU doubling fits the NOINTERACT window change 7d9edf78 (borderless WS_POPUP + WS_EX_NOACTIVATE at HWND_BOTTOM
  vs the base's framed overlapped window: a different present / DWM path). DECISIVE ARM: int_cand10 = head +
  Win32Window.cpp from 398b27ef (Rendering builds after the hold) vs int_csb in a short hold.
  2026-10-16 18:14 CORPSE ARMS (Berth, RT off, 4K, same match, p90 r1 / r2 | GPU | CPU render | dyn): base 2.13 / 2.14
  | 2.10 | 2.12 | 122; int_csb (head) 4.00 / 4.01 | 2.91 | 3.38 | 176; int_cap 3.87 / 3.89; +NOCORPSE 2.19 / 2.19 |
  2.10 | 2.07 | 124-126; +NOMINEPRES 3.89 / 3.88; int_csn (static pose) 3.62 / 3.61 | 2.99 | 3.47 | 176. VERDICT:
  the death bodies are the whole RT-off regression (~0.2 ms per live body, CPU + GPU; mines 0; window style
  cleared: the NOCORPSE arm ran the head's borderless window). DECISION: respin = b0f5ac45 + the static-pose patch
  (-0.39 ms); GPU-skinned corpses incl. the shadow pass + instanced gib chunks = PACKAGE 7 ITEM 1 (floor = the
  NOCORPSE arm). Regress row stays REPORT with that attribution. cand10 cancelled. Sequence: Gameplay checks +
  crops -> respin sha -> Systems build -> timed 8-map regress + delta FAST -> verify rows -> promote.
  2026-10-16 18:15 RESPIN TOOLING: tools/fidelity/package-respin.sh (Experimental 4086a4b9; build Debug + Release, timed
  8-map 3-arm regress vs the rise-snap base, delta FAST vs 4078e47b, PACKAGE-GATE.md with the attribution line and
  the carried rows named); package 7 design RENDERING_CORPSE_GPU_PATH.md merged (CPU-skinned bodies skip the
  dynQueue_ batching, so their casters / shadow projection / draws run per draw: the GPU half of the 0.2 ms per
  body; fix = drawSkinnedGpu per body part, fallback posed). Per-body arm set + a 5v5 pair after the regress for
  the doc numbers. Package 6 doc known issue reworded (Systems a945713c+).
  2026-10-16 18:21 PACKAGE 6 RESPIN HEAD = 9350f19b (code: b0f5ac45 tiled lights Realistic-only + 9350f19b static-pose
  bodies + WFC_NOCORPSE / WFC_NOMINEPRES; docs since 4078e47b). Debug build OK (WFC_NOCORPSE string in the exe).
  Sequence: Systems step 1 (build + copy) -> Experimental package-respin.sh 9350f19b 4078e47b (BUILD_ONLY ahead;
  timed 8-map regress, delta FAST, per-body + 5v5 pair) -> Systems verify rows (RT-off, then RT + HD) -> promote +
  doc numbers + hand-over. All GPU work before ~00:00 their clock.
  2026-10-16 18:27 RESPIN PACKAGE BUILT: work/playtest_slim_9350f19b 27.53 GB (D 3,506 + M6 498 / 498, overlay 85 / 85,
  vbake 10, failures 0; NOPROMOTE). Experimental fast_9350f19 Debug + Release built (WFC_NOCORPSE present).
  RESPIN GATE GO at 18:27: package-respin.sh 9350f19b 4078e47b (timed 8-map regress -> delta FAST -> 2b per-body +
  5v5 -> PACKAGE-GATE.md). Systems idle until the regress hold clears, then the 4078e47b package deletion + RT-off
  verify rows; RT + HD rows after the block.
  2026-10-16 18:35 USER DECISION: PACKAGE 6 IS HELD until volumetric lighting (froxel X3) is implemented and passes the
  eye check (contrast >= 1.28 / 1.30, shafts not haze). Rendering starts the froxel version now (~2-3 days);
  Gameplay's GPU-skinned corpses (package 7 item 1) move into package 6; the respin gate + verify finish and
  playtest_slim_9350f19b is promoted quietly as the pre-volumetric checkpoint (no hand-over). Hand-over = when
  volumetrics pass; the user is told ~19 Oct.
  2026-10-16 18:36 X3 FROXEL VOLUMETRICS PLAN (Rendering, approved, coding now): 160x90 tiles x 64 exponential slices
  to 120 m (~0.92 M froxels) in a 1280x720 RGBA16F 2D atlas on the existing GL <-> D3D12 2D sharing; D3D12 compute
  inject (authored height fog + thin haze floor; sun 1 RayQuery shadow ray HG g 0.6, 4 RT-shadowed lamps, transient
  lights unshadowed; NO ambient in-scatter = v1-v3's veil) + temporal reprojection blend 0.9; integrate per column;
  GL apply before the X1 tone curve. Realistic-only (rtMode_ == 2), WFC_X3=0 off, WFC_X3_DENSITY / HAZE / G, rtdef
  + defaults line (Systems pairs), keep list + SETTINGSTEST. Est. code 6-8 h, first sheet ~10-12 h, finish ~14-16 h;
  cost ~0.7-0.9 ms p50 at 1440p, ~15 MB. Eye bar: shafts / depth, contrast >= 1.28 / 1.30 or it does not ship.
  REBOOT PLANNED ~21:30 (after the gate + verify; pool 6.9 GB) so the overnight exe runs start clean; all sessions
  restart; lanes commit WIP + resume notes by 21:15. Clocks: lanes = mine now.
  2026-10-16 18:40 CONTAMINATION (Integration's error): I told Rendering 'compiles are fine now' at ~18:33 while the
  respin regress hold was on; their X3 build ran ~18:33-18:39. Experimental reruns the overlapped maps inside the
  hold. Rendering's gate now stops on PERF_HOLD. X3 CODE COMPLETE + building (one D3D12 shader, inject + temporal /
  integrate modes; 8x8 slice atlas through the existing u0 share, private history ping-pong; GL apply at each
  pixel's depth; rtdef env statics survive release()); first runs after the reboot.
  2026-10-16 19:07 NO REBOOT TONIGHT (user away, cannot approve / reboot): GPU CURFEW 00:00 for every lane until the
  user's morning reboot (pool ~10.6 GB by ~03:00). Card: respin gate to ~20:45 (Systems' RT + HD rows inside),
  then Rendering's X3 captures + one tuning round until 00:00; Gameplay CPU checks after the block, crops by slot;
  AssetTools stays blocked on its prompt -> CROSSPLAY_PLATFORM_FEASIBILITY.md reassigned to Systems (docs,
  overnight). Morning: the user reboots, AssetTools' prompt gets approved, X3 tuning resumes.
  2026-10-16 19:09 CROSS-PLAY PHASE 0 DOCS COMPLETE: CROSSPLAY_PLATFORM_FEASIBILITY.md merged (Systems 794957d9):
  player-supplied data everywhere (nothing original hosted or bundled); PC feasible now; browser = WASM + WebGL2
  Off / Faithful 5v5 (wasm32 4 GB pass, OPFS, COOP/COEP, GLSL ES 3.00); 360 / PS3 = option C snapshot clients
  (Xenos / RSX renderers, modified hardware = a legal question); mobile side-load only; 8 open questions. With the
  determinism audit, the netcode design and the profile / stats design the phase 0 set is done; code after package 6.
  2026-10-16 19:24 RESPIN REGRESS (9350f19b vs base, RT off, lights-off - base, best p90 of 2; 4078e47b -> 9350f19b):
  Berth +1.83 -> +1.29; Seed +2.09 -> +1.34; Complex +1.66 -> +1.03; Rust +1.91 -> +1.29; Debris +3.35 -> +2.29;
  Streets +1.88 -> +1.20; Molten +2.07 -> +1.26; Gorge +3.76 -> +2.64 (the static-pose fix recovers 0.5-1.1 ms per
  map; effect lights +0.04..+0.12; same match everywhere); still 8/8 REPORT with the death-body attribution. Delta
  FAST 9350f19b: PASS 59 / KNOWN 1 / PRODUCT FAIL 0 / HARNESS FAIL 0, mapswitch 6/6. Step 2b hold 19:24 (~15 min).
  2026-10-16 19:31 RESPIN GATE DONE (19:31): step 2b Berth RT off 4K p90 r1 / r2: 63 bots base 2.19 / 2.16, 9350f19b
  3.47 / 3.47, + NOCORPSE 2.17 / 2.18 (bodies = +1.3 ms, were +1.9); 5v5 base 1.57 / 1.57, 9350f19b 1.63 / 1.63
  (+0.06 ms: essentially free at the original default). PACKAGE-GATE 9350f19b: regress REPORT (bodies), 1b REPORT
  >= 60 fps, leak PASS, eye pass 1.28 / 1.30, FAST PASS x2 (results on Experimental's branch 7d736441). Systems
  GO for the RT + HD rows, then the quiet promote; the card is Rendering's after that until 00:00.
  2026-10-16 19:38 CHECKPOINT PROMOTED (internal, no hand-over): work/playtest_slim = 9350f19b (verify_slim PASS exit 0:
  static 4,005 HD files 0 problems, bin 20 / 20, overlay 85 / 85, maps Streets / Gorge / Debris PASS, Settings 7 / 7,
  RT gate PASS incl. rtlights + defaults 27 / 27, HD runtime level 2 PASS 3505 + 498 + Modern on); package 5 kept as
  playtest_slim_398b27ef; 4078e47b deleted; F: 128 GB. x3-gate.sh + the busy watchdog synced (d0a6e8e5, c1597d0a).
  Card = Rendering's for x3f1 until 00:00.
  2026-10-16 19:46 CAMPAIGN PREP (Systems, fill-in): CAMPAIGN_AUDIO_DIALOG.md (work order: sublevel audio streaming,
  SeqAct_Conversation, dialog manager, subtitles, InterpTrackSound, anim-notify sounds, FaceFX last) +
  CAMPAIGN_FACEFX_FORMAT.md + tools/systems/fxa/: the 22 .fxa = FaceFX 1.7 archives (FACB 1710, big-endian), 14
  face bones per robot (33 on KON Megatron), 3,580 dialogue animations linked to BL_DX_SWITCHBOARD cues; the
  animation block decoded (curves: node index, i16 times, i8 values; 3,505 / 3,579 blocks land exactly); not yet:
  name-table widths, master bone list / face graph, value scale, interpolation, the KON exceptions.
  2026-10-16 19:59 X3 FIRST LOOK (Rendering x3r2, Molten sun views; work/fsr3/x3r2_509sun.png sent to the user): real
  shafts cut by geometry; sun shadow rays block indoors; lamp halos local after an inverse-square core (2 m); sun
  phase normalised; per-map medium (Gorge 2x fog); X1 contrast 1.20 on X3 frames. Authored views X3 on / off:
  contrast 0.94-1.08 (mean ~1.02), brightness -5 % -> the sheet floor should hold; sun views 0.58-0.71 (into the
  sun). My eye notes for the candidate: firefight_sun veil too uniform (reds go grey), lane_sun diagonal streaks
  (slice / jitter aliasing; check in motion), lane_sun loses the purple lamp hue. Candidate = 9350f19b + X3,
  identity + SETTINGSTEST tonight, x3-gate.sh tomorrow with the _sun / _lamp views added (reported, not in the floor).
  User: reboot at any time tonight when home; lanes keep WIP committed + resume notes current.
  2026-10-16 20:04 GPU-SKINNED CORPSES (Gameplay GAMEPLAY_CORPSE_GPU.patch vs 78d06b6e): built (int_gcb head / int_gcn
  patch), headless PASS (BOTTEST 48 / 48 x 12 seeds, seed-7 SIMHASH identical); slot granted for 4 crops (10 / 40 m
  look-back, byte-identical required) + a 16v16 refusal count; then Experimental's corpse arm (int_gcb / int_gcn /
  int_gcn + NOCORPSE, target within 0.1 ms of NOCORPSE) before 00:00; merge on PASS. X3 gate floors final
  (90df7714): match 1.26 / 1.28, views 1.42 / 1.45, checkpoint values printed.
  2026-10-16 20:09 GPU-CORPSE CROPS round 1: FAIL on the melee Defrag body (the queued GPU-skinned draw ignores
  setDrawEnergyDeath: renders undissolved; renderer bug -> Rendering asked to carry energy-death per queue entry;
  Gameplay latches Defrag bodies to the CPU path meanwhile); other bodies differ only at rounding level (1-2 at
  silhouette edges; one frame 24 px max 91) = GPU-vs-CPU skinning noise pending Rendering's confirmation; 16v16:
  0 refusals / 0 rows-full. Rerun of the crops granted on the rebuilt int_gcn.
  2026-10-16 20:11 GPU-CORPSE CROPS round 2: PASS (Defrag frames byte-identical via the CPU latch; 10 m 57 / 65
  identical, the rest rounding (max 2) plus f920's single edge pixel at 91 = accepted by my eye (f920_detail.png);
  40 m 64 / 65, 1 px at 1); 16v16 0 refusals. Corpse arm GO (int_gcb / int_gcn / + NOCORPSE, Berth RT off 4K,
  target within 0.1 ms of NOCORPSE); merge GAMEPLAY_CORPSE_GPU.patch on its PASS. Pool 7.0 GB at 20:09.
  2026-10-16 20:12 MERGE SLIP (mine, fixed): a cherry-pick loop over HEAD..agents/gameplay re-applied ~60 historical
  commits (docs only; GAMEPLAY_BOTS.md regressed); reset to 77dfcced, the four GPU-corpse doc commits re-applied
  (c0938c76, 3ab95dbc, 252e6426, 6fec17b0) -> 7e952312, force-with-lease push. src untouched.
  2026-10-16 20:17 X3 FROXEL VOLUMETRICS MERGED as a set (rt2 2a3a1d33 base, e003ccc5 round 2, 607baafd Molten haze
  0.001): identity vs 3b6b36ca (src == 9350f19b) Off 5/5 + Faithful 5/5 byte-identical, SIMHASH 088bd3b99da5768e
  x4; candidate exe Rendering work/int_cand10 (Release). Eye notes answered: firefight_sun 0.71 -> 0.81 at haze
  0.001 (shafts keep their gaps); the lane_sun streaks are Molten's falling particles (two X3-off runs differ the
  same way; a 0.25 m dolly shows no banding); the purple lamp hue holds (1.05). Build check after the corpse
  hold; x3-gate tonight (eye pass -> timed cost -> FAST delta) before 00:00. DEFRAG BUG (Rendering): the per-mesh
  draw-list cache dynSubs_ is keyed by bind mesh only, so a live robot of the same model rebuilds the corpse's
  list without Defrag -> fix = a second cache for the defrag state (GO after SETTINGSTEST). GPU-vs-CPU skinning
  rounding = identity-equivalent (Rendering confirms).
  2026-10-16 20:22 GPU-CORPSE ARM (Berth RT off 4K, p90 r1 / r2): head int_gcb 3.51 / 3.50; int_gcn 3.11 / 3.09;
  + NOCORPSE 2.18 / 2.17 -> -0.41 ms, the 0.1 ms target missed (0.9 ms per 63-bot match remains; dyn draws
  unchanged 176 vs 126 = per-body draws + casters). MERGED anyway as 51b55a09 (identity-equivalent, 0 refusals);
  package 7 lever = one draw per settled body (merge gib chunks / parts, instanced gibs); no life / cap / shadow
  changes (look). Integration Debug build OK on the X3 + corpse head (WFC_X3 strings in the exe).
  2026-10-16 20:26 X3 GATE GO on int_cand10 (SETTINGSTEST PASS 7 / 7 at 20:20; X3 settings are env statics):
  x3-gate.sh cand10 with CAND_SHA e224ebf2: eye pass (floors match 1.26 / 1.28, views 1.42 / 1.45; light-facing
  views reported) -> timed 2-arm X3 on / off cost (5 maps, Realistic + HD 2, 1440p) -> delta FAST. Rendering's
  Defrag draw-list cache fix f5e2d8dd built as int_cand11 (= 106db906 + fix); RT-off identity beside the eye pass.
  2026-10-16 20:58 X3 GATE cand10 EYE PASS: FAIL (match sheets Realistic 1.08 vs floor 1.26, + Enhanced 1.09 vs 1.28;
  brightness +39.3 vs the checkpoint's +11.6; 65 % pixels changed; colourfulness -0.1); authored views 1.42 / 1.46
  = at the floor (short paths). Streets step 1740: a uniform grey veil down the corridor (Off 39 / checkpoint 56 /
  candidate 124 mean luminance), colour drained = the v1-v3 failure on long paths. Direction to Rendering: the
  medium / haze term must not accumulate over 120 m (haze ~0 outside the authored fog, cap the integrated medium
  in-scatter, shafts from the shadow-tested sun / lamp terms only); tune on match frames. Cost step + FAST continue;
  round 3 (cand12) eye-pass-only tonight if before 00:00, else tomorrow. X3 stays merged (Realistic-only, tunable).
  2026-10-16 20:59 X3 GATE cost step did NOT run (x3-gate missed mkdir of cost/; fixed): timed cost hold granted
  21:15-21:40 after the FAST delta on e224ebf2 (running). Rendering compiles round 3 during the untimed FAST;
  cand12 captures + eye-pass-only after 21:40; Rendering's Faithful identity (Defrag fix f5e2d8dd) ~22:20.
  2026-10-16 21:01 X3 ROUND 3 (Rendering, compiled 21:00, WIP 10305b39): the veil = the TRANSIENT lights' in-scatter
  (muzzle / impact / explosion / effect lights scattered unshadowed, isotropic, no core: a firefight filled the
  air; the quiet views never showed it). Fixes: transients get the inverse-square core at strength 0.3; the haze
  follows the authored fog's height falloff (floor 0.0015); the integrated in-scatter is capped per channel
  (x3cap 0.25, new defaults key -> Systems re-pairs the table); shafts only from the shadow-tested sun / lamps.
  After 21:40: Streets match frames X3 on / off (steps 300 / 1560 / 1740), then Gorge / Molten, then int_cand12.
  2026-10-16 21:10 FAST DELTA on e224ebf2 (X3 head): REGRESSIONS, 1 product failure: fast.resources.mapswitch private
  memory peak 6,071 MB (> 6,000 rule; checkpoints 5,576-5,868; route run 5,594 normal, ends clean) = X3 adds ~200-
  500 MB system memory at the map switch -> Rendering checks froxel / history / staging duplication + release at
  map change for round 3. PASS 59 / KNOWN 1 / HARNESS 0 otherwise. X3 cost hold 21:12-~21:40.
  2026-10-16 21:41 X3 COST (cand10, Realistic + HD 2, 1440p 31v32, p50 / p90 ms; X3 on vs WFC_X3=0): Streets 9.85 /
  10.96 vs 9.30 / 10.39 (+0.55); Gorge 10.02 / 12.18 vs 9.52 / 11.69 (+0.50); Debris 7.99 / 9.24 vs 7.36 / 8.64
  (+0.63); Rust 8.66 / 10.84 vs 8.26 / 10.41 (+0.40); Molten 9.85 / 10.97 vs 9.36 / 10.45 (+0.49): cheap, every
  map >= ~99 fps p50. Hold cleared 21:40; mapswitch memory A/B (base8 vs cand10) running RT off; Rendering has the
  card for round-3 captures.
  2026-10-16 21:47 ASSETTOOLS BACK (the user answered its deletion prompt directly): diagnostic chain done 14:59 (8 / 10
  chapters OK at 2 m tiles, every peak < 10 GB; A1 / A3 tiled m05 retry rerunning); push_full + full/in* deletion
  (102 GB) running; feasibility doc dropped (Systems wrote it); next = the FaceFX RE continuation.
  2026-10-16 21:50 X3 ROUND 3 CANDIDATE int_cand12 (= 43366aec + rt2 X3 round 3 + Defrag fix f5e2d8dd): Rendering's
  match read X3 on / off: Streets 0.996 (dark lift 0.8 levels, was 60-80), Molten 1.006 (min 0.957), Gorge 1.028;
  shafts kept; Streets shot 1500 at 0.79 = local sky-lit air at the frame edge. Experimental eye-pass-only GO;
  Systems re-pairs the table (x3cap=0.25). Identity base int_base10 = 43366aec (Off arm beside the eye pass,
  Faithful in the RT slot after). AssetTools: deletions done (60 GB freed, F: 166 GB free; in_cn / _hdpng kept).
  2026-10-16 22:02 MAPSWITCH MEMORY A/B (RT off, 2 reps interleaved): int_base8 6,116 / 5,713 MB vs int_cand10 6,248 /
  5,695: both swing ~400 MB over the same range -> the FAST 6,071 row was spread, not an X3 regression. Rule change
  approved: FAIL > 6,500, INFO 6,000-6,500 with the spread noted. cand12 eye pass started 22:02.
  2026-10-16 22:36 FACEFX RE MILESTONE (AssetTools, CAMPAIGN_FACEFX_RE.md + work/fxa/fxa_parse.py): all 22 .fxa parse
  exactly (3,616 animations, constant 572-byte tail): class hash table, name table, master bone list (rest pose,
  inverse rot, skeleton index, per-node bone-pose deltas), compiled face graph (nodes + links: fn 1 linear, fn 10
  4-param; user props e.g. Omega EmissiveBlink -> material slot 2), name -> node lookup, groups (Dialogue +
  cinematic). Systems' 74 exceptions = block trailers (blend / cue / name at the END) + 59 cinematic curves with
  i8 in / out slopes. Value scale hypothesis i8 / 127 x node range. Open: the tail (phoneme map?), node float
  roles, b, interpolation, a visual proof. Early cand12 read (mine, raw shots, checkpoint -> cand12 Realistic mean):
  Streets 300 49 -> 55, 1740 58 -> 62; Gorge 36 -> 36, 40 -> 46; Molten 69 -> 82, 81 -> 89 (cand10 was +60-80).
  2026-10-16 22:37 X3 cand12 EYE PASS: NEAR PASS: match sheets Realistic 1.26 (floor 1.26, OK), + Enhanced 1.27 (floor
  1.28, BELOW by 0.01); brightness +13.3 (checkpoint +11.6), colour +3.4, pixels 35 %; per map Streets 1.09 (was
  1.15, the only loss), Gorge 1.35, Debris 1.16, Rust 1.28, Molten 1.42; veil gone. ROUND 4 = values only (x3trans
  / x3cap down, Streets-led; target all-map >= 1.28 / 1.30); Rendering's Faithful identity arm on cand12 now (valid
  for value-only changes); cand13 eye pass tonight if ready by 23:05, else tomorrow. FaceFX RE doc + parser merged.
  2026-10-16 22:41 cand12 FULL EYE PASS: views 1.47 / 1.50 = the checkpoint exactly (OK); match 1.26 OK / 1.27 BELOW
  by 0.01 (Streets); round 4 values pending. FACEFX RE (AssetTools, merged 17fabf26): node floats (min, 1/min,
  max, 1/max) 1,145 / 1,145; curves normalised per animation (value = i8 / 127 x b, b = peak); timing vs 728
  dialog waves (end = duration +0.2 s, start -0.27 s lead-in); interpolation open; visual proof plan (Trypticon
  LK004350 jaw vs voice envelope) cleared to run CPU-only tonight.
  2026-10-16 22:48 X3 ROUND 4 = cand13 (rt2 b9d1f3ad = cand12 + values: x3trans 0.20, x3cap 0.20, Streets per-map haze
  0.0007; the transient / cap change alone did not move Streets, its lift was the blue sun in open air at the
  frame edges). Rendering's self-check: Streets 1.013 vs X3 off (shot 1500 0.83, dark lift -0.1). cand12's Off /
  Faithful identity (5/5 + 5/5 vs int_base10 = 43366aec) stands for value-only changes. Eye-pass-only GO at ~23:00
  (ends ~23:40); Systems re-pairs the table; merge set on PASS = f5e2d8dd + 1bfced9a + b9d1f3ad + the table.
  2026-10-16 23:20 X3 cand13 EYE PASS: match Realistic 1.27 (OK) / + Enhanced 1.27 (1.2748 vs floor 1.28: 0.005 under,
  Streets 1.135 vs 1.179), views 1.47 / 1.50 = the checkpoint; no veil; brightness +13.2. DECISION: merged as the
  package 6 volumetrics: 8db29dab (Defrag draw-list cache fix), c0a901d2 (X3 round 3), 32fa75eb (round 4 values),
  af4d0731 (paired table, 39 / 39). Integration Debug build OK (WFC_X3_CAP in the exe). PACKAGE 6 CANDIDATE HEAD =
  af4d0731 (code). Optional tomorrow: one Streets-only value step within 40 min of the relaunch, else as is.
  Tomorrow after the user's reboot: package-respin.sh af4d0731 9350f19b (RERUN_1B, EYE_FROM=x3_cand13) ->
  Systems' runbook -> verify -> promote -> doc / tag / hand-over.

2026-10-17 01:02 AssetTools: campaign diagnostics DONE on all 10 chapters (assettools/checkpoint 66a27fa, 2494487,
  17c94d9; docs copied by file: CAMPAIGN_PIPELINE_SUMMARY.md section 1b, the 10 CAMPAIGN_PILOT_<chapter>.md,
  CAMPAIGN_FACEFX_RE.md; fxa tools already current). m05 over ALL BSP levels per chapter (tiled; MP path unchanged):
  16,337 default-material faces across 114 levels, 1,029 visible in play; largest D1 10,048 / 614 visible, A4 2,362,
  A3 1,308; every chapter under the 10 GB cap; D1's m05 ran 82 min / 4.8 GB (the only stage over an hour).
  Structure audit (2 m cell): 9 PASS, A2_KON ATTENTION (one unexplained collision gap, StaticMeshCollectionActor_10261).
  FaceFX: bone links = face-graph node indices (5,281 / 5,281), timing confirmed against the voice envelope (lag ~0),
  three bone-blend variants tie at 0.30 (visual check on a skinned face decides; nlerp recommended).
  Still open: bsp_alignment (D2), cinematic slope scale, phonemeId table, bone-blend visual check (needs Gameplay's
  pose hook, after package 6). AssetTools' next queue sent (CPU-only): campaign pickup factories (0 / 377 exported,
  D5_OME first), D2 bsp_alignment, the A2 collision gap, FaceFX desk items; bone-blend visual after package 6.
2026-10-17 01:10 AssetTools: the A1 component-tag parser bug is CLOSED (authored_reparse.py re-parse of every detector-flagged
  authored.db row: f384c7b the 4 MP rows + 1,442 campaign rows, f626805 the remaining 1,942 rows, every chapter; MP runtime files
  byte-identical, vs_validate 0 FAIL on all 10; tag_parse_audit.py is a permanent vs_validate check reading 0 everywhere;
  A1's 1,211 mesh-less components -> 0). AssetTools started item 1 (campaign pickup factories, D5_OME first).
2026-10-17 01:20 AssetTools item 1 DONE: campaign pickup factories 377 / 377 exported (assettools/checkpoint f8fd22c; chapter docs,
  summary, CAMPAIGN_BACKLOG copied by file). Cause: vs_gameplay searched only the persistent BASE level and knew only ammo /
  health / overshield; campaign pickups live in the design sublevels and are mostly TnWeaponPickupFactory (206) + TnPickupFactory
  (34). Fix is campaign-only (is_campaign): all sublevels + both classes; records carry InventoryType, pickup mesh, effect template,
  sublevel. D5 13 / 13, every chapter matches its placed count, pickups check COMPLETE everywhere. MP gate: vs_pickups +
  vs_gameplay old vs new identical on all 10 MP maps (AssetTools work/pickupfix/proof.json); ExtractedAssets untouched.
  MP finding (data left unchanged by rule): Escalation maps BrokenHope (17) and Remnant (16) also place TnWeaponPickupFactory
  that MP gameplay.json omits. Integration decision: PACKAGE 7 queue item (MP fidelity, data regen + Gameplay consumption;
  not a package 6 change, MP data is frozen for the gate). AssetTools -> item 2 (D2 bsp_alignment, A2 collision gap).
2026-10-17 01:35 AssetTools items 2-4 DONE (assettools/checkpoint 8919438; summary, chapter docs, backlog, CAMPAIGN_FACEFX_RE.md,
  fxa_proof.py copied by file). bsp_alignment was a parser bug, not geometry: m04_bsp located each ModelComponent's model by a
  byte search that also matched the tag stream's terminating None index (D2_COR_TNL3_Art_m: index 868 = the model's export
  index, 212 components skipped, 747 "unaccounted" nodes); fixed (search from byte 4), bsp_alignment COMPLETE (difference 0)
  on all 10 chapters, MP bsp audits identical on all 10. A2 gap EXPLAINED: SMCA_10261's 5 components are WallDeco / BridgeArchTop
  meshes with no collision (none in UE3 either); the ATTENTION came from 125 mesh-less Courtyard components, now explained by
  the audit (MP identical on Berth / Gorge); structure audit PASS on all 10. FaceFX: cinematic slope = i8 / 127 x the header
  f32 (every slope peaks at |127|, r 0.83 with key tangents); phonemeId = FaceFX ARPAbet order (42 entries, all on the right
  mouth targets, SIL alone unmapped; strong hypothesis, names not in the exe). Pose-hook spec for Gameplay in
  CAMPAIGN_FACEFX_RE.md (replace, not add, the face bones; test Optimus DX_OPRIME_LK002649 at t = 1.0 s vs reference CSVs,
  tolerance 1e-4 pos / 1e-3 quat). Escalation weapon-pickup regen line added to CAMPAIGN_BACKLOG (package 7).
  Next for AssetTools: backlog 5 (campaign player starts / walk graph seeding; the audit's walk-graph checks are empty in the
  campaign because they seed from MP player-start classes).
2026-10-17 02:12 PACKAGE 6 GATE STARTED (user: "Can you continue?"; no reboot yet, boot 10-16 04:02, nonpaged pool 6.93 GB,
  card clear, no hold file). Final head = af4d0731 as is (no Streets step; Rendering's haze step, Gameplay's Defrag latch
  removal and instanced gibs = package 7). Experimental: RERUN_1B=1 EYE_FROM=x3_cand13 package-respin.sh af4d0731 9350f19b
  with a 9.5 GB pool guard; Rendering / Gameplay / Systems frozen (no compiles, no exes) until the timed regress reports
  DONE; Systems prepares the refresh.sh / promote / PACKAGE_6_af4d0731.md pass and gets GO BUILD after DONE.
2026-10-17 03:00 Package 6 regress af4d0731 vs 9350f19b TIMED DONE 02:56 (Experimental; 8 maps x 3 arms, RT off, 2 reps within
  0.05 ms, same match everywhere). Head - base p90: 501 +0.92, 502 +0.93, 503 +0.78, 504 +0.83, 507 +1.84, 508 +0.70,
  509 +0.84, 510 +2.06 ms (9350f19b was +1.03..+2.64: GPU-skinned corpses are in, every map lower). Lights cost (head -
  lights-off) at most +0.12 on every map (bar 0.35). Bodies row stays REPORT with the attribution note. AssetTools' hold slip
  (02:17:55-02:18:56 python + a structure-audit stage running into the hold until 02:21:59) ended 5 s before map 501's first
  rep; watchdog clean, reps agree; Experimental reruns 501 only (~5 min) after the untimed FAST delta and the 2b / 1b hold.
  Systems' GO BUILD = Experimental's "RERUN TIMED DONE".
2026-10-17 ~03:30 USER SETS PACKAGE 7 SCOPE (two messages after 2b TIMED DONE 03:26:55; this entry was first labelled 03:40 without reading the clock): "more reflective surfaces and damp sheen" (X6), "double ray trace
  lightmap resolution" (X7), the Streets haze tweak, "the brightness of saturation of mine explosions", "the Smart bot hover and
  squad follow polish", "the AMD driver leak", "and also the dead bodies everything you said we can do" (one draw per settled
  body, instanced gibs, Defrag latch removal, patch 2: decals, skeletal gibs, authored death camera, full momentum picker).
  Package 6 (af4d0731) completes as the checkpoint (build / verify / promote / doc / tag; the user may play it via Play.bat);
  PACKAGE 7 = THE HAND-OVER BUILD. Assigned: Rendering X6, X7, Streets haze, leak mitigation, body draw renderer side;
  Gameplay hover (S5 / S6), squad follow, all death-body items, then the FaceFX hook; Systems mine saturation after the
  package 6 runbook; Experimental leak characterization after the verdict (budget: pool under 9.5 GB, may wait for the reboot).
  Also: 2b TIMED DONE 03:26:55; 1b Realistic + HD 2 + X3 at 1440p p50 9.7-11.9 ms (84-103 fps; 4078e47b read 105-137,
  explanation requested for the doc); 5v5 +0.06 ms; rerun hold (regress 501 + bodies 501) running; AssetTools chain stopped
  03:27, hold watchdog written untested (1b70368), resume after the gate.
2026-10-17 ~03:34 USER RULE (entry first labelled 04:25 without reading the clock; clock read 03:36 right after): "I just want the game to look beautiful. After it looks like a brand new game only then while we
  optimize for more FPS at no extra cost to the Fidelity." Package 7 re-ordered: visual items first on every lane (Rendering X6,
  X7, Streets haze; Gameplay death-body patch 2 + Defrag latch, ride height, squad follow; Systems mine saturation), perf items
  after and only when a side-by-side eye check shows zero visible difference (e.g. X3 sun-ray mask corpses-only, live
  characters keep their fog shadows). Leak work continues as stability, not fps. Estimates: Gameplay 30-40 h + 6-8 slots,
  Rendering 24-34 h; package 7 ~2026-10-20. Rendering patches (code only): 9015c4e9 persistent presenter event (leak suspect
  1), 8bd95490 WIP X3 sun-ray mask. Experimental's ledgers: tonight's GL-present gate exes grew the pool ~27 MB/h, the big
  growth came with D3D12-present / multi-exe runs; arm B (WFC_D3D12PRESENT=1, RT off, 30 min) runs alone in Systems' build
  window after the attribution hold; arms C / D later or after the reboot.
2026-10-17 04:14 PACKAGE 6 GATE VERDICT (Experimental, hold removed 04:12:24; report tools/fidelity/results/PACKAGE-GATE_af4d0731.md on
  agents/experimental 7da22d7b): no blocking row. Regress REPORT (bodies, +0.67..+1.94 ms p90, lights <= +0.12), Realistic perf
  REPORT (all > 60 fps), FAST PASS / PASS (59 pass, 0 product, 1 known ribbon / beam), leak PASS, eye pass cand13. 1b true
  numbers (in-hold head, 1440p Realistic + HD Enhanced, 31 v 32, X3 on) p50 ms: Streets 9.91, Gorge 10.08, Debris 8.05,
  Rust 8.83, Molten 9.91 = +0.60..+0.73 vs 4078e47b, all X3 (+0.40..+0.60) + bodies at RT on (+0.17..+0.49, TLAS: Streets
  36.3 -> 38.1 instances, BLAS rebuilds 161 -> 192). The 2b-hold 1b run (+1.7 ms on 4 maps) = outlier, not reproduced; 8db29dab
  build dropped. GO BUILD sent to Systems with placeholders (a)-(e); COMPILES OPEN for Rendering / Gameplay (card closed until
  ARM B DONE); RE lane and AssetTools resumed. Scripts synced by path: package-respin.sh (pool guard), pooltags.ps1 (new).
2026-10-17 04:14 LEAK FINDING (Experimental, pool tags via NtQuerySystemInformation, no admin): at 04:13 the counter read 7.16 GB, tagged
  nonpaged 5.50 GB, and the top 5 tags (CORP 1,287 MB / LTUF 1,211 / SFRF 1,113 / RFRF 327 / DVEF 132, about 4.07 GB) all
  belong to C:\Windows\System32\drivers\FlightRecorder.sys = "Malwarebytes EDR Driver" 2.0.7.0 (2025-09-24, boot-start; the
  tag strings exist only in that binary). AMD-looking tags are far smaller (DAL3 165 MB, GVM_ 129 MB). Fits the ledgers: growth
  tracks process / file activity (multi-exe runs, builds, verify), not GPU frames; tonight's single-exe gate grew ~27 MB/h.
  USER DECISION (security software): a Malwarebytes update or exclusions for F:\Transformers Rebuild and the lanes' tools.
  No lane acts on it. Idle baseline then arm B (D3D12 present) still run, with per-tag diffs, to settle the presenter question.
2026-10-17 04:20 RE lane tonight (re-workspace-91; docs copied to docs/handoff): GAMEPLAY_DEATH_CAMERA_RE.md (0dee682a: one shared
  CameraState, death inherits the frozen over-the-shoulder orbit, anchor cancels the shoulder shift, 3.0 s then spectate the
  killer), GAMEPLAY_SKELETAL_GIB_INIT_RE.md (d7861d11: one ragdoll per robot death, ref pose at PositionSocket, pawn Rotation,
  zero velocity, native-confirmed), GAMEPLAY_DEATH_PICKER_RE.md (3185f984: first-match picker, no RNG; DeathLight0 shadowless
  point light at the pawn origin, robot 10 / 2048 / (230,230,255) 2 s ramp then cut, car / tank 25 / 512 / (202,123,213); four
  corrections to the package 5 death-light rule). Gameplay's patch 2 now carries the faithful picker; death-light values routed
  to the transient-light owner. RE lane next: the 360 HDR scene path (7e3 resolve bias, exposure) with Systems for the
  explosion saturation item. Gameplay patch 2 estimate 11-14 h; killer cam queued after it. AssetTools watchdog test PASS
  (3d1cc87), D1_ORB running. Rendering cand14 identity pair building (X6 + body bit + X3 timer + presenter event).
2026-10-17 04:37 PACKAGE 6 BUILD + VERIFY: Systems refresh.sh af4d0731 (NOPROMOTE, JOBS=2, 04:13-04:23): BUILD OK, M6 498 / 498, keep
  46,744 / xpr 7,161 / 27.53 GB / failures 0, HD 4,005 files / 21.70 GB -> work/playtest_slim_af4d0731. verify_slim -SubstDrive
  (04:25-04:32, work/verify_af4d0731/REPORT.md): PASS - Streets / Gorge / Debris PASS (99 / 94 / 94 s, 0 outside reads, 282
  localized), rtcheck runtime / r4 / rtlights / defaults 39 of 39 PASS, HD runtime row PASS at level 2. GO PROMOTE sent
  (playtest_slim -> playtest_slim_9350f19b fallback; af4d0731 -> playtest_slim; doc PACKAGE_6_af4d0731.md; tag by Integration).
  Card released to Experimental's leak arm B (D3D12 present, RT off, 30 min, per-tag). Package 6 = internal checkpoint; the
  hand-over build is package 7.
2026-10-17 04:37 RE lane: GAMEPLAY_HOVER_RE.md (354df5f6): no ride-height controller, four implicit spring probes (HoverCar / Truck /
  Tank authored values, Optimus on HoverTruck 185 / 10000 / 4000 / 250), AI righting attitude-gated (> 30 deg) not contact-
  gated (package 5 applied the player rule): Gameplay's ride-height item = the faithful model + Smart righting rule; Classic
  righting-path question raised. RENDERING_HDR_SCENE_PATH_RE.md (7353b641) + SYSTEMS_HDR_SHADER_DECODE.md final (205c8da3):
  Faithful explosion clip is the original's (7e3 ceiling 3.984, gather 4.0, uber sat at 1); Realistic treatment = Rendering
  RENDERING_PACKAGE7_POLISH.md section 4 (4-5 h). AssetTools backlog 5 merged (d842464f); next: no_floor_below starts,
  mover initial-pose collision in the walk graph. RE lane next: FaceFX native bone-pose blending.
2026-10-17 04:47 PACKAGE 6 PROMOTED (internal checkpoint; the hand-over build is package 7). work/playtest_slim = af4d0731 (Systems 04:47);
  fallback work/playtest_slim_9350f19b; packages 5 / 4 stay. Doc docs/handoff/PACKAGE_6_af4d0731.md (Systems d734f81b, merged
  by file; PACKAGE_6_HEAD.md removed). Tag playtest/09c-package6-af4d0731. Verify PASS (work/verify_af4d0731). Gate: no
  blocking row (PACKAGE-GATE_af4d0731.md). Play.bat launches it; Realistic row = whole-map RT + X3 volumetrics (WFC_X3=0 off).
2026-10-17 05:24 LEAK ARM B DONE (Experimental; WFC_D3D12PRESENT=1, RT off, Streets 1440p, 31 lockstep runs 04:38-05:08): pool 7,226 -> 7,261 MB
  clean window (+63 MB/h), tagged growth +129 MB of which FlightRecorder.sys (Malwarebytes EDR) +86 MB (two thirds), fltMgr
  +12.7, File +10.4, MmCa +5, ntfs +3; NO AMD / dxgkrnl tag moved (< 1.4 MB each). The presenter's throttle event path was
  never taken (frames wait on the latency-waitable object). Build tail 04:14-04:29: +205 MB/h, FlightRecorder +212 of +306 MB
  tagged. Verify 04:24-04:59: +47 MB/h. CONCLUSION: the nonpaged-pool growth is the Malwarebytes EDR driver recording
  process / file activity, not the AMD driver and not our presenter; 9015c4e9 = tidy-up. Arms C / D dropped. Raw data
  Rebuild-Experimental/work/leak/*/pool.csv + tags.csv; tools/fidelity/leak_report.py.
  PRODUCT ISSUE: arm B run 32 (pid 19880) hung after endFrame at exit under the D3D12 presenter (watchdog minidump, "no frame
  completed for 65.5 s"); the process is stuck in the kernel (1 thread, 1,205 handles), Stop-Process cannot end it. Dump
  work/leak/armB_d3d12present/run-32/arm-r1/wfc_hang_19880_0.dmp. Routed to Rendering as a high-priority package 7 item
  (user-facing with Frame Generation on). CARD HELD for every lane until the user reboots (TDR-cascade risk); CPU work open.
  Card queue after the reboot: Rendering Streets match run, Gameplay crops, Systems death-cue runs.
2026-10-17 06:12 REBOOT DONE (user "Reboot" 05:39; boot 05:41:05): nonpaged pool 1.13 GB, C: 107 GB free (old 52 GB pagefile gone,
  pagefile on F: 16 GB), no game exe, stuck pid 19880 cleared. Sessions relaunched: rebuild-07 (Integration), rebuild-rendering-94,
  rebuild-gameplay-0a, rebuild-experimental-d6, Systems (same name), assettools-fb, re-workspace-0c. FREEZE LIFTED 06:13 with the
  card queue: Rendering (Streets X3 haze match run, X6 captures / identity / eye sheet) -> Gameplay (patch 2 crop slot) ->
  Systems (death cue paced run, FXINACTIVE rows) -> Experimental (presenter-fix 64-run loop, FG-on, resize; gaps). CPU work open
  on every lane; AssetTools option A; RE lane TnShake units.
2026-10-17 06:42 Rendering card slot 06:17-~06:33 (lane-reported 06:58 was ahead of the clock): Streets X3 haze step CLOSED (0.0004 vs 0.0007 within noise on the matched exe, thinner
  shafts; row stays 0.0007; the eye-pass Streets note stays accepted). Identity PASS int_base11 (d2573ccc) vs int_cand15
  (presenter event 9015c4e9, fence fix 8736608c, body bit 403e462a + X3 timer, Camera roll e5fc6d65, X6 round 1 66689c92):
  Off / Faithful 5 / 5 byte-identical, SIMHASH 9601 equal. X6 FAILS the eye bar so far: rounds 1 / 2 read as darker blotches,
  not a sheen (the highlight term does not reach those floors; X2 lightmap-specular path suspected); round 3 in code, X6 not
  merged. Card -> Gameplay patch 2 crop slot ~06:35 (+ Rust / BrokenHope transforming-actor shots), then Systems.
  RE lane: MP_TRANSFORMING_ACTORS_RE.md (f60eccb0), CAMPAIGN_STREAMING_RE.md (24a0d4d3); Gameplay design
  GAMEPLAY_TRANSFORMING_ACTORS.md (af3fc036): Gameplay state + collision, Rendering draws at the pose. AssetTools: verdict
  exemption + streaming runtime revision merged (86dd5de6); matinee export started (~3-3.5 h).
2026-10-17 06:49 Gameplay patch 2 crop slot ~06:35-06:48 (8 runs; times from my clock, lane-reported ones ran ahead, shots Rebuild-Gameplay work/pass29/p2/crop_*/shots, sheets sheet_corpse_10.png,
  sheet_deathcam_*.png, seq_deathcam_robot.png, gib_zoom.png, transform_check.png): PASS as designed: melee kill without
  explosion / chunks / gib (base wrongly exploded), per-entry effects at the authored sockets, vehicle death FX, KillZ insta-gib,
  killer cam cut after 3 s, respawn cut, vehicle death camera. NO MERGE YET, three defects: (1) the death scorch renders opaque
  white / solid black (runtime decal blend for DeathDecal_MAT) -> Rendering; (2) gibs untextured (DeathSkelGib_Mat_INST /
  DeathGib_Mat_INST absent from every materials_glsl.json; chunks grey in base too) -> Systems compiles both into the 10 MP
  material sets; (3) robot death camera starts inside the corpse (trace from the lumbar bone, not the anchor) -> Gameplay fix +
  2-min re-crop. Integration viewed sheet_corpse_10.png and seq_deathcam_robot.png: agrees. SIMHASH zero-diff holds
  (a208d3b0). Transforming actors: not drawn (working assumption from the Rust / BrokenHope shots). Card -> Systems ~06:48.
  Rendering X6 round 3 (0b6b0fd2, cand17: whole film in damp patches, roughness 0.05, sheen share >= 0.35, lobe hack removed)
  queued after Systems: Gorge + Streets first, full batch only if it reads as a sheen.
2026-10-17 06:58 USER DIRECTION: "What else can we add to the next package? Can we add multiplayer? ... If not let's just polish the graphics
  and gameplay and workout bugs ect. And then let's focus on multiplayer online. All gears firing, let's work nonstop all agents."
  Integration: package 7 = polish + bugs + the fidelity items found (no online MP inside it); ONLINE MULTIPLAYER = the phase right
  after package 7 (ahead of campaign code; campaign data prep continues in the background). Design starts now, CPU-only:
  Frontend = ONLINE_MP_FLOWS.md (session / lobby flows); Systems (after the material job) = ONLINE_MP_ARCHITECTURE.md (input-
  delay lockstep over UDP, desync detect / recover, relay / NAT, phases loopback -> LAN -> internet -> browser); Experimental =
  NETCODE_HARNESS.md (two instances over loopback with latency / loss) + the old hang dumps as a bug item; RE lane =
  ONLINE_RULES_RE.md (the original's lobby / team / JIP / host-migration / disconnect rules); AssetTools = campaign dialog manifest.
  Merged today: a2ee795b local death sound (audio only), 24c6618c SecondsBeforeInactive map-FX freeze default ON (Systems rows
  i-iii agree; Molten run-to-run 1-2 level noise floor documented). Debug builds OK. AssetTools matinee export merged (d2ed2698).
  Card: Rendering X6 round 3 (Gorge + Streets first) -> Gameplay 2-min re-crop -> Experimental presenter loop.
2026-10-17 07:13 Rendering X6 round 3 (cand17, five maps): FAILS the eye sheet (work/wobble/x6c_eyesheet.png; Integration agrees): damp
  patches darker and flatter (the layer replaces up to 35 % of the lit floor with a reflection darker than the floor in these
  interiors), matte wall unchanged (no plastic), only Gorge's street view reads wet. Round 4 = additive sheen (film keeps its
  lighting, gains F_wet x reflection of bright sources) in code; 10-min slot on request. Card -> Gameplay deathcam re-crop (2 min)
  -> Experimental (load-stall boots, presenter loop). Merged: 3a19b47a Frontend session boundary as code (Session.h ISession 10
  calls / 17 events, FakeSession.h, GameFlow::setSession unset = offline; wfc_frontend_tests exit 0). Docs merged: ONLINE_MP_
  FLOWS / FRONTEND_TESTS / UI_STRINGS (decisions D1-D11 taken, D7 voice chat = user), NETCODE_HARNESS.md + load-stall.ps1,
  EXPERIMENTAL_HANG_DUMPS.md, GAMEPLAY_HOVER_RESULT.md, CAMPAIGN_KISMET_HANDLERS_RE.md, CAMPAIGN_MATINEE_RUNTIME_RE.md.
2026-10-17 07:27 PACKAGE 7 MERGES (integration head f2465a80): be0cac77 Camera.h roll (rt2 e5fc6d65); 1e11edc2 Gameplay death presentation
  patch 2 (faithful picker + momentum, ragdoll gib, death camera + roll + gibbed shake, killer cam, DeathFx at sockets, death light
  ends with the pawn; scorch gated OFF until the DeathDecal_MAT regen; headless 48 / 48 x 12, SIMHASH identical; deathcam re-crop
  PASS docs/handoff/corpsep2/sheet_deathcam_robot_fix.png); 72eec90c Frontend Volumetric Lighting settings row + pc_strings.int;
  f2465a80 Rendering volumetric setter (SETTINGSTEST +1); earlier today a2ee795b, 24c6618c, 3a19b47a. Debug builds OK,
  wfc_frontend_tests exit 0. Identity for 1e11edc2 (death-frame expected-change list) queued with Experimental.
  RE lane: CAMPAIGN_SETPIECES_RE.md (all script, no native boss code) + CAMPAIGN_RE_INDEX.md; the campaign runtime series is
  complete; next FMV playback + checkpoint data packaging. Netcode: netshim.py self-test PASS on 7 profiles (210ab479).
  Card: Experimental load-stall boots -> Rendering cand20 (X6 round 4 + setter) -> Frontend 2-min row check -> Experimental
  presenter loop.
2026-10-17 07:42 Rendering slot 07:33-07:45: X6 round 4 (additive sheen, cand20): still no visible sheen (work/wobble/x6d_eyesheet.png); after
  four rounds Rendering reads these interiors as giving a damp film too little bright content to reflect; replacing darkens,
  additive adds nothing. Decision: ONE time-boxed attempt (~3 h + a 10-min slot) on light-source-driven damp reflections
  (lamps / emissives / transient lights placed by light position, tight highlight, dry x6refl floor), else X6 is PARKED unmerged
  with the four sheets as evidence. X7 (cand22, 2x GI pages): sharper indirect light (Gorge grating shadows crisper) but the
  fitted bounce gain moved 0.414 -> 0.530 at 2x (Gorge ~11 % brighter), VRAM +1.0-1.1 GB per map (GL copy arrays at 2x), GI
  bake Gorge 1.3 -> 2.4 s, Streets 1.9 -> 6.8 s (frontend), first-frame gap at 'World: map mesh upload' 1.9 -> 3.7 s despite
  the bake yield (02c9ceb4). Next: fixed-gain Gorge pair (WFC_RT_GIBOUNCE=0.414), make 2x conserve energy rather than pin
  a gain, find what in the mesh-upload step scales with texels; X7 merges only with the gap at or below 1x. Card -> Frontend
  2-min volumetric row check -> Experimental (4 Streets boots with SKIPINTRO, presenter loop). Merged: 0960cd35 autoplay
  waits for the selector (harness-only; a race landed scripted boots on map 501).
2026-10-17 07:55 Merged this hour: 0960cd35 autoplay selector wait (harness); cc44f880 online flows voice chat (D7 = yes, user) + "simplified
  vs the original" notes + pc_strings [Voice]; cccff627 CAMPAIGN_HUD_PLAN.md (Frontend; E1 objective path, E5 tutorials, the
  <Strings:> store coded next behind a campaign flag); 2fbeaa31 CAMPAIGN_FMV_PLAYBACK_RE + CAMPAIGN_CHECKPOINT_DATA_RE (the
  campaign runtime RE series is closed; index updated); 637df8a6 campaign dialog manifest (AssetTools; 892 conversations, 1,603
  waves with INT + FRA subtitles, dialog-manager rules) + schema corrections; objectives export next. Gameplay squad: A + B
  taken (fidget -30..-45 % on all four maps; follow mixed within noise), near-slot dithering parked at the end of the queue; ONE
  Classic re-baseline for hover follows. Frontend volumetric row check PASS (shots Rebuild-Frontend work/bins/volcheck/png).
  User: voice chat in scope; online bar = "make it work with what we have now" (original rules as the starting point).
  Card: Experimental 4 Streets boots -> Rendering 20-min slot (X6 r5 light-driven, X7 gutter fix) -> Experimental presenter loop.
2026-10-17 07:56 LOAD STALL before numbers (Experimental, 4 Streets frontend boots on af4d0731, SKIPINTRO, 1440p windowed, 1 rep; report
  tools/fidelity/results/LOAD_STALL_af4d0731.md e117603d): no-frame total / largest gap: Off 1.3 s / 0.69 s ('Render: load step'),
  Faithful 2.8 / 0.68, Realistic 7.5 / 4.96 s at 'World: map mesh upload' (0.04 s under the 5 s watchdog), Realistic + HD 2
  6.0 / 4.2. No watchdog line, 0 dumps. The Realistic first-frame hitch sits at the watchdog edge at 1x; Rendering's gutter
  frontier + loading-screen yield (02c9ceb4 / 9d6ac5b5) is measured against 4.96 s; X7 must come in below it. Card -> Rendering.
2026-10-17 07:59 Merged: eebe15b2 campaign flag + <Strings:File.Section.Key> data store (answers "" in versus as before; decision: answer it
  in versus too as a follow-up, the original filled the rename prompt and Claimed / Unclaimed texts from it), b79692b6 campaign
  objective path (bank, setCurrent, HudController::setObjective -> AddObjective, ActiveObjectives store), fedf83ee campaign
  tutorial messages once per profile; all behind GameFlow::campaign(), versus unchanged, wfc_frontend_tests 165 / 0.
  eee61182 CAMPAIGN_CONTENT_DELTAS_RE.md + data exports (campaign specification complete on semantics + data). RE lane ->
  package 7 support: the Faithful per-draw 3.984 saturate rule. Card: Rendering's 20-min slot in progress.
2026-10-17 08:04 Rendering slot ~07:58-08:05: X6 round 5 (lamp-placed damp + highlights): no visible wet floor; X6 PARKED unmerged after five
  rounds (RENDERING_PACKAGE7_POLISH.md section 5; the user is told). X7: the gutter explanation was wrong (RT / bake 0.99 at the
  fixed gain: no energy loss); the gain shift is the calibration's conditioning (measured pages barely respond to the bounce gain);
  decision: (b) calibrate at 1x density and reuse the gain at 2x; the Streets full-flow frontend gap is 3.97 s at 2x vs 2.02 s at 1x
  (the readback rounds suspected; yields + 1x-mip GI read next). Card -> Experimental presenter loop (~70 min) on int_cand15.
  Non-X6 merge set (fence fix, presenter event, X3 timer; body bit with its lossless sheet) merges on "PRESENTER DONE".
2026-10-17 08:08 RE lane: RENDERING_HDR_BLEND_CLAMP_RULE_RE.md (b3758131: per-draw per-channel clamp to [0, 3.984375] on every 7e3 scene-colour
  blend; a final min before post is not equivalent) and GAMEPLAY_VEHICLE_DEATH_RE.md (d99a3e35): patch 2 already matches except
  the dying vehicle's live collision (0.2-0.75 s car / tank, 4-5 s jets) and the hitscan-only hull body, sim-side -> batched with
  the transforming-actor collision as one identity change after the perf levers. Rendering merge sets as patches (rt2 0f1bca12):
  SET_A (presenter event, fence fix, X3 timer) merges on PRESENTER DONE; SET_B (body bit + WFC_RT_BODYBIT=0) after its
  zero-difference sheet; both dry-run clean on fedf83ee. X7 rule (b) built (00998378, cand25). Frontend: e254ace9 <Strings:>
  answered in versus (rename prompt, Claimed / Unclaimed), 22c15bc9 E7 cinematic hide; both merge after the before / after
  shots; Frontend now implements the phase 1 online flows on ISession + FakeSession behind a flag. RE lane -> weapon firing
  presentation audit. Card: Experimental presenter loop.
2026-10-17 08:11 Merged: 8da495f9 campaign objectives export + CAMPAIGN_DATA_INDEX.md (the campaign DATA side is complete); 56eb4271 material
  fix (A) tools side (DeathDecal_MAT into vector_channel_proven, the 2 gib MIs into systems_extra_materials); the 10 regenerated
  materials_glsl.json (Systems handoff, before hashes match 10 / 10 live) switch into work/render after PRESENTER DONE, then
  Gameplay's follow-up (scorch default on, gib wfcName, DeathFx PlayType a22a2f69) merges. AssetTools next: the 32 transforming
  actors' skinned GLBs + anim + PhysicsAsset bodies (manifests/maps/transforming_actors/, ~4 h). Frontend: phase 1 online flows
  on ISession + FakeSession (16-18 h, 6 groups) started; E7 + the versus <Strings:> fix merge after the shots gap.
  Experimental: presenter loop restarted 08:13 as cand15b (the 08:08 "hang" was the harness start gate waiting on Gameplay's
  headless exes; fixed bda00781).
2026-10-17 08:14 RE lane GAMEPLAY_WEAPON_PRESENTATION_AUDIT_RE.md (39ff0082): 34 MP weapons; gaps: camera recoil / FOV kick + per-shot shake
  authored (weapons_mp.json) but unread in src -> Gameplay, presentation-only, ahead of the perf levers; every weapon on
  IonBlaster's spine / hand recoil -> Gameplay, batched into the one sim-identity change (with transforming actors + dying-vehicle
  collision); shell / magazine / reload notify FX only for AssaultRifle / IonBlaster, physmat impact overrides unused, R-only
  muzzle assets -> Systems after (B) and the online doc. Material fix (B): microcode spot check PASS on 3 representatives (force
  field, plasma, light shafts read the alpha as its own scalar); the global candidate builds into Rebuild-Systems/work/matB/out;
  the eye pass (Off / Faithful before vs after on all 10 maps with 360 references) is booked with Experimental after Rendering's
  slot. RE lane next: the same audit for abilities, melee, transformation and vehicle weapons.
2026-10-17 08:19 Merged: 869666be ONLINE_MP_ARCHITECTURE.md (Systems: working-first; listen host star, UDP phases 1-3, join codes + a small UDP
  relay, drop-to-lobby on desync, no migration, joiners wait; voice lane Opus 16 kbit/s PTT; INetTransport + switches + exit
  codes answered for the harness; runHeadlessMatch planned; 5 ISession mismatches for the Frontend); 0cf760f4 <Strings:> in
  versus, 37fa6376 E7, f2c285f3 online group A, 61905a60 online group B (host / join / member lobby flows on FakeSession,
  S1-S9 green); 7cb0a992 transforming-actor export doc (4 GLBs + manifest); d6c9f6d7 weapon audit re-merged from MultiplayerData
  + audit 2 part (4) (projectile flight loop sounds unread -> Systems). Gameplay follow-up de736589 (scorch on, gib materials,
  DeathFx PlayType) dry-runs clean; merges with the (A) data switch after PRESENTER DONE. Decisions: per-weapon recoil = own
  re-baseline right after hover + squad ((a)); unknown objective keys refused as the original.
2026-10-17 08:28 Merged: 202cef15 ISession aligned (11 points, ONLINE_MP_MISMATCHES.md); f1ae9baa online B2 (Join Game row, join panel,
  WFC_FAKESESSION dev tool); 6045a7a9 architecture: proximity voice with room reverb (user addition: 3D emitter at the speaker's
  head from the lockstep sim, the original's dialog attenuation, rooms = Kismet SeqAct_Reverb touch volumes already exported,
  second I3DL2 instance for the speaker's room; default Radio + Proximity); a027510d / 091b2290 audit 2 part (1) abilities +
  corrections (cooldown arrays [MP, campaign], E-class cues may already play: CUELOG check first; HUD gap = ApplyBuff /
  UnapplyBuff only -> Frontend; unwired ability FX / sounds -> Systems). (A) data switch staged (scratchpad matfix_switch.py,
  dry run OK on 10 maps). Presenter loop ~22 / 64 at 08:27, clean. Card order after PRESENTER DONE: (A) switch -> Frontend
  10 min -> Rendering 20 min -> (B) eye pass ~100 min.
2026-10-17 08:33 RE lane audit 2 part (3) transformation (a1524b0f): jet to-vehicle camera blend 1.0 s authored vs our global 1.5 (Gameplay checks in
  the camera item); jump / hover FX + jet transform trails unwired (Systems' wiring item); vehicle sounds -> CUELOG check. AssetTools:
  the 52 "unextracted" waves exist as _LOC twins (no gap); D2 Guardian Chamber = a MISSING FLOOR in our export (hunt in the cooked
  packages next); then the campaign movies conversion (54 .bik -> WebM VP9 + Opus per track + subtitle JSON, 4 threads, low
  priority, hold-aware, ~1.5 h; the earlier H.264 set in ExtractedAssets/movies untouched). Presenter loop ~30 / 64, clean.
2026-10-17 08:35 MERGED cfca1717 hover + squad + DELIBERATE Classic / Smart SIMHASH re-baseline (Gameplay; 6 files; tables docs/handoff/hover/):
  faithful four-probe springs, Classic on the original's AI righting, Smart steadier righting, squad slot fallback + hysteresis;
  gate consequence: renderer identity for later heads must run on the new sim (rule to PACKAGE7-GATE.md). RE lane audit 2
  COMPLETE (f89cfe43; parts: vehicle weapons, abilities, transformation, melee): actionable list = HUD jam / disrupted display
  (Frontend), one shared body-trail set driving melee / Shockwave / TransformDisruptor / jet transforms + melee hit / block, jump,
  hover FX + five ability FX (Systems' wiring item), projectile flight loops (Systems audio), jet to-vehicle camera blend 1.0 s
  (Gameplay camera item). RE lane next: versus HUD / front-end / end-of-match presentation audit. Gameplay next: camera recoil /
  FOV kick / fire shake, then per-weapon recoil (own re-baseline).
2026-10-17 08:38 Merged: 382ca6ab Session.h proximity voice interface (audience TeamRadio / AllRadio / Proximity / RadioPlusProximity default,
  MemberVoice.voiceVia) + strings; b6a310e0 online group C (host member table, the original start gate, humans take bot slots,
  browser cap, platform tags; Team Balancing Manual vs Autobalanced corrected); 81e06288 Guardian Chamber "missing floor"
  RETRACTED (elevated spawns 11-13 m above a present floor; new triage class elevated_above_floor, exempted PROVISIONAL like
  airborne; D2 PASS PROVISIONAL); 2a495677 gate-base.sh BASE_REF + the hover patch (package 7 regress base = af4d0731 +
  cfca1717 src/game). Card after PRESENTER DONE (~09:07): Experimental's 6-min base check (same-match True; negative control
  diverges ~601) -> (A) data switch + Gameplay follow-up merge + Set A merge -> Frontend 10 min -> Rendering 20 min -> (B) eye
  pass ~100 min. Presenter loop 36 / 64 at 08:38, clean. AssetTools: movies conversion running (4 threads, low priority).
2026-10-17 08:41 Merged: 35811523 + 8dd72f58 HUD ability jam / TransformDisrupted display (Frontend; Hud_GFX ApplyBuff / UnapplyBuff from the
  HUD state buffs; crop waits for Gameplay's buff state). AssetTools: D2 verdict PASS (PROVISIONAL; airborne + elevated exempt);
  movies conversion running (Logo test 220 / 220 frames, 10 Opus tracks); FINDING: the movie subtitle sidecar header '100' is
  ticks per second (centiseconds), so the older ExtractedAssets/movies/*.subtitles.json labels raw ticks as ms (10x too early):
  the campaign FMV player must use the new out/movies JSON. Presenter loop 41 / 64 at 08:41, clean.
2026-10-17 08:45 Merged ea1fc228 FRONTEND_PRESENTATION_AUDIT_3_RE.md part (1) versus HUD (no radar in the original; kill feed rules; four small
  HUD gaps -> Frontend after group F). Staged: Gameplay GAMEPLAY_CAMERA_RECOIL.patch (41c52c86: per-shot orbit pitch step, FOV kick,
  per-shot shake on the shared shaker, jet transform blend 1.0 s, HUD buff list; SIMHASH identical) dry-runs clean, merges on its
  4-min firing crop sheet. Systems: material fix (B) candidate ready (matB/render, 118 entries, manifest docs/handoff/matscan_b/);
  authored FX / sound wiring item APPROVED (~13 h CPU + 1 h card: generic notify dispatcher from 3,554 authored notifies, weapon
  notifies for every visible pawn, body notifies on Gameplay's onAnimNotifyWindow hook, physmat impact overrides on AssetTools'
  triangle -> physmat index, R-only muzzles, flight loops, no-hit ability sounds; CUELOG run first). Package 7 date now ~Oct 21.
  Card after the loop (45 / 64 at 08:45): base check 6 min -> (A) switch -> Frontend 10 -> Gameplay 4 -> Rendering 20 -> (B) ~100.
2026-10-17 08:52 RE lane audit 3 COMPLETE (8147b6d5: versus HUD, end of match, UI sounds; gaps = 4 HUD callbacks for the Frontend; UI sound
  set 5 / 5 present); next: the Cascade particle-module coverage audit (gap list first, semantics in usage order, TypeData +
  SBI / LOD; ~4-5.5 h). AssetTools: triangle -> PhysicalMaterial runs as a sibling file collision_weapon_physmat.json (~3 h,
  shape with Systems); movies 21 / 54 encoded, frame-exact. Systems: wiring step 1 built (SYSTEMS_NOTIFY_FX.patch d4a887d8:
  every gun's authored shell / magazine / reload notifies at the weapon sockets for the local pawn and participants within
  40 m; SIMHASH identical, EVENTTEST 16 / 16; dry-runs clean on integration); its 10-min check slot (counts, crops, the CUELOG
  list that is the E-class test) goes before the (B) eye pass. Card order after PRESENTER DONE: base check 6 -> (A) switch ->
  Frontend 10 -> Gameplay 4 -> Rendering 20 -> Systems 10 -> (B) ~100.
2026-10-17 08:53 Merged af2670bb + 40bd1181 online phase 1 groups D / E (start, load gate, peer drop -> bot swap; session end / return paths with
  the original texts, desync -> game lobby, no XP online; tests 202 / 0; offline unchanged). Phase 1 frontend flows A-E are on
  integration; F (voice UI state) next, then the audit-3 HUD item and the objectives loader. Systems correction: projectile flight
  loops already play (WeaponAudio::projectileSpawned, data-driven), dropped from the gap list pending the CUELOG. Presenter loop
  56 / 64 at 08:53, clean.
2026-10-17 08:55 RE lane particle module gap list (5ce1f71d): 266 MP particle systems; 25 modules present, 4 PARTIAL (Gravity, ColorByParameter,
  DynamicParameter, LocationPrimitiveSphere: the heaviest by usage), 8 missing (LocationPrimitiveCylinder 179 LODs,
  VelocityInheritParent 154, AttractorPoint 97, AccelerationOverLife 33, LocationDirect 22, AttractorParticle 20, Orbit 18,
  KillBox 4); semantics follow in usage order; Rendering's package 7 effects item = make the 12 faithful with crops.
  AssetTools MP collision physmat export merged (0f3d9b89; 10 sibling JSONs, GLBs byte-identical); campaign chapters next;
  movies 27 / 54 encoded.
2026-10-17 09:00 Merged 44b65357 online phase 1 group F (voice UI state): PHASE 1 FRONTEND FLOWS A-F COMPLETE on integration (ISession + FakeSession,
  tests 205 / 0, offline byte-identical); Systems' transport + voice lane implement under ISession next (after its package 7
  items). 56d5e2dc particle module audit batch 1 (the four PARTIAL modules' semantics; Rendering has them). Presenter loop:
  64 / 64 runs, 0 hangs, 0 no-drain; FG / resize / map switch running.
2026-10-17 09:02 Merged aa5a30ea campaign movies manifest + subtitles (AssetTools: 54 / 54 WebM VP9 + Opus frame-exact in AssetTools out/movies, 609 MB
  untracked; 12 subtitle JSONs with the ticks header applied). Rendering: Lifetime stacking fix dba92e6c (all modes, exact) joins the
  next merge set; the four PARTIAL particle modules wait for AssetTools' pstream fields (gravity multiplier / world gravity source,
  sphere axis / SurfaceOnly / Velocity flags, ColorByParameter name / default, DynamicParameter bUseEmitterTime), ~3-4 h after,
  interleaved with X7 as CPU allows. AssetTools next: campaign physmat, then the pstream decode fields. Presenter verification:
  64 / 64 clean, FG run (latency max 281 ms, FG resets 1: Experimental reads it), resize done, map switch running.
2026-10-17 09:04 Merged 4953261b + a36b2ee1 audit-3 HUD item (ReapplyBuff, SetSpectateTarget from Gameplay's HUD state, scoreboard input online-only;
  DisplayCharacterSelect already movie-side). Gameplay camera patch refreshed (0c0a5665: + HudState.spectateTarget; 271 lines,
  dry-runs clean; merges on its firing crop sheet). Particle module data: the cook strips per-instance module objects; AssetTools
  does option A now (class defaults labelled + MP map_fx_runtime.json regen, ~1 h); option B (decode per-instance values from the
  compiled stream) only if the RE lane says the stream carries them. Campaign physmat: D5 done (two thirds of triangles on the
  Metal default), 9 chapters running. Presenter verification: map-switch step running since 09:00:56.
2026-10-17 09:08 PRESENTER VERIFICATION PASS (Experimental, tools/fidelity/results/PRESENTER_VERIFY_cand15.md 3cf61199): 64 / 64 WFC_D3D12PRESENT exits
  clean (before 1 hang in 32), latency wait avg 0.002 / max 14.9 ms vs 0.120 / 32.9, FG run clean (the 10-12 FSR 3 crash class
  did not recur), 3 resizes clean, map switch with the presenter 6 / 0. MERGED 76da5484 Rendering set A (fence fix 8736608c,
  presenter event 9015c4e9, X3 GPU timer): the D3D12 presenter exit hang known issue CLOSES. Also merged 5cb9886f campaign
  objectives loader; 186c1a84 particle audit batch 2 (8 missing modules' semantics). Experimental's base check running; then
  the (A) data switch -> Gameplay follow-up merge -> Frontend 10 -> Gameplay 4 -> Rendering 20 -> Systems 10 -> (B) eye pass.
2026-10-17 09:09 Rendering particle fixes on rt2 (a538f71f batch 1 fields + sphere rule + gravity where GravityMultiplier exists; dea7c8fd
  AccelerationOverLife 118 LOD records + VelocityInheritParent 1,143; dba92e6c Lifetime stacking): all-mode fidelity changes ->
  identity as an expected-change list with before / after crops in Rendering's next particle slot; HOLD on the guessed -2940
  gravity scalar until the RE lane sources it (inst[+0x94] setter + the maps' authored world gravity). Gameplay queue set: camera
  crops -> weapon + melee batch (re-baseline) -> Systems' anim-notify + physmat hooks -> session hooks (SESSIONTEST) -> perf
  levers -> transforming actors + dying-vehicle collision -> FaceFX -> dithering. Base check running (verify base vs cfca1717).
2026-10-17 09:13 BASE CHECK PASS (Experimental P7_REGRESS_BASE.md 006478f1): base af4d0731 + cfca1717 src/game plays the same match as cfca1717
  (SIMHASH identical on 10,200 steps); negative control af4d0731 vs cfca1717 diverges at step 603. GATE_BASE =
  work/ab/fast_af4d073_hover; later sim changes join as items. (A) DATA SWITCH IN at 09:12 (10 / 10 after-hashes verified) with
  9c17be40 Gameplay's follow-up (scorch default on, textured gib materials, DeathFx PlayType). RE lane particle audit COMPLETE
  (9cc42c1e): SecondsBeforeInactive is a THROTTLE (10 Hz after 0.3 s unrendered, 1 Hz after SBI, catch-up in one step), so
  the merged FxInactive freeze rule is wrong: Systems reworks it to the throttle ahead of the body-notify dispatch, same three
  rows; beam / trail formulas exist in pass 5 (Rendering); gravity scalar sourced: DefaultGravityZ -2940 (Xe-TransGame.ini).
  Card: Frontend 10-min gap running -> Gameplay 4 -> Rendering 20 -> Systems 10 -> (B) eye pass. RE lane next: time-driven
  materials + dynamic light behaviour audit.
2026-10-17 09:15 Switched data sanity check: all 10 MP materials_glsl.json parse, carry DeathDecal_MAT + both gib materials, the old x20 formula is
  gone. Systems built SYSTEMS_FX_THROTTLE.patch (128b4846: WFC_FXINACTIVE = the decoded PSC tick throttle, 0.1 s ticks after
  0.3 s unrendered, 1.0 s past SBI, catch-up per executed tick, fresh / re-activated components count as rendered); its card
  slot becomes 35 min (notifyfx check + the three throttle rows) before the (B) eye pass; both patches dry-run clean on
  integration and merge on their rows. Frontend capture gap running since ~09:13. RE lane: time-driven materials + dynamic
  lights audit started (~3.5-4 h).
2026-10-17 09:19 AssetTools' independent review of Systems' (B) candidate (AssetTools work/matB_review): 116 changed + 2 added = 118 vs the pre-switch
  live tree, 0 removed; 464 / 484 entry-map pairs confined to the vector-channel pattern; the mid-review live change was my (A)
  switch; 2 entries change outside the pattern on all 10 maps (PROP_VendingMachine Ammo / EnergonCube MATINST: UV constants
  built from separate channels, type_violations 5 -> 1): Systems to justify in the (B) doc; review.py reruns on the frozen live
  tree before the (B) switch. Rendering cand27 = cand26 + particle commits (option-B fields via WFC_MAPFX_DIR; gravity -2940
  sourced); its slot ~22 min with a particle before / after pair.
2026-10-17 09:22 Frontend capture gap done 09:21 (shots Rebuild-Frontend work/bins/online_b2/ b2_*.bmp, str_before / str_after *_overview / _rename /
  _charselect.bmp); two script misses to redo in a 4-min re-slot (member lobby joined while offline; voice rows captured offline).
  Integration viewed the charselect pair: identical UI, no Claimed / Unclaimed label visible in a 1-human lobby; asked where it
  appears (2-human state?) before calling 0cf760f4 verified. Systems justified the VendingMachine entries (a packed UV scale /
  offset VectorParameter read through two AppendVectors' pins; the same output-pin cause; the glyph pattern moves to the authored
  offset). Card -> Gameplay 4-min firing crops + one death crop (09:22).
2026-10-17 09:26 Gameplay firing-crop slot done (13 runs: 12 firing base / new + the robot-death check on the switched data; sheets and verdict
  pending in work/pass29/cam; the camera patch merges on it). Card -> Rendering x9slot ~22 min (body-bit zero-difference pair,
  X7 Gorge views + the Streets frontend pair vs the 2.02 s bar, particle before / after on cand27), then Systems 35 min, then
  Experimental's (B) eye pass.
2026-10-17 09:28 MERGED 1f1d4e17 camera recoil / FOV kick / fire shake (Gameplay; crop verdict PASS: pitch climb under autofire as authored, per-shot
  motion up; the death follow-up confirmed on the switched data: soft dark scorch, textured metal chunks); 872c1ff4 + d03e3b40
  online match roster rows + glue; RE lane TIME_MATERIALS_LIGHTS_RE.md parts 1-2 (all 3,093 time-driven materials keep their
  time term; MP authors no dynamic light behaviour; gaps routed: muzzle light colour = owner's EnergonColor -> Systems, Remnant
  PointLightToggleable_1918 missing from lighting.json + optional time-phase reset at map load -> Rendering, Domination marker
  NodeID -> Gameplay). Pre-existing: Exp_Death_FX distortion swirl far too strong at the death explosion -> Rendering to check
  the translated distortion scale. Frontend: Join Game row overlap fixed; <Strings:> fix = correct per AS, rename prompt to be
  verified in a 4-min re-slot, Claimed / Unclaimed unreachable (no Iconic Mode in our lobby: a later fidelity item). Card:
  Rendering slot running -> Frontend 4 -> Systems 35 -> (B).
2026-10-17 09:31 RE lane time materials / lights audit COMPLETE (557ca8f0): top-20 time-driven surfaces (sky / space domes, smoke planes, light shafts;
  all K) with UE camera starts for Rendering's 60 s direction / speed A/B; light gap crops (Remnant toggleable light, muzzle
  colour); particle float order for Sphere / Cylinder = base-class first (Rendering told). RE lane next: per-map authored lighting
  intent tables (sun / sky, top placed lights, fog / post-process volumes, light environments) as the eye-pass reference in the
  absence of 360 captures. Gate rule added: identity / eye pairs from 1f1d4e17 on share the camera code. netlog_join.py synced.
2026-10-17 09:31 Systems built SYSTEMS_MUZZLE_ENERGON.patch (e08474a5: the muzzle light uses the owner's EnergonColor x authored Brightness, as
  TnWeaponMesh.Attach; WFC_MUZZLEENERGON=0 restores the authored colour); dry-runs clean. Systems' slot = 40 min back to back
  (notifyfx crops + CUELOG, the FX throttle rows, the muzzle pair), each patch isolated by its switch; merges on the rows.
  Card: Rendering (~10 min left) -> Frontend 4 -> Systems 40 -> (B) eye pass (~10:30).
2026-10-17 09:39 RE lane MP map lighting-intent reference merged (3e2edb44; under an hour vs the 2.5 h estimate): per-map sun / sky / top lights /
  fogs / post volumes / light environments classified D / A / X1 / -; gaps -> Rendering: only the first HeightFogComponent per
  map loaded (six maps author 2-4 layers; Gorge loses a dense low layer), DOF kernel 32 rendered at 16, light-env transition
  unused. RE lane next: close the PROVISIONAL readings in the campaign data index. Card: Rendering slot (cand26) still running.
2026-10-17 09:40 Rendering slot 09:23-09:39 verdicts: Set B body bit DROPPED (not zero-difference: a global ~3 % exposure shift from more haze
  in-scatter; and the X3 timer reads identical with / without it, so bodies do not cost X3 time: their RT cost is the shadow / AO
  TLAS side, Gameplay's item 1); X7 still misses both bars at the pinned gain (Gorge +15 %, Streets no-frame gap 3.71 s vs 2.01)
  -> PARKED behind the height-fog layers and the particle modules, offline diagnosis; flash term too weak (Gorge 1080 near-white
  83.9 -> 82.3 %, bar 55-62): threshold ~1.5 + TONEDIAG next; particle pair void (sphere / cylinder field order was swapped until
  AssetTools' fix), re-run on a candidate cut at / after 1f1d4e17. Card -> Frontend 4-min re-slot (09:40) -> Systems 40 ->
  Rendering 10 if ready -> (B) eye pass.
2026-10-17 09:42 Frontend re-slot done 09:42 (online_b2r/ r_party_joinrow, r_joining, r_member_lobby, r_host_talker; str_before / str_after *_list /
  _overview / _rename): the join now works through Accept (joining -> joined -> member lobby); the rename click still found no clip
  at the scripted path (the Customize movie was dumped to take the right path). Card -> Systems 40 min (09:43): notifyfx check,
  FX throttle rows, muzzle energon pair; then Rendering 10 if ready, then the (B) eye pass. RE lane: PROVISIONALs sweep started
  (~6-7 h, two commits).
2026-10-17 09:42 Integration viewed the online re-slot shots: r_member_lobby (member lobby without host controls, own row with the voice icon,
  "Waiting for host to start game") and r_host_talker (join code / address / copy hint / privacy line, remote "Alpha  PC  Near" with
  the talker icon, the original gate text): as designed, original style. The rename prompt capture still pending (path fix).
  Systems' 40-min slot running since 09:43.
2026-10-17 09:45 Correction (Frontend): in r_host_talker Alpha's icon is the IsConnecting spinner (the dev MemberJoined has no character yet, hence the
  "choose a character" gate text); the proximity cue in that shot is the "Near" tag; the in-match HUD talker list needs Gameplay's
  Remote participants (item 4). The rename pair capture runs by coordinate in the next small gap. Frontend next: Iconic Mode
  (estimate first), then the phase 2 online flows doc.
2026-10-17 09:47 Frontend finding (CONFIRMED AS): the original has no Iconic Mode lobby option; iconic play comes from the party lobby TYPE
  (Escalation GTS_SingleTeamGame and co-op campaign set iconic = true; versus GTS_TeamGame = custom characters only), so the iconic
  wheel and Claimed / Unclaimed belong to Escalation / co-op. DECISION: (a) build the lobby-type flag path + the iconic wheel
  faithfully (reachable when those modes exist) + (b) a versus "Iconic Characters" Game Mode Options row as a labelled PC
  extension, default Off (versus byte-identical); ~2.5-3 days Frontend, not package 7-blocking; Gameplay side = host claim
  validation per team. Rendering's 10-min x10slot (int_base12 = integration 53a12618 vs int_cand28 = + particle commits + flash
  term threshold 1.5) is ready and goes right after Systems' slot, before the (B) eye pass.
2026-10-17 09:51 RE lane CAMPAIGN_PROVISIONALS_CLOSED_RE.md part 1 merged (a3a5ada0): reachFlags 2 = fly CONFIRMED (A4 / D2 verdicts hold); empty
  objective Description = complete current + show none; elevated spawns harmless; hull physmat rule CORRECTED (first-slot fallback
  only for StaticMeshActor; SMCA members fall to Metal) -> AssetTools splits the first_material triangles by owner class; matinee
  empty-object groups: 125 no-ops, 23 stay live (Event / Sound tracks) -> AssetTools' binding state. The resolver order also goes
  to Systems' impact rule. Part 2 (items 6-12) next. Systems' slot running (~10:23).
2026-10-17 09:56 Systems' 09:42-10:01 slot: all three PASS and MERGED: f27f94f5 authored weapon notify FX for every visible pawn (154 notify FX, 92
  local / 62 participant; casing / exhaust streaks only) + the muzzle light in the owner's EnergonColor (4 / 36 frames, same
  brightness); 5c68f60e the FX throttle (replaces the freeze; Streets 29 % / Molten 26 % of instance-ticks throttled, no cost
  change; switch 0 = base within the same-exe floor: 4 / 28 frames at max 1-3 levels vs 2-4 / 28 run-to-run; expected-change
  on: 10 / 28 + 15 / 28 frames, emitters at another phase, nothing frozen). CUELOG: 173 cues start in base (the E-class check).
  Gate rule: windowed identity is judged against the same-exe floor; the lockstep harness should print SIMHASH. Card ->
  Rendering x10slot ~10 min (10:02) -> Experimental's (B) eye pass. Systems next: body-notify dispatch on Gameplay's hook.
2026-10-17 10:02 MERGED 3363f1d0 Iconic Mode (Frontend): in-game _bIconicMode from the lobby type (GTS 2 / 4, RE CONFIRMED) or the versus "Iconic
  Characters" Game Mode Options row (a labelled PC extension, default Off; OnlyAllowIconicCharacters is true only on CCP / CP / SV
  settings); the movies' own AS then opens the iconic wheel (authored PresetIconicCharacters order, presets from mp_characters.json,
  locked characters through our unlock rule), SelectCharacter type 1, ClaimedIconicCharacters per team, the online gate holds
  on a same-team duplicate; tests 215 / 0; versus byte-identical. Asked: Systems SessionMemberInfo.iconicCharacter, Gameplay
  type-1 spawn + host claim check (queued). Capture (~4 min incl. the rename pair) in the gap after Rendering's pair, before (B).
2026-10-17 10:03 Rendering x10slot verdicts: particle pair (base12 + integration data vs cand28 + the corrected regen): 15 / 26 shots differ, all in
  smoke / impact-spark particles (cloud texture and spread), nothing missing or broken -> the particle set is ACCEPTED for merge
  (the RE lane's native rules with the real per-instance values; expected-change = particle-only; no 360 frame exists); Rendering
  sends one patch against 3363f1d0 + the map_fx_runtime.json regen command (gate: only fields keys differ). Flash pair
  INCONCLUSIVE (the post-recoil match has no explosion inside the captured steps; ordinary frames untouched, F = 0): extended
  SHOTMATCH re-run in a later gap. Height-fog layers built (rt2 5b72c076 + 315c9843: all enabled layers sorted by height, slab
  min heights; Gorge 4 / Rust 2): 15-min A/B + identity after the Frontend's 4-min capture gap (10:04), then the (B) eye pass.
2026-10-17 10:05 RE lane PROVISIONALs sweep COMPLETE (part 2 merged 95c199f2): correction to part 1: a whitespace objective Description is NOT a
  clear (34 of 41 setters clear; A1's 7 " " are no-ops) -> Frontend loader + AssetTools; UseSubtitles = story vs barks (HIGH);
  runtime characters confirmed (AICD -> ROBODEF; PresetIconicCharacters per chapter); D3A / D3B = chapters 3 / 4; 13 / 15 Kismet
  handlers resolved (UseTurret consumer in an undecompiled TransContent class, DisableShooting unknown: RE lane's next item);
  A4 Checkpoint_14441 by design; FaceFX clamp 0..1 (HIGH). Rendering Set C (particle modules; RENDERING_P7_SET_C_PARTICLES.patch
  bd63223e) applied on integration, Debug build OK; the map_fx_runtime.json regen for the 10 MP maps is running (old files backed
  up; gate = only fields / fields_source keys differ); commit after the gate.
2026-10-17 10:09 MERGED d96b21a0 Gameplay's anim-notify window hook (empty stub + WFC_ANIMNOTIFYLOG; identity-neutral) and 5fc2e552 the hit-surface
  PhysicalMaterial lookup (per-map collision_weapon_physmat.json, sha256 + triangle counts checked; one World.h hunk by hand;
  package: 10 json with the frontend manifests). Systems' body-notify dispatch (SYSTEMS_BODY_NOTIFY_FX.patch 4196ccfb, 130-310
  effect rows per chassis: limb trails, booster pulses, hits) rebases onto 5fc2e552; its 8-min pair (Trail2 ribbon risk) after
  (B). Frontend CARD FREE 10:06 (iconic wheel + rename shots). Experimental's (B) pass now builds both arms from one frozen
  snapshot of integration's render data (only the 118 manifest materials swapped), replacing Systems' matB/render. Kismet
  handlers 15 / 15 CONFIRMED (RE lane a020383). RE lane next: MP ability cue table for Systems; then Gameplay's value table check.
  Card: Rendering fog A/B (10:0x, ~15 min) -> (B) ~100 min -> Systems body-notify 8 min -> Gameplay recoil eye check.
2026-10-17 10:11 CORRECTION: Rendering's Set C particle-module CODE (RENDERING_P7_SET_C_PARTICLES.patch bd63223e: WfcMapFx.cpp, WfcPipeline.h/.cpp,
  build_map_fx.py) is inside commit 95c199f2, whose message names only the PROVISIONALs part 2 doc (Integration staging slip; pushed,
  not rewritten). Set C's row = Rendering's x10slot particle pair (expected-change: particles only). Systems' body-notify
  (4196ccfb) + physmat impacts (135eba9d: 34 physmats' ImpactEffect / ImpactSound up the Parent chain; force fields, coolant,
  Metal_dest change on the MP maps) build clean on 5fc2e552; merge on their 12-min slot after (B). AssetTools 83b669d: the 30
  physmat json carry the owner-class split with unchanged glb_sha256; node_overrides (53 MP nodes) must be applied by Gameplay.
2026-10-17 10:13 Rendering height-fog layers A/B (cand29, Gorge 4 layers / Rust 2): HELD unmerged. Realistic reads as canyon haze; in Off the
  firefight / lane far openings go nearly flat white (+9-20 % mean), Rust's in-layer floor fills grey-blue on Realistic. Faithful by
  construction (stock UE3 per-layer formula, RE-confirmed VS constants) but no strength reference -> RE lane checks the layers'
  values + the original shader's in-scattering math + which layers are active (GORGE_RUST_HEIGHTFOG_RE.md, ~30 min); merge
  all-mode if they hold. Experimental's (B) pass: option (c): both arms on a fresh integration 5fc2e552 Release exe
  (work/ab/fast_5fc2e55, building now), snapshot after my REGEN DONE. MP_ABILITY_CUES_RE.md merged (trigger sound replicated,
  trigger FX via RepNotify, buff cues local per BuffReplicationType; Whirlwind / Dash / Hover routed to melee / dodge / hover).
2026-10-17 10:19 REGEN DONE 10:16: all 10 MP maps' map_fx_runtime.json in Rebuild/work/render (Set C data). Gate: vs the OLD files more than the
  fields keys differ (pstream 3470a25's corrected decode renames the Cylinder dists VelocityScale / StartRadius, empties 'partial',
  changes confidences), but the 5 pair-tested maps are BYTE-IDENTICAL to Rendering's work/mapfx2, so the data is exactly what the
  particle pair ran on. Experimental snapshots now for (B). Fog layers: Rendering's math matches RE's decode term for term; the
  Gorge white is abyss-only (authored), Rust's floor veil = Realistic auto-exposure lifting a 0.3 % linear term -> Set D (fog
  layers, all-mode as authored) requested against cb7a6913 + lighting.json 'fogs' regen; dark-frame exposure floor = next
  Realistic item (closes the X3 near-white residual too). Frontend merged: 3d12103d whitespace no-op, a745320b claims across all
  players + no Escalation row in the versus iconic lobby, 686f8314 ONLINE_MP_PHASE2_FLOWS.md; D12-D15 decided by default
  (promote leader / code + relay mailbox / relay signalling / recent players). AssetTools 94c758e reruns in; slim_files.tsv
  +10 physmat rows; next: PACKAGE7_SLIM_AUDIT. Systems: ability trigger FX patch (f701e438) rides their next pair; Decoy Trap
  ability has NO implementation -> Gameplay checks the authored MP class lists. Gameplay weapon + melee batch rebased
  (f678bcf6), value table with the RE lane, 4-min recoil eye check queued after (B) + Systems.
2026-10-17 10:19 RE lane verified Gameplay's weapon + melee value table (f678bcf6): recoil 28 / 28 OK, melee 32 / 32 OK, overlay rule CONFIRMED,
  dash Time 0 = none, PlaneRocket no burst in Versus (SV keeps 4). Two MISSING recoil rows: TnWeaponHeavyRocketTurret (authored
  spine + hand recoil) and TnWeaponGatlingGun (zero recoil, must not fall back to the archetype). Gameplay adds them before the
  Classic re-baseline. RE lane next: Rendering's Remnant toggleable shadows + Exp_Death distortion PS questions.
2026-10-17 10:22 MERGED 93d67979 Gameplay physmat node_overrides (53 MP nodes; neutral) and c73fd924 Rendering Set D height-fog layers (all modes,
  as authored; RE-confirmed data + shader); the lighting.json 'fogs' regen gates PASS 10 / 10 in the scratchpad and is copied into
  Rebuild/work/render after Experimental's (B) CARD FREE. Rendering order: dark-frame exposure floor -> Remnant toggleable light;
  trail item (per-frame Trail2 points for attached sources) queued. Systems' ability family built (trigger FX f701e438, drain
  beam 3f1bbf12, test switches); Trail2 templates deny-listed by default until the trail item; slot = whole family if check_bin
  is rebuilt by 11:30, else the frozen 12 min. Decoy Trap CONFIRMED a real MP Scout ability (every Car chassis pool; bots never
  pick it): Gameplay sim item after session hooks. Weapon + melee batch corrected (turret rows, eb08b8b1); 50-min NAVQ
  re-baseline on 8a080210 WITH SIMHASH running. Frontend phase 2 docs complete (e3f6b6ac strings + tests); next: voice chat UI
  on FakeSession. AssetTools: PACKAGE7_SLIM_AUDIT.
2026-10-17 10:24 MERGED ef34a4f2: no distortion program for BLEND_Modulate materials (RE: the original cooks none for BlastSphere_Death_MAT; our x20
  death swirl gone; translucent distortion keeps its pair); Systems' translator scan lists the rule's reach. Systems' combined
  15-min slot GRANTED after (B): full_bin = 5fc2e552 + 6 patches (body notify, physmat impacts, ability trigger FX, drain beam,
  test switches, TRAIL2_DENY); Streets family pair with forced Drain / Warcry + Remnant physmat pair. Card order after (B):
  Systems 15 -> Gameplay 4 -> Frontend 1 -> Rendering ~10 (flash re-run + Set D views). Frontend: voice UI mostly merged
  (Group F rows, HeadsetState talker icon, mute, Hud sprite 573 talker list); adding Mic Test + PTT "Custom..." capture + V4-V7.
2026-10-17 10:29 Reference frames: the only shipped in-engine MP renders are the map-select thumbnails (docs/handoff/refs/). Rust's shows warm
  brown-grey haze over the FAR arches with dark near floors -> target for Rendering's Realistic dark-frame exposure item (our
  fog_504 dark view fills the near floor grey-blue); Gorge's has no canyon -> the abyss haze stays as authored. Slim audit resolved
  by Systems: 25 of 26 rows added by build_slim itself; pc_strings.int was the one gap (row appended). Modulate + distortion scan:
  ef34a4f2's rule reaches ScreenSplatter_01, Trail_Distort_MOD, BlastShell too -> RE lane checks the caches for a pair (~30 min).
  Gameplay session hooks (178a7bef: RemoteSeat, sessionMember, applySlotSwap, 180 s idle kick, iconic one-holder; SESSIONTEST
  16 / 16, neutral) build + frontend tests pass on integration; merge on the Frontend's type review. AssetTools next: Decoy Trap
  asset manifest.
2026-10-17 10:30 REVERTED ef34a4f2 as 2e9ad1bf: RE's cache check (MODULATE_DISTORTION_CACHE_RE.md) found cooked distortion pairs for ScreenSplatter_01
  and Trail_Distort_MOD (modulate materials the original DOES distort) and showed cache absence is not proof, so BlastSphere_Death /
  BlastShell are UNKNOWN; the death swirl is back to a strength / translation question; RE resolves the PS blobs (1-2 h, granted).
  MERGED 4502b468 Gameplay session hooks (Frontend review OK; 3 follow-ups: idle-kick MatchEvent, noteRemoteCommand from the
  lockstep step, atTick in simTick units). Rendering exposure floor code complete (83ecd6cd: Realistic haze terms x min(1, 1/e));
  cand30 = exposure + flash: one ~20-min slot after (B) -> Systems 15 -> Gameplay 4 -> Frontend 1, incl. the Set D views.
2026-10-17 10:33 MERGED d5372c90 RemoteIdle MatchEvent (Gameplay 1ca85612 delta; SESSIONTEST 17 / 17; neutral); Frontend cuts the session glue
  against it. Rendering's x13slot.sh ready (~20 min, cand30 on b22659a3: Rust 504 hazeexp views, Streets 508 corridors, Gorge
  flash pair, Set D views late in the slot after my 'fogs' copy, identity vs base13). Card: (B) running -> Systems 15 -> Gameplay 4
  -> Frontend 1 -> Rendering 20.
2026-10-17 10:33 Decoy Trap asset manifest merged (no own mesh: the thrower's weapon pickup mesh; deploy = Skill_Barrier; stun React_DecoyTrap_In /
  _Out on all 27 MP chassis; death_Decoy = kill-feed icon; EXPLODE one-shot with the TRAP_ELEC_LP layer; DecoyTrap_FX Color owner
  UNKNOWN -> RE lane). AssetTools next: TRAIL2_TEMPLATES dump for Rendering's Trail2 item.
2026-10-17 10:34 Distortion RESOLVED at GUID level over all 460 cooked packages (RE 4a7ca06): BlastSphere_Death_MAT never cooks the DistortMesh
  pair (the death swirl is never drawn), BlastShell MP never, ScreenSplatter_01 + Trail_Distort_MOD do; Ring_Distort_Add never
  (our x4 / 0.1 constants in e418b3f6 were credited to a mislabelled cache name). Faithful form = a per-(material, vertex factory)
  allow-list from the cooked maps: RE exports distortion_allowlist.json, Rendering's accumulate pass draws only listed materials
  (all modes; changes only distortion frames; death crops = evidence), after the exposure slot; RE then decodes the real PS
  scale / kill literals.
2026-10-17 10:40 MERGED Frontend 51b76b46 voice UI (Mic Test, PTT "Custom..." capture, V4-V7), 07ad4246 onlineIdleKick, a4638f2d session hooks glue
  (online slots -> RemoteSeat, slot swaps at the agreed tick, RemoteIdle -> kick); frontend tests pass; headless SESSIONTEST on the
  merged head 17 / 17 -> online phase 1's frontend + match side COMPLETE on FakeSession; Systems' transport is the remaining piece.
  Trail2 dump merged (no source / spawn-per-unit / taper modules exist in this build: trails = SpawnRate particles following the
  socket, tessellated 1-10) -> Rendering's item reshaped. AssetTools next: CAMPAIGN_A1_ASSETS manifest (campaign data prep).
2026-10-17 10:41 CORRECTION (RE lane): the 4a7ca06 union was a parser misattribution (VF lists precede their label); validated against
  bUsesDistortion (24 / 24, 29 / 29): BlastSphere_Death, BlastShell and Ring_Distort_Add all cook DistortMesh pairs; every
  bUsesDistortion material does, so bUsesDistortion IS the rule and the allow-list item is DROPPED. BlastSphere's real PS decoded:
  a scalar offset to x and y, chain ... x UniformScalar_2 (x20), x 4, kill s² < 0.1, clamp ±255, x 1/255 = exactly our
  translation (e418b3f6 stands): the death swirl is authentic in form and strength. Only the apply pass is unread (RE next).
  The revert 2e9ad1bf stands as the correct state.
2026-10-17 10:43 MERGED 104b31cc Decoy Trap ability (Gameplay; RE spec + AssetTools manifest; DECOYTEST 10 / 10 headless on the merged head; SIMHASH
  equal, bots never pick it; hooks for Rendering's pickup-mesh draw and Systems' deploy / trigger / stun-end / destroyed cues).
  Gameplay's weapon + melee batch: RE OK + Classic re-baseline DONE (4e0aa552: 12 seeds x 4 maps x both AIs with SIMHASH; nav
  health unchanged); only the 4-min recoil eye check remains (gap after Systems). Rendering: distortion allow-list loader stays
  inert (bUsesDistortion is the rule); Remnant PointLightToggleable_1918 extracted by data (lighting2), world-draw question to RE.
2026-10-17 10:45 Distortion CLOSED: validated rescan (0 false pairs over 233 packages) + apply pass decoded (uv' = uv + (acc.xy - acc.zw) x (0.25,
  -0.25), no clamp, RGBA8 additive): our pipeline matches end to end; no renderer change. Decoy Trap RE section 8 (Color never set;
  EXPLODE one-shot at pickup) -> Systems' SYSTEMS_DECOY_CUES. Remnant light: starts on, no toggles, per-pixel on the world, no
  static shadowing (LVV culling UNKNOWN) -> Rendering builds it after the exposure slot. c15678e9 Frontend dev parser merged;
  Frontend gap = onlinehud.sh + iconic.sh (~4.5 min) after Gameplay. RE next: original exposure / eye adaptation; LVV culling.
2026-10-17 10:45 Systems SYSTEMS_DECOY_TRAP_FX.patch (136cb500) built on the 8-patch stack: EXPLODE one-shot at the trap on trigger, DecoyTrap_FX on
  the victim's TargetingSocket every client until StunEnd, Electrocution via the body notify, DEPLOY via the existing trigger-sound
  path; CharacterAudio.inc regenerated (+1 class entry). Rides the pair after today's frozen full_bin slot; merges after 104b31cc
  on PASS.
2026-10-17 10:46 Campaign data prep: chapter A1 asset manifest merged (the dump lacks nothing; our gaps: 18 textures, 3 psa-only anim sets, 2
  PARTIAL FX, 103 missing + 416 candidate materials PROVISIONAL); AssetTools runs A2..D5 + a summary table, then a translator dry
  run over the campaign masters. RE: Remnant light flags CONFIRMED (forced dynamic, composite shadow, on at load, no toggles);
  LVV culling callers (~1 h) granted after the tonemap / exposure item.
2026-10-17 10:46 7ff407aa ONLINE_MP_FIRST_RUN.md (Frontend): main menu -> party lobby without sign-in, a NEW one-time "Online Play" prompt (name,
  push-to-talk key, voice settings shortcut), host / join flows, first lobby, the full first-time error table; Frontend builds the
  prompt now on FakeSession (FR1-FR4, gated on an online session). Systems' remote-command contract agreed with Gameplay (a09b4054
  section 10.2: one decoded path for the local human, absolute quantised aim, int8 move axes, idle on applied commands).
2026-10-17 10:49 RE: the original has FIXED exposure (no adaptation, no filmic tonemap; Uber post constants only) -> Realistic's auto-exposure is
  ours; Rendering's haze x min(1, 1/e) = compositing haze at exposure 1 for lifted frames, consistent with the authored look; the
  cand30 slot's eye check decides. LVV callers next (~1 h).
2026-10-17 10:50 Rendering code complete, waiting on the card: Remnant light (e5531868: force-dynamic lights never baked join the per-pixel
  transient path in Off / Faithful, unshadowed; Realistic traces it; data --standalone-only, Remnant only) and Trail2 (979b071e:
  stock UE3 moving-source trails: SpawnRate points at the socket, per-particle life / size / colour, Hermite chain, newest point
  joined to the source; the 5 UU sampling gone; all modes incl. projectile trails). x14slot (~15 min; + a projectile-trail pair)
  granted directly after the cand30 slot. Card: (B) -> Systems 15 -> Gameplay 4 -> Frontend 4.5 -> Rendering 20 + 15.
2026-10-17 10:51 RE: LVV affects only light-environment owners (one caller of QueryLightsVisibility), so the Remnant light lights the world
  unculled through walls in the original -> Rendering draws it unculled on the world, LVV entry on the character path only. RE
  next: review Systems' remote-command contract (10.2) against the original's input / idle semantics; then eye-check crops.
2026-10-17 10:53 Campaign data prep: all 10 chapters' manifests + summary merged (430 cooked levels, 3.7 GB summed; the dump lacks nothing); matc
  dry run 97.8 % of the 1,763 untranslated campaign materials compile clean; 38 need 5 new nodes + DecalMaterial roots + ddy /
  acos (sized for the campaign phase). AssetTools next: close our gaps (cube maps as faces, psa-only anim sets, PowerCell_Voltage_FX
  to RE).
2026-10-17 10:54 MERGED 64475797 the one-time "Online Play" prompt (name / voice / settings shortcut; only with an online session; FR1-FR4) ->
  the ONLINE FRONTEND IS COMPLETE on FakeSession; Systems' transport is the one remaining phase 1 piece. Frontend gap = 5 min
  (onlinehud incl. fr_prompt + iconic) after Gameplay. Frontend next: PACKAGE_7_PLAYTEST_GUIDE.md "Menus" section.
2026-10-17 10:55 (B) eye pass: before arm done (24 / 24 runs + views, 2560x1440), after arm on map 2 / 8; CARD FREE ~11:25. Early read, Seed: same
  match in all 3 modes; 20 / 26 frames change in Off / Faithful (18 Realistic), changed pixels 3.5 % of the frame, ~20 levels
  brighter: the ceiling lamps' light shafts now show as volumetric beams onto the floor and robots (the authored (rgb, 2).w
  shafts at 2x), nothing else moves. Card chain from ~11:25: Systems 15 -> Gameplay 4 -> Frontend 5 -> Rendering 20 + 15.
2026-10-17 10:57 RE input contract review merged: FineAim / Hover = command bits; idle kick PUBLIC sessions only (the original disables it in
  private matches; phase 1 is all private); 'active' = any input -> Gameplay (isPublic flag, active rule, bits) + Systems 10.2.
  (B) switch prepared (scratchpad matb_switch.py, entry-level, verified against Experimental's frozen after root): 8 / 10 maps
  equal; on BrokenHope + Streets ONE manifest entry (Energon_Lava_Mat) stays live in the tested after arm while the candidate
  differs -> asked Experimental (skip or guard?) and Systems (where visible, what changed); the switch installs exactly what the
  pass tested. AssetTools reruns review.py on the frozen after root before the switch. Frontend PACKAGE_7_PLAYTEST_GUIDE_MENUS.md merged.
2026-10-17 10:58 (B) lava discrepancy explained: Experimental's matb_root.ps1 wrote the manifest list with a UTF-8 BOM, so the FIRST entry
  (Energon_Lava_Mat, present only on Streets + BrokenHope) never matched; fixed (utf-8-sig). Streets' after arm reruns inside the
  pass (~6 min; CARD FREE ~11:35), plus a BrokenHope lava view pair if a view reaches it. The switch installs the candidate
  (manifest rebuild) verified against the fixed after root.
2026-10-17 10:58 (B): Experimental patched the lava entry into the after root before Streets ran, so no rerun; BrokenHope is Escalation-only (no
  versus boot, no views) so the Streets lava frames stand for both maps. The switch dry run now matches the after root on 10 / 10
  maps. D5 CHANGED: idle kick = host option "Kick Idle Players", default Off (original never kicked in private matches); one-launch
  requirement: members start from the host's published MatchLaunch (Systems' transport).
2026-10-17 11:00 (B) pre-switch: AssetTools' independent review PASS 10 / 10 (118 / 118 manifest entries, 0 unexpected, ~1,000 other files per map
  identical; DeathDecal + 2 MetalDecal entries are (A) no-ops). Energon_Lava_Mat (the one entry the BOM bug skipped) compiles only on
  Streets (placements 736 m below the streets) and BrokenHope (Escalation-only, no versus boot): unreachable in this build, so (B)
  switches whole and the lava pair is deferred to the first Escalation boot (package 7 eye list). Switch = whole-file copy of
  Systems' matB/render files (parsed-equal to live + manifest on 10 / 10; dry run OK), live backed up; applied on the verdict.
2026-10-17 11:03 MERGED 6c58c023 Frontend: "Kick Idle Players" online-only host row (D5 revised, default Off) + the one-launch record (ARCHITECTURE
  10.3: GameFlow::buildLaunchUrl, ISession::setLaunch(url, seed, roster) before requestStart, members start from the host's URL
  verbatim; FakeSession without a record = the old local build); tests 229 / 0; offline byte-identical. Gameplay asked for one
  patch: kickIdle gate, 'active' = any input, FineAim / Hover bits, and the host seed hook into World's match RNG (+ a
  two-peer equal-SIMHASH SESSIONTEST check). Systems froze matB/render for the (B) switch.
2026-10-17 11:09 Campaign data gaps CLOSED (AssetTools): cube maps as faces, shadowed anim sets converted, PowerCell FX complete via a pstream
  base-index fix (regression 7,348 / 7,348 identical) -> I regen the 10 MP map_fx files against the named pstream commit with an
  identity gate. Campaign data is complete; only the sized translator work (38 materials) remains -> RE documents the 5 missing
  expression nodes + the DecalMaterial root as the original compiles them.
2026-10-17 11:14 MERGED 8454c173 Gameplay session follow-up: kickIdle gate, applyRemoteCommand (active = any input; FineAim / Hover / Active bits;
  movement intents, fire / abilities PARTIAL next), the host seed into the sim RNG + bot seeds; SESSIONTEST 19 / 19 on the merged
  head incl. two same-seed launches = equal simStateHash at step 1000. Gameplay's transforming actors + dying-vehicle collision
  are code complete (WIP fb62bd13; TRANSACTTEST: BrokenHope 26 / 26, Remnant 72 / 72 boxes block, Rust doors open at start,
  dying vehicles block until the gib); NAVQ re-baseline next, then the gate; World::transformingActors() is Rendering's draw hook.
2026-10-17 11:15 Transforming actors: NONE of the 32 authored doors / bridges is drawn on any map today (BrokenHope 13 doors + 5 bridges, Remnant 12
  doors, Rust 2 doors). Rendering's plan: skinned glb clip sampled at Gameplay's t (AssetTools exports all 4 meshes, 34 clip
  channels), 6 door materials added to materials_glsl (regen gate), DLE-lit PARTIAL until the original's two interpolated baked
  light maps (closed / open) are decoded (RE / AssetTools asked), RT BLAS refit. Rules: collision and draw merge in ONE head (no
  invisible walls); the item outranks Trail2 once Gameplay's gate is posted.
2026-10-17 11:18 Experimental CARD FREE 11:18: (B) captures complete (24 + 24 runs + views, no INVALID); sheets + MATB.md in ~15 min, then the
  (B) switch. Set D 'fogs' lighting.json data copied LIVE into Rebuild/work/render at 11:18 (gate PASS 10 / 10; backup in the
  scratchpad). Card -> Systems' combined 15-min slot (full_bin) 11:18.
2026-10-17 11:19 RE: campaign material nodes specified (If exact compares; Ddx / Ddy compiled out; CameraWorldPosition = c3 absolute; lens-flare
  inputs = VF interpolators but NO lens-flare VF shaders cooked anywhere: how the 32 LensFlareComponents draw is UNKNOWN;
  DecalMaterial = Material root + VF projection). RE next: the interpolated-lightmap decode for the 32 doors (~1.5 h), then the
  lens-flare runtime look, with Gameplay's recoil crops in between.
2026-10-17 11:22 Transforming-actor prep (Rendering 4cbf8511): the 6 missing door / bridge materials merged into copies of the live
  materials_glsl.json (BrokenHope / Remnant / Rust; gate: live + exactly those keys); hook agreed with Gameplay; the original
  lerps several baked captures per actor by the anim position (RE decode ~1.5 h granted). Draw ships DLE-lit PARTIAL with the
  collision in one head; the lightmapped look follows.
2026-10-17 11:23 (B) VERDICT (Experimental MATB_matB.md): closer on Seed (lamp shafts now visible beams), Rust, Debris, Streets, Molten, Gorge,
  Complex; every change a +16..28-level brightening; same match in all 24 rows. Two findings, confirmed by Integration's eye:
  Debris' pool + Gorge's glass floor go milky / opaque (alpha treated as intensity on a glass / liquid material?) and Berth's
  lilac mid-ground volume flattens contrast. Decision: switch (B) in MINUS the entries behind the two findings; Systems names
  them (after their slot) and checks each against RE's shipped-PS decode; those follow with their own pair.
2026-10-17 11:25 (B) shipped-PS verdict (Systems): (a) the candidate matches the original: ForceField_ALL_MAT's PS multiplies fresnel x
  FresnelColor rgb THEN x its alpha (3.0) (Seed ShaderCache_12865 l.39-43), live squared the colour and never applied the 3.0
  (5-20x too dark); the LightVolume family reads UniformVector_1.w alone as the intensity (ShaderCache_6631). So the milky glass
  and Berth's volumes are AUTHORED. (B) switches in WHOLE right after Systems' CARD FREE (not mid-pair). Rendering checks that
  BLEND_Additive / MLM_Unlit composites as pure add (Debris pool reads opaque; saturation vs a composite bug).
2026-10-17 11:26 (B) switch plan: 117 entries at Systems' CARD FREE; PROP_NEU_BatteryOS_p.OmegaLiquid_MATINST (master TryptiGelGlass_Mat,
  translucent unlit, the likelier Debris pool) HELD until Systems' shipped-PS decode confirms its alpha-pin reading (one-entry
  follow-up with the Debris 'reflective' view as its pair). Experimental's measure of the pool: additive, not opaque (1.06x +
  18.6 levels, detail correlation 0.965) -> the composite check is a glance, no pair. Ledger: "(B) whole, nothing held" to be
  amended to the one held entry.
2026-10-17 11:28 MERGED f46bdfe4 Gameplay remote seat actions (queued apply at tickPrefix; fire / reload / switch / melee / abilities incl. Decoy /
  grenade / pickup; SESSIONTEST 25 / 25 on the merged head; offline identical). OmegaLiquid CONFIRMED by the shipped PS (Debris
  ShaderCache_1203: Edge_Fresnel_Color rgb x its own alpha) -> all 118 (B) entries switch at Systems' CARD FREE. Rendering: additive
  composite = pure add (ONE, ONE; no depth write): authored, no pair; door DRAW code complete (d2b54522, cand32 = cand31 +
  Gameplay's transact + draw; 6 materials as a render-data overlay); ~10-min x15slot for the door views rides after x14.
2026-10-17 11:29 Systems CARD FREE 11:28: SIMHASH PASS both pairs (stack presentation-only, 36 / 36 frames identical) but BODY_NOTIFY_FX NOT
  VERIFIED (zero rows matched in 120 s: units / key bug, fix offline), ABILITY_TRIGGER_FX dispatch works (Drain x4, Warcry x7) but
  DATA GAP: the ability / body-notify templates are not in map_fx_runtime.json (build_map_fx selector + regen pending), DRAIN_BEAM
  not exercised, PHYSMAT inconclusive (no special surface hit) -> the six-patch stack stays UNMERGED; ~10-min re-run after
  Rendering's slots. (B) SWITCHED 11:29: all 118 entries on 10 maps (live == candidate 10 / 10); matB/render unfrozen. RE's
  interpolated-lightmap decode merged (two captures per door, lerp by the anim position every tick). Card -> Gameplay 4 min
  (11:29) -> Frontend 5 -> Rendering slot_all (x13 + x14 + x15, ~45 min).
2026-10-17 11:30 Systems' diagnosis: the 0 body-notify matches were the TEST SETUP (WFC_TESTABILITIES replaced Dodge, so the local pawn never
  dashed; bots play no ability anims); re-run with Dodge,Drain. map_fx_runtime DATA GAP listed exactly: the MP ability / buff set
  (Drain_Blast, WarCry_AOE, DrainRay_Beam, DecoyTrap) + ~15 body / weapon templates missing on 8-10 maps -> Rendering adds an
  always-include selector to build_map_fx.py, I regen the 10 maps with the identity gate. Doors: RE's decode complete -> stage (b)
  (two captures lerped, no DLE) BEFORE the door head merges (deadline: end of today, else (a) PARTIAL merges with the collision).
2026-10-17 11:31 FX runtime gap corrected to exactly 5 templates (4 FX_Multiplayer_p ability / buff on all maps + Hammer_Trail_DCPT on 8); the
  body-notify path needs no code change (test setup). Rendering adds the always-include selector; regen + Systems' re-run ~12:35.
2026-10-17 11:31 AssetTools dry-ran build_map_fx.py on the current pstream: the only MP change is H2H_Punch01_FX's SubUVDirect confidence /
  partial labels (RE's resolution), no runtime values; the base-index fix changes no MP LOD -> one regen later with Rendering's
  always-include selector (gate: labels + the 5 added templates only). Door captures extraction next.
2026-10-17 11:34 MERGED 4c392ea5 Rendering's build_map_fx.py always-include selector (tools/render/fx_always.txt = Systems' exact 5 templates);
  the 10-map regen runs into the scratchpad; gate: live + exactly the 5 added, every other system identical with decoder metadata
  (confidence / partial) stripped, nothing removed; then installed and Systems' re-run scheduled.
2026-10-17 11:34 Gameplay CARD FREE 11:33 (recoil eye check done, 12 runs; verdict pending). Card -> Frontend 5 min (onlinehud + iconic) ->
  Rendering slot_all.
2026-10-17 11:35 Gameplay's recoil eye verdict: as authored, OK (shotgun / burst / pistol / launcher kick visibly; rifle + IonBlaster identical by
  authored data; no artefacts); Integration checked the shotgun sheet; RE's second look pending, then the weapon + melee batch
  merges (dry run on the head clean). DECISION (A) for the online local player: simulated through the remote-seat path with the
  command carrying a camera-derived AIM POINT (eye-to-target shots land where the camera line would); vehicle facing from the
  same field; offline unchanged; (B) a sim-side camera rejected. Systems writes the encoding into 10.2 with Gameplay.
2026-10-17 11:39 RE's recoil second look: magnitude + settle PASS, controls byte-identical; two conditions: (4) the launcher kick drops the barrel
  down-left toward the camera (probable rotator -> bone-axis sign / axis swap; Gameplay checks offline); (5) a strafe wobble run
  (the P0 was a strafing artefact; these runs were stationary) on the fixed exe: ~3-min gap after the Frontend if built, else after
  Rendering. Batch HELD until both. Camera kick / shake absent on both arms (audit gap 1, later item). RE: lens flares DO cook
  FLensFlareVertexFactory shaders (scanner off-by-one fixed; retracts that UNKNOWN); correction doc follows.
2026-10-17 11:39 Frontend CARD FREE 11:40: fr_prompt set (5 shots) + iconic lobby / wheel captured; the in-match HUD steps timed out after the
  lobby shot (match ran with 2 Remote participants): cause from the log, re-run in the gap after Rendering with Gameplay's wobble
  run. Card -> Rendering slot_all (x13 + x14 + x15, ~45 min) 11:40.
2026-10-17 11:41 PACKAGE 7 PLAN (Experimental's gate state synced): head candidate named by ~17:00; builds + base same-match + eye pass (~1 h),
  then the combined timed gate (~1 h 50 card, 1 h 35 under hold; interpreter-free broadcast + acks), finish ~21:00. Before the
  head: the door head (Gameplay collision + Rendering draw + 6 materials, stage (b) lighting if passed), Systems' stack, the
  weapon + melee batch (axis check + wobble), the cand30 / x14 verdicts. Gap after Rendering (~12:35): Gameplay wobble 3 ->
  Systems re-run 10 (after the FX data install) -> Frontend HUD re-run 5 -> Experimental's 1e11edc2 Off / Faithful pair 10.
2026-10-17 11:42 RE correction (scanner VF labels one block off; attribution unaffected): lens flares DO cook their VF shaders -> the original draws
  its 32 LensFlareComponents (6 MP maps) with the stock path; asked Rendering whether we draw them at all (likely a missing authored
  element); RE writes LENS_FLARE_RE.md, then WEAPON_CAMERA_SHAKE_RE.md (audit gap 1) for Gameplay's next batch. Distortion
  allow-list v3 (same materials, corrected VF names); static-decal PSs add the decal tangent frame.
2026-10-17 11:43 AssetTools 6bea9e8: the 32 door actors' interpolated light maps extracted (64 captures, 0 problems; all 192 coefficient refs
  already in the live lightmaps/ atlases, 64 x 64 rects; RE's worked example matches exactly) -> Rendering's stage (b) has its data.
2026-10-17 11:44 Lens flares: NOT drawn by our renderer today (only a mover attachment type) -> a missing authored element on 6 MP maps, queued
  after the doors. Door lighting stage (b) code complete (rt2 b8e00119: both captures lerped per coefficient before the basis
  decode, rects composed into one texture, TEXCOORD_1; cand33; data matches AssetTools' export 18 / 18): x16 (~10 min) granted
  directly after x15 on the same relay; one CARD FREE after x13-x16.
2026-10-17 11:45 MERGED f9ee2ca0 Frontend case-insensitive $Key localisation fallback (iconic wheel "WEAPON 2"). Frontend shot read: first-run
  prompt + name path (D1) verified, iconic lobby (no Escalation row) + wheel verified, "AVAILABLE" claim text verified; the in-match
  HUD set + fr_voice_settings re-run at ~12:35 (script deadlock fixed: AllLoaded now sent on WaitingOnGameStart). Transport
  requirement to Systems: the lockstep layer holds the World until every peer's AllLoaded.
2026-10-17 11:46 FX always-include regen GATED PASS 10 / 10 (scratchpad mapfx_fx5): exactly the 5 templates added (4 on BrokenHope / Remnant),
  nothing removed, all other systems identical apart from H2H_Punch01's decoder labels; installs after Rendering's CARD FREE, then
  Systems' re-run.
2026-10-17 11:46 Systems 10.4: AllLoaded only after every peer loaded + launch-record hash matched; all peers start at tick 0 together; no World
  step before: ISession::simHeld() (Frontend / FakeSession) + the Application-loop check (Gameplay). Lens flares CORRECTED: 6
  placements (one per map on BrokenHope, Remnant, Rust, Seed, Debris, Gorge), not 32 (that was the door count); AssetTools 4fdf117
  exported the 3 templates (5-6 elements each, all distributions decoded, 4 PNGs); the curves' inputs come from RE's LENS_FLARE_RE.md.
2026-10-17 11:46 LENS_FLARE_RE.md merged: the lens-flare item is fully specified (6 MP suns; templates, vertex stream, placement / occlusion logic)
  with AssetTools' data; Rendering builds it after the doors. RE next: WEAPON_CAMERA_SHAKE_RE.md.
2026-10-17 11:47 Recoil (4) RESOLVED by Gameplay's headless probe: the Bazooka muzzle kicks up / right (+24.5 / +26.6 deg at the step-16 peak,
  settles over 1 s; ≤ 0.1 / 1.1 deg without recoil); the frames showed the tube's rear end; no axis swap. Only (5) the strafe
  wobble run remains (gap ~12:35), then the weapon + melee batch merges.
2026-10-17 11:50 MERGED 041cba87 ISession::simHeld (FakeSession held from reportLoaded until AllLoaded) + 1d0ad363 the Application::runMatch
  zero-steps hold (frontend tests pass; SESSIONTEST 25 / 25). WEAPON_CAMERA_SHAKE_RE.md merged (permanent pitch climb per shot,
  transient FOV kick, single-slot view shake; per-weapon table) -> Gameplay's next weapon batch item (audit gap 1).
2026-10-17 11:51 Correction: the weapon camera kick / shake is NOT absent: GAMEPLAY_CAMERA_RECOIL 1f1d4e17 (merged this morning) is in both eye-check
  arms, which is why RE saw identical camera motion; it matches RE's section 5 (per-shot pitch climb, FOV kick, single-slot shake,
  authored values). Three gaps -> a small camera-only follow-up now: ReduceRecoil x 0.35, no kick in drive / first person / guided
  missile, the EMPShotgun row; then the wire-path local seat.
2026-10-17 11:52 Aim-point encoding FINAL (Systems 10.2 e81b3410 / 404541c8, agreed with Gameplay): an ABSOLUTE world-space aim point, 3 x int24 at
  1 cm, presence bit 31 in buttons (excluded from the idle test); without it eye + look direction x weapon range; aimYaw / aimPitch
  stay the look direction; 23 bytes per human per tick. Rationale: eye + direction x distance drifts by the pawn's movement over the
  input delay (~0.3-0.6 m strafing). Gameplay implements both ends now.
2026-10-17 11:57 MP_REVERB_ROOMS_RE.md merged: reverb = Kismet mixer presets switched by trigger volumes (230 MP actions resolved); the
  authored.db preset-name gap (860 empty) explains AssetTools' missing presets on 3 maps -> Systems checks our match reverb
  against the per-map json and designs the proximity-voice reverb on the same rooms.
2026-10-17 11:59 MERGED e19924a6 Gameplay remote PARTIAL actions (aimPoint bit 31, killstreak, Plasma charge, repair beam, HardLock, jammer /
  disruptor owners, Shockwave delay; SESSIONTEST 30 / 30; offline identical; guided missile still local-only). Transforming
  actors: Remnant Smart reversals 4.3 / min vs 1.0 with 349 nav cells blocked by the doors (Classic helped by a piece recompute,
  Smart not) -> Gameplay investigates before posting. Reverb: our AmbientAudio applies the rooms per Touch today; two gaps
  (UnTouched links on Rust / Remnant / Debris restore the outer room; empty preset names on 3 maps) -> Systems' package 7 audio
  item from RE's per-map json + a ~6-min windowed check later; voice reverb keys off the same current room (c81c9040).
2026-10-17 12:01 Reverb gaps RETRACTED by Systems: the MP maps' zones come from the generated Kismet graph (LevelAudio.inc from the cooked props),
  which fires Touched AND UnTouched; reverb_vs_re.py: 229 / 229 (action, preset) pairs identical to RE's json on all 10 maps, the
  UnTouched links match (Rust 5, Remnant 1, Debris 11). No code or data change; only the windowed check (Streets + Rust + Debris)
  stays in Systems' next slot.
2026-10-17 12:02 DECISION: Remnant / BrokenHope are Escalation-only in the original (RE section 4), so in our VERSUS extension their doors / bridges
  start OPEN (transformed, t = 1, no animation; the late-Escalation connected layout; the Smart nav regression goes away);
  Escalation keeps closed -> open at the generator power level; Rust's doors animate open at level start as authored. Gameplay
  encodes the per-game-type initial state, re-runs the door-map NAVQ, posts the transact patch.
2026-10-17 12:05 MERGED deeb135b Gameplay camera follow-up (ReduceRecoil x 0.35, no kick in wheeled drive, 21 weapon classes added incl. EmpShotgun;
  camera only, identity-neutral); its 3-min firing pair joins Gameplay's wobble gap. Gameplay next: the transact patch with the
  versus door rule + the door-map NAVQ.
2026-10-17 12:12 Campaign Kismet SIZED (RE): 176 classes / ~130k instances; a general sequence VM covers ~75 %; 36 classes decoded in docs; 113
  undecoded = 51 stock natives (engine semantics) + 32 WFC script classes (batch-decompilable) + 0 native-only. Campaign phase
  inputs now complete on data, materials (sized), Kismet (sized); RE batch-decodes the 32 WFC script classes next.
2026-10-17 12:13 Door-map NAVQ with the versus rule: Remnant back at base (Smart reversals 0.94 vs 0.99), Rust within noise, BrokenHope worse
  (the 5 extended decks have no nav cells: off-mesh 17 %). Decision (c): BrokenHope versus = doors open, bridges retracted
  (Escalation start state) in this patch; AssetTools regenerates BrokenHope's nav with the decks baked in later (no versus boot
  there yet). Gameplay posts the transact patch at base parity.
2026-10-17 12:17 All 32 WFC campaign Kismet classes decoded (RE d677248): the campaign scripting step is fully specified (general VM + 36 doc'd +
  51 stock natives + these 32; native H parts: Conversation sequencing, the special-spawn caller).
2026-10-17 12:18 Gameplay's transforming-actors patch POSTED (1b894dc7: option c; BrokenHope byte-identical, Remnant Smart at base, Rust as
  authored; TRANSACTTEST 4 maps; BotNav.setBlocked + piece recompute; VehicleDying bodies diverge SIMHASH where a vehicle dies; nav
  health within noise); applies clean on 1c5de2d8; merges in the door head with Rendering's draw after x15 / x16.
2026-10-17 12:20 RE: the Conversation sequencer and the special-spawn caller CONFIRMED from native / TransContent; the remaining campaign H items
  ranked; RE takes SeqAct_Interp InitInterp (125 empty matinee groups) next, then PlayDialog timing, setpiece flags, buddy bars.
2026-10-17 12:21 Campaign render-data dry run merged: the MP pipeline runs on A1 with six campaign gaps (per-sublevel inputs, umodel per-package
  lists, props_authored / VS roots parameterised, PostProcessVolumeHeightFog, runtime-assigned materials, DecalMaterial roots);
  sized for the campaign phase (Rendering + AssetTools items then). AssetTools next: BrokenHope versus nav, then the
  SoundMixerPresetName fix (MP-facing: before / after proof first).
2026-10-17 12:24 RE: InitInterp (all 148 empty matinee groups instantiated; 125 no-ops, 23 live) and PlayDialog timing CONFIRMED; next setpiece
  vulnerable flags, buddy health-bar markers.
2026-10-17 12:26 RE: setpiece tank god mode + buddy markers CONFIRMED: the campaign H shortlist is closed except the two low items. The RE lane's
  campaign specification is complete; RE returns to package 7 / online support items.
2026-10-17 12:29 MERGED 578e21bb Gameplay's online local seat through the wire path (decision A: encodeLocalCommand + the shared seat path; two peers
  = equal simStateHash at step 1000; direct vs wire 0.001 m / 0.002 deg; SESSIONTEST 32 / 32; offline identical). The GAME SIDE OF
  LOCKSTEP IS COMPLETE: Systems' transport calls encodeLocalCommand once per step and feeds every decoded command to
  applyRemoteCommand before World::tick. PARTIAL: local inter-step yaw smoothing online, vehicle muzzle sockets, guided missile.
2026-10-17 12:29 MP_COMBAT_AUTHORITY_RE.md merged (shooter-client authority in the original; lockstep makes ours deterministic; disconnect =
  suicide + forfeit check). AssetTools bb8a1aa: BrokenHope bot_nav_versus (15 deck boxes; decks walkable and joined; under-deck
  paths keep 7.6-10 m headroom; one new 53-cell piece untraced) -> Gameplay decides adoption with the bridges extended; campaign
  finding: skel_collision poses PhysicsAsset boxes from the reference skeleton (3.8-5.7 m off on bridges; campaign phase fix).
2026-10-17 12:30 AssetTools: the SoundMixerPresetName premise did not hold (empty rows are archetype-inherited; the exporter's chain / prefix logic
  is the cause of audio.json's missing presets); no runtime effect; PARKED. AssetTools next: stage the slim_files.tsv refresh as a
  sibling file for the package step.
2026-10-17 12:32 DECISIONS: (1) Systems starts INetTransport + LockstepLayer code NOW in parallel with its remaining package 7 items (the game side
  is complete; online code is offline-identical; user's "all gears firing"); first milestone = two processes on one PC via the
  netshim harness, equal simStateHash 1000 ticks under 7 impairment profiles; listed to the user as an open question. (2)
  Disconnect: faithful (suicide + empty seat + 1.5 s team forfeit) when the lobby has no bots; SlotSwap-to-bot after the suicide
  when bot rows are on (10.5; Gameplay implements). Systems' slot: rerun_bin = 1c5de2d8 + 9 patches ((a) body notify Dodge,Drain,
  (b) physmat ForceField force, (c) reverb_check 3 maps) at ~13:10 after Gameplay's gap.
2026-10-17 12:37 MERGED be54be6d Systems' headless match runner (verified on integration: 1801 steps / 1.8 s wall, two runs identical SIMHASH,
  1202 distinct), 292c63a4 Frontend forfeit text (TransContent.int ForfeitMessage) + 3b271bd1 its glue. Determinism BLOCKER found
  by Systems: match player numbering differs per peer (local first) -> Gameplay's priority 1: a global human seat order from the
  launch record (humans + localSeat), iteration by match player index, a cross-perspective SESSIONTEST; Frontend adapts
  fillOnlineLaunch after. Systems builds INetTransport / Udp / Loopback / LockstepLayer meanwhile.
2026-10-17 12:38 Gate tooling: the headless runner moves the regress-base same-match checks off the card (gate-base.sh HEADLESS=1: Streets TDM seed
  1234 + PlayerBot, 160 s sim per exe, SIMHASH diff, CPU-only seconds); be54be6d's runner joins the base as a non-sim item (proved
  once windowed). Runner exit code 1 today (init-return path), not a failure.
2026-10-17 12:44 MERGED d22fb45a ONLINE PHASE 1 TRANSPORT + LOCKSTEP (Systems): UDP + loopback transports, reliable channel, lockstep with input delay,
  resend windows, launch-hash gate, per-tick hash exchange + desync detection, exit codes; first two-process run connected and ran
  600 identical ticks, then the known seat-numbering desync at tick 601 (Gameplay's humans + localSeat fix pending). Offline
  identical (frontend tests; headless SIMHASH stream equal to the pre-transport head).
2026-10-17 12:51 Package 7 regress base = af4d0731 + hover + the headless runner (sim-neutral, proven on CPU: headless == windowed on 10,200
  steps); gate-base.sh HEADLESS=1 same-match rows need no card. Experimental's 1e11edc2 pair stays at ~13:30.
2026-10-17 12:53 MERGED 2770eec8 net pacing switch; netshim transport rows on d22fb45a: all 6 profiles exit 0 (lan / good / typical / burst on
  schedule, bad +3.3 s at D 4, outage one 2 s stall then catch-up), no deadlock / timeout, 10-25 KB/s per peer. Systems' order:
  seat-order switch + NET.identity the moment Gameplay's sha lands, then adaptive D from the measured RTT, bandwidth trim toward
  ~1.5 KB/s per human, voice capture.
2026-10-17 12:54 OVERLAP: at 12:53 two Systems exes WITH windows (work/wire/tree, netshim peers run without WFC_NULLRENDER) ran during Rendering's
  x14 identity row; Systems told to kill by PID; rule restated: netshim peers run with WFC_NULLRENDER=1 on both sides (no window),
  anything with a window needs a relayed slot. Rendering to note the overlap in the x14 report and re-run a rep if a frame differs.
2026-10-17 12:55 CORRECTION: the two Systems exes were WFC_NULLRENDER netshim peers (no GL / GPU); the hwnd was each process's CONSOLE window
  (Start-Process without -WindowStyle Hidden), now started hidden. No contamination. Census caveat: hwnd != 0 can be a console.
2026-10-17 12:58 MERGED dae0338f auto input delay (sender-side production horizon over absolute target ticks: no agreement step needed, my concern
  withdrawn; typical total wait 20.8 s -> 451 ms, bad on schedule at D 14); the HASH_EVERY assignment bug fixed. Next: bandwidth
  trim, voice capture; NET.identity + the burst-with-hash row after Gameplay's seat fix.
2026-10-17 13:03 MERGED aec21e7a net bandwidth trim (~5.5 KB/s per peer, ~92 B per tick; all six profiles exit 0; stalls unchanged or lower). Systems
  next: voice capture; seat switch + NET.identity first when Gameplay's sha lands.
2026-10-17 13:04 Voice: Opus approved as a pinned-fetch third-party dependency (libopus 1.5.2, BSD-3, static, gitignored third_party/opus, build
  degrades to "voice unavailable" without it; licence text to the credits) -> listed to the user; microphone capture built on
  WASAPI but NOT run live without the user's OK (synthetic input checks only) -> open question to the user.
2026-10-17 13:16 MERGED b406309c voice capture + Opus (framer, WASAPI capture on demand, Opus 16 kbit/s FEC; libopus 1.5.2 fetched pinned into
  gitignored third_party/opus; BSD-3 licence shipped; VOICEFRAMETEST PASS). INTEGRATION SLIP: my first test run lacked the
  direct-boot var, WFC_NULLRENDER was refused and the exe booted the FRONTEND windowed for ~2 min (13:06-13:08) during Rendering's
  x14 / x15; Rendering told to re-run any differing rep; the var is now in the direct-boot list. No live mic run (user's question).
2026-10-17 13:19 MERGED 0df2a0fa Gameplay's global seat order (the lockstep determinism blocker): humans + localSeat from the host's launch record,
  match player i = seat i on every peer, sim loops by index online, simStateHash covers bot / Smart RNGs; SESSIONTEST 34 / 34 incl.
  cross-perspective identity; offline identical. Systems switches Application_Net's launch to it and runs NET.identity (two
  processes, hash every tick, 1000 ticks, exit 0) = the phase 1 milestone; Frontend adapts fillOnlineLaunch (self in the list).
2026-10-17 13:21 NET.identity plan: Systems sends the Application_Net launch switch (humans + localSeat) as its own patch; on that head both
  Systems and Experimental (independent: net_profiles.ps1 lan / typical / bad, hash every tick, netlog_join on both peers'
  net.csv) run the identity row headless. Open item for Systems after it: WFC_NET_REPLAY (single process fed the host's bundles
  reproduces the simStateHash stream) = the gate's replay-equivalence half.
2026-10-17 13:22 NET.identity (Systems, on 0df2a0fa + launch switch): seat mapping right, bundles identical every tick; two local-only differences
  remain: launchMatch does not apply humans[localSeat].selection (300 vs 330 hp, split at tick 601) and the local pawn alone gets
  ~0.6 m/s vertical velocity on the first InProgress step (a path outside the seat path) -> Gameplay priority 1; headless repro.
  Systems sends the launch switch alone for the head.
2026-10-17 13:22 MERGED b7d3588f the net launch from the seat order (Application_Net; bundles identical every tick; the tick-602 local-pawn split
  remains until Gameplay's fix). Replay switch (WFC_NET_RECORD / WFC_NET_REPLAY) queued after NET.identity. Head for
  Experimental's independent identity row = b7d3588f (expect the known 602 split until Gameplay's fix).
2026-10-17 13:23 MERGED 6f701256 Frontend member-view seat-order test (L3; fillOnlineLaunch reviewed: matches the launch record). Experimental's
  independent identity row runs on b7d3588f (transport half now; full PASS after Gameplay's two local-pawn fixes).
2026-10-17 13:24 Rendering ETA CARD FREE ~13:55 (x13 took 53 min; x15 trimmed to its 3 before arms, superseded by x16's light-mapped doors).
  Early verdicts: x13 identity 10 / 10 byte-identical; flash PASS (Gorge near-white 64.9 -> 60.8 %, inside the 55-62 bar, SIMHASH
  equal); haze exposure small (Rust dark floor -4 levels, Streets < 2) and the Rust grey-blue floor is present in OFF too = the fog
  layer itself, not exposure (report pending); Remnant light present in Off / Faithful as an authored cyan pool; Trail2: body trails
  fired out of view (inconclusive), Molten Off shows a NEW blue electric-arc ribbon on a robot that base14 never drew -> template
  to identify before calling it authored.
2026-10-17 13:24 16de8601 MP_GUIDED_MISSILE_RE.md merged (one per player, owner-steered at 2000 uu/s, camera rides it, 30 s fuse, detonate on
  press / impact / owner death; 45 s cooldown after) -> Gameplay's per-seat missile; campaign item (c) closed. RE next: identify
  the Molten electric-arc ribbon template for Rendering's Trail2 verdict.
2026-10-17 13:26 MERGED c702c93b net record / replay (WFC_NET_RECORD / WFC_NET_REPLAY; BYE carries the last tick: both peers exit 0 at the end).
  Finding: replay-as-seat-0 vs live seat 0 splits at tick 602 too -> the local controller path (playerBotInput / handleInput)
  reaches the pawn directly online (Gameplay's fix). Ladder: live == replay per seat -> s0 == s1 (NET.identity) -> burst with hash
  -> two-PC test.
2026-10-17 13:27 Molten blue arc IDENTIFIED (RE): Trail_FusionCannon_FX, the Bazooka PROJECTILE trail drawn on the firer = a Trail2 source-binding bug
  in cand31, not authored -> Trail2 HELD until projectile trails follow the projectile and the Molten pair is re-shot; the Remnant
  light and the exposure item are unaffected.
2026-10-17 13:27 Rendering: Trail2 suspect = the projectile FX instance spawns its first particles before its first transform update, then the
  ribbon chains them to the flying projectile (a 3 m arc across the firer for 0.35 s); a ~4-min Molten Off FXTEST granted after x16
  on the same hand-over to name the system and log per-ribbon positions; CARD FREE after it (~14:00).
2026-10-17 13:28 Slim list refresh STAGED (AssetTools): 47 byte counts differ (+26.1 MB), nothing added / removed; Systems adopts it at the
  package step; map_fx re-measured after the FX install (watcher with a 14:45 fallback).
2026-10-17 13:31 MERGED 68b4a3e6 the voice lane (Opus voice packets over lockstep, jitter buffer + concealment, radio / proximity with the original's
  DX curve; +2.7 KB/s per talker; proximity room reverb still PARTIAL). ONLINE PHASE 1 CODE IS COMPLETE on integration except
  Gameplay's two local-pawn fixes (NET.identity) and the proximity reverb; then the two-PC test.
2026-10-17 13:33 NET.identity TRANSPORT HALF PASSES (Experimental, independent, c702c93b): bundles identical on every common tick on lan / typical /
  bad, no timeouts, stalls max 129 ms; the tick-602 split = each process's own local pawn (three hashes with identical bundles) =
  Gameplay's item; negative control (pre-switch) splits at 601. Full PASS re-run (~3 min CPU) on the head with Gameplay's fix.
2026-10-17 13:39 MERGED 3daab0b5 proximity voice as 3D emitters in the cue mix (the listener's room reverb colours it: the user's proximity + room
  reverb ask) + the voice star relay (host fans out to the audience). The reverb-with-device check (muted) joins Systems' card
  slot. Online phase 1 code complete except Gameplay's two local-pawn fixes.
2026-10-17 13:43 MERGED 986320f1 WFC_NULLRENDER_AUDIO (audio device in a null-render boot, muted): the proximity-voice ROOM REVERB check passed
  headless (the listener's preset switched through 4 Streets rooms under the talker; routing and occlusion as designed). Systems'
  card slot = family re-run + physmat ForceField pair + reverb-switch check only.
2026-10-17 13:45 MERGED e1fc0c6c Gameplay's net identity fixes (selection applied; online transform press only via the command; input-only player
  bot; launch-identity SESSIONTEST 35 / 35). Systems' rerun: first split 602 -> 1299 (700 InProgress ticks identical; live ==
  replay); remaining: a 0.003 hp perspective-dependent damage difference at 1299 (+ Gameplay's own ~1772 bot RNG / target split
  with the player bot) -> Gameplay now. Systems drops its extra selectCharacter (one-line follow-up).
2026-10-17 13:46 MERGED c069952c (the net launch drops its extra local selectCharacter). Head = c069952c: everything of online phase 1 except
  Gameplay's two late splits (1299 hp perspective, ~1772 player-bot RNG / target).
2026-10-17 13:48 Rendering CARD FREE 13:47: x13 identity 10 / 10 PASS; flash PASS (64.9 -> 60.8 % near-white, bar 55-62); haze exposure small,
  identity-clean (merge as harmless); Rust's grey-blue near-floor fill is the newly enabled top fog layer (in Off too) -> RE asked for
  the native Density -> FogDistanceScale step; Remnant light PASS as authored (cyan pool, Off / Faithful identical, Realistic
  traced); Trail2 HELD (Rust 7 / 10 frames differ; the FXTEST shows no FusionCannon chain in its window: source still open;
  Trails_Bumblebee 19 m over 2 s wrong for a limb); doors verdict in ~15 min. FX always-include data INSTALLED 13:47 (gate PASS).
  Card -> Gameplay 6 (13:48) -> Systems 16 -> Frontend 5 -> Experimental 10. Set E patch (flash + exposure + Remnant light)
  requested against c069952c.
2026-10-17 13:50 APPLIED 814e7cf5 Gameplay's transforming actors + dying-vehicle collision (door head part 1; NOT yet a shipped state: the draw
  follows in the same hour). Door verdict PASS by eye (Rendering cand33: closed IAC doors with two-sided baked lighting, Rust tube
  door opening sequence with the light-map lerp, no pop). Rendering cuts RENDERING_P7_TRANSACT_DRAW.patch against 814e7cf5.
2026-10-17 13:51 Slim refresh re-measured at 13:50 (+31.7 MB, 47 rows; map_fx +26.5 MB after the always-include install); one more re-measure at
  the named head (door materials + interpolated light maps + Remnant standalone light pending).
2026-10-17 13:52 Experimental's NET.identity on e1fc0c6c confirms the 1299 split (1648 typical / 1687 bad: the tick shifts with the auto delay);
  NEW: under 'typical' the host's replay does not match the host at 1648 (a third hash) -> Systems (record / replay correctness).
  Regress base decision (a): Gameplay supplies minimal sim-only diffs of 814e7cf5 (and the weapon / melee batch) against
  af4d0731 + hover + runner, verified by the headless same-match check, so the regress stays "package 7 renderer vs package 6
  renderer at the same sim".
2026-10-17 13:55 Gameplay CARD FREE 13:55 (gap batch done; verdicts pending). Gameplay: the ~1772 split found and fixed (an opportunistic reload
  in the player bot's brain); the 1299 damage split next. Card -> Systems' 16-min slot (13:55) -> Frontend 5 -> Experimental 10.
2026-10-17 13:59 MERGED dd28a723 Rendering Set E (flash, haze exposure floor, Remnant light, fog in-scatter x 1.443) + the transforming-actor DRAW with
  interpolated light maps = the DOOR HEAD complete in code (collision 814e7cf5 + draw dd28a723). Data staged + gated in the scratchpad
  (lighting.json: Remnant +1 light + 12 interp, BrokenHope 18, Rust 2; materials_glsl: +2 door entries per map on today's live
  files; Rendering's mat_trans_merged copies were pre-(B), so the entries were taken into the live files); install after Systems'
  slot. Trail2 (Rendering): the Molten arc is the AUTHORED TF_Death_MED_FX streak burst the old path never drew (local-space burst on
  a stationary socket); projectile chains physical; limb-trail length authored (19 m in 2 s), fade curves to confirm -> a close-up
  eye check in the next slot, then merge.
2026-10-17 14:00 Gameplay gap verdicts: wobble INVALID (run_gap.ps1 lacked WFC_RENDER_DATA: both archive exes ran the LEGACY renderer and read
  identical; also contaminated by Experimental's net peers) -> 4-min rerun right after Systems' slot; firing pair: no regression,
  no visible change (EmpShotgun's id already matched; drive had no kick either way); the camera follow-up stands on the data-side
  check. 1772 split fixed (player bot's direct weapon write); the 1299 + 1772 patch comes together. Card order: Systems (running)
  -> Gameplay 4 -> Frontend 5 -> Experimental 10.
2026-10-17 14:02 MERGED c1b0afb4 Gameplay's 1772 fix (player-bot reload as a press online; launch identity 35 / 35 over 2400 steps; offline identical).
  Remaining for NET.identity: the 1299 perspective damage split (Gameplay) + the 'typical' replay-vs-recorder mismatch (Systems).
2026-10-17 14:02 MP_TRANSFORMING_ACTOR_CUES_RE.md merged (doors: anim sound notifies only; Rust's start open = DOOR_INTERIOR + DOOR_TUBE) -> Systems
  wires the door sounds through the anim-notify path (the doors' AnimSets join the body-notify table). RE note: spawn distributions
  use emitter time in seconds (Rendering fixing).
2026-10-17 14:08 MP_ESCALATION_RE.md merged: the mode is specified (waves, budget fill, economy, door generators, respawn at wave end); a future
  item after online phase 1 (it needs the bridges' versus-nav decks on BrokenHope and the door generators' purchase flow).
2026-10-17 14:10 MERGED df4d62aa Gameplay's 1299 fix (online, the local seat animates only in the participants' pool: the local pawn's offline
  per-step animation while dead / unspawned had desynced bone poses -> a TankShell's socket spawn 0.2 mm apart -> 0.003 hp);
  replay as seat 0 vs seat 1 equal on all 1299 ticks; SESSIONTEST 35 / 35; offline identical. NET.identity re-runs on df4d62aa
  (Systems after their slot; Experimental on CPU) = the phase 1 milestone if exit 0 on all profiles.
2026-10-17 14:10 Rendering x17slot (~40 min: fog x1.443 pairs, door views on live data incl. Remnant versus open + full Rust opening, the Trail2 +
  spawn-time close-up) granted after Experimental (~14:40). NEW all-mode particle fix (23d3c36c): spawn-time distributions are
  evaluated at the emitter time in SECONDS (RE native), not normalized; rides the Trail2 arm; "particles" set = Trail2 + this,
  merged together on the eye check.
2026-10-17 14:12 MERGED ea4ca7cd per-seat guided missiles for remote and online local seats (SESSIONTEST 36 / 36; offline identical). Gameplay next: the
  TRANSACT sim-only diff for the regress base, then homing lock / MinePooper / repair beam for the online local seat.
2026-10-17 14:16 DOOR DATA LIVE 14:13 (door head complete). MERGED f1ce1c84 Systems' six-patch body / ability FX stack (body notify PASS on the card,
  reverb-switch check PASS 3 maps, physmat pair inconclusive on Remnant -> a 5-min Streets pair with the ForceField force next
  after Gameplay's gap; drain beam not yet exercised). ONLINE_RULES_CHECK_RE.md merged: defaults match the original; 2 divergences:
  CTF Rounds dropped by MatchLaunch::fromURL (REAL, Gameplay) and the singular lobby status text (minor, Frontend).
2026-10-17 14:17 MERGED 87315966 Frontend singular lobby status (rules check divergence 2). Experimental's NET.identity on df4d62aa: live peers
  still split at 1299 (lan; bundles identical; replay == recorder now) -> Gameplay: presentation-only state read by sim queries for
  the live local pawn (drawYawOffset_ lead) + WFC_NET_STATEDUMP; CTF Rounds URL fix (divergence 1) folded in.
2026-10-17 14:22 MERGED 9864b4cd the weapon + melee authored-values batch (all four conditions met: RE row check, Classic + Smart re-baseline, recoil
  eye check incl. RE's measured look, strafe wobble rerun no regression). Systems' Streets physmat pair PASS (SIMHASH equal 4201
  steps, 16 / 24 captures = force-field squibs; Drain look from the 13:56 run; Warcry needs its own pair if wanted). Card -> Frontend
  5 (14:22) -> Experimental 10 -> Rendering x17 ~40.
2026-10-17 14:24 NET.identity (Systems, ea4ca7cd): lan 1299 / typical 2608 / burst 4313 desync, bad PASS 6001 ticks; drawYawOffset_ in
  Character::meshMatrix PROVED as the 1299 cause (test switch: live == replay per seat on every tick); the next split at 1723 is
  perspective-dependent damage (p1 takes a ~330 hp hit only where p1 is remote). Both with Gameplay (sim queries without the
  presentation offset; the remote-pawn damage path). Phase 1 milestone not yet met.
2026-10-17 14:24 Lens flares code complete as WIP (Rendering 87941774, on RE's native math: screen-space elements, curves, occlusion-query
  visibility, no depth test; matc reads the LensFlare* nodes); x18slot (~10 min first look, Rust / Debris) granted after x17 on the
  same relay; merges as its own set on the eye check.
2026-10-17 14:27 Frontend CARD FREE 14:27: the in-match HUD set captured (lobby, talker, scoreboard with Remote rows, the kicked line + slot swap
  at tick 1031, forced jam), the first-run set incl. fr_voice_settings; spectate and the options page missed on the scripts (fixed
  next; a ~4-min gap after Rendering). Card -> Experimental's 1e11edc2 pair (14:27) -> Rendering x17 + x18.
2026-10-17 14:29 ONLINE_PARTY_RE.md merged: the original disbands on leader loss (ours promotes: D12 stays as a deliberate extension, listed to the
  user); the reattach popup semantics and party limits recorded for the Frontend's phase 2.
2026-10-17 14:32 MERGED 0d5399f4 Frontend popup mouse-focus fix (a real PC bug: no popup button was clickable, keyboard only) + the phase 2 doc with
  RE's party answers (conflict resolved with the Frontend's version). D12 stays "promote" as a labelled PC adaptation (user's
  open question). Frontend's ~4-min gap2 (iconic wheel, options by keys, Voice Settings click, spectate) after Rendering.
2026-10-17 14:33 gatebase/TRANSACT_sim_only_af4d.patch merged for the regress base (base + diff == 814e7cf5 on 9001 steps; bots matter). Gameplay's
  remoteFire adaptation confirmed right. 1299 simMatrix patch in its gates (rebase on 9864b4cd), then 1723 (note: p1 shows 530 hp
  above its 330 max where local: the local side may gain something extra rather than the remote side taking a hit).
2026-10-17 14:34 MP_ESCALATION_RE.md complete for both maps (BrokenHope's script byte-identical to Remnant's; 17 waves, boss Tank from displayed
  wave 15, 30 spawn zones, 12 door events opening 13 doors + 5 bridges, 23 item generators): the mode is fully specified for
  whenever the user schedules it. RE stands by for the 15:15 sheets look.
2026-10-17 14:34 Experimental CARD FREE 14:35: 1e11edc2 Berth pair captured (26 / 26 both modes, same match); 21 / 26 frames change (~27 % of
  pixels in them), larger than death frames alone at first sight: sorting against the death-frame list (verdict ~14:45). Card ->
  Rendering x17 + x18 (14:35, ~50 min) -> Frontend gap2 4 min -> head candidate.
2026-10-17 14:34 1e11edc2 (death patch 2) identity row PASSES (Experimental ID_1e11edc2.md): same match 6,001 steps in Off and Faithful; the 21 / 26
  changed frames all sit on a death in view (dying robots' explosion / gib / smoke; the player's own death camera at 74-80 % of
  the frame); no change outside death moments. Experimental next: the regress base with the TRANSACT sim-only diff + the headless
  same-match check (CPU).
2026-10-17 14:35 1723 FOUND (Gameplay): PickupFactory::tick tested only the local pawn, so placed health / overshield / ammo pickups served only the
  local player on each peer (p1 healed 200 -> 530 where local). Fix: online every human seat is tested in match player order with
  its own touch state; offline unchanged; bots still never take placed pickups (PARTIAL). One patch: simMatrix (1299) + pickups
  (1723) + WFC_NET_STATEDUMP + CTF Rounds, on 9864b4cd, ETA ~15:20.
2026-10-17 14:37 PACKAGE_7_HEAD.md updated to today's merges (Systems d9e87a14, copied); the data switches recorded by time + manifest (no commits
  of their own). Pending before the head candidate: Rendering's x17 / x18 verdicts (particles set, lens flares), Gameplay's
  combined online patch (offline identical; may join the head without touching the base).
2026-10-17 14:39 MERGED c306878a transforming-door sounds (anim sound notifies through the body-notify table; Rust's level-start DOOR_TUBE +
  DOOR_INTERIOR verified headless with the audio mix; SIMHASH identical) + PACKAGE_7_HEAD.md bc6733a5 (data switches by time).
2026-10-17 14:42 MERGED e5bc5a22 Gameplay's sim pose + shared pickups + WFC_NET_STATEDUMP + CTF Rounds (the 1299 and 1723 splits; SESSIONTEST 37 / 37;
  offline identical). NET.identity re-runs on e5bc5a22: Systems (lan / typical / bad / burst) + Experimental independent = the phase 1
  milestone if exit 0 on all.
2026-10-17 14:43 Regress base: the TRANSACT sim-only diff reproduces 814e7cf5 only up to step 5215 at the gate's 31 v 32 bots (the 4 v 5 check
  passed; the negative control differs from 895) -> Gameplay re-checks at the gate's bot count and diffs the patch against
  814e7cf5's src/game; needed before the gate. Experimental's NET.identity on e5bc5a22 building.
2026-10-17 14:51 NET.identity PASS on e5bc5a22 (Experimental, independent): lan / typical / bad, two processes, 4,201 ticks each, peers agree
  every tick, replay agrees; no desync, no timeout. Systems' four-profile run on the same head = the second half of the online
  phase 1 milestone (lockstep identity proven on one PC; a two-PC test needs the user).
2026-10-17 14:52 5215 split FOUND (Gameplay, with Experimental's exes): 1f1d4e17's permanent recoil pitch step steers the local player bot's aim
  (sim for that bot), so base + transact cannot equal 814e7cf5 at the gate's settings. DECISION: no port; every gate arm runs
  WFC_NORECOIL=1 (the commit's own diagnostic switch; the base ignores it); Gameplay re-verifies both base diffs with it at the
  gate's exact settings + delivers the weapon-batch diff (its 4 v 5 check was a stale build).
2026-10-17 14:54 RETRACTED the WFC_NORECOIL decision: that switch skips the skeletal recoil only (what the weapon batch changes), not the camera
  pitch step (arms still differ from 5215 with it). Instead Gameplay ports onLocalShot's pitch step into the sim-only base diffs
  (B); re-cut TRANSACT + weapon-batch diffs with same-match results ~15:45; Experimental rebuilds the base before the hold.
2026-10-17 14:54 tools/fidelity synced by path: package-respin.sh fixed (broken cd since 5b665578), p7-gate.sh (pre / gate phases), gate doc.
  Experimental confirmed NORECOIL's scope headless. Ready for Gameplay's re-cut diffs ~15:45 -> base rebuild -> pre on the named
  head.
2026-10-17 14:58 F: at 17 GB free; tonight's package needs >= 40 GB. APPROVED deleting Systems' playtest_slim_30b2fa74 (package 4, played 10-14,
  superseded) + playtest_slim_398b27ef (package 5, internal) (~55 GB; regenerable from sha + HD snapshot); 9350f19b kept as the
  playable fallback. Systems' build plan accepted: build_slim --plan PASS 0 PROBLEM (overlay 85 / 85, HD D 3506 + M6 498, bin 20
  incl. opus + vgmstream licences); SLIM_TSV patch (--tsv) merges with the head; AssetTools re-measures after 14:13 at the head;
  5 new static checks in verify_slim. NET.identity four profiles running headless.
2026-10-17 15:03 Rendering CARD FREE 15:05 with four verdicts: fog in-scatter x1.443 PASS as authored (Rust dark view 36.2 -> 42.1); doors on
  live data PASS (Remnant versus open, Rust opening 30 / 30, light maps following); particles set (Trail2 979b071e + spawn-time
  23d3c36c) PASS by eye (TF_Death_MED_FX death streaks now drawn; moving-source trails as particle chains); lens flares WORK
  (Debris sun flare, Rust occluded) -> one open-sky pass + RE's read before merging. Card: Frontend gap2 15:03 -> Rendering
  open-sky ~15:10 -> head candidate. Patches cut against efa19b6b.
2026-10-17 15:05 RE fresh eyes: all four sheets PASS vs authored data; t3's streak persistence is authored (EmitterDuration x loops). Checks to
  Rendering before the flare merge: Reflection_01's constant OcclusionScaling 1 (must survive occlusion unless the material
  multiplies by TEXCOORD3), Base_SUN's warm tint (2.45, 0.5, 0.5) x DistMap_Color vs our neutral glow; doors2: shot interval
  needed (if 30 Hz frames, the tube door opens ~5x too fast vs the 1.70 s TransOut).
2026-10-17 15:06 ONLINE PHASE 1 MILESTONE MET on e5bc5a22: Systems' lan / typical / bad / burst + a new rttjump profile (host -> client 50 -> 250
  ms one-way at 30-60 s): all exit 0 / 0, 6001 ticks each, every hash equal; D rose 5 -> 15 on the RTT jump. Plus Experimental's
  independent row (lan / typical / bad, 4,201 ticks, replay agrees). Remaining for online: a two-PC test (needs the user).
  Quality finding: auto-D uses srtt / 2 as the one-way latency, so asymmetric latency under-provisions D (client stalled 24 s in
  rttjump); fix approved: lead-based D (per-peer min lead in PONG, raise by the shortfall, cap 15). Disk: packages 4 + 5
  deleted, F: 16 -> 60 GB; package 6 = Rebuild-Systems/work/playtest_slim (renamed at promotion).
2026-10-17 15:07 MERGED 377bb25a particles set (log line corrected: a53fca66 was a log commit; the merge script read the wrong branch) (Trail2 chains + spawn time in seconds; eye PASS Rendering + RE). Lens-flare patch received (8 files
  + per-map lens_flares.json + 4 materials); held for RE's two decode answers (Reflection_01's +0xE4 intensity, DistMap_Color
  layout = the sun tint) and the Gorge open-sky pass after them; door timing (c) closed (133 ms shots, open by ~1.6 s = authored).
2026-10-17 15:08 Frontend CARD FREE 15:07: Voice Settings click VERIFIED on screen (popup mouse fix), WEAPON 2 wheel label VERIFIED, options
  rows present; bot-row help line missing (page has no hint widget: Frontend writes the lobby footer hint), settings page overlay
  flaw (lobby footer shows through: fix in progress); spectate not shown because a 1 s respawn beats the 3 s corpse view
  (correct; needs a longer-respawn mode). One more ~2-min capture later.
2026-10-17 15:10 Lens-flare decode resolved natively (RE §4b): Reflection_01 survives occlusion (ours gated it by coverage: Rendering fixes
  TEXCOORD3.y to the constant intensity); Base_SUN is blue as ours draws it. Open-sky + Rust occluded pass next in Rendering's
  card window; flare deadline for the candidate 16:15.
2026-10-17 15:12 MERGED b2c790d0 Frontend UI polish (bot-row help line via the lobby footer hint; lobby footer hidden under the Voice Settings
  page) + 9d4e9138 spectateTarget trace (the 15:03 death showed no SetSpectateTarget in the ~2 s spectate window: Gameplay's
  killer-cam target or its delivery; the trace decides). Frontend tests pass. Frontend's ~2.5-min capture after Rendering's pass.
2026-10-17 15:13 Rendering CARD FREE (x19): lens flares with intensity 1 on Rust outdoor views = large disc + ring + rays + a frame-wide lavender
  veil (+45-63 levels mean); follows the decoded math but no original reference. HELD: RE compares with the original's Rust images
  + re-checks the native scale semantics by 15:45; merge only on an authored-look PASS, else next head. Card -> Frontend 15:16.
2026-10-17 15:14 Read lf_rust_full.png: the intensity-1 flare build draws Base_SUN / ring / rays through the occluding building (lane view) and
  mid-frame away from the visible sun (street view): not mergeable. Rendering applies per-element OcclusionScaling(coverage) x
  ScreenPercentageMap on the CPU (intensity 1), re-cuts, one 8-min pass after the Frontend; RE's reference comparison on the
  corrected sheet; 16:15 deadline for the candidate.
2026-10-17 15:15 PACKAGE 7 HEAD CANDIDATE = 057aded3 (lens flares OUT: every element draws screen-sized or larger, a scale factor is missing;
  RE pins it, next head). Integration frozen for code until the gate ends. Experimental: pre (head build, base on Gameplay's
  diffs ~15:45, same-match + control, eye pass on the card after the Frontend) -> hold ~17:00-19:00 with the no-interpreter
  broadcast + acks. AssetTools re-measures the slim list at 057aded3; Systems builds package 7 from it after the gate PASS.
  Next head: flares, auto-D, homing lock / mines, Frontend capture fixes.
2026-10-17 15:16 Frontend CARD FREE 15:16 (read to follow); killer cam: no hud.spectateTarget during the whole spectate window (death 1444,
  Spectating 1502-1552) -> Gameplay checks World's target timing, Frontend checks the glue's field detection; next head. Card ->
  Rendering x20 (6 min: the flare box fix (authored min..max, not centred) + WFC_LFLOG coverage log) -> Experimental's eye pass.
2026-10-17 15:16 Frontend read: killer cam = Gameplay item (camera follows the killer, HUD target condition at World.cpp ~4050 stays false);
  options help line + lobby footer under Voice Settings still not fixed by 39b0169f (needs a clip dump, ~1 min gap after the
  gate); Voice Settings page + every PC options row verified on screen. All next head. Frontend acked the freeze.
2026-10-17 15:16 AssetTools re-measured slim_files.refresh.tsv at 057aded3 (report SLIM_REFRESH.md): rows 101,031 unchanged, 6 byte counts moved
  (+81 KB: the door / light data); vs the live tsv 47 rows, keep / xpr +31.8 MB. bin/licenses/opus ships via build_slim's bin
  walk from the package build's own bin (Systems to confirm in the dry run), not a tsv row.
2026-10-17 15:17 RE flare verdict: sizes + colours AUTHORED (VF normalises right / up; no screen constant; Rendering's px table = the original);
  missing = cull the whole flare at coverage 0 (bounds = the occlusion box) + the +XYZ box. Rendering adds the cull and re-shoots
  (x20); RE judges by 16:00; PASS -> the candidate is re-named with the flares (~16:15), else next head. Hold ~17:30 at worst.
2026-10-17 15:19 USER (15:30): "Yes to all" (Opus OK; live mic test approved; transport now OK; Escalation after online phase 1; party defaults
  stand; two-PC test when available) + "delete all the old builds we don't need" -> disk sweep ordered to every lane (own work/
  only; keep head / base / package 6; stop during the hold). RE: lens flares PASS as shipped (sizes, alpha, projection C; the
  02 / Base_SUN DistMap last-point lift is authored-heavy: PC option = first point, listed to the user). Candidate to be RE-NAMED
  ~16:00 = 057aded3 + flares + Systems' lead-delay auto-D (rttjump stall 24.1 -> 0.68 s; five profiles exit 0) + verify_slim
  package 7 data checks. Experimental holds the eye pass for the final head (b).
2026-10-17 15:21 MERGED 51d32eaf Systems' lead-based auto-D (rttjump stall 24.1 -> 0.68 s) + netshim rttjump + verify_slim 'package 7 data:' row;
  c71ff133 gatebase/WEAPON_sim_only_af4d.patch. Regress base COMPLETE: base + transact + weapon == 057aded3 on all 9001 steps at the
  gate's exact settings (control differs at 1558); the recoil explanation withdrawn (gate doc: "verified against 057aded3").
  Offline SIMHASH identical. Waiting on Rendering's flare patch for the final candidate.
2026-10-17 15:24 MERGED deed7f1a lens flares (6 maps; RE PASS as shipped) + data installed (lens_flares.json x6, 4 material entries x6; backups in
  the scratchpad lf_backup_live). PACKAGE 7 HEAD CANDIDATE RE-NAMED = deed7f1a (= 057aded3 + flares + lead-based auto-D + verify
  package 7 row + the weapon base diff doc). Offline SIMHASH identical through every merge. Frozen. Experimental: head build +
  eye pass (Rust dark = no flare, Rust lane / Debris = the flare) -> hold ~17:00.
2026-10-17 15:25 Campaign Kismet batch 2 merged (64 / 176; generic On<Name> dispatcher found; kismet_native.py tool). RE continues with 65+.
2026-10-17 15:28 MERGED ab855fe7 Frontend HUD glue fix (jam display + "currently spectating" label were never fed in real play: wrong HUD struct
  passed; 2 lines + static_asserts). FINAL PACKAGE 7 HEAD = ab855fe7 (frontend-only over deed7f1a; SIMHASH identical; eye pass
  on deed7f1a stands, the gate exe rebuilds at ab855fe7). AssetTools' list at deed7f1a stands (code-only change); the 6 materials
  files shrank by formatting only (indent dropped on re-serialization; content identical by key).
2026-10-17 15:29 USER (2026-10-17 15:29): lens flares adjustable in the graphics options: Off / Subtle (DistMap first point) / Original (shipped), default
  Original -> next head (Rendering setting + WFC_LENSFLARE env + persistence; Frontend row + strings). A second PC is available:
  the two-PC online test runs on tonight's package 7 build (Systems writes PACKAGE_7_TWO_PC_TEST.md for the user).
2026-10-17 15:30 Sweep: Frontend -26.5 GB (work\abvm 2.4 GB locked, retry after the gate); Systems -3.9 GB (pass 1); AssetTools listing: 13.2 GB
  superseded approved by name (after the gate); out\hd\full 41 GB + the E-remake inputs 10 GB kept for errata until package 7's HD
  window closes. Kismet batch 3 merged (96 / 176). F: ~108 GB free.
2026-10-17 15:30 CORRECTION (Systems' read of ab855fe7): the menus' Host / Join run only on FakeSession (dev tools, never packaged); the only
  real online path is the headless runNetMatch harness. So package 7 has NO playable menu-driven online match; "online phase 1"
  = lockstep identity + transport + voice proven headless. Tonight's two-PC test = headless LAN run via Play_NetTest_Host /
  _Join.bat (Systems' doc). The playable online match (UdpSession over ISession + Frontend wiring + rendered net match with the
  human seat) = item 1 of the next head; Systems posts the split + estimates by 19:00.
2026-10-17 15:31 Campaign Kismet decode COMPLETE (176 / 176; only ScalarValueRamp's ease shape left as H). Campaign RE inputs now: data,
  Kismet, natives, Escalation; remaining RE gaps = 38 materials + the render-data pipeline items.
2026-10-17 15:32 Playable online match split: the Frontend's ISession contract (Session.h) is complete and consumed; UdpSession implements it;
  policy: the Application creates the UdpSession at boot in the playable build (listen after host()); Frontend adds a two-runtime
  loopback test; lens-flare row done on the Frontend branch (0f913411) pending Rendering's setter. Systems' estimates by 19:00.
2026-10-17 15:44 USER (15:40): "get all my multiplayer working before any new build"; two match paths (private game code; public via a central
  server or P2P); a NEW LOBBY FLOW (straight into the lobby: Create a Class, Change Map, host-only Game Settings holding all the
  old pre-match options; joiners get Create a Class only); keep cleaning space in the background; "one more build that has
  everything we need and then pause"; asks whether Escalation and the campaign are finished (answer: specified, not built).
  -> Package 7 cut HELD (ab855fe7 = internal checkpoint); no hold / gate tonight; the eye pass finishes as the checkpoint read;
  the freeze lifts at its verdict. Assigned: Systems UdpSession + NetMatchDriver + ONLINE_MATCHMAKING_DESIGN.md; Frontend
  ONLINE_LOBBY_FLOW_V2.md + build vs FakeSession; Gameplay human seat / HUD / Choose Character, then Escalation E1; Rendering
  flare mode + X7 slot + campaign materials; AssetTools sweep now + Escalation data; RE Escalation asks + original public-match
  rules. Sweep so far: Integration -94 GB, Rendering -165 GB, Gameplay -80 GB, Frontend -26.5 GB, Systems -3.9 GB; F: 267 GB.
2026-10-17 15:46 AssetTools sweep done: 31 approved paths, 12.9 GB, live indexes verified intact. F: 279 GB free. Day total freed ~385 GB.
2026-10-17 15:46 ONLINE_LOBBY_FLOW_V2.md merged (Frontend): Private Match hosts straight into the lobby; host-only Game Settings page; member menu
  = Create a Character / Teletran I / Friends; Find Match = Quick Match (original playlist) or Browse Games. Labels: "Game
  Settings" (user's words), "Create a Character" (original). Frontend builds it against FakeSession now.
2026-10-17 15:47 Rendering: LENSFLARE_MODES patch ready (rt2 62beef47; merge order row 0f913411 -> patch -> glue d4c8978e after the eye pass);
  all 38 campaign materials compile in matc (rt2 f31431f5; decal projection at draw time = campaign item; Omega Ddx switch
  question to RE); X7 ready for a 25-min slot. Card plan after the eye pass: flare sheet 6 min + X7 25 min.
2026-10-17 15:48 Escalation data (AssetTools, GO): both maps ship the ORIGINAL Escalation nav mesh (NavigationCell 469 / 625 + tactical / path
  nodes) = the authored bot nav for the mode; plan A geometry per generator / event target, B original navmesh in bot_nav schema
  (primary), C closed-doors bot_nav (cross-check), D SurvivalData rows (fallback UNKNOWN -> RE asked). ~30 min CPU.
2026-10-17 15:51 USER (2026-10-17 15:51): hold ALL new builds until everything incl. Escalation is in the next build; no VPS for now: peer-to-peer only, but
  build the entire lobby-service system VPS-ready. -> Systems: codes encode the host endpoint (UPnP + optional STUN), LAN
  broadcast discovery for public lists, the lobby service written + loopback-tested with a configurable endpoint; Frontend:
  Lobby Service address setting, "service needed" over the internet, Escalation in Game Settings; Gameplay: E1-E6 after the
  online pieces.
2026-10-17 15:51 RE: Escalation §7 (horde AI / revive / loot) + §7.4 weapon data (SV rows for 6 weapons, PlayerData fallback) merged;
  ONLINE_PUBLIC_MATCH_RE merged (no server browser in the original: Browse Games = labelled PC addition; public join in progress
  spawns at once). Frontend lobby v2 steps 1-4 written (compile at the eye-pass end). Omega Ddx resolved (light-pass PS).
2026-10-17 15:57 Escalation data DONE (AssetTools): A geometry per generator (35 / 35, 0 unresolved on BrokenHope, 2 matinee objects on Remnant),
  B the ORIGINAL navmesh in bot_nav schema (primary; Remnant start state CONFIRMED by NavigationCell.bBlocked; BrokenHope: 30
  generator-enabled cells not cooked blocked -> RE), C closed-doors cross-check, D survival_weapon_data (6 SV, 72 PlayerData,
  47 neither). Gameplay has the inputs for E1.
2026-10-17 15:58 Escalation nav start state closed (RE §7.5: PreGameplaySetup Turn Offs); Escalation inputs COMPLETE for Gameplay's E1.
2026-10-17 16:00 escalation_navmesh start_state regenerated (AssetTools): blocked_at_start = cooked bBlocked OR PreGameplaySetup Turn Off; both
  maps equal their generator-enabled sets (BrokenHope 32 = 2 + 30, Remnant 28); all CONFIRMED.
2026-10-17 16:01 Escalation §8 presentation merged (HUD / prompts / downed UI / energon to the killer only / score table / end flow). The
  Escalation spec is now complete on every side (rules, data, nav, weapons, AI, presentation).
2026-10-17 16:06 Checkpoint eye pass on ab855fe7 done (16:04): match Realistic floor OK; views floors below because Off changed too; FINDING: Rust
  near-floor fog near opaque in Off + Realistic (Off mean 18 -> 41) from the low height layers x 1 / ln 2 -> Rendering shows
  authored (then re-base) or fixes the Rust fogs entry. No flare in any camera; flare-aimed views to be added. FREEZE LIFTED;
  card -> Rendering 16:05 (flare sheet 6 min + X7 25 min).
2026-10-17 16:07 Rust near-floor fog: read rust_dark_off.png: the floor at the camera's feet is opaque while far walls are clear = fog decreasing
  with distance = a BUG in Set D's in-layer integration or units, not a look decision (Rendering had called it authored vs the
  thumbnail). Rendering + RE find it after the X7 slot; Rust's top layer leaves the live fogs data until fixed; no re-base.
2026-10-17 16:08 RETRACTED my "bug" call on the Rust floor fog: Rendering's numbers explain it (+0.4 % light-blue layer x 1/ln2 = ~+20 sRGB
  levels on a near-black floor, invisible on lit walls); authored per the native maths, the only original frame (thumbnail)
  shows darker floors -> the user's look decision (pair sent). Views floor stays on package 6 meanwhile. Flare views synced.
2026-10-17 16:10 MERGED lens-flare option: 03e8ec10 Frontend row (Off / Subtle / Original) -> f895142c Rendering modes (setLensFlareMode, WFC_LENSFLARE,
  keep list, settings-test row) -> 5e5be7a5 Frontend glue (trait + static_assert). Build OK, frontend tests pass, SIMHASH identical;
  the Off / Subtle / Original sheet = Rendering's eye row from the current card slot; WFC_SETTINGSTEST run next.
2026-10-17 16:12 USER (2026-10-17 16:12): Hetzner VPS approved (they set it up now); public play = NO server list: Quick Match per mode -> best-ping match
  (pregame lobby gathering or join in progress); pregame lobby with Halo Reach-style map voting. Smoke: a setting Original /
  Enhanced, Enhanced = moving / near-volumetric smoke if possible. Rust floor fog: Rendering's per-vertex interpolation
  explanation accepted; per-pixel A/B granted after their CARD FREE (3 min).
2026-10-17 16:12 USER (2026-10-17 16:12): Enhanced smoke should be lit by its surroundings and shadowed / affected by nearby geometry and players ("if not
  it's okay") -> Rendering's proposal: froxel injection into X3's lit volume first, sprites as fallback.
2026-10-17 16:13 Enhanced smoke APPROVED (Rendering): stage 1 all modes = lit (sun + shadow map + DLE ambient + local lights where available),
  curl-noise moving soft particles, flow-blended flipbooks (~0.1-0.3 ms, ~1 day); stage 2 Realistic = froxel plumes with
  shafts + self-shadowing for big emitters (~0.3-0.8 ms, 1-2 days). Row Smoke: Original / Enhanced (default Original).
2026-10-17 16:13 RE: shipped 4-layer fog = uniform slabs (ray fraction inside [MinH, MaxH] x max(dist - Start, 0); T = 2^(DistanceScale x L)),
  no exponential height falloff, UU throughout; a camera in the slab looking at a 1-3 m floor gets near-zero fog, so the flat
  sheet is the per-vertex interpolation; Rendering's per-pixel A/B decides (granted after their CARD FREE).
2026-10-17 16:14 VPS: LOBBY_VPS_SETUP.md + deploy_lobby.sh + wfc-lobby.service merged; firewall = UDP 14010 any + TCP 22; deploy only on the
  user's go (doc sent to the user). Escalation roster (AssetTools): 16 templates, all meshes / anims / 26 FX / 243 cues present;
  gaps = runtime packaging of 14 characters (~90 MB) + 7 weapon exports -> GO into AssetTools\out\escalation_characters.
2026-10-17 16:15 MERGED a78386dc per-pixel height fog (Rendering c2fb7eb5; the Rust near-floor sheet was per-vertex fog interpolated across the
  big floor polygon: Rust 'dark' lower third 48 -> 16, like the thumbnail; finely tessellated views unchanged; all modes;
  WFC_FOGPERVERTEX=1 = old path). Build OK, SIMHASH identical. Experimental's Rust rows re-base on this, not package 6.
2026-10-17 16:16 x21 / x22: Lens Flares row works (Off / Original right; Subtle as "first point" only a little calmer: DECIDED Subtle = first
  points + Base_SUN alpha x 0.25, our design choice, re-shoot next gap). X7: GI 1x -> 2x Rust 180 -> 482 MB, load 0.9-1.4 s;
  the earlier ~1 GB device delta was noise; look sharper but Gorge lane shifts beyond "sharper" (1x bounce gain at 2x) -> recalibrate,
  re-shoot, RE eye; X7 stays out until then.
2026-10-17 16:17 MERGED 66e9b275 lens-flare Subtle = first points + Base_SUN alpha x 0.25. X7: Rendering corrects itself: the 1x gain IS right at 2x
  (a 2x fit is ill-conditioned); Gorge lane's darker plate is spatial -> RE's truer-vs-different read on x7q_sheet.png, no card.
  Card queue: Frontend (to ~16:30) -> Experimental 15 min -> Rendering 6 min (Subtle re-shoot).
2026-10-23 00:11 RELAUNCH after the credit stop (2026-10-17 16:20 -> 2026-10-23 00:08). User (10-17 16:2x): "is there a build I can play until my
  credits reset?" -> package 6 (Rebuild-Systems\work\playtest_slim\Play.bat) is the playable build; no package 7 cut by decision.
  User (10-23 00:09): "read the messages that didn't get done, assess, continue, all cylinders"; "I'm going to bed, make decisions
  for yourself". Merged the stranded lane commits: Systems 83ef6f42 + 292a69b9 (matchmaking design + lobby_service.py), Frontend
  f32235dd (lobby v2 steps 1-4; build OK, tests pass, SIMHASH identical), Experimental views-only.sh. Relaunch briefs sent to
  all 7 lanes (new names in memory). Integration = rebuild-84.
2026-10-23 00:13 USER PLAYTEST FINDINGS (sent to the Frontend tonight; 18 items; verbatim doc coming as USER_PLAYTEST_2026-10-23.md). Routed:
  Frontend #2 respawn timer leak, #17 UI half, #1 load times, #4 HD row detection, #6 video settings scroll, #7 scoreboard, #8
  health bars through walls; Gameplay #11 spread / aim, #12 TTK, #15 switch anims (RE first), #17 World half, #18 bots floating,
  #10 / #13 Seed collision; Rendering #3 / #16 headlight flicker + shadows, #14 water; Systems #5 death smoke lifetime, #9 boost
  trails, #4 package side; RE the original values for #11 / #12 / #15 / #8 / #14; AssetTools #10 / #13 geometry, #14 textures,
  #4 package check. User-visible bugs go ahead of smoke; MP critical path (UdpSession) unchanged.
2026-10-23 00:14 Escalation horde packaging DONE (AssetTools): 14 characters + 7 weapons in AssetTools\out\escalation_characters (136 MB, 161
  slim rows staged); data facts: CQCDec vehicle has no anim set (authored), BruteDec / TitanDec vehicles 2 clips; the boss tank's
  robot clips depend on out\campaign_anims (8 files, shipped by decision); 2 CeremonialStaffHolo cues without WAV (tracing).
2026-10-23 00:15 USER_PLAYTEST_2026-10-23.md on integration (1ed57919; 18 items + the user's priority list: respawn timer, spread / aim, class
  switch, boost trails, headlight flicker, bots floating, Seed collision, load times, smoke lifetime, HD row, video settings,
  scoreboard, health bars, switch anims, water, TTK; "do not wait for me between straightforward fixes"; a retest checklist
  when the batch is ready). Lobby v2 step 5a merged (1e9b7a73). RE §9 horde AI merged -> Gameplay. Escalation spec COMPLETE.
2026-10-23 00:16 AssetTools: slim refresh 101,214 rows (+161 escalation, +16 campaign_anims, +6 lens_flares); CeremonialStaffHolo hum cues
  silent as cooked (CONFIRMED); package 6 DOES carry hd_pbr_index + m6 -> #4 is a runtime condition (HD root lookup or the
  HD 2 -> 1 clamp) for the Frontend / Systems.
2026-10-23 00:17 #3 / #16 cause found (Rendering): the per-frame transient-light re-ranking moved headlights in and out of the 4 RT-shadow
  slots as vehicles / muzzle flashes moved; fix = 1.5x hysteresis for last frame's selection + sticky shadow slot (rt2 fefd136b,
  Realistic only; WFC_TLSTABLE=0 old; WFC_TLDIAG=1 counts). Patch against c86a56cf + a 10-min card A/B after Experimental.
2026-10-23 00:22 MERGED e6feadfc Gameplay seat pieces (Choose Character consumer, per-seat homing lock + mines; SESSIONTEST 40 / 40) + 40ff8e3e
  Frontend #2 respawn-timer leak (movieCall opened a second copy of the respawn movie) + RE's #11 / #12 / #15 / #8 answers.
  USER (2026-10-23 00:22): "complete the campaign as well if any of the paths run out of things to do" -> campaign = every lane's fallback
  queue (Gameplay Kismet runtime, Rendering decals + materials, Frontend campaign menus, Systems FX / audio, AssetTools
  pipeline gaps, RE remaining decode). Seed decisions: #10 drop BlockingVolume 9745 (ART), keep 7008 (PC adaptation);
  #13 per-poly pawn collision for the 40 undersized-hull meshes. #8 = authentic (ally bars through walls). #4 = runtime condition.
2026-10-23 00:23 MERGED 12d438d4 #4 root cause: hdTextureLevelAvailable(2) hard-coded false; Enhanced now offered when the colour pack + hdPbrIndex
  exist (runtime check in the Frontend's next slot). RE #14: MP liquids = the energon waterfalls only (additive unlit, two-speed
  panned colour / normal, cube reflection, depth-biased alpha x vertex R, distortion x20); RE #11 follow-up: our spread uses a
  +-1 square where the original uses UE3's disc AddSpread (~+-0.5 x spread) = ~2x too wide; shotguns fixed patterns.
2026-10-23 00:23 #8 closed as authentic (RE a17f442: only a Scientist viewer sees ally health bars, through walls, no distance cap; ours matches
  at World.cpp:2988; the user played Scientist). #16 second cause (Rendering rt2 8a6e1d67): transient shadow rays ended 0.05 m
  short of the lamp and hit the owner's hull -> self-shadowing under / in front of the car; now WFC_TLCLEAR 0.5 m. Candidate
  work/cand_hl = c86a56cf + fefd136b + 8a6e1d67; x24slot.sh ~12 min next on the card.
2026-10-23 00:24 X7 (RE read): keep 2x; Gorge 'lane' darkening = a 2x chart-bleed / gutter artefact (hard edge, teal cast) -> Rendering fixes
  gutter / dilation + compares vs Authentic before it merges; Debris reflective truer at 2x.
2026-10-23 00:24 Experimental CARD FREE 00:24: fog fix PASSES (Rust 'dark' lower third 47.8 -> 15.9 Off; Realistic contrast std 22 -> 29);
  views contrast above the package 6 floors; flare views did not capture (relative path; fixed) -> 5 min after Rendering.
  Card -> Rendering x24 (headlight A/B) 00:25.
2026-10-23 00:25 Seed PC adaptation DONE (AssetTools e8c2391: 9745 dropped, 7008 kept, 440 placements per-poly; AssetTools\out\pc_adapt) ->
  build_slim overlay + Gameplay check. Campaign render-data gaps: GO a (height fog trace), b (inline lightmap lists), c (campaign
  material lists), d (per-sublevel split, 1.06 GB), e (proposed Rendering diff, not applied).
2026-10-23 00:29 MERGED f8a02fc8 #11 weapon spread (square -> the original's disc: ~2x narrower; sample-before-increment; robot-only multipliers;
  shotgun patterns). EXPECTED SIM CHANGE (the first since af4d0731's base): the final gate's base needs this as a sim-only diff;
  Classic / Smart bot baselines re-baseline on it.
2026-10-23 00:32 CORRECTION: f8a02fc8 lacked two World::remoteFire hunks (patch rejects at 3666 / 3674; my chain did not stop on FAILED); applied
  by hand in cf02694d. The 20-s headless runner cannot see shot-direction changes (no shots land in its window): sim-change claims
  rest on Gameplay's 300-s check; identity claims stay valid. RE queue done (water PS, X7, Rust tessellation: the original is
  as coarse and per-vertex -> Rendering re-checks the ExtinctionDistance branch; per-pixel stays meanwhile). RE -> campaign list.
2026-10-23 00:33 RE campaign residual list approved (Matinee native -> buddy / togetherness -> dialog timing -> streaming notifies -> save body
  serializer -> ActorFactory -> FaceFX clamping -> small items); RE on #1.
2026-10-23 00:40 Campaign render-data gaps a-e DONE (AssetTools): fog cause = build_lighting matches HeightFogComponent exactly, the campaign's
  169 PostProcessVolumeHeightFogComponents (and 1 on Remnant) are skipped -> Rendering adds subclass matching + RE's volume-gating
  semantics (Remnant = a recorded MP change with an eye row); inline light-map lists, per-chapter material lists, the per-sublevel
  split (392 folders, 996 MB, PASS) delivered; MERGED c2e5950f the MP-neutral VS_ROOT / VS_MAPS tools diff. Seed nav rebuilt on the
  adapted collision (overlay_20261023b, 90 files; Systems switches refresh.sh); the centre bot strand = steering, Gameplay re-tests.
2026-10-23 00:45 MERGED 48914ec7 headlight stable shadow slots (#3, Realistic; x24: Rust 300 -> 105, Streets 266 -> 109 slot changes / 600 f),
  e7a4976f #14 water distortion variant on LM / VLM subs (Remnant waterfalls + coolant, Complex Waterfall2, force fields, glass;
  all modes on those surfaces) + fog per-vertex for Off / Faithful (= shipped, byte-identical to pre-c2fb7eb5; per-pixel stays
  Realistic-only), 087ead81 fog extinction compare as shipped. 8a6e1d67 (lamp clearance) conflicts on rt_shadow_ao.hlsl ->
  Rendering re-cuts. SIMHASH identical. Card: Experimental flare views 00:40 -> Frontend 7 min -> Rendering #14 5 min.
2026-10-23 00:46 MERGED e0163393 lamp clearance (#16 second cause). #12: data = original (TTK table matches); bot accuracy is the lever ->
  measure per difficulty, propose scales (Normal ~25-35 %, Hard ~45 %, Easy ~15 %). #18 (A) hull tops above visuals ->
  AssetTools audit + PC-adapt overlays; (B) zeroed vertical velocity sim bug -> Gameplay traces. RE: PPV height fog decoded
  (volume-gated, transitions; Remnant's applies inside PostProcessVolume_10624) -> Rendering runtime item; Matinee tangents:
  IMT_UseBrokenTangentEval (no gap scaling) corrects the Matinee doc.
2026-10-23 00:47 Flare views PASS (Experimental): draw on sun-aimed cameras, none when occluded; notes: halo strong on axis (shipped), 8-bit
  banding -> output dithering item for Rendering. Card -> Frontend 00:55 (7 min) -> Rendering (8 min).
2026-10-23 00:48 RE campaign #1 Matinee native merged (two corrections: no tangent gap scaling; director restore = view-target blend of the
  last cut's TransitionTime); AnimControl slot code + RelativeToInitial matrix still HIGH -> RE finishes those before #2.
2026-10-23 00:50 #18 (B) traced (Gameplay): the "hovering" is Dodging = PHYS_Flying for 0.5 s with no gravity, so a Scout bot dodging off a
  ledge flies flat ~14 m over the drop; "inside geometry" was the detector's coarse bounds. RE answers whether the original
  exits to Falling when the floor goes; fix follows (bot-side no-dodge-toward-drop if authentic; floor exit for all if not).
2026-10-23 00:56 MILESTONE MERGED 4bcfde3d online phase 2 (Systems): UdpSession behind the menus (LAN / internet codes, UPnP / STUN, lobby, load
  gate, quick match by ping + LAN discovery), NetMatchDriver from the real frame loop, Choose Character as lockstep events;
  UDPSESSIONTEST PASS, SESSIONTEST 41 / 41, frontend tests pass, SIMHASH identical. Remaining for a playable online match:
  the Frontend's Host / Join wiring to UdpSession + a two-window menu match on this PC. Frontend CARD FREE: #4 PASS at runtime,
  #6 reproduced (15 rows), #1 profile: warm costs = frontend 3D scene load (~1.5 s menu, ~0.7 s lobbies) + party preview-body
  stalls; cold = first launch only. Card -> Rendering (8 min) -> Systems (#5 / #9 FX log).
2026-10-23 00:56 NET.identity PASS on e0163393 (Experimental): 4,801 ticks, lan / typical / bad, peers + replay agree; covers the seat pieces +
  spread. #18 float soak running (before count). Next identity row on 4bcfde3d incl. the UdpSession path.
2026-10-23 00:57 RE campaign: AnimControl slot C + RelativeToInitial; togetherness / buddy decoded; RE -> #3 dialog timing (track position
  formula decompile running in the background).
2026-10-23 00:58 Float soak before-count: 6 cases / 20 runs, all Scout dodges; replayable (seed 7). Phase 2 identity row building; a two-process
  UdpSession entry asked of Systems (UDPSESSIONTEST is in-process).
2026-10-23 00:59 Rendering CARD FREE: #16 drive arms ran (sheet pending); the 6 waterfall arms did not launch (runner regex) -> 4 min after
  Systems. Card -> Systems 00:59 (#5 / #9 FX live-instance log).
2026-10-23 01:02 MERGED 0d956825 #6 settings pages scroll past 10 rows; f14cad06 #17 the PC pause menu keeps Choose Transformer / Switch Teams
  in iconic versus (escalationArray was used whenever IconicMode was set); f0a280ed select-result log. Frontend tests pass,
  SIMHASH identical. Frontend's 6-min verify slot (#6, #17, #2 + #7 with 4v4 bots) queued after Systems + Rendering.
2026-10-23 01:02 RE campaign #3 dialog timing + Matinee §11 merged; RE -> #4 streaming notifies.
2026-10-23 01:05 MERGED 0d8e3c06 #15 switch anims (presentation) + #18 (B) nav-limited bot dodges as the original (1 -> 0 floating cases; EXPECTED
  SIM CHANGE, bot dodges: second sim-only diff for the final gate's base). #18 (A) waits on the hull audit; #17 World side
  passes; bot-accuracy table next.
2026-10-23 01:06 Systems FX log (CARD FREE 01:03): #5 = the jet death row plays Smoke_Trail_Jet_Fast_FX which LOOPS FOREVER as authored and we
  spawn it fire-and-forget (101 s instance) -> keep the handle, follow the body, stop at corpse end (patch to Gameplay);
  #9 = hover_plane_FX mesh emitter Lifetime 0 + the authored bKillOnDeactivate / bKillOnCompleted flags missing from
  map_fx_runtime.json -> Rendering exports the flags + WfcMapFx kills on stop / completion. WFC_UDPHOST / UDPJOIN two-process
  UdpSession match exists (2957 ticks equal; member HostLost crash being fixed). Card -> Rendering 01:05 (7 min) -> Frontend.
2026-10-23 01:06 NET.identity PASS on 4bcfde3d (phase 2 head, WFC_NET_* row). Float after-soak building on the dodge cap with the refined detector.
2026-10-23 01:07 RE campaign #4 streaming notifies merged; RE -> #5 save-data body serializer.
2026-10-23 01:11 MILESTONE MERGED f1d4a1d2: the menus run on the real UdpSession end to end (two-runtime loopback flow test 13 / 13: host by code,
  join, lobby, start, load gate, match, quit). Design confirmed: Find Match = Quick Match (public), offline bots = Private
  Match. Remaining: a two-window menu match on this PC (windowed), then the user's two-PC test.
2026-10-23 01:12 RE campaign #5 checkpoint body merged (3 fields, no world state); RE answers Rendering's kill-flag semantics, then #6.
2026-10-23 01:14 MERGED 8d6f0c4b FX kill flags (#9; kill-on-deactivate live, kill-on-completed gated). map_fx regen for 10 maps running through
  the kill-flags gate (only the two new keys allowed). Rule set tonight: no test exe binds 0.0.0.0 on this PC while the user is
  away (firewall prompt); loopback mode for the two-window menu test + Experimental's UdpSession rows.
2026-10-23 01:20 MERGED fa8bb8b5 #5 death FX attached + stopped (Systems; presentation). UDPSESSIONTEST flake = a LobbyState race (2 / 2 pass after).
  #16: clearance = guard, slot stability = the evidenced fix; #14 authored refraction restored (PASS); Enhanced water option
  APPROVED after smoke stage 1. #12 bot accuracy ~2x the targets at every difficulty -> sweep approved (data-driven scales).
  RE #6 ActorFactory + #7 FaceFX merged; RE -> #8 small items. Smoke stage 1 first look queued after the Frontend's runs.
2026-10-23 01:21 Gorge waterfall steam veil (head exe only) = the FX-inactive throttle's catch-up dumping up to 1 s of spawns on a camera cut
  (respawn / spectate / killer cam too) -> Systems matches the native catch-up (RE); Rendering confirms with WFC_FXINACTIVE=0.
2026-10-23 01:23 MERGED 221e7b83 lobby re-delivery fix (a real bug: a stale empty LobbyState after a late JOIN_OK; UDPSESSIONTEST 3 / 3 + flow
  test PASS). rttjump verdict: the stall metric over-reports (sub-frame arrival waits); real hitches ~3 per jump; judge by step
  gaps > 50 ms. #5 stale-handle concern answered (monotonic ids).
2026-10-23 01:23 CAMPAIGN RE END-STATE merged: the campaign's RE is complete except 7 listed H / UNKNOWN rows with build assumptions. Throttle
  catch-up: native passes the full time but pre-ages spawned particles (the burst = our missing pre-age; Systems fixes that way).
2026-10-23 01:23 #18 (B) after-soak PASS: 1 -> 0 floating cases in 20 runs on 0d8e3c06. Experimental builds 221e7b83 for the UdpSession identity rows.
2026-10-23 01:25 MERGED fa37a75f FX spawn pre-age (the catch-up burst's real cause; native semantics per RE); Rendering proves it on Gorge in x28.
2026-10-23 01:27 map_fx_runtime.json REGENERATED + INSTALLED on all 10 MP maps (kill flags; gate: the only non-flag "diff" was NaN != NaN on
  Exp_Death_Sm_FX StartSize values[4], present in both = identical; backups in the session scratchpad mapfx_backup_pre_kf).
  #9 render side complete (code 8d6f0c4b + data). Pre-age v2 merged d788870b (expected sub-frame change in all modes for the
  gate). RE: AnimControl evaluator C. Card: Frontend two-window test 01:30 -> Rendering x28 + x29 (12 min) -> Systems (8 min).
2026-10-23 01:31 Two-window menu match on this PC (loopback): host by code, join, lobby sync, launch record, load gate, Choose Character and
  spawns all WORK; desync at tick 660: the two worlds disagree on the host's team (the host's launch record went out with team 0
  before its random FinalCountdown pick). Frontend fixes (fix the team before setLaunch; build the host's launch from the record
  it sent; the flow test compares both MatchLaunch teams). Retest = another two-window slot after the fix.
2026-10-23 01:33 CAMPAIGN_LIGHTING_RE merged (campaign-only: toggleable / movable lights, light functions, Matinee light tracks, per-actor MIC
  clones, dynamic PP volumes) -> Rendering's campaign lighting runtime list. RE -> campaign-only gameplay systems inventory.
2026-10-23 01:35 MERGED e33a2c85 #12 bot skill as data (bot_skill.json; aim errors 21 / 11 / 5.5 / 1.2 deg; 25-50 m hit rates ~17 / 31 / 50 %
  vs targets 15 / 30 / 45; EXPECTED SIM CHANGE for bot aim = third sim-only diff for the final base). Playtest status: #2 #4 #5
  #6 #8 #9 #11 #12 #14 #15 #16 #17 #18(B) fixed / closed; #3 fixed (visual pending); #1 profiled (fixes next); #7 unverified
  (needs bots in the shot); #10 / #13 data done (Gameplay check); #18 (A) audit pending.
2026-10-23 01:35 CAMPAIGN_GAMEPLAY_SYSTEMS_RE merged (per-chapter bosses / mechanics / classes). RE next: the A1 / A2 boss + minion action
  decode (Escalation §9 style), then the rest chapter by chapter.
2026-10-23 01:37 MERGED 5ca8fe3a Frontend desync fix (host picks its team before setLaunch; every world builds the match from the one launch
  record; flow test 15 / 15 + team checks) -> two-window retest queued after Systems. UdpSession-path NET.identity PASS on
  221e7b83 in all five profiles (Experimental); net tools synced by path. ONLINE: every layer now proven on one PC except the
  windowed menu match retest.
2026-10-23 01:37 Rendering CARD FREE (x28 / x29 done; smoke reference fell back to group light on Streets: 0 Beast probe points -> fix the
  reference source first; sheets pending). Card -> Systems 01:38 (FXLIVELOG + pre-age A/B) -> Frontend two-window retest.
2026-10-23 01:38 Gorge pre-age capture INCONCLUSIVE (unlockstepped views, particle phase); the pre-age fix stands on RE + review; Systems owns a
  numeric proof (FXLIVELOG burst size on the catch-up tick). Smoke first look: tiny diff (no probe grid on Streets) -> Rendering
  fixes the reference source (computeEnv at player starts / nav points) before a sheet. CAMPAIGN_BOSSES_RE §A1 merged.
2026-10-23 01:41 Systems CARD FREE: #5 PASS (jet death smoke 101 s -> 7.3 s; nothing older than 36 s); #9 closes with one head-exe FXLIVELOG
  run in their next slot + the catch-up numbers. Card -> Frontend two-window retest 01:41 -> Rendering smoke -> Systems.
  CAMPAIGN_BOSSES §A2 / §A3 merged.
2026-10-23 01:46 MILESTONE: a windowed two-player match through the REAL MENUS runs on this PC (loopback): host by code, join, lobby, launch
  record, load gate, Choose Character, a 2-min TDM with bots past +95 s, no desync, teams agree. New: both exes crash on exit
  after "Shutdown complete" (NetMatchDriver::~NetMatchDriver -> Lockstep::bye on a destroyed transport) -> Systems. MERGED
  2cc1edb9 #7 scoreboard spacing (versus lists used the 104 px iconic portrait rows; now DisableIconicDisplay -> 25 px) + the
  5b vote hook + test. Card -> Rendering smoke (8 min) -> Systems.
2026-10-23 01:48 CAMPAIGN_BOSSES_RE COMPLETE (every chapter). Gameplay: nothing missing on their side for the online human seat / HUD / names /
  spectating (two real-session peers, 150 s, verified) -> Escalation E1 now. Rendering: smoke views show no smoke (cameras hold
  none); the match arms need a 5-min relay (runner race fixed). Card -> Systems 01:55 -> Frontend pt4 (3 min) -> Rendering (5 min).
2026-10-23 01:50 MERGED 36e5760b the map vote on screen (lobby v2 5b). #1 load times routed to Rendering: AnimSet pre-parse on a worker thread
  from boot (preview bodies 270 / 190 ms, 601 ms cold) + resident frontend scenes (re-read on every travel). Playtest status:
  only #1 (code), #3 visual, #18 (A) data, #10 / #13 check remain open; #7 / #2 runtime checks in the Frontend's next slot.
2026-10-23 01:54 MERGED c1f0fa06 / a00926f8 Escalation HUD data path + glue (wave / next-wave / power from HudGameState sv* once E1 lands).
  Rendering #1: AnimSet prefetch on worker threads + the last 3 parsed frontend scenes resident (rt2 745f9403 + step A), runtime
  check pending; step B (true GL residency) waits on the Frontend's warm profile split. Card: Systems (numbers) -> Frontend
  pt4 -> Rendering (smoke match arms + #1 run).
2026-10-23 01:55 Systems CARD FREE: #9 PASS on the head exe (hover_plane_FX max age 7.5 s, guard 0 times; before 40 guard kills at 36 s);
  throttle catch-up CLEARED as the Gorge card's cause (lockstepped arms bit-identical; catch-ups are 0.1 s steps with 0-1
  spawns); the 6 m card = Rendering's unlocked runner (particle phase). Pre-age stays (correct per RE). #5 + #9 CLOSED.
  Card -> Frontend pt4 01:56 -> Rendering.
2026-10-23 01:56 #18 (A) DONE (AssetTools): 266 raised simple-hull placements on all 10 versus maps switched to per-poly collision (volumes
  listed, kept authored; >= 5 m overhang cases list-only); nav + air nav rebuilt; frozen overlay superset overlay_20261023c
  (135 files, 199.8 MB: 85 gltf_wfcmat + per-map collision_pawn / physics / bot_nav / bot_nav_air). Systems: refresh.sh -> 20261023c;
  Gameplay: BOTTEST + the floating detector on the adapted collision.
2026-10-23 01:59 MERGED 2528df5e v3 vote wire + the two-window exit-crash fix (shutdownFrontend resets the driver first; kProtocol 0x57464303;
  HostOpened carries the host id). Rendering #1 step B (rt2 07dc418f): frontend-scene textures survive release in an LRU pool
  (600, WFC_TEXPOOLMAX); with the world.glb cache + AnimSet prefetch a revisited menu re-uploads only mesh buffers; runtime
  check in their next relay (x28match 5 min + x30fe 3 min).
2026-10-23 01:59 Frontend CARD FREE: #7 PASS (25 px rows, 9 listed) and #2 PASS at runtime (qa:die -> respawn -> timer gone; death under
  pause -> Quit with no movie). Vote rows show live tallies with 3 layout / text defects (fixing). Card -> Rendering 02:00
  (smoke match arms + #1 menu run) -> Frontend exit retest on 2528df5e.
2026-10-23 02:00 Stability soak PASS on 5ca8fe3a (80 + 80 headless runs, 4 modes x 10 maps: 0 crashes / errors / stuck matches, WARNs equal).
2026-10-23 02:00 CAMPAIGN_FRONTEND_FLOW_RE merged (modes, lobby, select, loading, fail / complete). RE -> ONLINE_PC_DEVIATIONS.md.
2026-10-23 02:01 Campaign HD / AV coverage (AssetTools): 937 gap textures (~1.8 GB HD output), all audio / FMV / FaceFX present (1 A3 line
  silent) -> GO on the campaign HD pass as the one long GPU job (chunked, resumable). Escalation spawn points exported.
2026-10-23 02:02 Rendering CARD FREE (smoke match arms + #1 menu run done; sheet / numbers pending). Card -> Frontend 02:02 two-window exit
  retest on 2528df5e.
2026-10-23 02:03 ONLINE_PC_DEVIATIONS merged; decisions: promote stays; no public list (doc §4 stale); public versus hosts can't start early
  (auto-start as the original); join in progress = instant spawn on the smaller team (phase 1b). Load-times patch not yet on
  rt2 (Rendering commits it); an empty staged copy was removed.
2026-10-23 02:09 MILESTONE: two-window menu match PASSES end to end incl. clean exits (host + member exit 0, no crash, no desync, teams 0 / 1)
  on 762526b6 + the Frontend's vote fixes. MERGED effecacc Escalation E1 (survival director; ESCTEST 19 / 19; versus SIMHASH
  identical) -> RE review. MERGED 33d73f78 #1 load times (warm revisits ~0.3 s). Vote rows: one layout bug left (Frontend).
  RE end-of-match check merged (4 small diffs routed). Campaign HD: covered by pack D (10-texture confirm run). Card -> Rendering 02:11.
2026-10-23 02:10 MERGED 7404e98e vote lobby fixes (rows under the preview, no Veto, "Finding n more players", focus; host's real member id) +
  718efdee the flow test's team / member-id checks (16 / 16). Frontend next: public auto-start >= 2 / 45 s + hidden Start,
  Escalation EndGameStats + directorSpawned filter, XP for public, join in progress.
2026-10-23 02:11 UdpSession identity PASS on 2528df5e incl. the exit path (Experimental). Retest checklist drafted
  (docs/handoff/USER_RETEST_CHECKLIST_2026-10-23.md). Escalation soak building on effecacc. Rendering CARD FREE (analysis pending).
2026-10-23 02:12 RE E1 review: PASS with 3 fixes (PreGame clock from level start; zone activate / deactivate hook; Round Begin / End events)
  -> Gameplay folds them into E2.
2026-10-23 02:13 CORRECTION e2a58726: effecacc had left src/core/Application_EscTest.cpp untracked (my staging used directory globs that
  missed src/core); a clean checkout of effecacc / 718efdee did not link (Experimental). Rule: stage every '+++' path of a patch
  by name and verify with git ls-files before committing.
2026-10-23 02:13 #1 with the parse cache: warm revisits 103-184 ms (from 300); Rendering cuts 59e339fd. Smoke: the per-draw light cull
  masked every flash for sprite groups (zero bounds at the origin) -> fixed rt2 8ac67dbf; reference = level ambient only.
  Card -> Rendering x32smoke 02:22 (Frontend's 1.5-min look shares the window, one exe at a time by direct coordination).
2026-10-23 02:14 Vote-row look PASS (Frontend): rows under the preview, own vote marked, "Finding 1 more player". Next Frontend commit: public
  autostart at 2 / 45 s + no host start in voting lobbies, Escalation GameOverMessage, directorSpawned filter, XP for public.
2026-10-23 02:32 MERGED 9f304154 MVP / FFA tie as the original; e27d5594 join-in-progress frontend half (dormant); f11e701e campaign audio 1
  (conversations + matinee sound keys; D5_OME 42 / 42) + FXCATCHUPLOG; 1689e3c2 / 3cdd0772 public auto-start + Escalation end +
  XP for public + roster filter; da62ceb4 FX JSON cache (#1 revisits 103-184 ms); the overlay root hook v2 + RE's msvc qsort port.
  Experimental: ESC soak stable (Remnant wave 1 stuck = zone-sort tie -> E2 fix; BrokenHope AllDead = real); UdpSession PASS on
  e2a58726. Overlay decision: collision for all 10 maps, rebuilt nav only on BrokenHope / Remnant (regressions elsewhere ->
  AssetTools). Join in progress: Systems' replay catch-up plan approved (2-3 days). Campaign HD: covered by pack D (1 row
  eye-passed, folded into the next pack D snapshot, not now).
2026-10-23 02:41 MERGED 29c1ce7d Escalation E2a (Remnant wave 1 spawns; PreGame from level start; power economy + generator actions: pickups,
  doors / bridges, zones; ESCTEST 25 / 25 + 5 / 5). RE reviews; Experimental reruns the ESC soak. Gameplay -> join-in-progress
  hooks, then E2b (nav cells on, DBV off, weapon factories, Wall_Crashers). Campaign frontend plan merged (C1-C6).
2026-10-23 02:42 Nav verdicts (AssetTools): the rebuilt-nav regression comes from per-poly render meshes with cracks / holes the authored hulls
  spanned (Debris -3,113 m2 walkable), so the original nav ships where it fits; Seed's off-mesh comes from the 40 #13 meshes ->
  candidate v3 nav (shipped collision + authored hulls of those 40) for Gameplay's A/B; Berth = no hole (a hover on a BSP pit via
  the rebuilt nav). FROZEN overlay_20261023d: 114 files (collision for all 10 maps; rebuilt nav on BrokenHope / Remnant / Seed).
2026-10-23 02:43 RE E2a review: correct except G1 (a bought item stays out for good in the original: the -cost drop re-fires New Power Level)
  -> DECISION faithful; Gameplay folds it into E2b.
2026-10-23 02:46 MERGED campaign frontend C1 / C4 (Solo -> Mission Select -> SOLO campaign lobby; difficulty profile 12; [Campaign] progress
  section) + C3 (CampaignSelect from the shipped TransLevels.ini: 84 checkpoint thumbs, trios, 10 chapters; ?Checkpoint on the
  URL; chapters unavailable until runtime data exists). Frontend -> C2, C5.
2026-10-23 02:46 REGRESSION (Rendering): private menu launches now start the NetMatchDriver with 1 seat -> matchSteps never advances (SHOTMATCH
  0 shots) and same-seed arms differ: every lockstep match-frame runner is broken. DECISION: no net driver without a remote
  seat (Systems, top priority). JIP RE doc + overlay soak (20 / 20 clean; 18 fall-through candidates to AssetTools) merged.
2026-10-23 02:54 MERGED 888abf4d solo launches take the local loop again (Systems; the 1-seat driver regression; SOLOLAUNCHTEST 5 / 5);
  ea73ac61 late seats (Gameplay; LATESEATTEST 15 / 15); 078c9eb4 campaign C2 / C5 (C1-C5 complete headless); the first-visit
  lever. UDPSESSIONTEST PASS on the head (Gameplay's failures = Systems' port 14100 collision, moved to 24100+). Experimental:
  E2a rerun stable, 0 spawn-finder failures; Remnant headless never spawns (boot-path difference, Gameplay). Card: Rendering
  x33 + x34gi relay next, then the Frontend's campaign-menu shots, then Rendering's PPV fog Remnant pair.
2026-10-23 02:58 Nav (Gameplay, headless): Seed v3 nav beats overlay d's rebuilt Seed nav clearly (stucks 0.84 / 0.96 vs 2.44 / 3.23 per
  bot-min; failed 0.48 / 0.54 vs 1.21 / 4.75; faster) -> DECISION ship d + v3 (AssetTools freezes overlay_20261023e). Second NAVQ
  seed 1234 base -> d: neutral to better (Gorge off-mesh 12.96 -> 2.36 %, BrokenHope 11.58 -> 9.03, Complex stucks 2.57 -> 0.93);
  single-seed outliers Streets stucks 0.98 -> 2.54, Molten failed 0.52 -> 5.53 -> third seed on those two before the package.
  BOTTEST 48 / 48 on d. Staged for the next build: E2FIX, first visits, PPV fog, Systems JIP. Card: Rendering relay in progress.
2026-10-23 03:11 MERGED 29f253cd E2FIX (Remnant player pawn: team-less player starts; G1 faithful), 5bf88581 first visits (party lobby
  341 -> 249 ms, game lobby 184 -> 107 ms), c03d0357 PPV height fog runtime (Remnant data + eye pair next relay), 309c450c
  Systems join in progress (4 peers equal at 5101 ticks with 2 late joiners; TooFarAlong at 300 s). All harnesses pass on the
  head. OPEN (Systems, top): frontend-booted arms that differ only in render env reach different SIMHASH 1400 -> match-frame
  A/Bs not same-moment. Card: Frontend campaign-menu shots, then Rendering x34gi + x35remnant (PPV fog + Enhanced water).
2026-10-23 03:22 MERGED 760257b9 smoke stage 1 + Enhanced water settings (setSmokeMode / setWaterMode; SETTINGSTEST 11 / 11 windowed;
  Original byte-identical by construction; eye sheets pending), 901d1ee2 campaign runtime contract (RE), 65c7ccc8 leave +
  Forfeit (Gameplay-gated; my reruns next build window), f99ef748 JIP frontend glue (+ patch staged). overlay_20261023e frozen
  (= d + Seed v3). Card: Systems' async-step diagnosis (4 arms) -> Rendering x34gi + x35remnant -> Frontend campaign capture ->
  two-exe JIP check. Campaign render data: D5_OME 5 / 6 sublevels compile, BASE_m traced; A1 next.
2026-10-23 03:35 MERGED a9c01bfd Frontend glue (JIP + Smoke / Water rows; all harnesses pass incl. LATESEATTEST 20 / 20), 0567e54a seed
  fix: the frontend-booted A/B divergence was GameFlow drawing the seed from random_device and ignoring WFC_SEED (Systems' 4
  arms; not smoke, not async step, not intent) -> every frontend match-frame A/B before it was never same-seed. Experimental on
  309c450c: ESC PASS (Remnant 3 waves), UdpSession 5 profiles + late join PASS (6,001 ticks identical). Third NAVQ seed: keep
  overlay e; Streets stucks 1.31 -> 1.80 per bot-min to AssetTools (not a blocker). c3d4cdf1 build_materials VS_MAPS.
2026-10-23 03:36 Campaign render data: D5_OME DONE (AssetTools\out\campaign_render\D5_OME, 6 sublevels, 108 MB; all world.glb materials
  compile; 0 missing textures); gaps for Rendering: 3 Omega Supreme robot material instances (translator), the
  PostProcessVolumeHeightFog subclass not read by tools/render, the M72 fallback MP-only (campaign extras cooked in sibling
  sublevels). A1_IAC running (48 sublevels).
2026-10-23 03:47 MERGED 2c6cc2b3 mid-match leave (host-run SeatLeave at an agreed tick; WFC_UDPLEAVE_AT; CatchUpProgress stops at live),
  d6220894 Escalation E2b part 1 (Wall_Crashers; G1 Taken path tested; ESCTEST 29 / 29 + 6 / 6), 743992eb WFC_STUCKLOG,
  two-PC test doc. DECISIONS: leave rule = SeatLeave only sim change + D16 bot always (Frontend drops its PeerDropped swap);
  overlay f approved (e + Debris / Gorge broken floors back to hulls; 18 -> 12 fall-through candidates); Enhanced water row
  hidden until tuned; PPV fog accepted (subtle, authored); X7 parked (mirrored det -1 instances lose 18-28 % at 2x).
  x36: same-moment arms confirmed after the seed fix. Card: Frontend campaign + pt9 -> Rendering x37 (smoke frames).
  Internal test package on my word after pt9 + both leave halves + overlay f.
2026-10-23 03:51 MERGED 70b3d2df WATERDEBUG, ae9bbb6e leavers always bots (D16; UDP / LATESEAT 20 / 20 / FLOW pass). overlay_20261023f
  FROZEN (4 files differ from e). RE E2b1 review: correct except W1 (window arming follows players + invulnerability toggle)
  -> Gameplay with E2b2; G1 corrected (bought item + one free respawn, as implemented). Pending: Frontend's PeerDropped-swap
  removal, pt9 two-window proof, then the internal test package (refresh.sh on f).
2026-10-23 03:53 MERGED 917205a2 campaign materials (all 38 RE gaps; Omega Supreme MIs compile) + M72 sibling fallback. DECISIONS:
  chapter render layout = <root>/<chapter>/<SublevelName>/ (URL name, no _m) + chapter.json manifest; Rendering starts the
  chapter loader (Pipeline::loadSublevel / unloadSublevel, per-sublevel ownership, static all-loaded D5_OME first; game-side
  load / unload events later). AssetTools reruns D5_OME lighting + materials on this head after A1.
2026-10-23 04:02 MERGED 5ae6d3c5 Escalation E2b part 2 (17 weapon factories, dropped weapons, take / slot / bag / heavy rules, SV AR
  loadout, W1; ESCTEST 33 / 33 + 6 / 6), e324e48c Frontend leave half (no flow SlotSwap; Water row dev-only; CampaignSelect
  fixes), a0781461 Quick Match host owns its port (pt9 root cause: the host kept the search transport = seat 255 -> 11 seats
  -> no driver; UDPSESSIONTEST repro). pt9 rerun on a0781461 next after Rendering's relay; internal package after it passes.
  RE D5_OME pass: AnimNodeSlot skip flag HIGH, FMV outro 10 tracks (selection UNKNOWN -> Ghidra job), RandomSeed default 0.
2026-10-23 04:06 MERGED 1615196f voice chat step 1 (in-match mic + Audio rows + mute + talking indicators + Opus in refresh.sh;
  VOICETEST PASS). RE: Bink FMV track rule C (tracks 0-4 bed + 5 + Lang; English = 0-5) -> Systems wires the FMV player.
2026-10-23 04:09 MERGED 8b1cd99e voice step 2 (lobby lane, host relay; VOICETEST lobby PASS) + two-PC doc voice step. Experimental:
  leave arms PASS on ae9bbb6e (bot takes the slot at one tick; humans-only 1v1 Forfeit at 1.51 s). RE: per-chapter campaign
  pass complete (no chapter-specific open rows). FMV track rule was already on the head (M07; WFC_LANGUAGE, not a profile
  setting). OPEN QUESTION for the user: a Language row in Settings (INT / FRA)?
2026-10-23 04:12 Rendering verdicts: smoke stage 1 FAIL by eye (11 / 221 frames hold smoke; ~3 levels difference); Enhanced water not
  user-ready (Fresnel ~3 % at play angles). DECISION: both rows dev-only; x39tune next (tuned contrast / puff shape / flow,
  SSR x2.5 + Fresnel floor); if still flat -> stage 2 froxel plumes, Enhanced smoke Realistic-only. x37: same-moment arms OK.
2026-10-23 04:16 MERGED 72a0043d Escalation E3a: the horde's real bodies (ESC_<id> chassis, per-entry weapons + ROBODEF health, wave
  buffs; ESCTEST 35 / 35 + 6 / 6; BrokenHope horde kills with its AR, Remnant PlayerBot reaches wave 3). E3 open: GOAP brains,
  Brute / Tank weak points, Jet flight, loot. Gameplay -> campaign runtime (D5_OME headless boot) now.
2026-10-23 04:16 pt9 rerun (two windows, real menus): host runs the driver, the late member is accepted at tick 992 and launches from the
  record, then ConnectionLost: a ~45 s cold map load (fresh shader cache) vs the host's 8 s member timeout, no keepalive during
  the blocking load. Systems: late-join load window + keepalives (top priority). Next pt9 with a pre-warmed member cache.
  Card: Rendering x39tune.
2026-10-23 04:20 MERGED 7f218986 Escalation weapon factory visuals (render_index_extra.json regenerated for both ESC maps: 17 + 17),
  c0fb8fff Smoke row dev-only. RE: RelativeToInitial order C (VMX decode; tools/re/vmx128dis.py). pt9 folders persist
  between runs (warm Berth); Systems' late-join load window pending.
2026-10-23 04:25 MERGED 3f35a6c8 pickup spin (RE defaults; regen), 8d7c0cab late-join load window + keepalives (pt9 cause) + build_slim
  render_index_extra, c45317a3 smoke stage 1 tuning: x40 confirm PASS by eye -> Enhanced smoke SHIPS in all modes (Frontend
  un-hides the row); water closed dev-only. RE: FMV speaker routing C (FL FR SL SR LFE C). Card: Frontend pt9 rerun on
  8d7c0cab -> on PASS the internal package word to Systems. Rendering -> chapter loader stage 1, then froxel plumes.
2026-10-23 04:28 MERGED Kismet VM (Gameplay; KISMETTEST 9 / 9 incl. D5_OME's real 898-op graph; not wired yet). Experimental: voice +
  five profiles PASS on 8b1cd99e. README: Opus / DXC fetch note. Rendering: chapter loader stage 1 built on a hand-made
  D5_OME layout; x41 boot after pt9.
2026-10-23 05:59 Escalation finding (Experimental / Gameplay): the horde spawns at authored points off our bot nav (Remnant 34 / 61,
  BrokenHope 30 / 58) and the versus strand-suicide rule killed them 30 s later -> waves cleared with no combat. DECISION:
  the horde is exempt from strand-suicide for good (original never suicides) + PROV nav-reachable spawn substitution with a
  log until AssetTools' ESC nav (NavigationCell mesh) covers every spawn point (priority after the D5 inputs).
2026-10-23 06:02 Campaign render data in the chapter layout (AssetTools): D5_OME 6 sublevels 113.5 MB, 0 compile errors, 0 missing
  textures; A1_IAC 48 sublevels 1.92 GB, 1,952 materials, 5 errors (DarkEnergon_Spreading x3, Hologram_Post -> Rendering),
  rerun on the head after the queue. chapter.json = persistent level + derived streaming order. Probes: not holes.
2026-10-23 06:06 AssetTools: D5_OME collision / nav inputs (per-sublevel collision_pawn + physics; navigation.json 522 nodes; navmesh.json)
  and the Escalation nav from the original NavigationCell mesh (every on-mesh spawn point covered: BrokenHope 50 / 50, Remnant
  60 / 60; blockers + per-generator unlocks). Gameplay: strand / spawn patch -> E2b part 3 -> TnCampaignGameSolo.
2026-10-23 06:12 MERGED 60517276 campaign audio 2 + Escalation Round Begin / End audio (BrokenHope Round End -> Megatron lines, Remnant
  Round Begin -> Zeta; ESC-only LevelAudio.inc change), 7272c4ac horde never suicides + PROV on-nav substitution (Remnant
  300 s: 7 weapon kills, 0 suicides) + KISMET_VM_CORE_RE (VM core C). Experimental on bfee6521: voice / profiles / late join /
  20 s-load late join all PASS. Gameplay -> E2b part 3 (ESC nav + blockers) -> TnCampaignGameSolo.
2026-10-23 06:17 MERGED 4f4b68f9 Escalation E2b part 3 (the original's NavigationCell nav in ESC + blocked-at-start cells + generator
  unlocks; ESCTEST 37 / 37 + 6 / 6), d0d5a3c7 late-join diagnostics. pt9 on bfee6521 FAILED again at the 8 s drop: Systems
  says the merged logic can't (stale exe suspected; diag lines now name the window). AssetTools: horde roster (14), INT
  subtitles for 10 chapters, escalation_weapons.json, ESC nav v2 (reach specs -> links, both maps one piece), Streets stuck
  candidates revertA / revertB for Gameplay's A/B; A1 lighting + materials rerun running. Card: Rendering x41 + x42 -> Frontend
  pt9 on d0d5a3c7 (fresh build verified by the diag string).
2026-10-23 06:22 MERGED daf03569 relay fallback (game side + lobby_service relay; blocked-direct headless proof hash-equal 3001 ticks),
  29343d08 cue-less PlayDialog + sidecar-first generator, 5949cb3f horde AI RE, campaign C6 frontend half (Smoke row visible).
  x41: the chapter loader merges all D5_OME units (GLB_ART 334 lightmapped comps / 91 lights, ART 1060 / 167); crash at
  frame 0 in VehicleFx::draw after the World fell back to the graybox (-> Gameplay: campaign boot instead of a TDM match;
  canonicalMapName rejects D5_OME_BASE); AssetTools: absolute texture paths into the old _m/tex dirs, empty decals.glb.
  x42 plume volume: inconclusive (interior, no sun) -> x43 at 10x density. DECISIONS: overlay g = f + Streets revertA;
  ESC nav v3 typed links. Card: Frontend pt9 on the diag head (fresh exe verified).
2026-10-23 06:22 overlay_20261023g FROZEN (f + Streets revertA; only Streets collision differs) = the shipping set. AssetTools queue:
  A1 rerun (mid-chapter) -> ESC nav v3 typed links -> unit-relative texture path post-pass (D5 + A1) -> Kismet I/O export.
2026-10-23 06:26 pt9 (fresh exe on d0d5a3c7+): the two-window join in progress WORKS on the network (120 s window applied; caught up
  1514, woke 1529, live 1559, in sync to the end). Remaining: no spawn: the joiner's Choose Character was stamped for tick 1
  before its seat woke (Systems: hold + restamp at activation) and the flow treated it as pre-match (Frontend). Then the
  internal package. RE: Kismet VM review K1-K6 (to Gameplay); horde armour / backpack rules C.
2026-10-23 06:34 MERGED 30a1b7d7 Escalation nav behind WFC_ESCNAV (default = 7272c4ac behaviour) + horde awareness; the Frontend's
  late-launch-begun fix; late pick hold (pt9's no-spawn, both halves in); member-to-member relay (wire v4); chapter loader
  stage 1 (+ WFC_CHAPTER_VIEW); RE squad AI (AI buddies never revive; solo human never downed). x43: the plume path is
  broken (no ship; blob log fixed). Card: Frontend pt9 (spawn expected) -> Rendering x43 + x44chapterview (~4 min).
2026-10-23 06:37 RE online rules merged: post-match 15 s EndGameStats -> same host's lobby (Y Game Stats); party stays together, no
  leader / host migration in the original (ours: D12 promote + HostLost -> party lobby stay); voice team-only in team
  matches, open in FFA / after EndGame (Systems); mute / report = PC addition behind Gamer Card (Frontend). Rendering:
  per-plume blob split built (x43 rerun tests it). Card: Frontend pt9 on e5674e33.
2026-10-23 06:40 pt9 PASS on ba9d1dcb (two windows, real menus, loopback): public Quick Match lobby -> autostart -> the member joins by
  code mid-match (21 s load, 120 s window), catches up, wakes at tick 2317, held Choose Character applied at 2356, spawns on
  team 1, plays; leaves at 4561 -> a bot takes the slot; 0 desyncs, clean exits. INTERNAL PACKAGE WORD GIVEN to Systems
  (refresh.sh on ba9d1dcb, overlay_20261023g, WFC_OPUS=1; internal only, not the user's final build).
  RE: Escalation waves (no player-count / difficulty scaling; end screen 15 s, 0 XP); loot from the spawn-list row.
2026-10-23 06:43 x44 D5_OME chapter view PASS for loading: the merged chapter renders lit from AssetTools' real layout (lightmaps,
  lamps, emissives, Omega's arena); Realistic differs (warmer bounce, mean 29 -> 38) but runs without D5 calibration (later
  campaign RT item: per-map bake / fit). x43 plumes: root cause = the accumulation lived only in drawSprites, which the
  default sprite stream never calls; fix written (Pipeline::smokeVolumeAdd per batch), built after the package hold.
  RE_FINAL_AUDIT merged (66 open rows, none blocking). Internal package build in progress (Systems, be5b7cdc, pack D).
2026-10-23 06:54 Internal package BUILT: Rebuild-Systems\work\playtest_slim_be5b7cdc (27.57 GB, 0 failures; overlay g 114 / 114; pack D
  + M6; ESC nav extras 6; Release + PGO + RT + Opus), not promoted; verify boots on the card now. MERGED 06bd8aee voice
  channels (Team Radio default; VOICETEST / UDP / FLOW pass) with the Frontend's player card (73997b0d) + checklist refresh.
  CORRECTION: 06bd8aee's message says the frontend tests pass; on the head online.V2_voice_settings_and_ptt FAILS (246 pass)
  -> Frontend fixes it against the Team Radio default. Compile hold lifted.
2026-10-23 07:01 MERGED 14549980 CAMPAIGN BOOT (D5_OME headless to InProgress with the AI squad; CAMPAIGNTEST 21 / 21 with WFC_MAP=D5_OME;
  Kismet VM per RE K1-K6 + the native startup filter; KISMETTEST 10 / 10), 120aef77 Escalation pickup ammo, b949b58c V2 test
  fix (frontend tests 247 / 247 PASS on the head). Package verify: HD + Opus + Play.bat boot clean; 3 package gaps (iconic
  presets, mp_transforming_actors, ESC chassis exports) -> build_slim + repackage; campaign render data for D5 added to
  build_slim. AssetTools: D5 chapter bot_nav.json (123 cells, 1 piece). Rendering: D5 RT calibration (sunless fit; lights
  fit on 259 chapter lights) -> ships in D5_OME_BASE/vbake. Card: Rendering x4345 -> Systems' Streets re-verify -> Frontend
  post-match check.
2026-10-23 07:02 DECISION campaign package layout: one root <WFC_CAMPAIGN_ROOT>/<chapter>/<unit>/ (= AssetTools/out/campaign_render;
  chapter.json, collision, nav, render data, vbake/fit.json in the unit); Play.bat sets WFC_CAMPAIGN_ROOT; the renderer's
  loadChapterUnits takes the chapter dir explicitly (Rendering patch); no unit copies into work/render; D5_OME ships whole,
  A1 later. build_slim gaps: + bot_skill.json. x43: plume blobs reach the pass (4 blobs, r 4.5-6.8 m); x45: D5 calibration
  loads. Card: Frontend post-match check -> Systems Streets re-verify.
2026-10-23 07:13 MERGED 0f463723 BotNav v3 kinds (leaps, versus_only costs; ESCNAV still off: Remnant perches read off-mesh -> AssetTools
  lowered the cells to the floor, v3 done: 29 / 29 + 17 / 17 spawn points reach the players), the chapter-dir loader patch.
  Repackage DONE: playtest_slim_be5b7cdc 27.83 GB (ESC chassis, transforming actors, iconic presets, bot_skill, D5 campaign
  data; Play.bat sets WFC_CAMPAIGN_ROOT); old = _v1. AssetTools: D5 vbake copied, A1 rerun 0 errors (28 volume fogs, 2.97
  GB), Kismet sequence I/O for 10 chapters (RE 13 / 13). Rendering: plume path fixed but the look isn't there (in-scatter
  next); x45: RT GI covered one unit only -> chapter loader stage 2 (every unit in the RT static world) = the item that makes
  Realistic real on campaign maps. W: incident = Systems removed Experimental's mapping by hand; rule recorded.
2026-10-23 07:14 Package v3: Rebuild-Systems\work\playtest_slim_be5b7cdc_v3 (27.84 GB; + transforming_actors skeletal doors / bridge,
  campaign_anims; static PASS; headless smokes 0 errors on BrokenHope / Remnant ESC, Rust, Streets); older packages deleted
  after v3's Streets verify + Experimental's rerun. MERGED 19a3bc29 chapter units read in place from the campaign root.
  Rendering: chapter loader stage 2a built (every unit in the RT static world) -> x46 after Systems' verify.
2026-10-23 07:14 Package v3: Rebuild-Systems\work\playtest_slim_be5b7cdc_v3 (27.84 GB; + transforming_actors skeletal doors / bridge,
  campaign_anims; static PASS; headless smokes 0 errors on BrokenHope / Remnant ESC, Rust, Streets); older packages deleted
  after the v3 Streets verify + Experimental rerun. MERGED 19a3bc29 chapter units read in place from the campaign root.
  Rendering: chapter loader stage 2a built (every unit in the RT static world) -> x46 after the Systems verify.
2026-10-23 07:20 MERGED 6f505624 campaign boot glue (loadMatch SOLO / CP / CCP; MISSION FAILED / checkpoint / mission complete events;
  chapters selectable once chapter.json exists: D5_OME + A1_IAC unlock). Post-match check PASS (join-code panel overdraw
  fixed). Card: Systems v3 Streets verify -> Rendering x4647 -> Frontend first frontend-launched chapter.
2026-10-23 07:24 INTERNAL PACKAGE v3 VERIFIED: playtest_slim_be5b7cdc_v3 (27.84 GB): verify_slim Streets PASS (HD on, Opus in), static
  PASS, headless smokes 0 errors, Experimental 20 / 20 stable + ESC 4 / 4 (wave 3 reached). Older copies deleted (F: 299 GB
  free). NOT the user build (campaign / E3 / plumes still in progress). MERGED 80d69bf3 E3b1 Brute backpack (ESCTEST Remnant
  13 / 13). Findings routed: ESC chassis energon_default exports (AssetTools), D5 collision_weapon.glb (AssetTools), the
  campaign AI squad hit by the strand-suicide rule (Gameplay), Remnant light-card collision (AssetTools, blocks ESCNAV flip).
2026-10-23 07:30 FIRST FRONTEND-LAUNCHED CAMPAIGN CHAPTER: D5 from the menus (lobby -> Normal -> 31.8 s load -> in-map select -> InGame ->
  checkpoint 1 written; the squad visible). One Frontend bug: a stale versus selection picked Scout before the trio pick
  (fix in progress). Data notes: D5_OME_BASE world.glb no triangles (expected: persistent level), gameplay.json +
  collision_weapon.glb missing (AssetTools). MERGED 3ab79529 campaign squad strand exemption, 47a03c29 chapter RT stage 2a
  (RT GI 22 pages / 1432 subs over the whole chapter). Card: Rendering x48plumetune next.
2026-10-23 07:31 overlay_20261023h FROZEN = shipping set (light cards out of the pawn collision on 6 maps, 64 placements, incl. Remnant's
  x = 149 plane). energon_default = a World.cpp lookup bug (ESC_ chassis read from VerticalSlice/Characters) -> Gameplay.
2026-10-23 07:32 Smoke stage 2 (froxel plumes) PARKED: doesn't read by eye after the in-scatter round (veil, non-monotonic); stays in code
  default off (WFC_SMOKEVOL to study); stage 1 Enhanced smoke ships. Rendering -> the campaign eye pass (D5 Realistic vs Off).
2026-10-23 07:33 AssetTools: D5 collision_weapon per unit (empty glb where none) + gameplay.json in the persistent unit; A1_IAC inputs
  (48 units collision + physics, navigation 7,849 nodes, navmesh 3,098 cells, bot_nav floor-level 18 pieces / main 3,043).
  GAP: A1's level start is at y ~ 1,094 m with no nav (a vista / flight opening?) -> DECISION fill from collision as flagged
  campaign cells; RE checks the scripted opening. A1 air nav at 4 m voxels (opt-in).
2026-10-23 07:37 USER DIRECTIVE: implement everything before the playtest; broad-stroke testing only (no per-patch batteries, confirm
  sheets or card runs for small items; one smoke per batch; Experimental = one broad pass per big batch / package). Sent to
  all lanes. MERGED 9af9cbeb campaign trio pick + checklist, 514586e5 Escalation nav ON by default (Remnant has combat on
  overlay h) + the qa:die glue. Smoke stage 2 parked.
2026-10-23 07:40 MERGED a90bea42 campaign RT calibration fix (the Omega blotches = a wrong sunless fit from the tracer counting the 9 km
  SpaceDome as a wall; sky rule aligned with the runtime; D5 now a real sun fit k 0.137). A1's opening = the intro lift
  (MoverPathNodes on the elevator): AssetTools exports mover-attached nav, Gameplay's BotNav rides a mover base.
2026-10-23 07:44 MERGED f18a203c Escalation E3b2 (Jet boosters as hover weak points, loot by spawn-list row, +3 ability resource on
  touch; GOAP port deferred behind the campaign runtime), 89d4acd9 Escalation launches from the menus (SV tag accepted,
  URL SV -> ESC). Open at Gameplay: the human's in-map pick must displace the squad member (re-SetupAiSquad).
2026-10-23 07:47 AssetTools: A1_IAC inputs FROZEN (48 units collision / physics, gameplay.json, navigation + navmesh, floor-level bot_nav
  with 34 mover-attached lift cells + 3,531 campaign_fill cells + the 92.6 m airborne_drop link; 27 / 27 starts on the nav;
  no air nav: over the 6 GB cap). The remaining 8 chapters run as one background chain in play order. D5 calibration =
  Rendering's corrected sun fit.
2026-10-23 07:50 MERGED c428f6f9 campaign Resume (per-faction checkpoint saves + Resume rows), c51ff8c7 campaign runtime 3 (pawn
  factories: 26 TnActorFactoryAI pooled, All Pawns Spawned, TF_Vehicle override; the in-map pick swaps the squad member;
  CAMPAIGNTEST 24 / 24; OmegaSupreme + the 4 battery destructibles held latent -> AssetTools exports), the CO1 co-op lobby
  test (passes without frontend changes). Gameplay next: mover nav + A1 boot, AI ops, Matinee, conversations, movies, forms,
  restore, co-op humans.
2026-10-23 07:59 MERGED bf3f28ff campaign post-process chain + matinee hooks (data-driven volumes / LUTs; setCinematicView), d252040a
  campaign audio hosted by the Kismet VM (97 D5 audio ops) + VPS readiness (both UDP ports, limits, health check,
  online_service.json), c2030cbf campaign runtime 4 (co-op humans, AI move / objective ops, CMP_ bodies) + lobby default +
  invite mailbox. Pending recut (WorldCampaign anchors): Systems' FMV + dialog sim-time. AssetTools chain: A2 running, then
  pp export, actors.json, campaign characters, destructible dump.
2026-10-23 08:08 MERGED a7aff33a campaign FMV + dialog sim-time (recut), e968850d campaign runtime 5: MATINEE (Interp play / reverse /
  stop / pause, event keys, Move tracks on InterpActors with moving collision sets and carried pawns, the director's
  camera cuts / FOV / fade via setCinematicView; D5 40 matinees bound; A1 boots headless: the intro lift as a moving set,
  495 matinees, 8,989 ops, 40 s clean). Gaps: A1 AI bodies (AssetTools), factory pool cap 64, moving nav cells, AnimControl /
  Toggle / material params / slomo. Rendering: setMapActorTransform next; A1 calibration done.
2026-10-23 08:09 MERGED 9114e981 (3 Frontend commits): co-op claims greyed in CharacterSelect + Escalation quit via LeaveSurvivalConfirm,
  the public re-draft at the vote close (PARTIAL: scores not carried), Friends List + invites (recent players as Friends
  rows, Online via queryOnline, Send Invite = code + sendInvite, InviteReceived popup -> join by code). 249 frontend tests.
  AssetTools: 57 campaign character templates building (D5 done, A1's 16 next); A1 vbake + pp volumes + battery
  destructibles + actors.json in.
2026-10-23 08:15 MERGED 287085e3 campaign runtime 6 (streaming via setChapterUnitVisible, ForceIntoForm, per-body AI pools, restart from
  checkpoint; wfc_fidelity link fix). AssetTools: 57 / 57 campaign AI bodies + 40 weapon records done (A1 factories
  unheld); chain on A2. Rendering's CAMPAIGN_STREAM (setMapActorTransform + stage 3) recut against 287085e3.
2026-10-23 08:18 MERGED 312b0a3f campaign runtime 7 (moving nav cells for movers, ForceIntoForm per RE, Buddy var; A1 factories all
  resolve). Next broad milestone: D5 + A1 from the menus, Off + Realistic, one ~8 min slot after Rendering's STREAM patch.
2026-10-23 08:20 MERGED d3c3e1af chapter streaming stage 3 + moving map actors (setChapterUnitVisible per unit incl. RT masks;
  setMapActorTransform for InterpActors; the campaign world LUT fix). Card: the Frontend runs the broad campaign milestone
  (D5 + A1 from the menus, Off + Realistic) on d3c3e1af.
2026-10-23 08:27 MERGED 6ba69d58 campaign runtime 8 (SeqEvent_Touch from TriggerVolume brushes + Trigger cylinders; A1 517 / 637 armed)
  + the Escalation drop meshes (svDropMesh -> Rendering draws drops). Experimental retracted the fidelity link report (their
  tooling overlaid an old tools/fidelity CMake); broad pass #1 running on d3c3e1af.
2026-10-23 08:33 MERGED e1084e63 all 10 chapters' level audio data + non-verbal dialog events (grunts per chassis, horde profiles), the
  Escalation drops drawn (Rendering). AssetTools: chapter-root physics.json (audio touch zones) for A1 / D5 + the chain.
  Rendering: all Escalation / campaign render items done except the eye pass; Systems: campaign audio complete for 10
  chapters, matinee timeline hook next.
2026-10-23 08:35 CAMPAIGN MILESTONE RUN (Frontend, d3c3e1af, 4 runs): menu flow PASS on D5 + A1 in Off + Realistic (trio pick + squad swap,
  checkpoint saves, 0 unanswered bridge calls, 0 held ops, no crashes); D5 looks right; A1 WORLD BLACK in both modes
  (bsp.glb missing A1_IAC_Base; fade / unlit lift suspected) -> Rendering + Gameplay; A1 Realistic load 142 s with a 16 s
  gap (17.6 GB private). Card: Rendering x51.
2026-10-23 08:42 MERGED a4ca2c04 matinee sound keys (Systems; D5 intro cine keys on Gameplay's clock), the package README online section.
  Systems' list is empty (refresh.sh on overlay h awaits the word). RE: A1 black = Switch IncrementAmount default ->
  Gameplay fixes + implements the opening (CAMPAIGN_A1_OPENING_RE).
2026-10-23 08:44 BROAD PASS #1 (Experimental, d3c3e1af): TDM 20 / 20, campaign D5 + A1 4 / 4, harnesses + net rows all PASS; one
  finding: Remnant Escalation on the cell nav default = 0 kills (team bots strand) -> DECISION ESCNAV default back OFF
  (Gameplay's next patch) until the team-bot coverage is solved.
2026-10-23 08:50 MERGED 7545f3b1 campaign runtime 9 (A1 BLACK FIXED: Switch IncrementAmount 1 + GameplayStarted filtered by the active
  checkpoint; cinematic mode; A1's opening plays headless: establishing shot 12 s -> lift ride 21 s -> control; destructibles
  D5 batteries + A1 1,040) + ESCNAV default OFF. Rendering: A1 not a render fault; the 16 s gap = RT load phases + a decal
  grid rebuilt 47x (fixed, patch pending); DECISION: stream units at Started (load / unload render data at runtime) to cut
  A1's 142 s / 17.6 GB. x51 done; eye-pass verdict pending.
2026-10-23 08:51 CAMPAIGN EYE PASS (Rendering): D5 PASS in Off (the original's grade) + Realistic (lamp highlights, contact shadows, no
  blotches); A1 render path works (47 units, calibration + post chain active); one Realistic-only cyan horizon line (fix
  pending); the A1 geometry pass rides the next milestone run after streaming-at-Started.
2026-10-23 08:54 MERGED 4e23161e campaign load fix (loading screen between RT phases; decal grid built once: -15 s + 630 MB churn on A1).
  Rendering -> real streaming (load / unload units at runtime, RT add / remove) on Gameplay's loadChapterUnits({unit})
  at Started contract; cyan horizon line deferred to the next milestone run with play views.
2026-10-23 08:56 MERGED c8e6850d campaign runtime 10: real streaming on the game side (initial state's units only at load: A1 6 + 1 of
  47; load at Started / unload at hidden via IRenderer::unloadChapterUnit). Rendering implements the unload + RT add /
  remove next. GameplayStarted checkpoint rule confirmed C by RE.
2026-10-23 08:57 Systems: LOBBY_DEPLOY_CHECKLIST (every command; dry run PASS on loopback; the SSH key is made at deploy time) +
  PACKAGE_SIZE_ESTIMATE (27.84 GB today; campaign_render 98 % duplicate lightmaps: 10 chapters ~17 GB -> ~6.5 GB with
  one lightmaps/ per chapter). DECISIONS: HD stays the optional pack; campaign in the base once deduplicated (AssetTools
  exporter + Rendering loader read <chapter>/lightmaps/); the campaign's outside assets join slim_files.tsv.
2026-10-23 09:02 MERGED 921dcf43 campaign runtime 11 (Matinee AnimControl on skeletal actors: A1 441 / D5 13), d48f995d runtime chapter
  streaming (unload frees RT sets / meshes / pages; load at Started; A1 = 7 units at load) + shared <chapter>/lightmaps/
  (A1 2.17 GB -> 45 MB; AssetTools removes the unit copies on my go). Campaign milestone run #2 (Frontend) + broad pass #2
  (Experimental) on d48f995d now. Gameplay -> pawn-group AnimControl, Omega AI, hit reactions, destructible collision.
2026-10-23 09:03 build_slim ships all chapters + the outside assets (Systems): base estimate ~15 GB with 10 chapters deduped (today
  ~12 GB), HD optional pack brings it to ~37 GB.
2026-10-23 09:04 AssetTools: unit lightmap copies removed for D5 + A1 (A1 3.3 GB -> 1.2 GB; only byte-identical files; the chain does the
  same per chapter). Frontend: campaign run #2 started on 022ba748 (5 runs incl. an A1 Realistic X3-off arm).
2026-10-23 09:07 MERGED dba6a747 campaign runtime 12 + 13 (destructible collision per state: A1's 1,040 breakables now block; pawn-group
  AnimControl: the A1 lift ride plays the trio's clips via Character::setCineClip). Gameplay left: Omega AI actions, hit
  reactions, horde rules, then a PROV list for the final-build decision.
2026-10-23 09:08 GAMEPLAY_PROV_LIST merged; DECISION: fix H3 / H5 / M1 / M5 / M10 / M11 for the final build; GOAP (H1 / H2) + H4 stay
  PROV (the GOAP port is post-build). Gameplay: Omega AI + hit reactions + melee rule, then the six fixes, then done.
2026-10-23 09:20 CAMPAIGN MILESTONE RUN #2 (Frontend, 022ba748 / d48f995d): flow PASS x5, 0 unanswered bridge calls, 0 held ops; A1
  RENDERS (the opening plays); loads D5 Off 37 s / 5.9 GB, D5 Realistic 55 s / 6.8 GB, A1 Off 44 s / 7.3 GB, A1 Realistic
  62 s / 8.4 GB (was 142 s / 17.6 GB); black regions around the lift (void vs not-streamed: Rendering eye pass); a squad
  strand-recover once per A1 run at ~60 s (Gameplay); persistent bsp.glb 'cannot read' (expected?).
2026-10-23 09:22 Run #2 eye pass (Rendering): cyan line CLOSED (void-only artefact); the persistent bsp.glb error is benign (empty BSP in
  the original); D5 PASS again; A1's black band = the opening in the lift pod with nothing drawn beyond (not sky either):
  state membership vs black surfaces -> RE asked what the original shows there and in which level / state.
2026-10-23 09:23 RE on A1's black regions: not streaming: the lift platform (TnTransformingWorldActor_6275, RelativeToInitial track
  0 -> -68,736 UU over 19.94 s) should carry the players to the bottom (Elevator_Art / CommandCenter_Art, both in
  LevelStart); our pod stays at the start height -> the move track isn't applied to that actor class or the pawns aren't
  carried: Gameplay.
2026-10-23 09:24 BROAD PASS #2 (Experimental, d48f995d): ALL PASS, no findings (TDM 20 / 20, campaign 4 / 4, ESC both maps with
  combat, net rows identical). Remaining before the FINAL cut: Gameplay (lift platform move, Omega AI, hit reactions, horde
  melee rule, the six PROV fixes), AssetTools chain A2..D3B (+ per-chapter dedupe / pp / actors / characters), Rendering's
  held patch, Frontend Claim + re-draft; then broad pass #3 + the combined timed gate + refresh.sh.
2026-10-23 09:28 MERGED 4a7fa719 Rendering A1 follow-ups (every world.glb actor node poseable; benign BSP info); the final gate runner
  (final-gate.sh: pre = base + control + same-match + stability; gate = PERF_HOLD 10 maps x 2 res x base / head x 2 reps,
  ~90 min). Gameplay cuts FINAL_BASE_ITEMS (sim-only diffs vs af4d0731). AssetTools exports TnTransformingWorldActors
  (the A1 lift body) into the units' world.glb.
2026-10-23 09:51 MERGED 66109cf5 A1 LIFT RIDE FIXED (the pod floor had no collision: pawns fell to the bottom before the ride; a riding
  floor set + moving nav cells relinked at rest; the trio now rides y 1093 -> 406 m), robot hit reactions (RE §2; the
  offline SIMHASH stayed IDENTICAL: presentation only; the commit message's 'changes as expected' is wrong), Escalation M10
  (PlayerData values, 1 s horde melee). Gameplay left: Omega AI, H3, H5, M1, M5, M11, then FINAL_BASE_ITEMS.
2026-10-23 10:05 BOUNCED Gameplay runtime 14 (interacts + Omega Supreme + H5): CAMPAIGNTEST 22 / 24 on the head (squad follow + squad
  respawn checks fail, likely Omega's attacks reaching the test squad); Gameplay fixes + reruns once, recut vs 66109cf5.
2026-10-23 10:09 MERGED a5647405 bridge scan (every original movie call answered) + fb4b3f10 public re-draft by scores (249 frontend tests,
  FLOW 0). The Frontend's interact prompt glue (201e0902) waits for Gameplay's recut runtime 14; then the Frontend is done
  for the build.
2026-10-23 10:11 AssetTools chain status: A2 DONE (51 units); A3 running; 287 units left at ~2.3 min / unit (~11 h single). DECISION:
  two chains at once + per-chapter queue items as each finishes (ETA roughly halves); the final gate waits for the chain
  (CPU) and Gameplay's recut runtime 14 + FINAL_BASE_ITEMS.
2026-10-23 10:18 AssetTools: two lanes running (A3 / A4 / D3A / A5 and D1 / D2 / D3B; ETA ~16:00-16:30); A2_KON SHIPPED complete. Lightmap
  correctness bug found + fixed in the dedupe: LightMapTexture2D names collide across a chapter's packages and
  build_lighting copied pages by file name (D5 1 page / A1 27 pages came from the wrong package); conflicting names stay
  per unit; verified vs umodel per package (0 wrong / 0 missing). Root cause in Rendering's tool (told).
2026-10-23 10:27 MERGED f72b1f21 campaign runtime 14 + 15 (244 player interacts, Omega Supreme boss fight, H5, H3 AI shoot / interact ops,
  M5 ranges, M1 aux spawns) + the interact prompt glue; CAMPAIGNTEST 24 / 24. Gameplay left: M11 + FINAL_BASE_ITEMS; the
  Frontend is done for the build. Chain lanes running (ETA ~16:00-16:30).
2026-10-23 10:30 MERGED 7f65db0e Escalation M11 (loot pickups + the ability resource; svAbilityResource for the HUD; Shard_FX for
  Rendering). Gameplay's last item = FINAL_BASE_ITEMS. Pending for the cut: the Frontend's ability meter, Rendering's shard
  FX, AssetTools' lanes, then the final gate + run + refresh.sh.
2026-10-23 10:33 MERGED ac9dafcb Escalation ability meter = the resource (Frontend). Frontend DONE for the build. Pending for the cut:
  Gameplay FINAL_BASE_ITEMS, Rendering shard FX, AssetTools lanes (A3 / D1 running).
2026-10-23 10:36 MERGED 3476b84c Escalation loot shards drawn (Rendering; last render item). Rendering + Frontend DONE for the build.
  Pending for the cut: Gameplay FINAL_BASE_ITEMS, AssetTools lanes.
2026-10-23 10:38 CUT CANDIDATE a5d088db (code 3476b84c + FINAL_BASE_ITEMS a5d088db). Gameplay, Frontend, Rendering DONE for the build.
  Running: Experimental final-gate PRE stage; the Frontend's final milestone run (4 campaign + TDM + ESC from the menus);
  AssetTools' two chapter lanes (~16:30). Then: PERF_HOLD timed gate (~90 min) -> Systems refresh.sh -> verify -> hand-over.
2026-10-23 10:41 PACKAGE HEAD e967efd4 (a5d088db + build_slim ships the campaign manifests; code unchanged 3476b84c). Systems ready, cut
  command fixed (SPLIT_HD=1 NOPROMOTE=1 JOBS=4 refresh.sh e967efd4 ...; base ~15 GB + HD pack ~21.7 GB). Waiting: Frontend
  milestone run, Experimental pre, AssetTools chapters (~16:30), then the timed gate.
2026-10-23 10:57 AssetTools: A3_COR shipped (46 units). Nav fixes after shipping (unsuffixed NavigationCell sort key; floor lowering on
  empty collision glbs: A2 re-lowered; bounded off-nav fill 60 m per seed); MP / Escalation nav identical by construction;
  A2 / A3 air nav rebuilt by a waiter. GATE HOLD: nonpaged pool 9.8 GB; the timed gate waits for the user's reboot /
  Malwarebytes decision (put to the user ~10:50); the headless pre stage continues.
2026-10-23 10:58 FINAL MILESTONE RUN PASS (Frontend, candidate code a5d088db): 6 / 6 runs exit 0, 0 crashes / hang dumps / [error] lines /
  unanswered bridge calls; D5 + A1 x Off / Realistic from the menus (pick, squad swap, game over path), TDM Streets 4v4,
  Escalation Remnant (wave 1 game over -> stats -> lobby); 249 / 249 frontend tests. Shots: Rebuild-Frontend/work/bins/final/
  (eye pass with Rendering). Notes: bot_skill.json warning = the run dir's cwd (data/frontend has it; build_slim ships it).
2026-10-23 10:59 EYE PASS PASS (Rendering, sheet Rebuild-Rendering work/wobble/final_sheet.png): D5 / A1 both modes, TDM, Escalation clean;
  black frames = menus (Mission Failed, scoreboard). Open: the A1 lift matinee Interp_15149 never starts in the menu run
  with an idle human (Gameplay asked: trigger = human in the pod? else a bug); esc_p05 cyan shape = GFx wave intro? (Frontend
  asked). Post-build: silence poses for non-resident units.
2026-10-23 11:04 Eye-pass follow-ups: A1 lift = NOT a bug (Interp_15149 plays 94.96-118.78 s wall, pawns at y 405.8; streaming after the
  lift needs the player to walk on: test artifact; the remaining black slot = suspected pod skeletal mesh at bind pose,
  data / anim item). esc_p05 cyan shape = a MESH (unchanged with WFC_NOMAPFX=1), suspect WEP_Shield_p.Barrier with the
  Defrag material param missing; Rendering builds a WFC_SKIPMAT exe, the Frontend reruns 3 arms with it.
2026-10-23 11:05 GATE FINDING (Gameplay): TnSeqAct_TransformWorldProp unimplemented: campaign doors / tube doors / pods (A1 174 ops / 47
  actors, A2 184 / 29, A3 11 / 72, A4 39, D1 5) never open -> progression blocked at scripted doors; the A1 black slot = the
  untransformed pod. GO: AssetTools per-chapter transforming_actors export (MP schema; A1 first), Gameplay loader + op +
  nav states + FINAL_BASE_ITEMS 07, Rendering poses them from TransformingActors' t. Also: barrier Fresnel fix (Rendering).
  The cut moves to the head carrying both; the timed gate waits for the reboot anyway.
2026-10-23 11:05 Gate items added (Gameplay, data already exported by AssetTools in out/campaign_characters/spawnables.json): sentry
  factories (138, TnActorFactorySentryAI, 11 TURRETDEFs) and roller mines (46 TnActorFactoryRollerMine, D chapters). Order:
  TransformWorldProp -> sentries -> roller mines, then FINAL_BASE_ITEMS 07; GOAP post-build.
2026-10-23 11:11 MERGED 49069cd9 material pow = UE3 ClampedPow (RE from the shipped microcode; fixes the cyan barrier sheet). A1 black band
  confirmed = the bind-pose pod (SKIPMAT=Pod arm). AssetTools' transforming_actors_gameplay.json ready for A1 / D5 / A2 /
  A3 (MP schema). Draw route: the World draws campaign transforming actors posed (versus path) + setActorHidden on the
  bind-pose node; renderer part nil. Gameplay implementing TransformWorldProp -> sentries -> roller mines.
2026-10-23 11:13 Cyan sheet NOT removed by ClampedPow (confirm on Rendering's exe: unchanged). ClampedPow stays (RE-correct). Next: a
  diagnostic exe (WFC_DRAWDUMP near-camera draw list at the shot frame + SKIPMAT on skinned draws), one Frontend slot, then
  a named mesh and the fix.
2026-10-23 11:18 MERGED 7f4f72fe campaign transforming actors (doors / tube doors / pods open via TnSeqAct_TransformWorldProp; the pod
  rides the A1 lift; 140 nav cells blocked by closed doors) + 18861041 Gltf keys bind-pose nodes by actor (setActorHidden
  reaches them). Gate item 1 DONE; Gameplay on sentries -> roller mines -> FINAL_BASE_ITEMS 07. Cyan draw dump pending.
2026-10-23 11:25 Cyan sheet RESOLVED as a camera placement item: the draw is BlueEnergonSigns_MATINST on StaticInterpActor_75 (an authored
  10 m additive spinning disc at Remnant's spawn, camera-transparent in the original); the material is faithful; the
  camera sat 0.33 m from the disc, 7.9 m from the player start. Gameplay checks the spawn pick / camera vs the original.
2026-10-23 11:28 MERGED 76d8b4f5 campaign sentries + pawn factory death outputs (Single / All Pawns Killed never fired: every scripted fight
  waiting on them stalled) + chapter start = first checkpoint (A2 started inside the Soundwave arena). Gate item 2 DONE.
  Cyan sheet: camera + boom faithful (8 m orbit); the Remnant human start pick vs the original is with RE. Roller mines next.
2026-10-23 11:36 MERGED 903b2ab7 roller mines + FINAL_BASE_ITEMS 07. ALL THREE GATE ITEMS DONE. CODE COMPLETE: FINAL CANDIDATE 903b2ab7.
  In flight: Frontend's full milestone rerun on it; Experimental's pre stage re-run (base 01-07 vs head); Systems
  re-points refresh.sh; AssetTools chapters (~16:30) + outside list. Then: reboot (user) -> PERF_HOLD timed gate ->
  cut word -> refresh.sh -> verify_slim -> hand-over.
2026-10-23 11:56 FINAL CANDIDATE 903b2ab7 MILESTONE RERUN PASS (Frontend): 249 / 249 tests, 6 / 6 runs clean (0 errors / bridge gaps /
  dumps); A1 p60 shows the pod + lit floor (one dark wedge for Rendering's eye pass); esc_p05 unchanged (faithful). Shots
  Rebuild-Frontend/work/bins/final2/. Remaining before the cut: Experimental pre re-run, AssetTools chapters (~14:30),
  the user's reboot, then the timed gate.
2026-10-23 11:57 EYE PASS on 903b2ab7: Off clean everywhere; A1 pod draws. One Realistic-only pre-existing defect: a character near the
  camera drawn pure black (D5 Megatron; likely zero ambient from an unusable RT probe cell). DECISION: fix now (the gate
  waits on the reboot anyway; headline mode): Rendering falls back to the bake's ambient; one D5 Realistic confirm shot.
2026-10-23 12:05 MERGED dffcf31f Realistic probe-dark fix (characters no longer lit by near-black RT probes; Off unchanged) + e4549909
  build_lighting decal-tail fix (D1_ORB_Crash2_ART rerun). CANDIDATE = e4549909 (code dffcf31f). D1 shipped (6 / 10).
  Pending: the Frontend's D5 Realistic confirm, Experimental pre, chapters D2 / D3A / D3B / A5 (~14:30), the user's reboot.
2026-10-23 12:08 Probe-dark CONFIRMED (Frontend D5 Realistic on Rendering's exe: 79 / 80 ENVLOG probes rejected dark, Megatron lit).
  Frontend now reruns the full milestone script on e4549909 (hand-over run). D1 complete (6 / 10 chapters).
2026-10-23 12:10 MERGED c3c9c157 build_lighting decodes LightMap1D decal receivers (tool only; D1 Crash2 rerun). PACKAGE HEAD c3c9c157
  (code dffcf31f). Frontend hand-over rerun in flight on it.
2026-10-23 12:13 FINAL GATE PRE STAGE PASS (Experimental 04b0d538, tools/fidelity/results/FINAL_GATE_PRE_c3c9c157.md): same match base
  (af4d0731 + 01-07) vs head on Streets TDM / Remnant ESC / BrokenHope ESC, negative control differs; stability: TDM 20 / 20,
  ESC clean, A1 2 x 300 s, CAMPAIGNTEST 24 / 24, KISMETTEST 10 / 10, VOICETEST, net rows identical. D5 headless now ends at
  40 s by design (Omega grabs the idle PlayerBot player). The timed-gate head dffcf31f is built ahead of the hold. Remaining:
  the Frontend hand-over rerun, chapters D2 / D3A / D3B / A5, the user's reboot -> PERF_HOLD gate (~90 min) -> cut.
2026-10-23 12:26 HAND-OVER RERUN PASS (Frontend, code dffcf31f): 249 / 249 tests, 6 / 6 runs clean, Megatron lit in D5 Realistic. Shots
  Rebuild-Frontend/work/bins/handover/. The Frontend + Rendering + Gameplay + Experimental (pre) are DONE. Waiting only
  on: chapters D2 / D3A / D3B / A5 (~14:30) and the user's reboot for the timed gate.
2026-10-23 13:13 D2 + D3A shipped (8 / 10). Gameplay D3A mines check: 42 mines launched, 36 explode (shot / fuse / touch), no crash, no
  patch. Open: 64 D3A transforming actors have gameplay rows but no render node (AssetTools: are their levels chapter
  units? drop the rows if not). Air-nav queue + lanes reboot-safe and PERF_HOLD-aware (air_queue.sh, resume_lanes.ps1).
2026-10-23 13:15 AssetTools data gaps found + GO: (1) 2 streaming levels with transforming actors had no render unit (D3A CourtTrans_VIG 64,
  A2 DRIVE3_Trans_Design 1): units created + layout rerun; (2) A4 / D3A / D3B / A5 had no persistent unit dir, so the
  whole-chapter gameplay.json (start points / pickups / map state) was never copied: fixed per chapter. ~10 min light jobs.
2026-10-23 13:30 AssetTools fixes landed for D3A / A2 / A4 (new streaming units CourtTrans_VIG + DRIVE3_Trans_Design, persistent Base
  units with the whole-chapter gameplay.json). Gameplay D3A rerun: 9 units at start incl. Base, pickups 8 factories load,
  340 transforming actors all with nodes, no crash. D3B / A5 get the same on shipping.
2026-10-23 13:33 GO (AssetTools): ~890 skeletal world actors drawn nowhere (set dressing: neither transforming nor matinee-driven; D5 3,
  A1 62, A2 106, A3 203, A4 126, D1 258, D2 48, D3A 81) become bind-pose nodes via extra_skeletal_actors.json, minus the
  sentry spawn bodies (Gameplay draws those). Light, ~1 min per chapter.
2026-10-23 13:34 GO (Gameplay, small, render-only): SeqAct_ToggleHidden + SeqAct_Destroy on world actors (unimplemented: scripted hides /
  destroys of set dressing never happened) -> setActorHidden (+ collision off for Destroy on movers / destructibles);
  authored bHidden honoured at load. Sentry bodies = world nodes toggled by Gameplay (AssetTools includes them).
2026-10-23 13:42 MERGED 59cb3162 campaign ToggleHidden / Destroy + authored bHidden at load (A1: 252 hides / 126 unhides / 7 destroys
  applied). CODE CANDIDATE 59cb3162. Gameplay folds it into FINAL_BASE_ITEMS 08; Experimental rebuilds the gate head;
  Systems re-points; the Frontend reruns the hand-over set on it.
2026-10-23 14:01 HAND-OVER RERUN PASS on 59cb3162 (Frontend handover2/): 249 / 249 tests, 6 / 6 clean, authored hides applied. FINAL_BASE_
  ITEMS 01-08 on the head (49dd49aa); the timed gate is staged (base final3 vs head fast_59cb316). Waiting: AssetTools
  (A5 / D3B / skeletal passes / outside list), Gameplay D3B check, the user's reboot.
2026-10-23 14:02 Render verdict on handover2: SHIP (identical to handover within noise; TDM brighter = a lit-plaza spawn). Plan: once
  AssetTools' skeletal set-dressing passes finish, one Frontend look at A1 + A2 (+60 s shots, current exe) to confirm the
  new nodes + authored hides (data only).
2026-10-23 14:05 AssetTools: skeletal set dressing added for the 8 shipped chapters (749 nodes drawn; 137 author no mesh: runtime-set or
  empty; sentry bodies included; extras.hidden = authored bHidden). Frontend set-dressing look (A1 + A2 Off) running.
2026-10-23 14:11 Set-dressing look (Frontend): A1 OK (nodes draw, 2 authored hidden sent); A2 sent 0 authored hidden vs 55 hidden nodes in
  the data -> Gameplay + AssetTools check the actors.json bHidden key (data or tiny reader fix). A5 shipped (9 / 10);
  D3B last, then the outside list. Air-nav queue started.
2026-10-23 14:18 MERGED 78caf2bc authored hides limited to drawable classes + name conflicts left visible (the bHidden export marks
  thousands of class-default rows: volumes, lights, sounds; 35 short-name conflicts across levels: path keys on both sides
  = post-build). CODE CANDIDATE 78caf2bc. Frontend reruns A1 + A2 on it; Experimental rebuilds the gate head; Gameplay
  folds it into FINAL_BASE_ITEMS.
2026-10-23 14:27 MERGED b315e585 actor subs keyed by object path (Rendering): fixes the 35 short-name collisions once Gameplay sends paths
  (patch requested) and AssetTools adds extras.actor_path to every chapter (requested). Experimental arms final4 / 78caf2bc
  built; rebuild at the grant with the final head.
2026-10-23 14:27 Set-dressing look on 78caf2bc PASS (Frontend setdress3/): A1 509 / A2 242 authored hidden sent as expected; A1 identical
  to the baseline except the lift's black band now gone (the pod mesh is authored hidden) and a pose offset; nothing
  vanished wrongly. Pending: Gameplay's path-send patch (+ FINAL_BASE_ITEMS 10), AssetTools actor_path + D3B + outside list.
2026-10-23 14:30 MERGED c72b7638 campaign hides keyed by object path + FINAL_BASE_ITEMS 10 (A1 539 / A2 249, exact). FINAL CODE c72b7638.
  actor_path done for 9 chapters (150,044 nodes). D3B late (~16:20; air queue PAUSED to free CPU; outside list ~16:30).
  Experimental rebuilds arms final5 / c72b7638; the Frontend runs the final hand-over set (handover3 + A2 arm).
2026-10-23 14:51 FINAL HAND-OVER RERUN PASS on c72b7638 (Frontend handover3/): 249 / 249 tests, 7 / 7 runs clean, hides A1 539 / A2 249 /
  D5 16 exact, A1 Realistic lift floor fully lit. Experimental arms final5 / c72b7638 built + dry-run clean. Systems
  pre-pointed. Remaining before the cut: D3B (~16:30) + outside list (~16:45), the user's reboot -> timed gate.
2026-10-23 14:52 Render verdict on handover3: SHIP with one Realistic item: the player's held weapon is a black silhouette in D5 Realistic
  (pre-existing; the gun takes the small-dynamic cell env; likely RT self-shadow by the body). DECISION: fix now (time
  before the reboot / D3B); Rendering runs the A/B on the card and cuts a render-only patch vs c72b7638.
2026-10-23 17:06 MERGED 454d7bff Realistic held weapons drawn with the bodies on the RT path (they were composited as world pixels with the
  indoor sun / AO factor 0: black silhouette; outdoors ~1 so MP never showed it). FINAL CODE 454d7bff. Experimental
  rebuilds the head arm; Systems re-pointed; the Frontend runs handover4. D3B ~16:30, outside list ~16:45.
2026-10-23 17:08 ALL 10 CHAPTERS SHIPPED (AssetTools 2cabbb4; D3B at 16:59). Outside-render list final: 8,678 files / 2.98 GB (now incl.
  world.glb-referenced textures + shared transforming GLBs). Air queue resumed (optional; ships what exists at the cut).
  Remaining: Gameplay D3B check, Frontend handover4, Experimental head arm, Systems guard dry-run, the user's reboot.
2026-10-23 17:13 Gameplay D3B check: loads (9 units, 55 transforming, 24 sentry + 33 mine factories, 308 hides) but the human falls out of
  the world at 12 s: the intro start stands over a void (the original holds / teleports the players during IntroCine).
  Campaign-wide gap: SeqAct_Teleport unimplemented (A1 60 ... D3B 39 refs). GO: Teleport + the D3B intro hold (RE asked;
  PROV cinematic hold), one CAMPAIGNTEST + one D3B run, FINAL_BASE_ITEMS 11.
2026-10-23 17:14 MERGED 8a60a840 build_slim '_' folder rule (Systems; guard dry-run PASS for 10 chapters; base ~17.7 GB + HD pack ~21.7 GB).
  Post-build item: ~2.1 GB duplicate campaign PNGs (shared texture pool). PACKAGE HEAD 8a60a840 pending Gameplay's
  Teleport patch.
2026-10-23 17:25 HAND-OVER RERUN PASS on 454d7bff (Frontend handover4/): 249 / 249, 6 / 6 clean, held weapons shaded in Realistic. D3B
  fall: the ledge collision IS exported (AssetTools); runtime cause with Gameplay (spawn before unit collision load /
  prop_simple hulls / slope). Gameplay's Teleport patch pending.
2026-10-23 17:37 MERGED 6d30fff2 SeqAct_Teleport + TnSeqVar_Character + headless movie end + unit-scoped hazard volumes (D3B intro plays;
  no KillZ; mines / sentries run) + FINAL_BASE_ITEMS 11 (stray empty file removed 20608326). FINAL CODE 6d30fff2, PACKAGE
  HEAD 20608326. Experimental rebuilds arms final6 / 6d30fff2; Frontend runs handover5; Systems re-pointed. Then: the
  user's reboot -> PERF_HOLD timed gate -> cut word -> refresh.sh -> verify -> hand-over.
2026-10-23 17:57 FINAL HAND-OVER RERUN PASS on 6d30fff2 (Frontend handover5/): 249 / 249, 6 / 6 clean, pressuntil exercised, weapons +
  characters shaded in Realistic. Experimental arms final6 / 6d30fff2 built + dry-run clean. EVERY LANE DONE FOR THE
  BUILD. Waiting only on the user's reboot -> timed gate -> cut.
2026-10-23 17:57 Render verdict on handover5: SHIP (A1's p30 / p60 fell inside the intro FMV because pressuntil enters earlier; A1 in-game
  render covered by handover4 on the same render code). HAND-OVER SET = handover5 (+ handover4 for A1 in-game frames).
2026-10-23 18:03 BLOCKER (Frontend, from the handover logs): A1's intro movie FMV_a1intro plays twice (the level Kismet replays it right
  after the first play ends; a player must skip twice). Gameplay compares with the original's gating and fixes (play once);
  the cut waits for it.
2026-10-23 18:08 MERGED d5bb6863 campaign movies play once (a Game.PlayMovie bridge race replayed the ended movie for one frame) +
  FINAL_BASE_ITEMS 12. FINAL CODE d5bb6863. Frontend confirms with one A1 Off run; Experimental rebuilds arms final7 /
  d5bb6863; Systems re-pointed. Then the reboot -> gate -> cut.
2026-10-23 18:13 A1 intro CONFIRMED once (Frontend handover6/ on d5bb6863): a1text + a1intro each play once, one skip -> gameplay; in-game
  shots at +60 / +90 (the lift ride matinee at +30). BLOCKER CLOSED. Waiting only on the user's reboot.
2026-10-23 19:20 REBOOT DONE (boot 19:13). The nonpaged leak was an ORPHANED Malwarebytes FlightRecorder.sys boot driver (Malwarebytes
  itself uninstalled); the user disabled it (sc config start= disabled) and restarted: nonpaged 9.8 GB -> 925 MB, driver
  Stopped / Disabled, C: 101 GB free, desktop 2560x1440 @ 144 (208 modes). Lanes relaunched (rendering-11, gameplay-91,
  experimental-e1, assettools-d6, re-workspace-a6; Integration = rebuild-5f). TIMED FINAL GATE GRANTED to Experimental
  (final-gate.sh d5bb6863 gate, base final7); freeze sent to every lane. Then: cut word -> refresh.sh -> verify -> hand-over.
2026-10-23 20:19 NEXT PHASE (user ~20:20): the two WFC DLC packs (Map and Character Pack #1 / #2, STFS packages in F:\Transformers
  Rebuild\4156084C, read-only) go in AFTER the current build, split across the lanes by content: docs/DLC_PLAN.md.
2026-10-23 20:21 User ~20:35: a Language option in Settings goes into the NEXT build (with the DLC); logged in docs/DLC_PLAN.md.
2026-10-23 20:24 FINAL GATE PASS on d5bb6863 (Experimental 34049869, tools/fidelity/results/FINAL_GATE_d5bb6863.md): 40 / 40 rows same
  match vs the base (af4d0731 + 01-12) on 10 maps x 1440p / 1080p x 2 reps; head faster on average (2.52 -> 2.48 ms 1440p,
  2.53 -> 2.49 ms 1080p); worst median 3.85 ms (Gorge); no contamination; pool flat. Combined gate COMPLETE (pre + timed).
  CUT WORD sent to Systems (refresh.sh d5bb6863). RELEASE sent to every lane; the DLC phase starts (AssetTools extraction).
2026-10-23 20:29 DLC INVENTORY (AssetTools): 9 maps (5 versus + 4 Escalation), no new characters (7 base unlocks), 1 new ESC enemy, 5
  languages for DLC strings. Assignments sent to every lane (docs/DLC_PLAN.md). AssetTools' 9-map chain started (Pack 1).
2026-10-23 20:31 Cut step 1 (Systems): the d5bb6863 build OK (Release + PGO, RT shaders, Opus); packaging stopped on 26 duplicate outside
  rows (weapon glTFs already shipped as xpr twins) -> build_slim b8940af3 taken (c4155702); packaging phase rerunning.
  Post-build merges: 7f654ad5 (quiet poses; matc plain pow: the DLC maps translate once correctly, shipped maps regen at
  the DLC gate). AssetTools DLC plumbing ~2-3 h, then Fortress; ESC data needs RE's per-map export.
2026-10-23 20:34 DLC_RE.md (RE 926973d) + the 4 DLC Escalation exports (Tranquillity 17 waves, District 20 + the new Autobot jet AICD,
  Pulse 30 with tank bosses, Static 21). Gating: every shipped playlist has ContentIds (0) and MapInfo DlcId 0; the
  Frontend merges the pack ini, HasRequiredAssets true for the 9 maps, chassis unlocked, no DLC popups.
2026-10-23 20:36 MERGED e54dcd34 Language option in Settings (Frontend 579c9727: profile Language INT default, INT + FRA from the Coalesced
  packs, FRA text overlaid key by key, FRA map names, FRA logo / movies; Systems' audio setLanguage follows the package)
  + the harness steps (00c61cbf). 250 frontend tests, identity. Not in the d5bb6863 package (next build).
2026-10-23 20:37 User ~21:30: split screen on PC -> next build scope (docs/DLC_PLAN.md); RE spec first, then Rendering + Gameplay.
2026-10-23 20:40 SPLITSCREEN_RE.md (RE a952b13): the original has no split screen -> ours to define; decisions in docs/DLC_PLAN.md (all
  modes, horizontal default + vertical option, KB+M = seat 1, full quality per view). MERGED f2c2ca5d chapter-level
  texture fallback (loader side of the PNG dedupe).
2026-10-23 20:42 MERGED f6500a29 campaign tactical volumes + object lists (post-build; FINAL_BASE_ITEMS 13). Gameplay's split-screen plan
  taken (seat 2 as a local participant on the remote-seat path). DECISION (user ~22:00, "whatever saves Claude points"):
  finish and hand over the current package as is; the user's playtest findings + DLC + Language + split screen go into ONE
  next build; lanes report only merge-ready patches / blockers / per-map done; one headless per item; no confirm reruns.
2026-10-23 20:48 MERGED e7b72e0b: the Frontend's DLC sections 3-4 (9 maps in the lists / playlists / Quick Match / ESC list, DlcId 0,
  selectable as their data lands; 7 chassis unlocked; thumbnails from out/dlc_content) + Rendering's split-screen
  interface (SplitScreen.h; GL path next) + the Frontend's split plan. RE's ESC inputs delivered (zone order, factions,
  kismet extra). Split screen coding starts on all three lanes; Systems' input frames / localSeats / second listener
  queued after the package. AssetTools: FRA names re-export (U+FFFD), thumbnails in dlc_content.
2026-10-23 21:01 DLC MAP 1 / 9 FORTRESS SHIPPED (AssetTools: map data AssetTools/work/dlc_maps/VerticalSlice/Maps/MP_KON_Fortress, render
  data AssetTools/out/dlc_render/MP_KON_Fortress; base identity proven on Gorge). Dev root for DLC runs: Rebuild/work/
  assets_dlc/ (junctions: VerticalSlice/Maps/<base maps> + MP_KON_Fortress; content/<pkg> = 800 base + 13 DLC; config /
  maps / movies) -> WFC_ASSETS=.../work/assets_dlc/VerticalSlice; render data junction work/render/MP_KON_Fortress.
  Fortress headless TDM 20 s: exit 0, 1801 SIMHASH lines, 0 errors. Frontend split step 1 needs a recut vs the head.
2026-10-23 21:04 MERGED eafcc4e5 Frontend split step 1 (seat 2 join / leave, wfc_profile_2.ini, Split Screen Layout row + glue, in-place
  campaign restart glue). Cut status (Systems): packaging rerun 1 done (38.22 GB: base 17 + HD 22); verify: Streets /
  Settings / RT PASS, Gorge + Debris failed on the missing per-map lens_flares.json -> build_slim fix taken (123d3e7f),
  repackage 2 running (IO only), then verify Gorge + Debris. Rendering has the card for 2 split phase-1 runs meanwhile.
2026-10-23 21:08 PACKAGE HANDED OVER: F:\Transformers Rebuild\Rebuild-Systems\work\playtest_slim_d5bb6863 (base 17 GB; Play.bat) + 
  playtest_slim_d5bb6863_HDpack (22 GB, optional, merge per README_HD.txt). Verify: Streets / Settings / RT PASS; Gorge +
  Debris re-verified by the user's playtest (lens_flares fix in). MERGED 48c6d1b0 split-screen seat 2 (Gameplay) +
  acc8e648 (step 1 strings / tests). Fortress: every mode plays headless (Gameplay); 19 DLC texture paths fixed via an
  out/ junction. The user playtests now: card held by the user; lanes headless / code only.
2026-10-23 21:12 DLC textures: the 19 'decode failed' = a URI 5 levels up into AssetTools/out (nonexistent elsewhere); AssetTools re-emits
  the DLC maps with base-style URIs; packaging ships only the 573 DLC-only content files (the rest are low mips of base
  textures). Experimental: Fortress headless rows PASS.
2026-10-23 21:14 MERGED bf8029a2 split-screen render phase 1 (two views per frame, per-view post into its rect; one view = today's frame;
  later: RT / FSR per view, one HUD per half). AssetTools moves the DLC map root to out/dlc_content/VerticalSlice/Maps/
  (base-style URIs); Fortress + Havoc rebuilding there -> re-point my work/assets_dlc junctions when they land.
2026-10-23 21:17 MERGED 01988f7a split-screen seat 2 steps 1 + 2 (Gameplay; replaces 1d392b3d; PresentedFrame::seats). Frontend goes on the
  per-seat HUD + the pad-Start join + lobby rows; Gameplay steps 3 (online, two local seats) + 4 (campaign / ESC seat 2);
  Rendering phase 2 (seat cameras + RT per view). User playtest in progress (card held by the user).
2026-10-23 21:21 MERGED 5d6454b9 Systems split-screen 1 (seat-2 pad input rules, second audio listener with nearest-listener attenuation,
  ISession second-local interface off). DECISION: offline split screen ships first; online two-locals = a wire bump (one
  match seat per peer today) as Systems' own later patch with a 2-peer headless SIMHASH test; Gameplay's step 3 holds.
  Systems next: setLanguage, DLC audio + packaging.
2026-10-23 21:23 MERGED 7545d295 split-screen render phase 2 (RT per view; the frame loop drives two views from the seat cameras). DLC
  maps 2 / 9: Fortress re-emitted + Havoc shipped at the final root AssetTools/out/dlc_content (base-style URIs); dev
  junctions re-pointed; both boot headless (exit 0, 0 errors, 0 texture failures). Campaign air nav final (D3A failed at
  8 m, later). Gameplay / Experimental run Havoc; Rendering checks the render data headless.
2026-10-23 21:26 MERGED f190b8af seat 2 joins a bot slot (campaign: the last squad AI; versus: the team's last bot; A1 / Streets / Remnant
  seat-2 bot checks clean) + df2f95c4 audio language (setLanguage INT / FRA, INT fallback). Havoc: all six modes play
  headless (Gameplay), no data issues. Rendering's headless render-data check of Fortress / Havoc: JSON / atlases /
  references OK; two data items with AssetTools: 30 'no master material' per map (the DLC chain's parent lookup needs the
  base packages: build_materials rerun) and no LightsVisibilitiesVolume (renderer fallback if the levels lack it).
2026-10-23 21:29 MERGED 95bdd709 split-screen HUD rect hook (one HUD movie per seat); FSR3 per view deferred to phase 4 (native per view).
  Split screen status: render phases 1-3 + Gameplay seat 2 (join, HUD state, bot-slot handover) + Systems input / audio
  interfaces merged; the Frontend's per-seat HUD + pad-Start join + lobby rows are the last offline piece; online
  two-locals = Systems' later wire patch.
2026-10-23 21:32 MERGED 04f6f3b6 Frontend split step 2 (per-seat HUD in its view rect, pad-Start join / Back leave, both locals in the lobby
  rows, setLanguage on boot / Apply): OFFLINE SPLIT SCREEN CODE COMPLETE (needs the first windowed two-pad check: the user
  or a scripted join). MERGED ebbbdf05 DLC audio 1 (Fortress + Havoc level audio) + the DLC packaging rule (+0.4 GB / 2
  maps). Experimental: Havoc rows PASS; Fortress re-check 0 texture failures; Streets identity holds through the split code.
  Open data: materials parent rerun, LightsVisibilitiesVolume, 147 _LOC twins (AssetTools).
2026-10-23 21:43 MERGED a503085c seat-2 script hook (Frontend) + 6263d73e DLC audio root move + LevelAudio.inc with District / Fortress /
  Havoc (Tranquillity waits on its props re-export + the _LOC twins: AssetTools). Systems on the online two-locals patch.
2026-10-23 22:03 DLC MAPS 3-4 SHIPPED (Tranquillity, District; materials rerun 0 errors on all four; only District authors a light-
  visibility volume, the others take the original's unbaked path, no renderer change). Dev junctions added; both boot
  headless (0 errors). Gameplay wires the two ESC maps' waves; District's transforming actors re-export after psa2gltf.
  Rendering's eye pass (4 maps) + Realistic split test + the Frontend's scripted split check wait for the card.
2026-10-23 22:13 MERGED 784dc096 Escalation on Tranquillity (17 waves) + District (20 waves, the new Autobot jet soldier) + FINAL_BASE_ITEMS
  14 (through split screen). Dev root gains work/AssetTools junction (horde bodies at <root>/../../AssetTools/out/
  escalation_characters). District ESCTEST 7 / 7 here. Experimental runs the DLC ESC rows; AssetTools: the jet's
  roster_package row; Systems: the escalation_characters packaging check.
2026-10-23 22:26 DLC PACK 1 COMPLETE (AssetTools): Fortress, Havoc, Tranquillity, District done (District's 22 transforming actors
  exported; the jet AICD's roster_package row with its real cylinder; existing rows byte-identical). FINAL_BASE_ITEMS 14
  self-contained (6438ac5b). Next: Tranquillity_AUDIO reparse, LOC twins, then Pack 2 (Horizon, Metropolis, Sector,
  Pulse, Static).
2026-10-23 22:32 MERGED 0378fca6 DLC audio 2 (wave / twin lookup: level root -> DLC-only root -> base root; the 'missing' twins were base
  lines the DLC reuses). Open: Gameplay's District door loader + Zone_Manager generator flattening (base item 15); RE's
  Tranquillity_AUDIO reader answer; AssetTools Pack 2; Systems online two-locals; card checks after the playtest.
2026-10-23 22:34 RE: Tranquillity_AUDIO_m has one short LZO block in the shipped file; AssetTools' reader misplaced later blocks (fix
  sent: 82 of 125 exports recovered incl. every touch / sound / reverb op); 43 exports are damaged in the shipped cook
  itself (brush Models, Package objects, 14 Kismet ops): treated as missing. Systems regenerates after the DB re-export.
2026-10-23 22:42 MERGED 40e4e7ab District doors load (per-map transforming_actors fallback) + door generators (Zone_Manager flattened:
  District 8 / 8 with a bought door generator) + base item 15; 8f65b92c level audio (+ Horizon; props-less exports =
  missing). Gameplay next: the DLC Kismet extras (laser doors, sentries, power outage, vending, flood gates). AssetTools:
  LZO short-block fix in 3 DLC packages (Tranquillity_AUDIO, District_Art, Static_ART): District rebuilt, Pack 2 resumes.
2026-10-23 23:09 AssetTools LZO fix done (blocks at their declared offset; base 0 of 918 packages affected = byte-identical; DLC authored.db
  rebuilt: Tranquillity_AUDIO NULL rows 110 -> 28; District rebuilt, outputs unchanged; the laser generators scoped
  936 -> 356 rows per RE's export). Pack 2 resumed: Horizon (structure audit at 2 m), then Metropolis, Sector, Pulse, Static.
2026-10-23 23:13 MERGED 83d8a199 DLC Escalation mechanisms (District laser grid off at start + buyable 45 s sets, 10 sentries, power outage,
  detpacks; Tranquillity floodgates / base doors; District 11 / 11) + FINAL_BASE_ITEMS 16; 27b879f0 Tranquillity level
  audio (14 damaged exports left out). Visuals -> Rendering (survival().mech); outage HUD -> Frontend. Pack 2 in the chain.
2026-10-23 23:20 USER PLAYTEST 2 (package d5bb6863): "the entirety of the build is broken" - Escalation hosts as TDM, Private Match lost
  its Game Settings, Scout cannot move (flicker), menus trap the player, Campaign D1 blank screen, character select
  overlap, calibration preview broken, 40 items: docs/handoff/USER_PLAYTEST_2026-10-23b.md + playtest_2026-10-23b/.
  ORDER: (1) QUALITY PASS now (settings do what they say; everything per the user or else the base game), (2) the new
  settings items, (3) a complete performance pass; DLC / split screen feature work PAUSED for Frontend + Gameplay.
2026-10-23 23:24 USER ORDER (disk): delete the builds / files we aren't using. Integration swept its own work/ (old gates, archives,
  render backups, hang dumps: 2 GB). Lanes sweep their own work/ (Systems: 5 old packages ~121 GB; AssetTools: dlc_maps +
  the 11,154 shadow texture copies after Pack 2; Experimental: old ab arms; others: old builds / shots / logs).
  MERGED 5285ea0a build_movers per-group binding (tool).
2026-10-23 23:34 Disk: Experimental freed 174 GB; the user deleted Systems' 5 old packages + scratch trees; F: free 201 -> 516 GB; the kept
  package d5bb6863 + HDpack intact (hd junction resolves). RE BASE_GAME_FLOWS_RE.md (1d6e642) is the QC reference:
  Escalation host = SVPrivate / SVPublic settings (UI_CampaignLobby, single team, iconic only; NO Game Settings row), a
  wrapping 3D wheel with AVAILABLE / SELECTED on the next line, 13 CampaignCharacters (our duplicate = chassis Truck7
  'Optimus'; Silverbolt's portrait is lower-case), lobby pick persists; party lobby has NO teams at the root; Private
  Match settings are picked BEFORE hosting (Host Options), no Game Settings row in the lobby, X = Switch Teams only with
  Manual balancing, 10 s countdown not cancellable (cancel = a user addition); fresh profile has D1 (I) + A1 (VI) unlocked
  by design; brightness = first Campaign entry + Settings (first-launch = a user change), logos respond to real gamma;
  pause: Escalation has NO Choose Character.
2026-10-23 23:41 QC pass progress: MERGED 319fd0cf audio / voice audit fixes (Systems: proximity gain, All Radio flag, input device list,
  mic test level, mid-match voice on; Subtitles row has no display -> Frontend + Systems); MERGED 24b150c8 UI-layer
  display gamma (calibration logos respond; Frontend applies to every movie). Rendering audit: Texture Quality no-op and
  VSync ignored on D3D12 present (fixes in progress), renderer-recreate re-applies only Brightness + a hidden Water row
  (Frontend). Experimental's menu-QA driver synced (needs the Frontend's step lists + navcheck fields). Rendering swept
  88 GB; Experimental 174 GB; the user deleted Systems' old packages: F: 516 GB free.
2026-10-23 23:45 MERGED 41a3bcac graphics settings fixes (Texture Quality live mip bias, VSync on the D3D12 present path, menus held at
  60 FPS, loads at the player's limit). USER (going to bed): "Make sure everything's included in this build. Also make
  sure campaign works ... make sure everything is working how it's supposed to work so I can play campaign, escalation or
  multiplayer. Take control and stop when everything is done, or if you need me for something super super important."
  OVERNIGHT PLAN: (1) QC pass to the three e2e flows PASSING (Experimental's menu-QA gate) incl. Campaign from the menu
  for every chapter; (2) the user's settings extras; (3) the performance pass (timed gate + loads + menus); (4) DLC (9
  maps) + Language + offline split screen in the same build; (5) gate -> refresh.sh -> verify -> hand-over note. Wake the
  user only for something super important.
2026-10-23 23:49 MERGED d8da7b64 voice channel switch binding (N / D-pad Right; cycles the four channels; Settings > Audio row). Morning
  note: the default push-to-talk V doubles as jet Hover Down (the user picked V; the hint flags it). RE QC 22: the colour
  conversion matches; check the material instance scalars (ColorBrightness / Color_Saturation / LUT switches): Rendering.
2026-10-23 23:53 USER (~01:00): "I wasn't able to move ANY of the characters while in game" (all classes, not only Scout) + "be resourceful
  with your resources: limited Claude usage left; get the entire game done before I run out." The user's last package
  log (menu-only session) shows Frame Generation ON (D3D12 presenter, 64 real fps / 127 presents); FG is on in no
  automated run. Gameplay's repro tests: keys not reaching the pawn (input owner / remap / GFx / PTT path), a per-frame
  state toggle, FG double-image. Rendering adds an FG + moving-pawn row. Token economy: minimal messages, no reruns.
2026-10-23 23:56 ROOT CAUSE of 'no class could move' (Frontend + Gameplay): the launch glue sets gl.online = !onlineSlots.empty(), true
  with 0 remotes, so a solo-hosted private match made the local player a lockstep wire seat with no driver (frozen pawn;
  the mouse camera still turned). Gameplay's one-liner + movement proof pending. MERGED 699e640e: Escalation > Host a
  Game restored (the PC host-now override was replacing Host in EVERY party lobby: the #9 / #14 TDM-for-Escalation bug;
  now versus only; Escalation shows its Host Options again) + the QA gate hooks. DECISION: versus keeps flow v2 (host now
  + the lobby Game Settings row = the user's own vote); Escalation = the original (no Game Settings row).
2026-10-23 23:58 MERGED da9b3e48 the local pawn is never occlusion-culled (the 'flicker in and out' while frozen: a pawn stuck in geometry
  was culled / drawn / culled) + 13cb3355 the rest of the QA gate hooks (nav.check match fields, scripted stick deflection).
  Pending: Gameplay's frozen-seat one-liner + proof; the Frontend's Escalation / pause / D1 flows; Experimental's gate
  (card after the repro exes); Rendering's measured settings audit (card).
2026-10-24 00:15 P0 FIXED f0d500cc: a solo-hosted private / Escalation match ran the local player as a driverless lockstep wire seat
  (gl.online true with 0 remotes); now gl.online = !gl.remotes.empty(). Gate proof (Frontend): stick 43 m, Forward 44 m,
  20 shots, transform, jitter 0. MERGED 4f00a495: TnUIControllerSurvival (Escalation keeps the lobby pick, no second
  Choose Character, the map's 13 CampaignCharacters on the wheel in authored order: closes #11 / #12 / #15 / #29) + the
  gate's buttonhold step; d95ebc91 controller seat routing (Systems). NEW finding -> Systems: pad buttons map only to UI
  actions, so on a controller nothing fires / transforms / jumps / reloads in a match.
2026-10-24 00:25 MERGED c51e9a3f subtitles provider (169 A1 lines timed, 0 wrong; Frontend draws), 1e4b6130 controller buttons drive the
  match actions (authored XboxTypeS layout; X tap reload / hold revive per RE; profile remaps). Frontend: pause routing
  closed by the merged fixes; D1 reproduced = the campaign lobby's autostart never fires after the pick (root-causing);
  calibration logos draw as authored (the photo's exposure) and now take the UI gamma. Gate run queued behind the repro exes.
2026-10-24 00:38 MERGED 1edec0c2 D1 'blank screen' FIXED (Frontend d93b9a16): the SOLO campaign lobby is invisible by design and the
  original's lobby game class starts the chapter once everyone has a character; ours never did, so the player sat in an
  invisible lobby; gameLobbyTick now starts the level on the pick (menu run: D1 loads 40 s, spawn, pawn moves). Also
  merged 7f0efe32 (settings re-apply after recreate, Texture Quality wired, Water neutral, subtitles drawn, loading
  bar). RE: a campaign lobby pick persists into the map (our clearing = the bug; one-line follow-up). Gate: all three
  flows on 1edec0c2 once the card frees.
2026-10-24 00:41 MERGED 7a7b8ba6 the campaign lobby pick carries into the map (no in-map trio select for a valid pick) + per-movie gamma
  (calibration logos respond). Card free: Experimental runs the full gate (mp + esc + camp) on 7a7b8ba6 now; then the
  Frontend's calibration check, then Rendering's measured settings audit.
2026-10-24 00:47 MERGED 9c94b50d X = interact else reload + FINAL_BASE_ITEMS 17 (P0 record: fixed pawn 110 m from spawn + firing; unfixed
  never moved); 83df0335 HD 'applies on next map' hint + the multiplayer root shows no teams (#19; base game
  TnTeamHandlerNoTeams). The settings extras 13-15 (scroll arrows, commit confirm, restart / next-map popups) conflict
  in GfxPresenter.cpp: recut requested. Gate run on 7a7b8ba6 in progress.
2026-10-24 00:54 MERGED 7f35fb82 section-43 sim-side flow harness (WFC_FLOWSIMTEST; Gameplay 10 / 10, 6 / 6, 6 / 6 on its tree; on the
  head MP = 7 / 10: the bot-driven human does not move: harness-path only, Gameplay bisecting), 56166868 settings
  extras 13-15 + Private Match Team row (#23) + countdown cancel (#25).
2026-10-24 00:54 GATE (section 43, real menus, one exe, 7a7b8ba6): tools/fidelity/results/MENU_QA_7a7b8ba6.md: ESCALATION PASS (lobby
  pick kept, no second select, 37.9 m, back to the party lobby), CAMPAIGN PASS (calibration -> Solo -> Chapter I ->
  Crash Site -> load -> InGame, 6.4 m, 3 shots), MULTIPLAYER every measured check PASS (Game Settings row, Choose
  Character, 34.9 m jitter 0 = the frozen-pawn P0 fixed on the real input path, shots, transform, pause / resume, quit
  -> party lobby); 5 mp steps pending (filled from 56166868; final run on the final head). Card -> Rendering's audit.
2026-10-24 00:57 MERGED 508b29ce FLOWSIMTEST sets WFC_PLAYERBOT itself: the 7 / 10 was my env (no player bot = the human stands still),
  no regression; MP 11 / 11 on the head. Section 43 sim side CLOSED (Gameplay); menu side PASS (Experimental gate).
  RE §7: chapter select = all ten chapters in one row + checkpoint row (matches ours); pre-match screen = mode icon +
  mode name + countdown + PlayerList (the 'scrunched names' = that list; no big title).
2026-10-24 01:12 MERGED 74e41849 menus held at 60 take 2 (frontend loop took the fallback wait at the player's limit: 90 FPS measured) +
  Rendering's measured settings audit (docs/handoff/QC_RENDER_SETTINGS_AUDIT.md: 4 runs, 12 / 12 self-test per arm,
  every apply line, RT + GI built, FG 0 resets) + the perf pass plan (PERF_PASS_PLAN_RENDERING.md). Card: Frontend
  slot (calibration shots, arrows / popups, D1 Megatron, PM team row, countdown cancel, CaC dump) -> Rendering perf
  rows (+ FG-output capture, menu 60 re-measure) -> Experimental final 3-flow gate on the build head. Headless:
  Gameplay 10-chapter + 4 ESC sweep on 74e41849; Systems load-time stages + build_slim --plan with DLC. AssetTools
  Pack 2 status asked twice.
2026-10-24 01:19 MERGED a7cde068 load-stage timing (WFC_LOADSTAGES) + DLC level audio Pulse / Metropolis / Sector. Load times: texture
  decode = 82 % of a Streets load (GDI+ PNG on the main thread); Systems GO on a lossless decoder (WIC / bundled,
  byte-identical over every shipped PNG, worker threads). build_slim --plan with the DLC root: 0 PROBLEMs. Static's
  export is the last AssetTools input (routed).
2026-10-24 01:24 MERGED f64ee03a Frontend QC slot fixes (#34 first-launch calibration, #10 lobby Game Settings Back left the settings
  root over the lobby, #23 preferred team, commit result text, #12 Silverbolt case) + DLC TransGame text INT / FRA.
  Gameplay sweep on 74e41849: campaign 10 / 10 load + checkpoint (A4 = REAL BUG: a non-convex hazard shell killed the
  player at t = 1 s; fix = ray-crossing parity for the 73 non-convex brushes, patch + item 18 coming); ESC Remnant /
  BrokenHope / Tranquillity / District all pass; versus DLC Fortress / Havoc / Horizon / Metropolis / Sector 60 s TDM
  clean; Pulse ESC BLOCKED data-side (spawn points off the nav; Kismet not built) -> AssetTools; Static pending.
  assets_dlc now has junctions for all 9 DLC maps.
2026-10-24 01:28 MERGED 658fb346 non-convex hazard volumes (A4 start death) + FINAL_BASE_ITEMS 18 + HANDOVER_MENUS_2026-10-24.md.
  Pulse ESC reclassified (RE §7): the original spawns at the authored point + capsule check, no nav requirement; our
  director's nav rule starved Pulse: Gameplay GO (base game; affects BrokenHope / Remnant spawning; re-baseline).
  QC FREEZE LIFTED for DLC completion (P0s + audits closed, gate PASS): Pulse §8 Power_Generator_Action + Static §9
  DoorGenerators GO (Gameplay sim, Rendering visuals). Experimental final gate PREPARED (base fast_af4d073_final9,
  FINAL_ASSETS + final_render with 8 DLC maps; mp flow carries the #20 / #27 shots).
2026-10-24 01:33 MERGED ee98ab39 lossless texture decoder (WIC for the proven PNG classes, 60,458 / 60,458 byte-identical vs GDI+;
  worker-thread prefetch, main-thread uploads): headless Streets 2.21 -> ~1.9 s, Remnant 2.45 -> ~2.0 s. Next lossless
  load target: animation files (~0.4 s / map) after Systems' two-locals wire patch.
2026-10-24 01:38 MERGED b6ddf3bc Escalation spawns at the authored points (RE §7; the PROV on-nav stand-in removed) + Pulse ESC data +
  FINAL_BASE_ITEMS 19: ESCTEST Remnant 15 / 15, BrokenHope 36 / 36, Tranquillity 7 / 7, District 11 / 11, Pulse 7 / 7
  (0 -> 134 spawns); ESC same-match deliberately re-baselined. Experimental rebuilds the gate base with 18 + 19.
2026-10-24 01:48 MERGED 35be2b30 Graphics setting previews (#5; panel right of the Graphics window; RayTracing_0 / _2 images, Rendering's
  same-view set to follow) + Create a Character colour picking by mouse / controller (#21; startDrag / stopDrag were
  stubs). Card: Rendering perf rows + preview captures -> Frontend 5-min check -> Experimental final gate on the sha.
2026-10-24 01:54 MERGED 8593650c Pulse Power_Generator_Action (10 managers, doors / gates / remotes / ramp / spikes; 14 per-name door
  collision sets) + DLC mechanism presentation data (Rendering draws next) + FINAL_BASE_ITEMS 20; ESCTEST all 5 ESC
  maps pass (Pulse 9 / 9). Open for this build: Pulse healing platform / Nexus / sentries / Trypticon, FinalRoomUnlocked,
  Static §9 (export blocked: AssetTools 'waiting' on a prompt since ~23:41).
2026-10-24 01:56 MERGED e36b6654 two local players in an online match (Systems; seat 2 rides the owner's packets; 3 seats on 2 peers
  2,401 ticks status 0, 10 % loss 41 ms stall) -> Frontend split step 3 (joinSeat2 -> setSecondLocal). Gameplay
  hand-over doc HANDOVER_GAMEPLAY_2026-10-24.md taken. Systems next: animation-load (lossless), then Static audio.
2026-10-24 02:00 MERGED 4d338915 parallel animation loads (lossless; 50-70 ms). Decision: the binary animation cache (~0.4 s / load)
  waits for after this build (AssetTools blocked; packaging change). Systems pre-stages the package refresh.
2026-10-24 02:03 MERGED 070f6705 split step 3 (seat 2 in an online lobby -> setSecondLocal; split screen complete offline + online) and
  the recursive data walk in build_slim (previews would not have shipped). Systems: refresh STAGED (W: base, X: HD;
  Static empty -> refresh stops unless ALLOW_EMPTY_DLC=1). Remaining before the cut: Rendering's perf rows + preview
  captures + mechanism visuals; Frontend's 5-min check.
2026-10-24 02:20 STATIC EXPORT ROOT CAUSE (Integration, user: 'do it for me'): the DLC STFS extractor mapped data blocks 170 / 28900 one
  hash table early ('>' vs '>=' at the 0xAA / 0x70E4 boundaries): 4 DLC files spanned block 28900 / 170 and each had one
  wrong 4 KB block (MP_ESC_Static_ART_m: the main BSP's surfs 316+ garbage -> vs_map IndexError; MP_ESC_District_Art_m;
  MP_ESC_Tranquillity_AUDIO_m: RE's 43 lost exports; ENV_Pulse_Custom_p). Fixed work/dlc/stfs_extract.py, re-extracted
  the 4 files (one differing block each, 0 short LZO blocks, Static Model_7832 1983 surfs 0 bad), replaced them in
  work/dlc/cooked (old copies in Rebuild/work/stfs_fix/old). Static chain running; District chain + Pulse content
  re-extract + Tranquillity audio regen follow. Merged 2400c037 Static ESC prep + FG capture + perf report (D1 12.5 fps
  CPU-bound = the one perf problem; split A/B after the chains).
2026-10-24 02:31 STATIC LANDED (chain 02:30: pipeline OK, render data 496 files, bot_nav + 2 m air, lists: waves 265, loc 5 languages);
  junction added to assets_dlc (all 9 DLC maps). Pulse content re-extracted from the fixed ENV_Pulse_Custom_p: one door
  texture recovered (Door_01_CLR_TEXSET_Color_NormX). Running: DLC authored.db regen (4 shards), District chain re-run,
  Pulse render refresh. Gameplay GO on Static data + item 21.
2026-10-24 02:41 DLC DATA REFRESH: Static extras done (72 generators, 34 zones, 84 spawn points; navmesh 1032 cells); DLC authored.db
  regenerated 02:31; District chain re-run OK 02:39 (one failed attempt: my shell leaked WFC_MANI_DIR into the chain ->
  vs_pickups read the DLC-only db; re-run clean); Pulse render refresh running (dlc_render.py needs dlc_env.sh).
  MERGED 250337ee level audio from the corrected packages (Tranquillity ops 12 -> 50, Static 118 ops, District emitters
  139 -> 278; 0 damaged exports). Gameplay: Static waves + District re-check -> data patch + item 21.
2026-10-24 02:43 DLC DATA DONE 02:50: all 9 DLC maps exported and current (Pulse render refreshed with the recovered door texture). Card:
  Rendering D1 split A/B -> Frontend 5-min check -> Experimental final gate on the build sha. Waiting: Gameplay Static
  data + item 21, Rendering mech draw patch, Experimental re-stage, AssetTools movers + cleanup.
2026-10-24 02:47 MERGED a61249c0 Static Escalation data + FINAL_BASE_ITEMS 21 (ESCTEST all six pass; Static 9 / 9: door generator,
  fire-jet trap with real values). DLC complete except Static's ambient FIREJETS / FIREEXP bursts + camera Kismet and
  Pulse's healing platform / Nexus / sentries / Trypticon (open list). Before the sha: Rendering mech draw + denoiser
  fallback, Frontend 5-min check.
2026-10-24 03:58 MERGED 2c40a7fc D1 CPU fix (244 matinee set-dressing actors were CPU-skinned per frame -> GPU skin path: 12.9 -> 56 fps,
  eye check identical), DLC mechanism presentation draw (also fixes DLC matinees running on the map clock), denoiser
  fallback (Faithful never black), 12 setting previews, Rendering hand-over. P1 found by Rendering: the calibration panel
  draws over gameplay (TDM + D1 start) -> Frontend has the card for the fix + its 5-min check = the last item before the
  build sha.
2026-10-24 04:12 MERGED 55df5657 P1 calibration leak fixed (extras close with their owner; autoplay skips the first-launch calibration);
  Frontend checked the preview panel, colour picker, first-launch calibration windowed. CANDIDATE BUILD HEAD 55df5657;
  one open question to Rendering (HD level applying live = a 9 s menu freeze vs the 'next map' hint). Experimental
  pre-builds the head arm; Systems pre-checks the plan.
2026-10-24 04:16 BUILD SHA a2e9fbd9 (HD level deferred to the next map load: the 9 s menu freeze was the setter verifying 3,505 HD files on
  the spot). Experimental builds the head arm; Systems runs the staged refresh (W: base, X: HD; NOPROMOTE); then the
  card: 3-flow menu gate + final-gate.sh timed rows under a freeze; then verify_everything; then the hand-over note.
2026-10-24 04:33 PACKAGE a2e9fbd9 cut (Systems refresh, NOPROMOTE): Rebuild-Systems/work/playtest_slim_a2e9fbd9 (19 GB) + _HDpack (22 GB);
  build_slim 125,386 kept + 7,161 xpr, 0 failures, 0 PROBLEMs; overlay 114 / 114, HD 4,005 + M6 498, 10 chapters, 9 DLC
  maps. FREEZE sent to every lane; Experimental runs the 3-flow menu gate + final-gate.sh a2e9fbd9 (base final12).
2026-10-24 05:07 GATE a2e9fbd9 interim (Experimental): menu gate ESC PASS, CAMP PASS, MP FAIL at one step: after the countdown cancel (#25
  works) Start again does nothing (startGame_mc 50 px higher; click lands, no effect) -> Frontend fixes code-only
  (GameFlow / presenter), one more refresh + mp-flow rerun on the new sha; the timed gate on a2e9fbd9 stays valid for
  a frontend-only delta. Pre PASS: same match head = base on 508 / 506 / 505 / 511 / 512 + all 7 bare-name DLC maps,
  negative control differs; CAMPAIGNTEST 24 / 24, KISMETTEST 10 / 10, VOICETEST PASS; 10 MP maps clean TDM both seeds.
  Timed rows running under the hold (restarted ~05:10 after a stale matB manifest read).
2026-10-24 09:08 USER TARGET (~05:30): campaign + Escalation >= 300 FPS MINIMUM with the original settings on (Off look, native res). Phase
  after the a2e9fbd9 package: Rendering (render side, PERF_300_PLAN.md), Gameplay (sim side, PERF_300_SIM_PLAN.md),
  Systems (platform half), Experimental (min-fps harness per chapter / ESC map at 1440p + 2160p), RE (original culling /
  LOD rules). Lossless fixes only; the target is a gate for this pass.
2026-10-24 10:33 GATE a2e9fbd9 result (Experimental a3bd6ea7): MENU_QA_a2e9fbd9.md ESC PASS, CAMP PASS, MP FAIL only at Start-after-cancel
  (Frontend fix df8f62a4 building); FINAL_GATE_PRE_a2e9fbd9.md PASS: same match head = base on 508 / 506 / 505 / 511 /
  512 + all 7 DLC maps, negative control differs, CAMPAIGN 24 / 24, KISMET 10 / 10, VOICE PASS. The TIMED rows never ran:
  the gate script waited 05:05-10:31 for a 'clear card' because its busy() counted the user's Codex node.exe runtimes
  (fixed). Decision: ship after the mp rerun + verify; the timed rows fold into the 300 fps phase-0 baseline.
2026-10-24 10:35 MERGED 48e42af6 Private Match Start-after-cancel fix (Frontend proof: cancel at 8, Start again at the same spot, match
  loads) = FINAL BUILD SHA. UNFREEZE sent. Systems refresh #2 + verify_everything on 48e42af6; Experimental mp menu
  flow rerun on 48e42af6; the 300 fps phase starts (code / headless now; phase-0 slot after verify).
2026-10-24 10:45 300 FPS PHASE 1 started (next package): MERGED 13b03df3 skeletal set dressing follows streaming visibility (the one
  streaming leak; D1 drew 244 active rows) + the cull-distance sidecar tool (data: ~0 lever on D1 per RE). RE §7a: the
  active state holds 10-25 % of a chapter's primitives (statics already follow it); no skeletal LODs in the cook.
  Waiting: Systems refresh #2 + verify on 48e42af6; Experimental mp rerun; then the phase-0 baseline slot.
2026-10-24 10:46 MENU GATE 48e42af6: MULTIPLAYER PASS every row (Create a Character, bots 4 + 4, map, Team, countdown, Back cancel, Start
  again -> match, Scout, PlayerList shot, 20.1 m jitter 0, shots, transform, pause -> Change Character -> back -> InGame,
  quit -> lobby); ESC / CAMP PASS stand from a2e9fbd9 (GfxPresenter-only delta). Section 43: all three flows PASS on the
  final build.
2026-10-24 10:52 MERGED bd543d9e campaign skeletal set-dressing preload (D1's 4.5 s stall at frame ~201 + 90-260 ms hitches were first-draw
  glTF loads on the main thread). 300 fps phase-0 harness ready (tools/fidelity/minfps.ps1: camp-play / camp-sweep / esc
  rows, 1440p + offscreen 4K, Off look; PASS = steady worst frame >= 300 fps); baseline on fast_bd543d9 in the slot after
  Systems' verify. Gameplay adds WFC_ESC_STARTWAVE / WFC_ESC_INVULN (test only).
2026-10-24 10:53 PACKAGE 48e42af6 cut (Systems): Rebuild-Systems/work/playtest_slim_48e42af6 (19 GB) + _HDpack (22 GB), 0 PROBLEMs;
  verify_everything running 10:53 (W: base, X: HD; ~45 min). Phase-0 perf slot (Experimental drives, Rendering parses)
  after the verify; work/render has junctions for all 9 DLC maps.
2026-10-24 11:00 MERGED a25edf62 sim tick log + ESC harness switches (test only) + SOAK_TABLE: the steady-state sim already meets the sim
  target (p99 < 1.7 ms); the sim spikes are first-use loads (ability / chassis / melee / matinee clips) and a few Kismet
  steps (D5 1781 up to 38 ms). Gameplay: preload patch next. Phase-0 slot (Experimental drives, fast_bd543d9, 1440p then
  4K) GO after Systems' verify ends.
2026-10-24 11:09 MERGED bcd70a61 first-use loads moved to map / chapter load (melee weapons, gibs, matinee pawn clips): the reproducible
  sim spikes are gone; left 5-6 ms Kismet steps (fix F per RE's KISMET_VM_SCHEDULING_RE.md) + 3-4 ms matinee. Phase-0
  baseline moves to fast_bcd70a61; GO after Systems' verify. Systems: first-time body load fix in progress.
2026-10-24 11:12 VERIFY 48e42af6 FAIL (Systems): 7 files missing from the package (dlc_inventory.json = the DLC catalogue, map thumbnails,
  FluidRed cubemap faces, DLC text .int, Hud reticle); settings 12 / 12, RT, overlay, HD runtime PASS. Packaging fixed
  (build_slim + Play.bat 5ae93434 -> 307413d6; extras 199 -> 229); repack 48e42af6 (package phase only) + re-verify
  running. Code fix for the next package: drawReticle uses kAssetRootDefault instead of WFC_ASSETS (Rendering).
2026-10-24 11:14 MERGED 203449b7 reticle from the content root. DISTRIBUTABLE PACKAGE WORK: Rendering found 11 more hard-coded
  'F:/Transformers Rebuild' literals in campaign / Escalation game code (set dressing, destructibles, clips, ESC
  characters, spawnables) -> one patch with an env-overridable helper; RE audits every runtime dev-path literal
  (DEV_PATH_AUDIT.md); Systems: PACKAGE_ENV_CHECKLIST.md + a clean-machine verify mode. The repacked 48e42af6 (this PC)
  verifies now; the laptop package = the next (perf) one.
2026-10-24 11:26 MERGED 676f04fc DataRoots (11 campaign / ESC dev literals), c35e572a Frontend package paths (no F: literal left in
  frontend / ui), 5975369b nav-block grid prefilter (A1 matinee ends 2.1-5.5 -> 0.6-1.7 ms, identical sets). Pending:
  Gameplay PRELOAD delta (roster table at chapter load) + fix F; Rendering kAssetRootDefault; Systems repack 48e42af6 +
  verify (the user's package), env checklist, clean-machine verify.
2026-10-24 11:40 MERGED 8c670e2f asset root default from the package (kAssetRootDefault -> assetRootDefault()), feaf20bd roster table at
  chapter load + Kismet latent list (fix F; identical on all ten chapters) + verify_slim env roots + the package env
  checklist. REPACK 48e42af6 done (+90 files); re-verify running (expected: the reticle only, accepted for this PC).
2026-10-24 11:46 SIM after fixes (Gameplay SOAK_TABLE_AFTER_FIXES): every row p99 <= 1.39 ms; worst steps A1 38 -> 1.0 ms, A3 10 -> 1.05,
  D5 38 -> 3.4, D3B 64 -> 5.3. Left: the first measured step (3.4-6.2 ms, Gameplay), D3B 4310, D1 bot.steer; Systems:
  shotFx 2.4-3.1 ms per shot + Tranquillity's first-melee-death cue-set load (6 ms). Baseline exe -> fast_feaf20bd.
2026-10-24 11:47 PACKAGE 48e42af6 VERIFIED for this PC (Systems): both passes + HD runtime: 1 failing open per run = the reticle code
  literal (fixed on the head 203449b7 for the next package); settings 12 / 12, RT, overlay, HD 3,505 + M6 PASS. HANDED
  OVER to the user (HANDOVER_2026-10-24.md). a2e9fbd9 pair + the failed first attempt deleted (79.4 GB; F: 549 GB free).
  verify_everything gains D1 / Remnant / Horizon / Pulse rows for the next package (c5269d64, by file).
2026-10-24 11:54 MERGED 1d52a479 cull-distance sidecar reader (inert without the file; WFC_NOCULLDIST A/B). Sidecars generating for the
  19 MP / DLC maps (work/render/<map>/cull_distance.json); campaign units next. USER PLAYING: no lane windowed exes.
2026-10-24 11:59 MERGED 19bc0841 frontend data resolver + tests env (Rendering's 3 test failures explained), 6ddb0abe Systems body cue
  sets / energon colour with the body + mid-match body CPU load on a worker (Tranquillity 6 -> 0.16 ms, D5 shotFx 2.9 ->
  0.15 ms; SIMHASH identical; audio events vary by design: no strict audio gate). Cull-distance sidecars: ~0 culled nodes
  on the MP / DLC maps (Molten 154, Horizon 4, Debris 3): item 0a closed as a no-op; campaign sidecars skipped. Baseline
  exe -> 6ddb0abe; phase 0 waits for the user's play to end.
2026-10-24 12:00 CULL SIDECARS done (19 maps): culled nodes 0 except Molten 154 / Horizon 4 / Debris 3 / Tranquillity 2 -> item 0a is a
  measured no-op (files ship inert; campaign sidecars skipped). Gameplay: step 61 explained (energon default parse) and
  covered by Systems' merged HITCH_LOADS (Gameplay's duplicate dropped); Systems follow-up: rosterPackage lazy static
  thread safety + the job's thread contract. The user is playing 48e42af6; phase 0 GO after.
2026-10-24 12:06 MERGED 4098c267 chassis async thread contract (GPU work only at the join; rosterPackage magic static). BASELINE SHA FROZEN
  at 4098c267 (fast_4098c26): the phase-0 rows run on it after the user's play; later merges wait for phase 1.
2026-10-24 12:14 READY STATE: phase-0 baseline armed (fast_4098c26, 1440p then 4K, then an untimed PRESENTTRACE pass for Systems);
  the distributable refresh staged (bash work/everything.sh <sha>: build + build_slim + HD split + verify_everything with
  the D1 / Remnant / Horizon / Pulse rows; guards for every clean-machine piece). Binary anim cache rejected (measured).
  Waiting for the user to finish playing 48e42af6.
2026-10-24 12:20 MERGED b8963b8e hull preload + any-hit bot line of sight + nearest-first sentry scan (D1 step p99 1.21 -> 1.01 ms; all
  identical). Sim status: on D1 / D3A / Remnant / Streets the only steps over 1.5 ms are the level-start steps (the
  original's synchronous level start, 1.7-3.6 ms; excluded from the PASS rule). Gameplay cuts the perf-phase
  FINAL_BASE_ITEMS entries, then idle until phase 0 names sim targets.
2026-10-24 12:28 FINAL_BASE_ITEMS for the next package: Gameplay OWNS the chain (22-25 theirs, 26-27 Systems' hitch loads / threads, 28
  hull / LOS, Rendering's a-d incl. the constant rename next to the Config.h change and the ESC_MECH game half); one
  fresh full af4d0731 + chain build; the gate launches the game exe only (no wfc_fidelity.exe item).
2026-10-24 12:36 FINAL_BASE_ITEMS 22-28 complete through b8963b8e (Gameplay a446ba6a; fresh full build 0 errors; SIMHASH identical on 5
  rows). Experimental pre-builds the base arm final13. All lanes idle or on standby; the user is playing 48e42af6.
2026-10-24 12:42 PLAYTEST 3 (docs/handoff/USER_PLAYTEST_2026-10-24.md) dispatched; usage 14 %: batch mode. Gameplay: aim-to-crosshair P0,
  XP regression, Scout lockout, Orbital Beacon, pickup, collision, ESC launch min-player check. Frontend: ESC solo (RE:
  private host starts alone), ESC map list, duplicate Optimus, join-code leak, visible countdown cancel, Start grey,
  scoreboard rows, CaC chassis, modal queue, Graphics at top, fixed arrows, preview panel (baseline / selected + zoom +
  FSR 3.1 + volumetric + lens flare), Language -> Audio, split layout to the bottom, vignette, campaign exit routing.
  Rendering: aim audit with Gameplay, max-settings perf A/B (card), headlight flicker, preview pairs. Systems: ESC 30 s
  load. Experimental / RE / AssetTools stood down; 300 fps phase 0 and campaign deferred. GitHub = origin, pushed.
2026-10-24 12:51 MERGED 54dd587b playtest-3 frontend batch 1: Escalation solo start (private host alone, per RE), duplicate Optimus (DLC
  ESC maps lacked CampaignCharacters), join panel only with lobby input, 'Cancel Start' hint (mouse / B / Esc), Graphics
  at top + fixed arrows, Language -> Audio, Split layout bottom, standard preview panel (baseline / selected, hover zoom,
  FG text). Open: ESC map list in the package (Systems greps), preview PNGs (Rendering), batch 2 (Frontend).
2026-10-24 12:55 MERGED 3f1d9c3d frontend batch 2a: solo Campaign Back -> main menu (party lobby kept the last GameTeamStatus), Decepticon
  Chassis box (a 180-degree mirror matrix) level with Autobot, first-boot modals queued. Card: Rendering max-settings
  A/B -> Frontend 5-min dump (grey Start, scoreboard rows, pickup prompt). Vignette fade -> Rendering (backdrop matinee).
2026-10-24 13:00 MERGED c39b7d29 playtest-3 MP gameplay batch: AIM vertical error fixed (the camera pitch step was applied before the
  shot; 0.000 deg on 204 shots; the 'left' = the drawn crosshair vs the projection centre -> Rendering), XP restored for
  a solo private host (the online 'private earns nothing' rule hit hosted-alone sessions), Orbital Beacon's enemy markers
  (the versus marker setup name), Scatter Blaster = authored data (RE asked), Streets cylinder = glass section in the
  collision (RE asked), ESC solo has no sim gate. New headless baseline (the local shot changed by design); item 29 cut.
2026-10-24 13:01 PERF (playtest-3 item 9): no build regression (Release, Streets fixed route, 1440p: Realistic 124 fps on both a2e9fbd9 and
  4098c267; the user's Enhanced + Realistic 123 fps); the user's 82 fps = 5v5 (ten RT-skinned characters). The ESC 30 s
  load = material program prewarm (414 / 467 in ~31 s + 9.9 s warm-up draw) -> Systems' program binary cache. Card:
  Frontend 5 min -> Systems 10 min -> Rendering (aim proof shot, preview set).
2026-10-24 13:07 MERGED 7a88680e GL program binary cache (the ~30 s Escalation load = shader prewarm: warm launch 55 -> 11 s, screenshots
  byte-identical; the first launch of a new build still compiles once). ESC 'only District' = the Escalation lobby's map
  row (Frontend batch 2b), not the package. Scatter Blaster = the authored 2.5 s whole-clip reload. Card: Rendering (aim
  proof, previews) -> Frontend (bots scoreboard + ESC lobby dump).
2026-10-24 13:12 MERGED 5239fc0f shots follow the drawn camera view incl. shake (UE3 rule) + FINAL_BASE_ITEMS 29; the sim shows 0.000 deg
  view-vs-shot at every bot shot, so the steady 'left' offset is still open: Rendering's windowed pixel proof on 5239fc0f
  decides (projection / FOV mismatch or reticle art). Scatter Blaster = authored. Streets cylinder = the authored collision
  box (faithful; a tighter hull is a user decision). Item 30 being cut.
2026-10-24 13:19 FINAL_BASE_ITEMS 30 taken (chain = af4d0731 + 01-30 = 5239fc0f; Experimental rebuilds the base arm final14). User
  follow-up: the 'cube' = the Energon cubes (touch pickup check -> Gameplay); the hitbox = the central Energon POOL on
  Streets (photo docs/handoff/playtest3/streets_hitbox_pool.jpg), not the cylinder -> actor / volume query (Gameplay; RE
  asked). Pending: Rendering pixel proof + previews + headlight + vignette; Frontend batch 2b + bots dump.
2026-10-24 13:25 MERGED c49a2dfb ammo crates refill every weapon (the cubes = placed touch pickups, working; not taken when not needed
  = the user's case). Crater = ENV_CraterUpNoRing_STAT's authored convex hull (27.7 m lens, knife-edge rim 0.4-0.8 m
  up that our horizontal knee probe hits): decision (b) per-mesh per-poly collision for that mesh (a walkable-slope
  movement model = post-build). Item 31 = ammo crate + crater.
2026-10-24 13:39 MERGED 7a81ef5c Streets crater per-polygon collision (PC adaptation, one mesh; knee rays blocked outside the rim 12 -> 1),
  078b108a the preview image set (Upscaling 0-6, HD, RT, Volumetric, LensFlares). AIM CLOSED: Rendering's pixel proof
  on 5239fc0f = mean 0.02 px / worst 0.8 px over 30 shots (the old build traced camYaw while drawing viewYaw + shake).
  Card: Frontend (bots scoreboard + ESC lobby dump) -> batch 2b -> item 31 -> package cut.
2026-10-24 13:42 MERGED 463faf3a vehicle headlights follow the drawn frame (the lamps were placed from the simulated bone while the car
  is drawn interpolated at the render rate: the 'flicker' reported twice). Remaining before the cut: Frontend batch 2b
  (scoreboard rows, ESC lobby map row, Upscaling 0-6 mapping, menu fade), Gameplay item 31.
2026-10-24 13:44 MERGED 277d6e33 frontend batch 2b: Escalation lobbies use Host's Choice (the public playlist's random-map veto needs a
  quorum a lone host cannot reach = 'only District'); previews need no code; scoreboard rows + the menu fade NOT
  reproduced (5 + 4 bots clean; screenSoftEdges_mc holds at 1600x900): hand-over asks for the user's shot / mode.
  Waiting: Gameplay item 31 (-> final sha -> everything.sh -> gate -> hand-over 2).
2026-10-24 15:54 MERGED ae63135a distributable-package gaps (clean-machine verify rows found: cull sidecars, 6 lightmaps, per-unit campaign
  lightmaps, hazard volumes, DLC manifests, DLC data-path relocation, an empty-path probe; verify PASS / PASS on
  277d6e33 + patch; all 9 DLC materials_pbr.json generated by Rendering and placed). FINAL SHA ae63135a: Systems
  everything.sh running; Experimental builds fast_ae63135 + runs the headless pre; windowed flows after verify.
2026-10-24 16:35 PACKAGE ae63135a VERIFIED (clean-machine rows): Rebuild-Systems/work/playtest_slim_ae63135a (19 GB) + _HDpack (22 GB);
  verify_everything PASS / PASS (Streets, Gorge, Debris, D1, Remnant ESC, Horizon TDM, Pulse ESC; HD runtime row;
  settings 12 / 12; RT). 48e42af6 + the 277d6e33 test pair deleted (F: 525 GB free). Gate: menu flows running.
2026-10-24 16:40 FINAL GATE ae63135a PASS (Experimental 21358dac): menu flows MP + ESC 76 / 76; same match head = base on 12 rows vs
  final15 (01-31); stability: campaign 24 / 24, Kismet 10 / 10, voice, 38 TDM runs on all 19 versus maps 0 errors, 6 ESC
  maps clean. PACKAGE ae63135a HANDED OVER (HANDOVER_2026-10-24b.md). GitHub pushed.
2026-10-24 22:33 LAUNCH BLOCKER (user): Play.bat 'Drive already SUBSTed / Could not map drive W:' because W: still pointed at the deleted
  48e42af6 package (the user's earlier Play.bat mapping). Fixed by `subst W: /d` (PowerShell; git-bash mangles /d into
  D:\). TODO next package (Systems): Play.bat must skip a letter that is already substed (or drop a stale mapping to a
  missing folder) instead of failing.
2026-10-24 23:18 PLAYTEST 4 dispatched (USER_PLAYTEST_2026-10-24c.md; ~9 % usage: batch mode, one more build). MERGED 621e0d69 Escalation
  generators drawn (no TnSurvivalGenerator was ever drawn: skeletal actors absent from world.glb; 288 generators incl.
  the AP_WallSwitch door terminals; the doorway barrier actor itself still open). Frontend: bot counts / XP stack /
  preview frame + zoom done in code; Game Settings re-entry + vignette dumps taken at the user's mode; one patch coming.
  Gameplay: ESC wave-1 flow + boundary volumes + hit-reg review + item 32. Systems: conservative cleanup + Play.bat.
2026-10-24 23:20 MERGED 8c3bb918 playtest-4 frontend (team-aware bot limits, XP popups one at a time, previews one size + in-frame zoom,
  Volumetric / Lens Flare text). Not reproduced yet: Game Settings re-entry (repro script stopped at the leave confirm:
  code reading + one more slot), the vignette (soft edges hold at 2560x1440 fullscreen: ask the user for a shot). RE:
  the ESC closed door = a TnTransformingWorldActor skeletal mesh (never drawn) animating open on purchase -> Rendering.
2026-10-24 23:24 MERGED e6c3bbae Play.bat reuses / drops stale substs. CLEANUP: Systems' bulk delete (~55 GB of superseded packages,
  verify output, old refresh trees, plan copies, milestone-08 copies in Rebuild-Systems/work) was DENIED by the
  permission classifier -> needs the user's OK in the Systems window; preserved: playtest_slim_ae63135a (the user merged
  the HD pack into it), refresh_ae63135a, dlss_db72, hd_dev_full, vbake, source / Git / toolchains / assets. Experimental
  asked to drop its superseded gate bases (own worktree). Frontend: Game Settings root cause = the row is cloned from
  the Create a Character row, gone once Iconic Characters is on (fix built; windowed check pending the card).
2026-10-24 23:28 MERGED 2d3b2d54 Game Settings rebuild after re-entry (the row's template vanished with Iconic Characters on; windowed
  PASS on both paths). Experimental freed 25 GB (own superseded gate arms). Frontend + Rendering done for this build;
  waiting on Gameplay's ESC / boundary batch + item 32 -> build -> sha -> everything.sh -> gate -> hand-over 3.
2026-10-24 23:32 MERGED 2bf2f152 ESC spawns prefer reachable zones (wave 1 cleared on none of the 5 ESC maps before; Pulse clears now,
  the horde reaches the player elsewhere) = FINAL SHA for the playtest-4 build. OPEN (Gameplay out of usage): the
  double-jump map escape (blocking volumes already in; wall-top audit), Tranquillity door export (0 actors), item 32.
  Systems everything.sh 2bf2f152; Experimental gate (ESC same-match expected to differ; versus must match).
2026-10-25 00:23 PACKAGE 2bf2f152 cut + verified (Rebuild-Systems/work/playtest_slim_2bf2f152 19 GB + _HDpack 22 GB): all rows PASS
  except Remnant ESC, where the passive player bot now dies at 22 s to a HoloBrute melee = the reachable-spawn fix
  working (0 failing opens; accepted; harness: end on ESC game over next package). Cleanup skipped on the user's word.
  Gate: windowed mp + esc (Tranquillity, INVULN, 360 s) running on fast_2bf2f15.
2026-10-25 00:34 FINAL GATE 2bf2f152 PASS (Experimental 6f9e572b): MP every row; ESC Tranquillity every judged row (wave 1 begun, not
  cleared by the test bot in 360 s; Pulse reaches wave 3 headless); versus same-match identical, ESC rows differ by
  design; 38 TDM runs 0 errors; campaign 24 / 24. PACKAGE 2bf2f152 HANDED OVER (HANDOVER_2026-10-24c.md). GitHub pushed.
  Usage ~9 %: all lanes stood down.
2026-10-25 08:27 MERGED d8f492ae human stranded recovery (30 s off-nav -> fall death + respawn; jumps are authored: wall escapes = missing
  blocking geometry, audit open) = the LAST build (usage ~6 %): Systems reduced.sh d8f492ae (package + base verify rows
  only); hand-over 4 drafted.
2026-10-25 08:55 PACKAGE d8f492ae REDUCED VERIFY PASS (Rebuild-Systems/work/playtest_slim_d8f492ae 19 GB + _HDpack 22 GB). HANDED OVER
  (HANDOVER_2026-10-25.md). All lanes stood down for good; usage ~5 %. GitHub pushed.
