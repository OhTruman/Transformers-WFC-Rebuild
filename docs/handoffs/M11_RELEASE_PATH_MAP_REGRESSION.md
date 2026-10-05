# M06b human playtest: incomplete Streets / Berth in the real Release build (Rendering M11)

**For:** Integration (merge + rebuild), Experimental (validation), Frontend (FYI).

## CONFIRMED ROOT CAUSE
**Every map was drawn without depth testing after the frontend.**
- The 3D frame (`GLRenderer::beginFrame` → `wfc::Pipeline::beginFrame`) never established depth test, depth func,
  scissor, stencil, colour mask or polygon mode. It set them once in `init()` and inherited whatever the previous user
  of the GL context left.
- The frontend's GFx host leaves `GL_DEPTH_TEST` disabled.
- After title → lobby → match, every map surface was therefore drawn in submission order with no depth testing:
  - architecture hidden by whatever was submitted after it;
  - flat, translucent-looking layers;
  - effects "floating" in space;
  - Optimus, HUD, particles and some floors and decals still visible.
- A direct boot (every automated harness) has no GFx host before the first frame, so `init()`'s state was still in
  place and the map rendered correctly.

**Evidence.**
- **The human's session log** (`F:\Transformers Rebuild\wfc.log`, account OhTruman, 20:50) shows complete loads:
  - Streets `world.glb` 1,983,988 triangles, 2,249 submeshes, 2,239 programs, 2,081 / 2,249 lightmap bindings, original
    shader path;
  - Berth 1,292,885 triangles, 1,755 programs.
  - Nothing was missing on disk or at load.
- **Real Release executable** (`Rebuild\build\release\bin\wfc_rebuild.exe`), the human's profile, the same pinned camera:

  | path | draws (world / BSP / culled / materials) | entry GL state | image |
  |---|---|---|---|
  | direct boot | 1900 (1468 / 394 / 939 / 154) | `depth=1` | correct |
  | frontend → Streets | identical | `depth=0` | broken: 88 % of pixels differ; the recording |

  Identical submission plus a broken image points to GPU state, not data.
- **Fix:** integration 681fd29 plus this fix, Release layout, the integration render data (junction), the human's
  profile: frontend → Streets matches direct boot apart from the HUD and the not-yet-spawned character.
- **Reproduction switch:** `WFC_M11_INHERITSTATE=1` skips the per-frame state, for this reproduction only. The new
  check FAILs every in-match frame with "1812 opaque draws without depth testing". The image metrics (black 0.1 %,
  flat 7 %) stayed normal, which is why the M10 checks passed this build.

**Two owners, both fixed.**
- **Frontend** (Experimental's bisect: b1fce97 made the HUD run the GFx pass every match frame): the pass now saves and
  restores every GL state it touches (agents/frontend a96f841).
- **Rendering** (this commit): the 3D frame establishes its own state.

Merge both: either one alone fixes the human route, and together they cover any future 2D layer.

**Memory (Frontend observed +460 MB on a second Streets visit in Debug).**
- In the Release single-process session, loaded Streets went 2,867 → 2,940 MB on the second visit, then stayed flat
  (2,945 / 2,950 / 2,950 MB). Berth went 2,725 → 2,687 MB.
- That is a one-time step, not per-visit retention. The Debug observation (Streets → Seed → Streets) is not reproduced
  here [UNKNOWN for Debug / Seed].

**Why M10 missed it.** The M10 transition audit recorded the inherited `depth=0` but judged it harmless. It compared two
matches that both came after the frontend (so both were affected), and a direct boot from a different camera.
Corrected here.

## Disproved / not involved
- **Render-data root, selected map, files opened:** all correct in the human log. The texture paths in
  `materials_glsl.json` are absolute; the only relative path, `lvv_0.bin`, resolves against the map folder.
- **Resolution / fullscreen:** no correlation. The map renders identically in each of these:
  - cold boot at 1280×720 windowed, 1920×1080 windowed and fullscreen (the desktop's 2560×1440);
  - a runtime change to 1920×1080, then fullscreen, then entering Streets;
  - a restart with the settings persisted.
  - The human's own broken session changed no display settings.
- **Map transitions:** Streets → lobby → Berth → lobby → Streets in one process. Both Streets matches draw identically
  (2033 draws, 1641 world / 357 BSP); Berth draws 1273 world / 288 BSP. No retained or stale resources.
- **GL errors:** 0 in every run (`glGetError` drained each checked frame).

## Fixed (agents/rendering)
1. **`GLRenderer::beginFrame` establishes the frame's GL state:**
   - depth test on, `LEQUAL`, depth writes;
   - no scissor or stencil;
   - full colour mask, fill polygons, back / CCW culling mode, blend off.

   Per-material blend, depth writes and culling stay per draw.
2. **Guards that fail on this exact build:**
   - every opaque draw issued without depth testing is counted (`opaque_no_depth_test`), and any such draw FAILs the
     verdict;
   - GL errors are counted and logged per checked frame.
3. **`tools/render/release_path_check.sh <exe> <scratch> [profile]`:**
   - the player's path, with no `WFC_RENDER_DATA` override: frontend boot, Multiplayer → Private Match → TDM → Streets
     → lobby → Berth → lobby → Streets;
   - it fails on any VISUALCHECK FAIL, missing render root, missing capture, or a map drawing below its structural floor
     (Streets ≥ 800 world / 150 BSP draws, Berth ≥ 600 / 100);
   - verified PASS on the fixed build and FAIL on the reproduced bug.

## Integration: exact steps
1. Merge agents/rendering, which brings:
   - M10b `359da41`, `504730f`;
   - this M11 commit.

   Expect a conflict only where `src/core/Application.cpp` includes collide (take both).
2. Rebuild Debug and Release.
3. Regenerate the render data with the merged tools:
   `powershell -ExecutionPolicy Bypass -File tools\render\build_render_data.ps1 -Map Standard`.
   - Required for the lobby: the UI families get the roster materials and the `TR_AllShader_p` fallback, and the lobby
     scene now prefers the persistent `UI_PartyLobby` / `UI_Lobby` family.
   - Streets comes out byte-identical in geometry, lighting, LVV, decals, movers and FX; materials have 0 GLSL changes.
   - The other MP maps keep their existing data unless you regenerate them too.
4. Validate from the real layout:
   `bash tools/render/release_path_check.sh build/release/bin/wfc_rebuild.exe work/<scratch> [profile]`.
   Run it from the Rebuild root, one game process at a time.

## Not Rendering (handoffs)
- **Fullscreen ignores the saved resolution:** fullscreen comes up at the desktop's 2560×1440, not the saved 1920×1080
  (platform window / Frontend).
- **Oversized HUD and side-visible movie:** GFx stage scaling against the window aspect (Frontend's presenter). Not
  correlated with the map loss.
- **AMD driver resets:** no GL errors, and stable in the long single-process session (see FIDELITY M11). A driver reset
  that persists after this fix needs its own capture; UNKNOWN.
