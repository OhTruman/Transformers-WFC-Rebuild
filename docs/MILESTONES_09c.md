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
  DLSS (M5 D): non-commercial project (user 2026-10-12; licensing revisited before any commercial release); the "NVIDIA DLSS"
  credit goes on the start-up splash, the About screen (if any) and the credits - clean and unobtrusive.
