# Lane watch vs origin/integration/milestone-05 (1e14900) - 2026-10-04 03:56

Nothing here is merged or integrated: these are the lane heads as pushed. Final verdicts only on an integration commit.

## agents/frontend  `0160cf1`  (5 ahead, MOVED since last watch)

- 0160cf1 Frontend scenes drawn under the menus: AssetTools UI level exports + Kismet camera
- 2111ad9 Frontend scenes: Kismet / matinee / camera runtime for the live levels under the menus
- fd883dc Loading screen keeps presenting during the synchronous map load (core::loadYield)
- 8b6466e Frontend: movie audio for the intro chain on the Systems device
- 76b8287 Frontend: PC SKU presentation, mouse support, logical UI bindings, start-screen rule

- areas: other 12, SEAM 5, frontend 17, gameplay 1, rendering 1, tools 2
- **validate in isolation:** partly - lane-local parts only
- **needs Integration:** CMakeLists.txt, src/core/Application.cpp, src/core/Application.h, src/core/Application_Frontend.cpp, src/game/World.cpp (launch path / World / Application seams)
- conflicts with origin/integration/milestone-05: none
- hooks added: WFC_NO_FRONTEND_SCENE, WFC_LOADSHOTS, WFC_ASSETS, WFC_PLATFORM, WFC_CACHE, WFC_SCENE_ONLY, WFC_GFX_EMPTY, WFC_DUMP_SHAPE; removed: -
- new trace evidence: flow input.bindings, scene.release, script.ui, script.clickclip, platform, movie.audio, movie.audioPrefetch, movie.audioStart, scene.levels, scene.view, scene.enter, scene.matinee, gfx.mouse, gfx.cursor; log -

## agents/gameplay  `2c3854d`  (4 ahead, MOVED since last watch)

- 2c3854d Pass 21d: boost-state flicker fixed at the source, vehicle slope contact, high-refresh camera guard
- 3cf84d8 Pass 21c: Conquest (DOM) and Power Struggle (KOTH) on the shared match framework
- 391b7f0 Pass 21b: match HUD state, kill feed events, match-end state, health regeneration
- 3e4650d Pass 21a: fix M05 "interlaced" character (camera per render frame), pre-match spectator presentation

- areas: docs 2, SEAM 6, gameplay 11
- **validate in isolation:** partly - lane-local parts only
- **needs Integration:** src/core/Application.cpp, src/core/Application.h, src/game/Match.cpp, src/game/Match.h, src/game/World.cpp, src/game/World.h (launch path / World / Application seams)
- conflicts with origin/integration/milestone-05: STATUS.md, src/game/World.cpp
- hooks added: WFC_CAMSYNC, WFC_MODEPLAYTEST, WFC_VEHDROPLOG, WFC_CAMRE; removed: -
- new trace evidence: flow -; log CAMSYNC, MODEPLAY, VEHDROP, VEHTEST
- owner claims (new doc headings): ## PASS 21d ΓÇö BOOST-STATE FLICKER, VEHICLE CONTACT, HIGH-REFRESH GUARD (2026-10-04, gameplay agent) / ### Boost exhaust open / close (human-reported; diagnosed by Rendering) ΓÇö FIXED at the source / ### Vehicle contact (ramps / angle changes) / ### High-refresh pawn / camera separation ΓÇö regression guard / ## PASS 21c ΓÇö CONQUEST (DOM) AND POWER STRUGGLE (KOTH) ON THE SHARED MATCH FRAMEWORK (2026-10-04, gameplay agent) / ### Shared framework (one Match, not one engine per mode) / ### Conquest (DOM) ΓÇö CONFIRMED bytecode / ### Power Struggle (KOTH) ΓÇö CONFIRMED bytecode

## agents/rendering  `be90034`  (7 ahead, MOVED since last watch)

- be90034 Rendering M09: roster-driven character materials, per-owner character state, uniform caching, docs
- 004327c Rendering M09: frontend 3D scenes, loading yields, map-generic render data, TextureSample RGB output
- 314bd06 Rendering M08: WFC_PICK compares the movement collision ray; Canvas simple-element shader decoded
- 2668cfc Rendering M08: FIDELITY / STATUS (regression diagnoses, HUD ownership, Canvas layer, validation)
- 658d2b8 Rendering M08: WFC_PICK authored-surface diagnostic (IRenderer::pickWorld); boost FX flicker handoff to Gameplay
- ec80577 Rendering M08: Canvas HUD layer (original fonts + marker draw rules), in-match HUD ownership handoff
- 429bcf3 Rendering M08: diagnose the M05 character 'interlacing' regression (Gameplay camera frame pacing) + handoff patch

