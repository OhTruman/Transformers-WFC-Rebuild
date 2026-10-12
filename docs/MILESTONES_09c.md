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
USER 2026-10-12: "D all the way" - full pass top level is D (stronger mid/fine detail + relief), ladder D -> C -> B -> A -> original.
USER 2026-10-11 (superseded by D): HD full pass uses pilot variant C (4x + mid-band detail + stronger normals), per-texture fallback to B / A on
gate fails; HD-on uses Rendering's compressed sRGB S3TC/BC7 upload with a post-filter PWL correction (Rust 4x ~3.3 GB; budget 8 GB/map).
- M5 Upscaling + frame generation (D3D12 interop -> FSR 3 / DLSS / FG), optional, off by default.
  USER 2026-10-11: start after M3 ships; AMD first, then NVIDIA. Order (docs/handoff/SYSTEMS_M5_UPSCALING_FRAMEGEN_PLAN.md):
  1 D3D12 present stage 2 (flip-discard + tearing; also removes the +0.1 ms fullscreen GL flip gap the user hits)
  -> 2 temporal inputs -> B (3 shared targets, 4 FSR 3.1 SR, 5 post placement, 9 settings) -> 6 HUD layer
  -> C (7 FSR 3 FG, 10 frame cap) -> D (8 DLSS SR + DLSS FG via Streamline, checks on the RTX 5090 laptop).
  Option A (AFMF 2 driver FG) is a user-side Adrenalin toggle: offer it to the user as a test when they're home.
  D3 licensing: read the NVIDIA RTX SDK terms before shipping D.
- M6 Modern materials mode (optional).
- M7 Ray tracing (shadows + AO -> reflections -> GI), optional, off by default.
