# Gameplay regression sweep, 2026-10-13 (state: complete — 2 bugs found and fixed, no open gameplay FAIL)

**User request:** "hunt for bugs, memory leaks, and any problems I have ever resolved or caught".

**Gameplay lane's share:** re-verify every gameplay / bot / mode fix with its original repro where automatable.

**Setup:**
- Release builds of integration archives in Rebuild-Gameplay/work/int_<sha>.
- Shipped renderer (WFC_RENDER_DATA = the integration tree's work/render, read only).
- Windowed, one exe at a time, untimed slots.
- Abort the batch on any Display 4101 (TDR).
- Each run's peak / last private memory is recorded.
- Logs: Rebuild-Gameplay/work/pass29/b51 (stuck), b52 (sweep), b54 (camera-cut proof).

## Verified

| Fix / issue | Repro | Build(s) | Result |
|---|---|---|---|
| Camera cuts (1604234f → integration 2013d67d): render-only notifications | Classic lockstep SIMHASH base vs patched (TDM_3, DM_5, CTF_7, TDM_11); PARTICIPANTTEST; WFC_MOTIONVECTORS + WFC_CAMCUTLOG | fb3fa288 ± patch | **PASS**: 4/4 identical over 3001 steps; 22/22 both arms; explicit "spectate" / "spectate end" cuts arrive on the frames the heuristics caught ("map load", 19 m "position jump") |
| Exit crash after init-false harnesses (0xC0000409, "libc++abi: terminating"): RenderWatchdog's global std::thread joinable at static destruction | WFC_PARTICIPANTTEST=1; WFC_BOTTEST=1 (both AIs) | crashed on b6b6e38 (BOTTEST ×2) and fb3fa288 (PARTICIPANT); clean on 51260f56 | **FIXED** (Integration's guard, 51260f56). The same exit path serves 52 harnesses at 51260f56 (code inspection) |
| VehicleFx::draw crash on the graybox fallback (0xC0000005 read 0x0, VehicleFx::draw +0x1ad) | TDM match with WFC_MAP=MP_NO_SUCH_MAP | 51260f56 crashed in 3 s; 51260f56 + 1bac7bfb (integration b1363fca) clean for 300 s | **FIXED** (1bac7bfb). WeaponFx audited: already guarded |
| pawnRayHit: pawns behind the ray origin counted as hits at 0 (5eeb3f7) | PARTICIPANTTEST (missile / roller checks) | 51260f56 | **PASS** 22/22 |
| HurtRadius visibility: blasts launched spawning pawns through floors (b749a83; 47 → 4 launches then) | Gorge 5v5 Smart, seeds 1-3, spawn-launch diagnostic | b6b6e38, 51260f56 | **PASS**: 0 spawn launches in all six matches |
| Stranded 30 s → suicide; no-path / open-air rejoin (8845aeb, a723407, b749a83) | Gorge 5v5 Smart seeds 1-3, WFC_STUCKWATCH | b6b6e38 vs 51260f56 | **PASS**, identical on both builds: STUCKWATCH 0 / 0 / 0; stranded suicides 0; rejoin walks 3 / 3 / 12; out-of-bounds notes 3 / 1 / 1; every match ended |
| Bot movement / stuck regression (playtest build vs head) | WFC_BOTTEST classic + smart | b6b6e38 vs 51260f56 | **PASS**: 39/39 both AIs on 51260f56; repaths, stuck events and longest idle identical (deterministic) |
| BOTTEST "AI cost per step" FAIL (Smart max 13.85 / 7.58 ms vs 6 ms on b6b6e38; Experimental saw 8.9 ms on 05319349) | WFC_BOTTEST smart | 51260f56 | **Not a code issue**: 0.67 / 4.59 ms max on a quieter machine, same simulated match. The spikes were machine load |
| QA "freeze bots" tool let a vehicle-form bot coast ~5.4 m (QABOT 6/7, both AIs, deterministic) | WFC_QA=1 WFC_QABOTTEST=1 | 51260f56 fail; 51260f56 + 1c34ad56 7/7 both AIs (max 0.01 m) | **FIXED** (1c34ad56): the freeze zeroes the bots' horizontal speed when it starts (DEV / QA path only) |
| Memory | private bytes sampled every 30 s; one long Gorge 5v5 Smart match | 51260f56 | **No leak**: flat at 4.50 GB from 30 s to 870 s (≈ 383 s of match, then a TDR stalled the run, see below). Every run's peak 4-6 GB during load |