- areas: docs 6, other 1, SEAM 1, rendering 18, tools 10
- **validate in isolation:** partly - lane-local parts only
- **needs Integration:** src/core/Application.cpp (launch path / World / Application seams)
- conflicts with origin/integration/milestone-05: FIDELITY.md, src/core/Application.cpp
- hooks added: WFC_FRONTENDSCENE, WFC_SCENECAM, WFC_SMOKE_FRAMES, WFC_SHOT, WFC_RENDERHZ, WFC_CAMLOG, WFC_PICK, WFC_MARKERTEST, WFC_SHOTEVERY, WFC_RENDER_DATA; removed: -
- new trace evidence: flow -; log CAMLOG, PICK
- owner claims (new doc headings): ## MILESTONE 09 ΓÇö FRONTEND SCENES, LOADING, ROSTER READINESS, RE OVERNIGHT HUD (2026-10-04) / ## MILESTONE 08 ΓÇö PLAYTEST REGRESSIONS, IN-MATCH HUD OWNERSHIP, CANVAS LAYER (2026-10-04) / ## RENDERING MILESTONE 09 (2026-10-04, agents/rendering) ΓÇö frontend scenes, loading, roster readiness / ## RENDERING MILESTONE 08 (2026-10-04, agents/rendering) ΓÇö playtest regressions, HUD ownership, Canvas layer / ## 2b. Canvas text and HUD markers (M08) / ## 2c. Frontend 3D scenes, loading yields, characters (M09) / # Handoff to Frontend (and Gameplay): the in-match HUD (Hud_GFX) and the Canvas layer / ## Ownership: one presenter per original layer

## agents/systems  `0f293c7`  (4 ahead, MOVED since last watch)

- 0f293c7 Systems M07: documentation, audio handoff, verified Frontend / Gameplay patches
- 8769ecb Systems M07: MatchAudio Gameplay-event adapter (mode tag -> game-type message class)
- 5f8d7c9 Systems M07 checkpoint: match / announcer audio, Gorge manifest path, Frontend movie-audio hookup patch
- 831a6dd Systems M07 checkpoint: movie audio (Bink tracks through Systems), unflushable movie preset

- areas: docs 3, other 4, systems 12, gameplay 15, SEAM 3, tools 8
- **validate in isolation:** partly - lane-local parts only
- **needs Integration:** src/game/MatchAudio.cpp, src/game/MatchAudio.h, src/game/World.h (launch path / World / Application seams)
- conflicts with origin/integration/milestone-05: FIDELITY.md, STATUS.md
- hooks added: WFC_MOVIE_LANGSLOT, WFC_MATCHAUDIOLOG; removed: -
- new trace evidence: flow -; log ANNOUNCER, MATCHAUDIO
- owner claims (new doc headings): ## MILESTONE 07 SYSTEMS ΓÇö MOVIE AUDIO, MATCH / ANNOUNCER AUDIO, LIFECYCLE RE-VALIDATION (2026-10-04, agents/systems) / ### Boot-movie audio / ### Match / announcer audio (MatchAudio) / ### Second map / ### Loading "frozen" / ### Classification / ### Test totals / ## SYSTEMS MILESTONE 07 (2026-10-04) ΓÇö boot-movie audio, match / announcer audio

## Pairwise merge preview (lane heads)

| pair | conflicting paths |
|---|---|
| frontend + gameplay | STATUS.md, src/game/World.cpp |
| frontend + rendering | FIDELITY.md, src/core/Application.cpp, src/render/gl/WfcPipeline.cpp |
| frontend + systems | FIDELITY.md, STATUS.md |
| gameplay + rendering | src/assets/SkinnedModel.cpp, src/assets/SkinnedModel.h, src/core/Application.cpp, src/game/World.cpp, src/render/Renderer.h |
| gameplay + systems | STATUS.md, src/core/Application.cpp, src/game/PlayerController.h, src/game/World.cpp |
| rendering + systems | FIDELITY.md, src/game/VehicleFx.cpp |

## AssetTools `6febd79` (MOVED)

- 6febd79 RE OVERNIGHT 2026-10-04 incorporated: canonical roster package (RE chassis ids, 0 mismatches, transform clips, resolved stats), HUD element map + spatial cues + closed minimap verdict, runtime-ready UI scene render indexes
- fef844d Roster availability labels (MULTIPLAYER CONFIRMED / SHARED / CAMPAIGN ONLY), class icons, extra frontend screens (campaign, escalation, private match, friends, leaderboards, challenges, create character)
- 6ff8415 Generic map pipeline: run_map_pipeline.py + map_complete.py; Gorge end to end (17 stages, deterministic, completeness report + blockers by owner)
- 2046233 Frontend backgrounds: UI level scenes through the map pipeline + ui_scenes.json (streaming, cameras, Matinee keys, skeletal actors, per-screen black-background verdicts)
- 43f2679 HUD package: ownership, anchor layout + runtime widget map, elements, icon sets, crosshair rules; minimap/radar negative search
- 36f37a5 MP content: character roster, classes, weapons, animation compatibility, character-select UI, AI/bot evidence, mode visuals, bundles

## RE-Workspace `1ca9c28`

- 1ca9c28 Overnight RE: shipped MP HUD layout/kill feed/announcements/end presentation, no-minimap proof, vehicle LT/overhang, input model, frontend background ownership, chassis roster, AI architecture + MP nav gap, CTF/bomb return rules
- 28debca M05 playtest RE: vehicle input/state/suspension model, versus mode spec, settings, frontend scene, loading thread, MP character presets, bots absence, scoreboard
- 85e340c M05 blocker answers: lobby map/mode selector behaviour, TDM bootstrap payload, team/spawn/death/end/restart semantics, HUD ownership, TDM name tags, pickup timers/regen
- bea63b4 Handoff index: M05 Phase 8 additions (markers, pickups)
- 61d7bb9 M05: health pickup is a full heal (SHT_AddAllSegments), overshield = HealthMax+550; segmented health table; instant-respawn source
- 00e40dc M05 Phase 8: HUD player/objective marker semantics (Canvas markers, TDM ally/enemy tags), flag marker timing, pickup acceptance rules, factory mesh zero-offset + actor spin

