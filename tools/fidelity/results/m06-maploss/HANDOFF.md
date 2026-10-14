# M06 / M06b map-presentation loss: handoff for Rendering and Integration

**Product under test:** the user's exe, `Rebuild\build\release\bin\wfc_rebuild.exe`.
- Built 2026-10-04 20:18 from the M06b work; SHA-256 `f4c08f93…`.
- Run here byte-identical, in the same layout (`<tree>\build\release\bin` with the tree's `work\render` three levels up).
- No `WFC_RENDER_DATA`, a fresh or saved profile, and one graphical WFC instance at a time (the gate waits for other
  sessions' renderers).

## Why the old tests passed
See [AUDIT.md](AUDIT.md). In short:
- **Pixels looked in the wrong place.** Every pixel check used direct boot (the map matrix, sweeps, visual-compare's
  "131 / 131 unchanged", reference cameras), where the environment is drawn.
- **The right path was judged on state.** The frontend route, where the user plays, was judged on events and counters.
- **The product's VISUALCHECK has no expectation to fail against.** It PASSes `world=35` in play exactly as it PASSes
  `world=1641`.
- **The gates never ran the product the way the user does:**
  - render data was always passed or branch-generated;
  - a different exe layout (`work\ab\<n>\build-release\bin`);
  - a fresh profile at 1280×720 every time;
  - Release-only visual verdicts.

## The test that now detects it
`tools/fidelity/map-loss-gate.ps1 -Exe <the exe where the user runs it>`

The route: frontend → lobby-selected **Streets** → match end → lobby → **Berth** → lobby → **Streets**, in one process,
for four display states: fresh profile, saved 1280×720, saved 1920×1080, saved low textures + VSync.

Three independent expectations, none taken from the pipeline under test:
1. **Pixels.** World coverage with the HUD band and the player excluded (`Present-World`); FAIL when most in-play
   frames show no textured environment.
2. **Draws vs the same exe's direct boot at the same start.** In-play world draws must reach ≥ 50 % of it.
3. **The known-good runtime's view of the same start.** M05 1e14900, the last human-confirmed Streets, used as a
   corroborating reference.

It also records the process outcome and any Windows display-driver / crash events in each run window, the intro
side-edge content, and the HUD widget size at 720p vs 1080p.

`presentation-gate.ps1` (the authoritative M06 presentation gate) also catches it:
- `gameplay.world.*`;
- `gameplay.route_vs_direct`.

## Result on the user's exe: MAP PRESENTATION LOSS on 12 / 12 map visits
| state | Streets (first) | Berth | Streets (after Berth) |
|---|---|---|---|
| world detail, in play (healthy ≥ 0.15) | 0.03–0.07 | 0.11–0.16 + untextured floor | 0.03–0.06 |
| VISUALCHECK in-play world / BSP / materials (frontend route) | **35 / 20 / 32** | **152 / 19 / 45** | **36 / 21 / 33** |
| same exe, direct boot at the same start | 1641 / 357 / 160 | 1087 / 273 / 130 | 1739 / 451 / 166 |
| ratio | **0.02** | **0.14** | **0.02** |

- The same result holds in all four display states: fresh, 1280×720, 1920×1080, and low textures (low textures:
  69 / 491 world draws, still FAIL).
- **Correlation:**
  - the loss correlates with the **frontend-launched match only**;
  - it is **not** the launch layout (the render root resolves correctly; the load reports the full 325 materials and
    1975 lightmapped components);
  - **not** the resolution or the saved settings;
  - **not** the map transition: the first Streets visit is already lost; Berth and the return to Streets are equally
    lost; the lobbies stay at a constant 5 world draws.
- **Root cause (Frontend, confirmed by fix):**
  - bisected to Frontend b1fce97, the in-match HUD movie;
  - Frontend a96f841 explains it: the UI pass left `GL_DEPTH_TEST` / `GL_CULL_FACE` disabled for the next world frame.
- **Rendering note:** with that state left behind, the renderer issues only about 2 % of the environment draws (not
  just invisible ones). That is worth checking: culling / occlusion should probably not depend on GL state set by
  another pass.

## Other observations
- **Process / GPU.** All 8 user-exe runs and the fix runs exited cleanly; none hung, and **no display-driver events**
  occurred in any of my run windows.
  - The System log shows **seven Display 4101 resets** (2026-10-04 19:28, 19:29, 19:40, 20:25, 20:43, 20:45, 20:46).
  - Several sessions ran renderer windows concurrently in that period, including three of mine at 19:34–19:38. During
    this pass other sessions repeatedly started renderers while mine waited.
  - So the resets cannot be attributed to one build or one vendor from this data. Recommendation: reproduce with a
    single renderer system-wide.
- **Intro side edges:** not reproduced in windowed 1280×720 / 1920×1080; the side columns are black (≥ 0.98). It may
  need fullscreen or a non-16:9 display; human check.
- **HUD scale:** not reproduced in windowed 720p vs 1080p; the health widget is 0.102 of the width in both. It may
  need fullscreen or another aspect ratio; human check.

## Frontend fix a96f841: verification (the four questions)
| question | result |
|---|---|
| 1. complete Streets world restored? | **Yes.** Frontend route spawn / moving detail 0.15–0.21 in both matches (was 0.02–0.06); route vs direct 0.78 (was 0.28); map-loss gate: Streets → Berth → Streets all PASS in normal and 1080p (draw-count check not available: the lane build has no `WFC_VISUALCHECK`) |
| 2. original HUD still active? | **Yes.** Hud_GFX opens in both matches, hides during pause and returns on resume (4 / 4), announcements fire, and the HUD is drawn in every in-play frame |
| 3. previous frontend fixes still work? | See the table below |
| 4. gross-world-loss detector passes? | **Yes** on a96f841. It **still FAILs** the user's exe (which lacks a96f841): Integration must merge a96f841 before the next playtest |

Previous frontend fixes on a96f841:

| fix | result |
|---|---|
| pause menu cleared on Resume | PASS (similarity to the pause frame −0.03) |
| Extras → Movies | FULLY FUNCTIONAL: a movie plays, and it exits |
| Extras → Credits | starts and exits |
| Accounts → Create | FULLY FUNCTIONAL: typed text arrives (dump and frame), the dialog exits |
| Mouse/Keyboard Layout | read-only card, CONFIRMED ORIGINAL per Frontend decompile; 42 descriptions; PASS |
| quit / return routing | Quit → "Quit Game?" Yes → party lobby; Back + Yes → title (gate updated, route completes) |
| back / forward ×3, Extras Concept Art | FULLY FUNCTIONAL |
| Create a Character | exits correctly (detail → list → party lobby); **the preview model is never loaded** (empty preview pane) |
| character selection | UI PASS; Gameplay receives Jet4; body still Optimus (AssetTools + Gameplay, unchanged) |

**Remaining visible differences (owner):**
- the party / game lobby preview area beside the panel is empty (one blank area 0.61–0.84) — Frontend + Rendering
  (preview helpers in Rendering 504730f are not in a96f841);
- the selected body is Optimus — AssetTools + Gameplay;
- the Create a Character preview model is not loaded — Frontend + Rendering (preview entry points landed on Rendering after a96f841).
