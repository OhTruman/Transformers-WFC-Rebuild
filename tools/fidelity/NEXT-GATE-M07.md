# Milestone 07: the gate Integration runs

```
.\tools\fidelity\m07-gate.ps1 -Ref origin/integration/<milestone-07> -Build            # full, about 4-6 h
.\tools\fidelity\m07-gate.ps1 -Ref origin/integration/<milestone-07> -Build -Quick     # about 2 h
.\tools\fidelity\m07-gate.ps1 -Ref <same> -ReportOnly                                  # recompose M07-GATE.md from existing runs
```

- **Build:** isolated Debug + Release of exactly that sha in `work\ab\m7_<sha>`, with render data for every launchable
  map and the 5 UI levels.
- **Layout:** a mirror of the user's layout (`build\release\bin`) for the map-loss test.
- **Suites:** run strictly one renderer at a time. They wait for other sessions' `wfc_rebuild.exe`
  (`WFC_GATE_GPU_WAIT_MIN`, default 240 min), so please **close other renderer windows** while it runs.
- **Read `M07-GATE.md` from the top.** The VISUAL HEALTH line overrides every count. PASS / asset / draw counts are
  never accepted as presentation.

## Suites and what makes them fail
| suite | fails when |
|---|---|
| presentation | the frontend-launched world is missing (HUD / player excluded) or < 0.5× the direct boot at the same start; the menu persists over play; a screen soft-locks; typed text does not arrive; the Streets reference diverges |
| maploss | Streets / Berth lose their environment through the lobby in any display profile; in-play world draws < 0.5× the same exe's direct boot |
| renderstate | after any overlay the next world frame is empty, has opaque draws without depth test or GL errors, or inherits non-world GL state; **or the negative control (both defences off) passes** (detector blind = TEST FAULT) |
| frontend | movie content not 16:9 or the frontend visible beside it (16:9 / 4:3 / 16:10); a black / blank / untextured screen; missing render data; nav.check (focus owner hidden, screens over gameplay, stray modal); wrong return route; created account name not shown; selection name mismatch; title frozen, invented Matinee tracks, or camera not moving |
| characters | an export missing or broken; UI ≠ Gameplay ≠ loaded robot body ≠ vehicle body ≠ faction ≠ class ≠ loadout; **OPTIMUS FALLBACK** named |
| maps | per map: load, spawn, walk, jump, vehicle, boost, transform, boost → transform, collision, kill plane, respawn, match end, frontend world, unload, map audio |
| modes | per mode by its own rules: setup, objective actors, spawn class, timer, score / objective, death / respawn, end, results, lobby return, second match |
| lifecycle / acceptance | the M05 / M06 lifecycle and playtest checks (quit routing auto-adapted to the pass-4 "Quit Game?" box) |
| soak | unbounded growth after pass 1 (working set, handles, threads, AS heap / display nodes / GL shapes / graveyard), or a map costing more on each revisit |

## Hooks the gate consumes
If a hook is missing the check reports SKIP / UNKNOWN, not PASS.

| hook | from | used by |
|---|---|---|
| `WFC_VISUALCHECK` + `<shot>.json` (`gl_entry_state`, `opaque_no_depth_test`, `gl_errors`, `viewport`) | Rendering M10 / M11 | renderstate, maploss, maps, soak |
| `WFC_GFX_NO_GLRESTORE`, `WFC_M11_INHERITSTATE` | Frontend a96f841, Rendering M11 | renderstate negative controls |
| `navcheck:`, `display:`, `type:` script steps | Frontend pass 4 / 5 | frontend, renderstate, soak, presentation |
| `MATCH spawn … drawn= fallback=` | Gameplay 21f | characters |
| `WFC_CHARSELECT`, `WFC_LIFECYCLE`, `WFC_MATCH`, `WFC_GAMEMODE`, `WFC_MODEPLAYTEST`, `WFC_CHAOS`, `WFC_START` | Gameplay / Integration | characters, modes, maps |

**Proposed hooks** (currently UNKNOWN in the gate):
- `WFC_TEAM=1` (Decepticon presets);
- a teleport below KillZ (kill-plane exercise);
- a pre-menu-gate input script (prove that gameplay ignores input while a menu owns it).

## Expected changes from tonight's specialist heads (read-only inspection, not merged)
- **Frontend 4b2fae1:**
  - a96f841 GL-state restore (world restored);
  - typing into text fields, quit-box routing, Choose Character ownership;
  - customization camera / preview pawn;
  - GFx GC fix (df79c6f: an intermittent Create a Character crash).
- **Rendering c22e356:**
  - per-frame world state reset (M11), preview pawn pose;
  - VISUALCHECK `noDepth` / `glErr` / `gl_entry_state`;
  - `WFC_MEMCYCLE`.
- **Gameplay 1a4e36b:**
  - explicit chassis fallback logging;
  - character-selection spawn gate;
  - per-form look settings.
- **AssetTools:** 33 chassis exported (robot / vehicle / character.json, all loadable). Only the IonBlaster weapon is
  exported, so loadout checks report ASSET MISSING for the other preset weapons.
