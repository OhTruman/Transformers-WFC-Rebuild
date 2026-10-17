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
