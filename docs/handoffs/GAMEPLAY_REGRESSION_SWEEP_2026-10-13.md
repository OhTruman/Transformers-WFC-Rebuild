# Gameplay regression sweep, 2026-10-13 (state: partial — essentials done, full harness sweep pending)

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
| Memory | runner samples per run | all of the above | No growth seen. Gorge 5v5 private ~4.5-5.4 GB at the end of 150 s; the 10-minute leak run is pending (below) |

## Found during the sweep (not gameplay code)
- **Driver resets (Display 4101) at 07:45, 07:55, 08:02, 08:14, 08:29.** Cause, per Integration: aggregate GPU oversubscription across lanes.
  - The GPU is now limited to two exes at a time.
  - Integration 45a8b9c0 / 14c3ffca end a stalled exe with exit 3 instead of hanging.
- **Legacy-renderer GPU hang:** DM lockstep on Streets, 10 pawns, ~45 s in, when no render data is found.
  - Open item for Rendering (fallback path, not the shipped one).
  - Gameplay's earlier identity refs (b47 / b48) ran on the legacy renderer; this sweep uses the shipped one.
- **Stray file `F:\Transformers`** (a few status lines) written by Gameplay's runner through an unquoted path.
  - The runner is fixed.
  - Deletion is blocked for agents; the user can delete it.

## Pending (runs from ~14:30 in slot B, pausing while the user plays)

**Harness sweep on 51260f56:**
- Weapons and combat: WEAPON, SWITCH, TDM, CTF, SCORE, FINEAIM, MUZZLE, RMUZZLE, CHARGE, PROJFX.
- Vehicles and transforms: XFORMVIS, VEHPHYS, VEH, XFORM, RISER, DROP.
- QA tools: QA, QABOT.
- Loading and spawning: CHASSIS, PRELOAD, PICKUP, SPAWNFILL.
- Modes and match: MODE, MODEPLAY, MATCH, DLE.
- Bots: BOTNAV, BOTOBJ ×2, DETERMINISM ×2.
- Progression and player actions: XP (progression persistence), CLASSCHANGE, DOUBLEJUMP, ENGAGE, BARRIERWALK.
- Markers and misc: EVENT, MARKER, MARKERS, HEIGHT, RELOAD.

**Memory:** a 10-minute Gorge 5v5 run sampled every 30 s.

Not automatable (left to the playtest): feel of Smart vs Classic, fine-aim feel, vehicle handling feel.
