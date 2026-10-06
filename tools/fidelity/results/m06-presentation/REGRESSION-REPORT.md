# M06 presentation regression: what the human caught and the gate missed

**Build:** `integration/milestone-06` 95edd7b, the build the human recorded. It was run here as the human ran it: Debug
exe, PC SKU, Integration's render data (copied), and the real frontend route.

**Gate:** `tools/fidelity/presentation-gate.ps1`. It is also a suite of `final-gate.ps1` (`present`), and its verdict
now heads `FINAL.md`.

**New verdict for 95edd7b: VISUALLY BROKEN** (26 FAIL, 15 PASS, 1 UNKNOWN). The earlier Experimental M06 report ("a multi-map WFC game …
TDM PLAYABLE") was **overly positive and is withdrawn** for visual health.

## 1. What human testing caught (reproduced here)
| human report | reproduced | evidence |
|---|---|---|
| Streets gameplay with most of the world black or missing; Optimus, HUD and effects visible while the architecture is absent | **yes**: on the frontend-launched match only. A direct boot of the same exe at the same start draws the corridor | `evidence/route_vs_direct_same_start.jpg`, `evidence/m06_human_config_route.jpg` |
| menu state visible while gameplay accepts movement | **yes**: after Resume the pause menu stays drawn (similarity 0.97 to the pause frame) while the UI state is InGame, so real input reaches gameplay | `evidence/m06_human_config_route.jpg` (a10 / b10) |
| Extras screens that soft-lock | **yes**: Extras → Credits (credits never start; Back, Cancel, Start and Esc all do nothing) and Extras → Movies → Intro (no movie; no exit) | `evidence/frontend_softlocks.jpg` |
| account / name text input does not work | **yes**: Accounts → Create → "New Account": typed keys never appear and the dialog cannot be left | same |
| character customization becomes stuck; no correct character preview | **yes**: the preview shows "LOADING" spinners and never loads a model; the party-lobby preview area is an empty dark panel (blank area 0.71–0.85) | same; `present.frontend.screen.*_party` |
| selected characters still resolve to Optimus | **yes**: Scientist → Gameplay chassis Jet4, but the loaded body is `Characters/Optimus/robot.glb` | `present.charselect.body_resolves` |
| blank / grey areas | **yes**: Find Match → playlist → a blank game-lobby frame; the Mouse/Keyboard Layout page lists no bindings | `evidence/frontend_softlocks.jpg` |
| malformed / distorted frontend background geometry; missing / wrong frontend 3D presentation | **not on this machine for 95edd7b** (title stable and textured for 60 s), but **yes on the Frontend lane head 9e67bf8**: a giant untextured sphere, a grey lobby slab and noise plates after a return. The new gate FAILs all three | `evidence/frontend_lane_malformed.jpg` |
| malformed Team Deathmatch descriptive scrolling text | not reproduced as a measurement: the ticker text is garbled at times in frames. It remains a **human check** (no text-rendering oracle) | — |

## 2. Why the old checks missed it
1. **Gameplay visuals were judged by "not black" only.**
   - The Streets in-match frame scored "near-black 6% → HUMAN".
   - Smoke, fog, sky, the HUD and Optimus are not black, so a frame with no architecture passed.
   - The HUMAN bucket was then treated as "fine pending a look", and the report still said TDM PLAYABLE.
2. **Map visuals came from direct boot.**
   - The map matrix, the Streets sweeps and visual-compare ("131 / 131 unchanged vs M05") all boot straight into the
     map.
   - The defect exists only when the match is launched from the frontend.
3. **Counters agreed on both routes.**
   - Materials, lightmapped components, lights, meshes, audio and match events were identical.
   - Every counter-based check passed.
4. **The frontend route was only checked for flow, not pixels.**
   - Events advanced (spawn, kills, match end, return), so the lifecycle checks passed.
5. **Visual verdicts used the Release exe.** The human runs Debug (not the cause here, but a coverage gap).
6. **Soft-locks were never exercised.**
   - The scripted paths only covered Multiplayer → TDM.
   - Extras, Accounts, the Settings sub-pages, customization and text entry were never entered, so "no exit" could not
     be observed.
7. **Interaction was inferred from presence.** A screen object that opened counted as working. No typed text,
   rebinding or preview was ever verified.
8. **Harness defects found while building the new gate:**
   - the first version printed "no catastrophic failure" over 23 FAILs (wrong results field). The gate now refuses to
     print a verdict when its FAIL list and the report disagree;
   - scripted input (`WFC_AUTO*`) is injected after the product's menu input gate, so "pawn moves under a menu" is a test
     artifact, now reported UNKNOWN;
   - a dropped newline silently disabled the route-vs-direct check. A skipped part now reports SKIP explicitly.

## 3. What now rejects this build
| check (`present.*`) | 95edd7b, human config | known-good control |
|---|---|---|
| `gameplay.world.{spawn,moving,moving2}` × 2 matches: textured world with the HUD band and player excluded | **FAIL** 6 / 6: detail 0.019–0.055 | M05 1e14900: 0.198 / 0.224 PASS |
| `gameplay.route_vs_direct`: same start, direct boot | **FAIL**: 0.055 vs 0.197 (ratio 0.28) | — |
| `transition.second_match.*`: load / unload / load | **FAIL**: equally empty in match 2 | — |
| `transition.pause_cleared.*` | **FAIL**: similarity to the pause frame 0.97 | M05: PASS |
| `exclusive.menu_visible_while_moving` | **FAIL** | M05: PASS |
| `frontend.screen.*_party / _lobby`: scene area beside the panel | **FAIL**: one blank area 0.71–0.85 | — |
| `frontend.scene.*`: untextured / placeholder / noise signatures | PASS for 95edd7b; **FAIL** for Frontend 9e67bf8 (untextured 0.23 / 0.43; noise 0.43) | M06 title over 60 s: PASS |
| `charselect.body_resolves` | **FAIL**: Optimus for Jet4 | — |
| `watchdog.*.softlock`: Back, then Cancel / Start / Esc; screen identity from GFx dumps (menuMain_mc visible) | **FAIL**: extras_credits, extras_movies, accounts_create | extras_concept, settings, customization exit, back/forward ×3: PASS |
| `watchdog.*.classification` (SCREEN PRESENT / DISPLAY CORRECT / INTERACTION WORKING / FULLY FUNCTIONAL) | **FAIL**: text entry (no characters arrive), rebinding (no bindings listed, K does nothing), movies (none plays), customization preview (no model) | extras_concept: FULLY FUNCTIONAL |
| `reference.streets_pipeline`: 13 fixed Streets cameras vs the known-good M05 frames | PASS: direct boot is intact, so the defect is route-specific, not the map pipeline | — |
| map matrix: VISUAL COVERAGE / LIGHTING (`map-verdicts.ps1`) | Streets **NOT VISUALLY PLAYABLE** (frontend-launched world empty). Gorge, Rust and Remnant are **NOT VISUALLY PLAYABLE** (predominantly dark) until a human confirms the darkness is authored | — |

The M05 control is itself VISUALLY BROKEN, correctly: its title was black, a known M05 failure. Its Streets world, pause / resume and fixed-camera reference pass. So the gate separates a missing world from a present one, rather than failing everything.

## 4. Ownership (comparative runs, not file names)
| defect | evidence | owner |
|---|---|---|
| frontend-launched match loses the world (Streets) | Direct boot is intact in every build. The frontend route is intact on M05 1e14900 and on Frontend 0160cf1, and broken on Frontend b1fce97 … 9e67bf8 and on 95edd7b. **Bisect over the 11 Frontend commits 0160cf1..9e67bf8: first bad `b1fce97` "In-match HUD movie (Hud_GFX) fed by Gameplay; results screen data; GFx runtime fixes"** (single parent 0160cf1). `WFC_GFX_EMPTY=1` and `WFC_NO_FRONTEND_SCENE=1` do not avoid it. The Rendering (4215359) and Gameplay (e258179) lane heads have no frontend route to compare | **Frontend** (HIGH); the fix may touch the Rendering interface |
| pause menu stays drawn after Resume while input goes to gameplay | identical on Frontend 9e67bf8 and 95edd7b | **Frontend** |
| Extras Credits / Movies soft-locks; Accounts text input; empty rebinding page; Find Match blank lobby | 95edd7b; the screens are Frontend-owned GFx | **Frontend** |
| customization preview never loads; party-lobby preview area empty | no character model is loaded (only Optimus is exported) | **Frontend + AssetTools** |
| selected body resolves to Optimus | Gameplay receives Jet4; only `Characters/Optimus` exists in VerticalSlice | **AssetTools + Gameplay** |
| malformed title / grey slab / noise plates | Frontend lane head 9e67bf8 with its own render data (UI_FrontEnd holds only lightmaps); not on 95edd7b here | **Frontend / AssetTools** (render data for UI levels); **Integration** to confirm which data the recorded run used |
| dark Gorge / Rust / Remnant | direct boot, all builds | **Rendering**, HUMAN CHECK REQUIRED |

## 5. Still needs a human
- Whether the title and lobby scenes look like the shipped game. The gate rejects black, blank, untextured, noise and
  missing content, not "wrong but plausible".
- The garbled TDM description ticker, and any text rendering.
- The dark maps (Gorge, Rust, Remnant): authored darkness or missing lighting.
- Real keyboard / mouse / pad input under menus. Scripted input bypasses the menu gate; a pre-gate input hook is
  proposed (`WFC_INPUTSCRIPT`).
- Any malformed-geometry case that only appears on another machine or driver, or with a different render-data build:
  **Integration should record which `work/render` the recorded playtest used** (the human's tree versus a fresh
  `build_render_data` gives different UI-level data).
- Feel: animation, vehicle, transform, audio. Unchanged from `HUMAN-CHECK-M06.md`.
