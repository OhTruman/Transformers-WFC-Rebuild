# Overnight lane previews (2026-10-04): not a verdict

The lane heads were inspected as pushed: nothing merged, no product code changed. Exports were built with
`m05/build-target.ps1` into Experimental's `work/ab`. Frontend's heads contain integration/milestone-05 (1e14900), so a
Frontend export is the integrated product plus the Frontend lane. The other lanes branch from their M05 inputs and
can only be validated on their own surfaces until Integration merges them.

Final verdicts come only from `final-gate.ps1` on the next integration commit.

## agents/frontend: 76b8287 → 8b6466e → fd883dc → 2111ad9 → 0160cf1 (contains integration)
| check (playtest #) | 76b8287 | 8b6466e | 0160cf1 | note |
|---|---|---|---|---|
| intro video (1) | PASS 4/4 | PASS 4/4 | PASS 4/4 | |
| **intro audio (1)** | FAIL 0/4 | **PASS 4/4** `movie.audioStart` handles | PASS 4/4 | audibility / sync: human |
| title / menu background (2) | FAIL 94% black | FAIL 94% black | **drawn (25% black)** → HUMAN | grey low-poly sphere + rainbow-banded rings: looks like missing materials, not the Cybertron orbit (sheet) |
| lobby background (2) | FAIL 64% black | FAIL | **FAIL: flat untextured grey 34%** | a large grey card plane covers the right half (UI_CharacterCustomization_m CybertronCard / SpaceDome material?) |
| **frontend after Back / return (2)** | black (as before) | — | **FAIL: 84% black vs 25% first visit** | the scene is not restored after travelling back to UI_FrontEnd_m: **new defect candidate** |
| title keys (3) | PASS | PASS | PASS | CONFIRMED: the title reacts to Start only (FrontEnd_GFX inputEvent); keyboard Start = F3 |
| controller (4) | HUMAN | | | XInput mapped; not injectable |
| back navigation (5) | PASS (5 checks) | | PASS | B returns to the parent with focus restored; B at the party root = QuitToMainMenu |
| **loading keeps updating (6)** | **FAIL: frozen ~10 s** (4/41 captures change) | | **animates** (12/46 change; spinner, then "in Streets" and the tips appear) | `core::loadYield`. A broken bitmap ("h:" glyph) is drawn centre-screen: human / Frontend |
| overlay gone (7) | PASS | | PASS | |
| PC SKU (no Start gate, Exit Game) | new default | | | the gate pins `WFC_PLATFORM=XBOX360` (spec); R8 covers PC |

- **Needs Integration:** `Application*.cpp` / `CMakeLists.txt` (launch path); match HUD (Hud_GFX), which waits on
  Rendering's Canvas / HUD ownership.
- **Conflicts with 1e14900:** none.
- **New hooks:** `WFC_PLATFORM`, `WFC_LOADSHOTS`, `WFC_NO_FRONTEND_SCENE`, `WFC_SCENE_ONLY`, `WFC_GFX_EMPTY`, `WFC_CACHE`.
  New script steps: `ui:<Action>`, `mouse:`, `click:`, `clickclip:`.
- **Stale test:** the Xbox 360 key paths would break on the PC default. The gate now pins the console presentation and
  uses `ui:` actions through UiBindings when available.

## agents/gameplay: 3e4650d → 391b7f0 → 3cf84d8 → 2c3854d
Isolated self-tests on 3cf84d8 (`work/preview/gp21_*`):
- `WFC_TDMTEST` 39/39.
- CAMSYNC at 144 Hz, per-frame camera vs per-tick cache:
  - robot 0.0003° vs 1.276°;
  - hover 0.011° vs 1.189°;
  - boost 0.023° vs 0.649°.

  This is the "interlacing" fix, matching Gameplay's table.
- `WFC_XFORMTEST` 0/76.
- **`WFC_CHAOS` (123 starts) still reports 1 run under the map at `TnTeamPlayerStart_10894` (1.42 m, 8 frames).** It
  is unchanged since d541c78 and is also present in integration 1e14900.

Other notes:
- **Not yet previewed:** 2c3854d (boost-state flicker, vehicle slope contact, high-refresh camera guard). These address
  playtest #10 / #11.
- **Needs Integration:** World / Match / Application seams. Conflicts with 1e14900: STATUS.md, `src/game/World.cpp`.
- **New hooks:** `WFC_CAMSYNC`, `WFC_MODEPLAYTEST`, `WFC_VEHDROPLOG`, `WFC_CAMRE`.

## agents/rendering: 429bcf3 → … → be90034
On 2668cfc, `motion-jitter.ps1` parts C and D were validated with Rendering's diagnostic hooks:
- frame pacing at 60 / 144 / 240 Hz: robot and vehicle PASS (M04-style camera), robot camera pops INFO;
- per-frame vehicle and robot frames (`WFC_SHOTEVERY`): no ping-pong.

This proves the detector Integration needs for the interlacing regression.

- **New in M08 / M09:** a Canvas HUD layer (original fonts and marker draw rules), frontend 3D scenes, loading yields,
  roster-driven character materials.
- **Needs Integration:** `Application.cpp`. Conflicts with 1e14900: FIDELITY.md, `src/core/Application.cpp`.
- **The gate needs these hooks carried by Integration:** `WFC_RENDERHZ`, `WFC_CAMLOG`, `WFC_SHOTEVERY`. Without them,
  jitter C / D report WAITING.

## agents/systems: 831a6dd → 5f8d7c9 → 8769ecb → 0f293c7
- Movie audio (the Frontend hookup verified on Frontend 8b6466e).
- Match / announcer audio through a Gameplay-event adapter. The gate's `AUDIO.match_start_music` expects
  `BL_LVL_MP_MX.DM_START` (RE OV A5) and `no_menu_music_in_match`.
- **Needs Integration:** `MatchAudio` + `World.h`. Conflicts: FIDELITY.md, STATUS.md.

## AssetTools / RE
- **RE 1ca9c28:**
  - **no minimap, radar or compass** (CONFIRMED by exhaustive absence). The minimap test is retired to "none
    presented".
  - Kill feed: Hud_GFX `GameMessage`, 5 rows, 5 s + 1 s fade, team colours.
  - Match music `DM_START` / `DM_FINALSTRETCH_LP` / `DM_END_*`: the old "no music in a match" check is retired.
  - Lobby / title backgrounds are live 3D levels.
- **AssetTools:** canonical roster, HUD element map + negative minimap search, UI level scenes (per-screen
  black-background verdicts), the generic map pipeline (Gorge end to end).

## Tests that entered the final gate tonight
- `playtest-acceptance.ps1`:
  - intro video + audio;
  - backgrounds per screen (black / flat untextured grey FAIL, drawn = HUMAN);
  - scene restored after return;
  - title keys per the shipped handler;
  - back navigation;
  - loading longest-frozen interval;
  - overlay gone;
  - match-state vs HUD table;
  - kill feed per RE A2;
  - PointEvent;
  - result state;
  - minimap absent.
- `motion-jitter.ps1`:
  - A: simulation / animation steps;
  - B: composed-frame ping-pong;
  - C: frame pacing via `CAMLOG` / `RENDERHZ`;
  - D: per-frame vehicle frames;
  - E: vehicle states vs RE PT 1.2 (nitro 3 s / 8 s / ×1.5 / ×0.3, refusal in cooldown).
- `vehicle-collision.ps1`:
  - robot scenarios;
  - ramp hard stops (no authored wall / step within 4 m ahead);
  - visible geometry vs authored block flags (look-here list).
- `transform-stress.ps1`: rapid repeated transforms; the floor-above-the-pawn detector; transient sink vs fall-through.
- Gate:
  - R7 score-limit lifecycle ×3;
  - R8 PC SKU;
  - `ui:` logical input;
  - match music;
  - per-match audio baseline (`audio.baseline` / `audio.loaded` / `audio.unloaded`).