## Harness sweep (integration 51260f56, shipped renderer)

| Result | Harnesses |
|---|---|
| **PASS** (all checks) | WEAPON 19/19, SWITCH 32/32, PARTICIPANT 22/22, TDM 43/43, CTF 12/12, SCORE 9/9, FINEAIM 3/3, MUZZLE 5/5, RMUZZLE 4/4, CHARGE 9/9, PROJFX 3/3, XFORMVIS 16/16, VEHPHYS 27/27, QA 8/8, CHASSIS 14/14, PRELOAD 1/1, MODEPLAY 21/21, BOTNAV 7/7, BOTOBJ 12/12 (Classic and Smart), DETERMINISM 2/2 (Classic and Smart), XP 16/16 (progression), CLASSCHANGE 22/22, DOUBLEJUMP 3/3, SPAWNFILL 10/10, ENGAGE 3/3, BARRIERWALK 3/3, EVENT 16/16, MARKERS 5/5, BOTTEST 39/39 (both AIs) |
| **FIXED in this sweep** | QABOT 6/7 → 7/7 (1c34ad56) |
| Measurement only, ran clean | MATCH (state machine: 20 kills → MatchOver), MODE (per-mode objectives), PICKUP (24 factories), HEIGHT, RISER, VEH, DROP; XFORM: 0/76 transforms under the map |
| Not a direct-boot harness (ran until timeout; not gameplay pass / fail) | DLETEST (sits in the frontend), MARKERTEST (visual tag diagnostic), RELOADTEST (Rendering's map-reload diagnostic, `=<frame>`) |

## Found during the sweep (not gameplay code)
- **Driver resets (Display 4101) at 07:45, 07:55, 08:02, 08:14, 08:29, 10:29, 11:06.** Cause, per Integration: aggregate GPU oversubscription across lanes.
  - The 10:29 and 11:06 resets stalled two runs on 51260f56, which is older than the 14c3ffca device-removed backstop: the DLETEST frontend run, and the leak run at match t ≈ 383 s ("watchdog: no frame completed ... 'beginFrame'").
- **GL API error in the frontend:** "GL debug [ERROR] type 0x824C src 0x8246 id 1001" logged 13000+ times while DLETEST sat in the menus. Routed to Rendering / Frontend.
  - The GPU is now limited to two exes at a time.
  - Integration 45a8b9c0 / 14c3ffca end a stalled exe with exit 3 instead of hanging.
- **Legacy-renderer GPU hang:** DM lockstep on Streets, 10 pawns, ~45 s in, when no render data is found.
  - Open item for Rendering (fallback path, not the shipped one).
  - Gameplay's earlier identity refs (b47 / b48) ran on the legacy renderer; this sweep uses the shipped one.
- **Stray file `F:\Transformers`** (a few status lines) written by Gameplay's runner through an unquoted path.
  - The runner is fixed.
  - Deletion is blocked for agents; the user can delete it.

## Left to the playtest (not automatable)
- How Smart vs Classic feels.
- Fine-aim feel.
- Vehicle handling feel.
- On-screen marker presentation (MARKERTEST is visual only).

## Commits from this sweep (agents/gameplay)
- 1bac7bfb: VehicleFx graybox guard (integration b1363fca).
- 1c34ad56: the QA freeze parks the bots.
- 194bf420 and this update: this document.
