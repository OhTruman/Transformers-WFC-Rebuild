# Milestone 05 end-to-end gate (Release)

- target: ref=1e14900b150349f8da2affb024106f3d7b83cc50; sha=1e14900b150349f8da2affb024106f3d7b83cc50; built=2026-10-04T00:36:46
- exe: `F:\Transformers Rebuild\Rebuild-Experimental\work\ab\m5int\build-release\bin\wfc_rebuild.exe`
- runs: R4; cycles 20; 2026-10-04 05:42

**PASS 23 / FAIL 0 / KNOWN 0 / INFO 1 / SKIP 5 / HUMAN-CHECK 0**

## DATA

| status | check | owner | evidence |
|---|---|---|---|
| PASS | manifest.frontend_flow | AssetTools | AssetTools manifests\frontend_flow.json |
| PASS | manifest.frontend_gfx | AssetTools | AssetTools manifests\frontend_gfx.json |
| PASS | manifest.frontend_modes | AssetTools | AssetTools manifests\frontend_modes.json |
| PASS | manifest.frontend_maps | AssetTools | AssetTools manifests\frontend_maps.json |
| PASS | manifest.frontend_loading | AssetTools | AssetTools manifests\frontend_loading.json |
| PASS | manifest.frontend_audio | AssetTools | AssetTools manifests\frontend_audio.json |
| PASS | manifest.frontend_localization | AssetTools | AssetTools manifests\frontend_localization.json |
| PASS | manifest.mp_map_catalog | AssetTools | AssetTools manifests\mp_map_catalog.json |
| PASS | frontend_unit_tests | Frontend | wfc_frontend_tests exit 0; failing:  |

## AUDIO

| status | check | owner | evidence |
|---|---|---|---|
| PASS | map_audio_released_each_unload | Systems | after each of 20 unloads (voices / instances / level cues / PCM MB): 0/0/0/36.489 / 0/0/0/36.489 / 0/0/0/36.489 / 0/0/0/36.489 / 0/0/0/36.489 / 0/0/0/36.489 / 0/0/0/36.489 / 0/0/0/36.489 / 0/0/0/36.489 / 0/0/0/36.489 / 0/0/0/36.489 / 0/0/0/36.489 / 0/0/0/36.489 / 0/0/0/36.489 / 0/0/0/36.489 / 0/0/0/36.489 / 0/0/0/36.489 / 0/0/0/36.489 / 0/0/0/36.489 / 0/0/0/36.489; pre-load baseline PCM 36.489 MB (Systems guarantee: 0 / 0 / 0 / baseline) |
| PASS | map_audio_loaded_each_match | Systems | map audio at each match load: level MP_IAC_Streets, level cues 32, PCM 97.179 / 97.179 / 97.179 / 97.179 / 97.179 / 97.179 / 97.179 / 97.179 / 97.179 / 97.179 / 97.179 / 97.179 / 97.179 / 97.179 / 97.179 / 97.179 / 97.179 / 97.179 / 97.179 / 97.179 MB |
| PASS | menu_music_resumes | Systems/Frontend | frontend music starts: 21 for 20 returns (+ boot) |
| PASS | no_menu_music_in_matches | Systems | frontend / lobby music starts inside 20 matches: 0 (game-type DM_* music excluded: it belongs to the match, OV A5) |
| PASS | no_stale_reverb | Systems | after 20 returns, the next reverb change still starts from a map preset (stale map reverb): 0 |
| PASS | doubled_sounds_cycles | Systems | doubled cue starts over 20 matches: 0  |

## LIFETIME

| status | check | owner | evidence |
|---|---|---|---|
| PASS | cycles_completed | Integration | 20/20 frontend -> Streets TDM (15 s of walking, turning, firing, boosting) -> frontend cycles; exit 0 |
| PASS | memory_stabilizes |  | private MB after each unload: 2467.289 > 2467.516 > 2476.441 > 2488.77 > 2488.648 > 2494.867 > 2499.699 > 2509.574 > 2571.848 > 2531.289 > 2512.676 > 2559.328 > 2534.512 > 2537.41 > 2536.176 > 2531.84 > 2554.992 > 2539.207 > 2531.715 > 2545.457 (slope 4 MB/cycle after cycle 1); at the frontend after each return: 1386.3 > 1389.5 > 1403.4 > 1391.8 > 1395.6 > 1397.4 > 1401.3 > 1435.5 > 1434 > 1434.8 > 1439.1 > 1435.2 > 1433.9 > 1453.4 > 1453.5 > 1454.9 > 1451.8 > 1454 > 1459.4 > 1464 (slope 4.3).  |
| INFO | memory_peak |  | peak private memory over 20 cycles: 3327.6 MB |
| PASS | handles_threads | Integration | handles at each return: 655 > 667 > 667 > 676 > 685 > 687 > 695 > 705 > 715 > 719 > 727 > 735 > 743 > 754 > 762 > 768 > 776 > 784 > 794 > 804 (slope 7.8); threads: 22 > 18 > 14 > 16 > 16 > 13 > 16 > 18 > 18 > 15 > 13 > 14 > 15 > 14 > 14 > 13 > 13 > 13 > 17 > 17 (slope -0.1) |
| PASS | gl_release_per_return | Rendering | GL objects released at each return: textures=72 buffers=0 framebuffers=0 renderbuffers=0 vertexArrays=0 programs=0 |
| PASS | audio_pcm_per_match | Systems | peak decoded PCM per match: 97.2 > 97.2 > 97.2 > 97.2 > 97.2 > 97.2 > 97.2 > 97.2 > 97.2 > 97.2 > 97.2 > 97.2 > 97.2 > 97.2 > 97.2 > 97.2 > 97.2 > 97.2 > 97.2 > 97.2 MB; peak live instances per match: 68 > 69 > 68 > 68 > 68 > 68 > 68 > 68 > 68 > 68 > 68 > 68 > 68 > 68 > 68 > 69 > 68 > 68 > 69 > 69 |
| PASS | ui_movies_per_cycle | Frontend | GFx movies opened per cycle (after the first): 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6 |
| PASS | no_stale_flow_state | Frontend | returned snapshots with leftover match / lobby state: 0 |
| PASS | one_launch_per_cycle | Gameplay/Integration | Gameplay launches 20, frontend match loads 20 |
| SKIP | counter.map_collision | Integration | no product counter for map_collision across travel (proposal: one 'LIFETIME <name>=<count>' line per level.begin, protocols\RUNTIME-EVENTS.md); covered indirectly by process memory / handles |
| SKIP | counter.map_objects | Integration | no product counter for map_objects across travel (proposal: one 'LIFETIME <name>=<count>' line per level.begin, protocols\RUNTIME-EVENTS.md); covered indirectly by process memory / handles |
| SKIP | counter.particles | Integration | no product counter for particles across travel (proposal: one 'LIFETIME <name>=<count>' line per level.begin, protocols\RUNTIME-EVENTS.md); covered indirectly by process memory / handles |
| SKIP | counter.event_queues | Integration | no product counter for event_queues across travel (proposal: one 'LIFETIME <name>=<count>' line per level.begin, protocols\RUNTIME-EVENTS.md); covered indirectly by process memory / handles |
| SKIP | counter.timers | Integration | no product counter for timers across travel (proposal: one 'LIFETIME <name>=<count>' line per level.begin, protocols\RUNTIME-EVENTS.md); covered indirectly by process memory / handles |

