# Milestone 06: independent validation of `integration/milestone-06` 95edd7b

> **CORRECTION, after the human playtest.** The visual verdict of this report was **overly positive and is withdrawn**.
> Re-validated with the new presentation gate, this build is **VISUALLY BROKEN**:
> - the frontend-launched Streets match draws almost no world;
> - the pause menu persists over play;
> - several frontend screens soft-lock;
> - text input and rebinding do not work;
> - the character preview never loads.
>
> Flow, match rules, audio, collision and lifetime results below are unaffected. Every "TDM PLAYABLE" / "visual" statement
> here is superseded by [`../m06-presentation/REGRESSION-REPORT.md`](../m06-presentation/REGRESSION-REPORT.md) and
> [`MAP-VERDICTS.md`](../m06-presentation/MAP-VERDICTS.md).

| | |
|---|---|
| integration commit | `95edd7b34107e6bab5a44d6cbbc191db46209e8a` (verified against `origin/integration/milestone-06` before the build) |
| build | isolated export (`tools/fidelity/m05/build-target.ps1 -Name m6int`): **Debug and Release built from 95edd7b, 0 errors**. Render data generated in the export for all 10 MP maps + 5 UI levels (`tools/render/build_render_data.ps1 -Map`, about 10 s per map) |
| Release exe | `work/ab/m6int/build-release/bin/wfc_rebuild.exe` |
| Debug exe | `work/ab/m6int/build/bin/wfc_rebuild.exe` |
| runs | 2026-10-04 14:50 – 18:05 (stress re-analysed with the per-map KillZ rule), Experimental tools at this commit |
| human list | [`tools/fidelity/HUMAN-CHECK-M06.md`](../../HUMAN-CHECK-M06.md) |

## Verdict
**M06 is a multi-map WFC game, not "Streets plus files"**, with three playability defects that a player hits in the
first minutes and a set of per-map visual / collision gaps.

- **Every one of the 8 TDM maps** completes the real user path, from cold boot to the lobby (frontend → Multiplayer →
  Private Match → TDM → the map chosen with the lobby selector → countdown → loading → character select → spawn →
  robot / vehicle / boost / firing / transforms → pause / resume → kills, deaths and 5 s respawns → score-limit end →
  EndGameStats → lobby).
- **The selected map reaches every owner**: lobby id → frontend launch → Gameplay → Systems audio → render data. KillZ,
  movers, starts, pickups, level audio and spawn clusters are each read from that map's own data, and the values
  differ per map as authored.
- **17 consecutive matches over all 8 maps in one process** are clean: audio fully released after every match, 0
  doubled cues, 7 UI movies per cycle, bounded memory after the first pass.
- **Blocking defects found** (owner):
  1. **The pause menu stays on screen after Resume** (Frontend). The game is back InGame, but the menu is drawn over
     live play until the match ends. Every resume input does it. **New in this pass.**
  2. **Kill-feed rows drawn on top of each other** (Frontend). Reproduced visually.
  3. **The selected character's body is never drawn** (AssetTools + Gameplay). The selection reaches Gameplay, but the
     pawn is always Optimus with the IonBlaster.
- Further failures are listed below per owner. Fortress / Havoc / Tranquillity are **SOURCE DATA ABSENT** (no packages in
  the dump; the catalog lists them `cooked=0`), so they are not counted as runtime regressions. BrokenHope / Remnant are
  **MAP PRESENT / RUNTIME CAPABLE, GAME MODE BLOCKED**.

