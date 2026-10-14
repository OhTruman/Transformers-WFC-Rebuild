# Milestone 05: integrated product validation, integration/milestone-05 `1e14900`

| | |
|---|---|
| integration commit | `1e14900b150349f8da2affb024106f3d7b83cc50` (Integration report: STATUS / FIDELITY) |
| build | isolated export, `tools/fidelity/m05/build-target.ps1`: **Debug and Release built from 1e14900, 0 errors** (the build's own fidelity harness: 322 PASS / 0 FAIL / 9 KNOWN) |
| Release exe | `work/ab/m5int/build-release/bin/wfc_rebuild.exe` |
| Debug exe | `work/ab/m5int/build/bin/wfc_rebuild.exe` |
| runs | 2026-10-04 00:37 – 06:40; Experimental tools at 3413314+ (harness fixes listed below) |

## Overall automated result
| suite | result |
|---|---|
| lifecycle gate, Release (R1–R8 + owners' self-tests, harness-corrected rerun) | **PASS 100 / FAIL 3 / KNOWN 1 / INFO 4 / SKIP 8 / HUMAN 20** |
| lifecycle gate, Debug (R1, R2, R4; harness-corrected rerun) | **66 PASS / 2 FAIL**, the same 2 product FAILs as Release (return to Press START, no match music). The first Debug run's 4 FAILs were harness faults, since fixed |
| combined `FINAL.md` (every suite, `final-gate.ps1 -ReportOnly`) | **PRODUCT FAIL 24 / TEST-HARNESS FAIL 0 / PASS 253 / KNOWN 1 / UNKNOWN+WAITING 9 / HUMAN 44 / INFO 12 / SKIP 18 / PARTIAL 1** (the 24 product FAILs are the rows in the table below, counted per check) |
| 20-cycle lifetime soak | **23 PASS / 0 FAIL** |
| transform stress (1,176 runs, 14 scenarios × 84 starts) | **2 genuine fall-throughs**, 138 legal large drops, 1,036 PASS (after detector correction, below) |
| vehicle collision (924 runs, 11 scenarios × 84 starts) | forward drive / boost / nitro / turning: **0 pass-throughs** (int-04: 9 / 24); 21 pass-through FAILs in other scenarios; 16 ramp hard-stop candidates |
| visual regressions (merged vs int-04 vs Rendering lane) | **33 fix-present, 0 fix-lost, 0 diverged**, 98 unchanged |
| playtest acceptance (human-reported issues) | 16 PASS / 7 FAIL / 1 PARTIAL / 5 WAITING / 5 HUMAN (these were known before the playtest; it confirms them) |
| motion / vehicle states | see `jitter/` (frame-pacing hooks not integrated: WAITING) |

## The eight questions
1. **Cold boot → TDM match through the reconstructed frontend: YES.** The run uses keys only, through the shipped
   movies:
   - intro chain 4 / 4 decoded and played to the end;
   - Start → Multiplayer → party lobby → TDM (`EditGameMode` on focus) → host options (BLK B4 defaults) → game lobby
     (map index 0 = Streets) → 10 s countdown;
   - match URL identical to RE 3.1 → loading "Team Deathmatch / in Streets" + 3 tips;
   - PendingMatch 10.15 s → spawn at TnTeamPlayerStart in the team's initial cluster (7810 for Autobots).
2. **Complete the lifecycle and return: YES.**
   - Score-limit matches: kills step team scores 1…5; deaths → Spectating at **3.00 s**, respawn at **4.98 s**
     (×8); score-limit end → EndGameStats, HUD hidden → lobby at **15.0 s** with MapId 508.
   - Time-limit match: 600.0 s → GameEnded → lobby.
   - Pause → Quit → frontend.
3. **Another match without restarting: YES.**
   - 3 consecutive score-limit matches; scores restart at 1.
   - Host options persist (TimeLimit 600 in match 2).
   - Player state is fresh each match: robot, 50 / 150.
   - 8- and 20-cycle loops complete.
4. **Memory / resources bounded: YES.**
   - Private memory after each unload: 2,467 → 2,545 MB over 20 cycles (+4 MB per cycle, flat within noise).
   - Peak 3.33 GB.
   - GL release constant (72 textures per return).
   - Audio after every unload: 0 voices / 0 instances / 0 level cues, PCM 36.49 MB = baseline. Map audio identical at
     every load (32 cues, 97.18 MB).
   - **INFO (Integration):** handles grow monotonically 655 → 804 (+7.8 per cycle). Small, but not flat.
5. **Smoke / glass fixes survived integration: YES (measured).**
   - Every viewpoint Rendering changed (33) matches Rendering's lane; nothing reverted (`visual_compare`).
   - Whether the look is right stays a human check.
6. **Boost → robot under-map fixed: YES for the reported case.**
   - Every at-speed vehicle→robot scenario: 0 fall-throughs. That covers boost, nitro, both turn directions, long
     boost, late nitro and airborne; int-04 failed 127 / 756.
   - **2 genuine fall-throughs remain**, in rapid repeated transforms while nearly stationary:
     - start 15: hovering vehicle → robot sinks through the floor at −716.9 to the level below;
     - start 35: robot → vehicle sinks 1.1 m into a 0.57 m slab.
   - Plus Gameplay's own `WFC_CHAOS` case at `TnTeamPlayerStart_10894` (1.42 m, 8 frames).
   - Owner: Gameplay.
7. **New regressions from combining the lanes: none found in the merged behaviour.** Every lane seam the gate checks
   works:
   - the frontend selection reaches Gameplay, Systems and the render map; the map / mode / time / goal agree;
   - DM via the mode list runs DM (goal 20, FFA starts);
   - audio follows the selected map;
   - the render unload is called (constant GL release);
   - the loading underlay is gone in play.

   The defects below are missing features or lane bugs, not merge breakage.
8. **Ready for the user's real playtest: YES, with the known list below.** The M05 human playtest has since happened.
   Its findings are now automated tests (`playtest-acceptance.ps1`), and the next integration is gated on them.

## Product FAILs (owner)
| check | evidence | owner |
|---|---|---|
| return to Press START after a match | frontend after the match matches the title frame (difference 0.09) instead of the main menu. **Fixed on agents/frontend 76b8287** (`ShouldShowStartScreen` per the recovered script); awaits integration | Frontend |
| no match music | `DM_START` / `DM_FINALSTRETCH_LP` / `DM_END_*` (RE OV A5, CONFIRMED) absent. **Implemented on agents/systems 5f8d7c9 / 8769ecb**; awaits integration | Systems (+ Gameplay events) |
| under-map: rapid transforms | starts 15 / 35 in `r2v_rapid`; `WFC_CHAOS` TnTeamPlayerStart_10894 | Gameplay |
| collision pass-through, robot | robot_jump 8 / robot_turn 7 runs through StaticMeshCollectionActors at hull heights 1.2–1.6 m (`vehicle_collision.csv`) | Gameplay |
| collision pass-through, truck off-axis | reverse 2, strafe 1, boost-cycle 2, boost-right 1: the forward hull probes don't cover these directions | Gameplay |
| ramp / open-floor hard stops (candidates) | 10 of 16 at one spot, (111.3, −706.2, −419.5), from starts 51 / 68: no exported wall or step within 4 m. Gameplay 2c3854d "vehicle slope contact" may address it. Human look needed (simple collision isn't exported) | Gameplay |
| intro has no audio | 0 / 4 `movie.audioStart`. **Fixed on agents/frontend 8b6466e** (verified 4 / 4 on a build containing integration) | Frontend / Systems |
| black frontend / lobby backgrounds | title 94%, lobbies 64%, after return 94% black. **Frontend 0160cf1 draws the scenes**, but the lobbies show a flat untextured slab (34%) and the scene is not restored after a return | Frontend / Rendering / AssetTools |
| loading screen freezes during the load | longest frozen interval 3.7 s (10 s on slower loads). Frontend fd883dc animates it on the lane | Frontend |

## KNOWN / PARTIAL / WAITING / UNKNOWN
- **KNOWN:** Gorge is listed disabled (no render export). Correct for M05; not counted as a working map.
- **PARTIAL:**
  - EndGameStats reads `<CurrentGame:AttackingTeamIndex>` and `<TnMenuItems:Specialties>` unanswered; the experience
    panel shows undefined / NaN (no XP service).
  - Health regeneration (RE I3) is not in 1e14900 (Gameplay 391b7f0 implements it).
  - Death / spectate camera not reproduced.
- **WAITING:** HUD presentation (clock, team score, kill feed, PointEvent, displayed-vs-Gameplay values). Hud_GFX is
  not drawn in play; Rendering M08 adds a Canvas HUD layer and Frontend / Rendering own the HUD handoff.
- **WAITING:** frame-pacing measurement (`WFC_RENDERHZ` / `WFC_CAMLOG` / `WFC_SHOTEVERY` are on agents/rendering
  only). The "interlacing" cause (camera position per simulation tick) is fixed on Gameplay 3e4650d. Verified in
  isolation (CAMSYNC 144 Hz: 0.0003° vs 1.28°); awaits integration.
- **UNKNOWN:** per-bone animation discontinuity (no pose log in the product).
- **CONFIRMED absent, not a defect:** minimap / radar / compass (RE OV A9). The test asserts none is presented.

## Stale tests retired / harness faults fixed (this pass)
- **Retired expectations:**
  - `PlayerRestartDelay` 6.0 (dead config, BLK E8);
  - host's map preselected after a match (BLK C7);
  - lobby team = match team (BLK E2);
  - "only Streets selectable";
  - **"no music in a match"** (RE OV A5: game-type music plays);
  - minimap UNKNOWN (RE OV A9: absent).
- **Music check:** now accepts the package-qualified cue (`BL_LVL_HUD_INTERFACE.FRONTEND_MX_ORBIT_01`), as Integration
  reported. The sequence was correct and the expectation stale.
- **Harness faults found on 1e14900:**
  - R7 match segmentation (PowerShell's empty-array truthiness);
  - doubled-cue identity: two events of one layered cue are not a double;
  - AMB fallback read the next match;
  - `PickWinningTeam` empty-team rule (a lone player's team wins a 0–0);
  - RELOADTEST matched "0 failed";
  - match-validator prefix and score-reset rules;
  - transform-stress detector: actor-height reference and slopes produced 190 false fell-throughs; corrected to the
    authored floor above the pawn at the crossing: 2 genuine;
  - jitter tool: `RD` = Remove-Item alias, flattened luma arrays, `$h` / `$H` collision.

## Human check
See `tools/fidelity/HUMAN-CHECK-NEXT.md` (13 items) for the next build, and `HUMAN-CHECK-M05.md` for this one.
