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
  WEP / CHR hook rows (Systems) pending; in-game check + pairs after the restart. Stage 1 composite: RT shadows / AO on dynamic surfaces + the
  dynamic-light term only (option b: end of the opaque pass), lightmapped statics untouched except character contact AO.
  DLSS (M5 D): non-commercial project (user 2026-10-12; licensing revisited before any commercial release); the "NVIDIA DLSS"
  credit goes on the start-up splash, the About screen (if any) and the credits - clean and unobtrusive.