## Automated results (all against 95edd7b)
| suite | result |
|---|---|
| lifecycle gate, Release (R1–R8 + owners' self-tests) | 95 PASS / 9 FAIL. 6 of the 9 are **Experimental harness faults** (the gate assumed selector index 0 = Streets; on M06 index 0 = Seed, as BLK C4/C7 predict), now fixed. The corrected R1+R5+R7 rerun is **56 PASS / 0 FAIL** (R1+R5) and all R7 checks PASS. Remaining product FAIL: Gameplay `WFC_CHAOS` on Streets |
| lifecycle gate, Debug (R1, R2, R4) | **68 PASS / 0 FAIL** |
| playtest acceptance | 17 PASS / 2 FAIL / 10 HUMAN / 3 WAITING / 1 PARTIAL. `nav.back_twice` was a test fault (it compared against a different menu focus; fixed: difference 2.1). Remaining product FAIL: `hud.point_event` |
| motion / jitter | 34 PASS / 1 FAIL (frame pacing, robot walk + turn at 144 Hz) / 1 UNKNOWN / 1 WAITING |
| 8-map TDM real path (`m06-maps-tdm.ps1`) | **8 / 8 complete**; 7 PASS + 1 HUMAN per map |
| map matrix, 10 maps (`map-matrix.ps1`) | 10 / 10 load and render; CHAOS / XFORM per map below |
| visual: Streets vs M05 merged baseline | **131 / 131 unchanged** (glass, particles, ramps, sheets): no Streets visual regression |
| map chain, 17 matches (Streets → Gorge → Seed → Berth → Complex → Rust → Debris → Molten → Streets ×2, one process) | 17 / 17; audio / UI / launch checks PASS; memory bounded after the first pass (see Lifetime) |
| transform stress + vehicle collision, 8 TDM maps × 12 starts × all scenarios | see the table under Vehicle / collision |

## Map matrix
Categories: LOADS / STRUCTURALLY TRAVERSABLE / VISUALLY PLAYABLE / TDM PLAYABLE / MODE BLOCKED / PARTIAL / BROKEN.
In the table:
- **chaos**: Gameplay's `WFC_CHAOS`, 20 starts × 20 s, as runs under the map / KillZ / stuck;
- **xform**: `WFC_XFORMTEST`;
- **views**: 28 product frames spread over the whole map by farthest-point sampling of the navigation nodes.

| map | loads | structurally traversable | visually | TDM via frontend | category | notes |
|---|---|---|---|---|---|---|
| MP_IAC_Streets | yes (11.7 s) | chaos 0 / 0 / 0; xform 0 / 76. **Gate CHAOS (123 starts): 1 under the map at TnTeamPlayerStart_2602 (2.83 m, 26 frames)** | reference; unchanged vs M05 | **PASS** | **TDM PLAYABLE** | |
| MP_UND_Gorge | yes | **chaos: 1 under a BSP floor, 1.43 m, from TnBombPlantPoint_4905, reproduced**; xform 0 / 76; vehicle hard stop from start 19 at (−72.8, 12, −102.5) | **11 / 28 views mostly black**; large **solid-black geometry** at the stop location (candidates `Big3_Building3Walkway_NoEmiss_MATINST` / `ENV_CORE_CanyonFloor_A`); overviews show flat beige surfaces | **PASS** | **TDM PLAYABLE, visually PARTIAL** | darkest map; human look first |
| MP_IAC_Seed | yes | 0 / 0 / 0; 0 / 76 | coherent interiors; overview sky flat cream | **PASS** | TDM PLAYABLE | |
| MP_IAC_Berth | yes | 0 / 0 / 0; 0 / 76 | interiors not washed out by metrics (median luma 36, contrast 25, 0 washed views); **hazy beige overviews**. Integration's "washed out" is not reproduced in play views: human check | **PASS** | TDM PLAYABLE | |
| MP_UND_Complex | yes | 0 / 0 / 0; 0 / 76 | coherent | **PASS** | TDM PLAYABLE | |
| MP_IAC_Rust | yes | **1 stuck, reproduced** (vehicle at (69.9, 11.6, −121.1), BlockingVolume_11615 + 3 SMCAs) | dark (6 / 28 mostly black) | **PASS** | TDM PLAYABLE, visually human check | |
| MP_ORB_Debris | yes | 2 stuck, reproduced | **shader compile failure reproduced** (`Megatron_com_Mat`, glTF fallback); strong single-hue casts, one near-white view: "mottled" is a human check | **PASS** | TDM PLAYABLE, PARTIAL | |
| MP_KON_Molten | yes | 0 / 0 / 0; **xform 0 / 76** (Integration's 1 / 760 not hit in this sample) | coherent; rain, lava. 328 materials active (Integration: 328 / 333; floor TextureSetSample unsupported) | **PASS** | TDM PLAYABLE | |
| MP_ESC_BrokenHope | yes | 1 stuck; xform 0 / 76 | dusk skyline drawn; 288 materials (Integration 288 / 341) | n/a: Escalation (SV) only | **MAP PRESENT / RUNTIME CAPABLE, GAME MODE BLOCKED** | |
| MP_ESC_Remnant | yes | 2 stuck; xform 0 / 76; KillZ 0 m authored (map floor 7 m and up: CONFIRMED from data) | space skybox drawn; several near-black corridors | n/a | **MAP PRESENT / RUNTIME CAPABLE, GAME MODE BLOCKED** | |
| Fortress / Havoc / Tranquillity | — | — | — | — | **SOURCE DATA ABSENT** | no `MP_*Fortress / Havoc / Tranquil*` package in Game Dump; catalog `cooked=0` |

### Escalation (BrokenHope / Remnant)
Frontend exposure works: Main menu → Escalation → party lobby (Find a Public Game / Host a Game / Escalation Info /
Leaderboards) → Host a Game → host options → Create Game → `UI_CampaignLobby_m?Game=TnGameLobbyGameSurvival?GameModeTag=SV`.

The lobby shows Remnant (506) and "Finding 1 more players". The authored rule is `gamelobby.autostart
requiredPlayers=2`, so a solo user cannot start. Survival mode is not implemented in Gameplay (Integration's STATUS
agrees).

The maps themselves load, render, traverse (CHAOS and XFORM) and have per-map KillZ and pickups. INFO (Frontend): the
lobby movie pushes `SetSelectedMapID(505)` but the lobby state stays 506; which is original is UNKNOWN.

## Generic architecture (map data, not Streets)
- Read per map at runtime, with differing values:
  - KillZ (`physics.json` TnWorldInfo): Streets −750, Gorge −75, Berth −35, Rust −5.1, Debris +100, Remnant 0 m; all
    match the authored data;
  - movers and hidden actors;
  - player starts (Streets 84, Gorge 115, Berth 120, …);
  - pickup factories;
  - level audio (Systems `audio.loaded` level = the selected map);
  - render data (`WFC_RENDER_DATA/<map>`).
- Initial spawn clusters come from the map's `gameplay.json` (Seed spawned in its InitialSpawn cluster 11813).
- **Remaining Streets literals in `src/`:**
  - defaults when no map is given (`World.h` `map` / `mapName_`, autoplay `"508"`);
  - test-only code (`WFC_MODETEST`, URL-parser self-tests);
  - the diagnostic audio-cycle fallback (`World.cpp:1114`).
  - None of them affects a frontend-launched match. Nothing to remove.

## Streets regression
- Path, match and audio:
  - cold boot → intro → title → TDM path → match → return lands on the **main menu** (M05 FAIL, now PASS);
  - time-limit and score-limit matches;
  - second match;
  - DM ownership;
  - PC SKU.
- Glass, smoke, particles, ramps: 131 / 131 visual measurements unchanged vs M05.
- Transform stress on Streets: **0 fall-throughs in 168 runs** (M05: 2 genuine).
- Vehicle collision on Streets: 6 pass-throughs in 132 runs (M05: 21 / 924).
- Gameplay CHAOS on Streets: 1 under-map run (above).

## Intro / frontend / audio
- **4 / 4 movies**, each with exactly one `movie.audioStart` and an `audioStop` at the end (0 voices, no stream left).
- **A/V offset reproduced**: the audio clock trails the video at the end by 0.42 / 0.10 / 0.10 / 0.15 s (Integration:
  0.35 / 0.10 / 0.12 / 0.13).
  - The 128 s FMV is no worse than a 7 s logo, so this is a **constant start offset, not drift**.
  - Activision's first presented frame is already 0.20 s into the movie, which accounts for its larger offset.
- Track 5 stays **PROVISIONAL**; the native selection rule is **UNKNOWN**.
- **Skip**: each of the 4 movies skipped mid-play (FMV at 6 s) stops its sound in the same frame; title music starts
  only afterwards.
- **Music sequence**: frontend → party lobby → lobby → `DM_START` → `DM_FINALSTRETCH_LP` (at 5 kills left, as
  `Match.cpp` and RE OV A5 define) → `DM_END_<winner>` → lobby music.
- **Lifetime and backgrounds**: 17 returns with 0 menu music inside matches. Backgrounds are drawn on every screen (M05:
  black).
- **Loading**: frames 10 / 60 / 120 inside a match load all differ (it animates; M05 froze).

## HUD
| item | result |
|---|---|
| health / overshield, ammo, crosshair, team score | drawn; **team score follows Gameplay** (0/0 → 1/0 → 1/1): VISUALLY VERIFIED |
| clock | not visible in TDM play frames; whether the original shows it: HUMAN |
| kill feed | **FAIL (Frontend)**: two rows drawn at the same position (`evidence/killfeed_overlap_crop.jpg`). Data correct |
| PointEvent (+points) | **FAIL (Frontend)**: 0 `_global.PointEvent` pushes on kills (TnHUD KillTransactionObserver, CONFIRMED authored) |
| respawn timer / spectate | drawn ("0:02 WAITING TO RESPAWN") |
| match start / announcement | "Team Deathmatch" announcement + 5 announcer lines per match (Optimus, `DialogCharacters.OPRIME`) |
| results | EndGameStats "Experience Earned"; PARTIAL: unanswered data-store reads, XP 0 offline |
| minimap | absent, as RE OV A9 says (CONFIRMED ORIGINAL) |
| red Cybertronian glyph text at death | shown; whether it is authored decoration: HUMAN |
| dumps | `dump:` only reaches frontend-presenter movies, not the in-match HUD (hook gap, not a product failure) |

## Character selection
| check | result |
|---|---|
| UI SELECTION WORKS | **PASS**: "Choose Character" (CustomTransformers_GFX) with the 4 roster classes; Down + Accept picks Scientist |
| GAMEPLAY RECEIVES SELECTION | **PASS**: `match.characterSelected name=Scientist chassis=Jet4`; `MATCH spawn ... chassis=Jet4` |
| CORRECT BODY RESOLVES | **FAIL (AssetTools + Gameplay)**: the loaded assets are `Characters/Optimus/robot.glb`, `vehicle.glb` and the IonBlaster (Scientist's authored loadout is BurstRifle). Only Optimus and the IonBlaster are exported to VerticalSlice; the source packages exist (`TR_AirRaid_ROBO_p`, …) |
| CORRECT BODY RENDERS | **FAIL**: Optimus is drawn (expected, as Integration reports) |

## High refresh / jitter
- **Lockstep simulation and animation** (robot walk / strafe / turn / fire, vehicle drive / boost / nitro / turn):
  - 0 displacement spikes;
  - 0 frozen or doubled animation steps.
- **Presentation frames**: 0 ping-pong frames.
- **Gameplay `WFC_CAMSYNC`**: the per-frame camera holds 0.0003° at 60 / 144 / 240 Hz, against 0.7–1.3° for the old
  per-tick camera.
- **Product frame pacing** (`WFC_RENDERHZ` / `WFC_CAMLOG`): **PARTIAL**.
  - Robot walk + turn at 144 Hz: mean 0.0034, 25% of frames above 0.004. The broken M08 reference was 0.0095.
  - 60 Hz is at the limit (5.0%).
  - 240 Hz and vehicle boost + turn are clean.
- **Captures cannot show what a person sees on a 144 Hz panel**, so smoothness is HUMAN item 8.

## Vehicle / collision per TDM map
Each map ran 12 starts spread over its own start list, with:
- transform stress: 14 scenarios, 168 runs per map (boost / nitro / turn / airborne / rapid vehicle↔robot);
- vehicle collision: 11 scenarios, 132 runs per map (drive, boost, boost left / right, nitro, boost cycle, reverse, strafe
  boost, robot walk / turn / jump).

KillZ is each map's authored value. "Pass-through" means the pawn's hull crossed exported blocking collision. The
exported collision is not the runtime's simplified bodies, so these are **HIGH, not CONFIRMED**, and each comes with a
location to look at.

| map | transform: fall-through / runs | vehicle: pass-through | ramp / hard stops (cluster) | stuck (INFO) | locations |
|---|---|---|---|---|---|
| Streets | **0 / 168** (M05: 2) | 6 (robot_turn ×3 through SMCA_13239 at (116.3, −704.6, −311.5); boost_cycle, reverse, robot_jump) | 0 | 35 | |
| Gorge | 0 / 168 | 1 (robot_jump, SMCA_12888 at (−167.0, −34.8, 91.1)) | 8: **(−73, 12, −103) ×5**, the solid-black geometry | 25 | 2 end points with no exported floor (visible grated floor: INFO) |
| Seed | 0 / 168 | 2 (robot_jump SMCA_13553 at (226.8, 14.8, −193.0) h 0.8; robot_turn SMCA_9651 at (−86.4, 8.9, −142.1)) | 0 | 31 | |
| Berth | **1 / 168**: v2r_rapid start 30, through a **7 cm floor** at (−184.0, 7.5, −94.0), landed 9 m lower | 1 (drive SMCA_4425 at (−184.3, 8.8, −23.8) h 0.8) | 0 | 39 | |
| Complex | 0 / 168 | 3 (boost_cycle SMCA_7283; robot_jump SMCA_13834, SMCA_5767) | 14: **(−8, −9, −28) ×10**, (215, 5, −82) ×3 | 53 | |
| Rust | 0 / 168 | 2 (robot_jump SMCA_5939 at (−38.8, 10.1, −203.9); SMCA_11061 at (−281.5, 33.7, −65.4)) | 6: **(−170, 10, −200) ×5** | 22 | |
| Debris | 0 / 168 (4 end points with no exported floor: INFO) | 4 (boost_cycle SMCA_15797, drive SMCA_8578 h 0.8; robot_jump SMCA_2997, SMCA_7687) | **24: (−233, 133, 55) ×21** | 21 | worst hard-stop spot of all maps |
| Molten | **0 / 168** (Integration's 1 / 760 not hit) | **5: 4 vehicle runs from start 25 through SMCA_11993 at (200.3, −7.0, −36.3), hull top 1.6 m** (a low overhang?); robot_jump SMCA_13801 | 7: (−6, 12, −56) ×6 | 48 | |

Owner for all of these: **Gameplay**. The clustered hard stops are the places a driver will feel as "stops for no
reason" (human check 15). Gorge's cluster sits on the solid-black geometry (Rendering, see the map matrix).

## Lifetime / memory
17 matches over 8 maps, one process:

| measure | result |
|---|---|
| frontend private MB after each return | first pass (each map loaded the first time): 2141 → 2534; **second pass flat 2521–2547 (≈ +2 MB / match)**. One-time caching, bounded, not a leak |
| Streets after unload, passes 1 / 2 / 3 | 2522 / 2910 / 2915 MB |
| peak private | 3.77 GB, at the Rust load (Integration: peak ≈ 2.99 GB over 4 maps; the higher figure comes from the larger maps in the chain) |
| GL textures released per return | constant per map (Streets 72, Gorge 60, Seed 91, Berth 61, Complex 72, Rust 105, Debris 49, Molten 61) |
| audio after every unload | 0 voices / 0 instances / 0 level cues, PCM 36.49 MB = baseline (17 / 17) |
| **handles** | **668 → 785, +7.3 per match, through both passes**. Not cache-like: still growing after every map has been seen. Small, but monotonic. Integration's +3 per match is reproduced as +7 here. Owner to be found (INFO, Frontend / Rendering per Integration) |
| threads | 21 → 13, flat |

## Failures by owner
| owner | failure | evidence | label |
|---|---|---|---|
| **Frontend** | pause menu stays drawn after Resume, until the match ends | `evidence/pause_stays_after_resume.jpg`; PauseMenu_GFX `movieClosed` only at match end | CONFIRMED (new) |
| **Frontend** | kill-feed rows overlap | `evidence/killfeed_overlap_crop.jpg` | CONFIRMED |
| **Frontend** | no PointEvent pushes on kills | acceptance `hud.point_event` | HIGH |
| **AssetTools + Gameplay** | selected character body / loadout not resolved (always Optimus + IonBlaster) | `evidence/character_select.jpg`, load log | CONFIRMED |
| **Gameplay** | CHAOS under the map: Gorge 1.43 m under BSP (TnBombPlantPoint_4905); Streets 2.83 m (TnTeamPlayerStart_2602) | map matrix, gate SELFTEST | CONFIRMED |
| **Gameplay** | stuck runs: Rust 1, Debris 2, BrokenHope 1, Remnant 2 | map matrix | CONFIRMED |
| **Gameplay** | vehicle / robot pass-throughs (24 over 8 maps) and clustered hard stops (Debris (−233, 133, 55) ×21, Complex (−8, −9, −28) ×10, Gorge, Rust, Molten) | stress table | HIGH (exported collision is not the runtime's simplified bodies) |
| **Gameplay** | Berth rapid vehicle→robot transform through a 7 cm floor (start 30) | stress table | CONFIRMED (geometry), same class as M05's rapid-transform cases |
| **Gameplay / Rendering** | frame pacing at 144 Hz (25% of frames over the threshold) | jitter C | PARTIAL |
| **Gameplay** | Survival (Escalation) mode not implemented: BrokenHope / Remnant game mode blocked | Escalation probe | CONFIRMED (Integration agrees) |
| **Rendering** | Gorge dark / black geometry (11 / 28 views mostly black; solid-black mesh at (−72.8, 12, −102.5)) | `evidence/matrix_MP_UND_Gorge.jpg`, `gorge_black_geometry_stop.jpg` | HIGH |
| **Rendering** | Debris `Megatron_com_Mat` shader compile failure; Molten TextureSetSample floor (Integration) | render log | CONFIRMED |
| **Rendering / AssetTools** | flat beige / cream backdrops in overviews (Seed, Berth, Gorge, BrokenHope) | matrix sheets | HUMAN CHECK REQUIRED |
| **Frontend / Rendering** | handles +7 per match | chain soak | UNKNOWN owner (INFO) |
| **Experimental** | gate index-0 = Streets assumption, back-navigation focus, chain-soak warm-up and per-map GL rules, KillZ −749 in the stress tools | fixed in this commit | TEST / HARNESS |

## Known deviations (not counted as M06 failures)
- Fortress / Havoc / Tranquillity: SOURCE DATA ABSENT.
- After a match the lobby shows selector index 0 (Seed), not the map just played: BLK C7, HIGH.
- XP / profile values offline (0 XP, unanswered EndGameStats reads).
- Track 5 PROVISIONAL.
- No minimap (CONFIRMED ORIGINAL).
- Per-bone animation discontinuity: UNKNOWN (no pose log).

## Reproduction
```
.\tools\fidelity\m05\build-target.ps1 -Ref 95edd7b34107e6bab5a44d6cbbc191db46209e8a -Name m6int
cd work\ab\m6int; foreach ($m in <10 MP maps + 5 UI levels>) { .\tools\render\build_render_data.ps1 -Map $m }
.\tools\fidelity\final-gate.ps1 -Ref 95edd7b... -Name m6int -Only gate,accept,jitter,debug,visual -BaselineVisual work\m05final_1e14900\visual_playtest
.\tools\fidelity\map-matrix.ps1 -Root work\ab\m6int -OutDir work\m06\map_matrix -Views 16 -Chaos 20
.\tools\fidelity\m06-maps-tdm.ps1 -Root work\ab\m6int -OutDir work\m06\tdm_maps
.\tools\fidelity\m05-e2e-gate.ps1 -Root work\ab\m6int -Runs R4 -Cycles 17 -ChainMaps 508,510,501,502,503,504,507,509
.\tools\fidelity\transform-stress.ps1 / vehicle-collision.ps1 -Map <map> -StartSample 12 [-Scenarios all]
```
