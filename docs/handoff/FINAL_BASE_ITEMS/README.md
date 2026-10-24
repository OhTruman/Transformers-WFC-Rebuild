# FINAL_BASE_ITEMS: af4d0731 (package 6) + the simulation through 7f65db0e

Six patches cut with `git diff --binary af4d0731 7f65db0e -- <paths>`. They cover every path except the renderers and the GFx UI,
so the base keeps package 6's rendering (src/render/gl, src/render/d3d12, src/ui) and runs the head's simulation.

## Apply order

Apply in a git checkout of af4d0731, with `git apply <patch>`. They touch disjoint paths, so any order works; this is the
recommended one.

1. `01_render_interface_headers.patch`: src/render/Renderer.h, Camera.h, Mesh.h. The IRenderer additions are default no-op
   virtuals, so package 6's GL renderer compiles unchanged. Camera gains roll (0 = the same view matrix); SubMesh gains glTF
   mesh / node indices (collision physmats).
2. `02_assets_platform_audio.patch`: src/assets, src/platform, src/audio.
3. `03_game_sim.patch`: src/game. This is all of the simulation:
   - versus: the spread, the dodge cap, bot skill, leave / Forfeit / leave-to-bot, hit reactions;
   - Escalation: E1–E3b, the strand exemption, NAVOFF, M10 / M11;
   - campaign: Kismet, matinee, interacts, Omega, destructibles.
4. `04_net.patch`: src/net (online MP; Leave / Forfeit go through the session).
5. `05_core_frontend_build.patch`: src/core (headless match runner, tests, net app), src/frontend, src/main.cpp, CMakeLists.txt
   (src/net glob, ws2_32, optional Opus).
6. `06_data.patch`: data/ (campaign audio data, bot_skill.json, iconic_presets.json, gameplay tables).

Note: `git apply` run from a subfolder of another repository's worktree applies only part of the patches (memory:
git-apply-in-archive-noop), so use a checkout of af4d0731 itself, or `git init` the extracted tree first, as the check below did.

## Verification

- **Patches:** applied with `git apply` to a fresh af4d0731 tree, the result is identical (ignoring CR) to the overlay that was
  built and tested.
- **Build:** the overlay builds clean (Release, llvm-mingw).
- **Same match, headless:** compared against the 7f65db0e head build, `WFC_SIMHASH=1`, the identical SIMHASH stream at every step:
  - MP_IAC_Streets TDM, 4 + 5 bots, WFC_PLAYERBOT, 60 s: 4,201 steps;
  - MP_ESC_Remnant ESC, WFC_PLAYERBOT, 120 s: 1,675 steps (the run ends at the player's game over in both);
  - MP_ESC_BrokenHope ESC, WFC_PLAYERBOT, 120 s: 2,530 steps.
- **The Escalation maps 505 / 506** boot in the base (both above).

## What stays at package 6 in the base

- The GL renderer: RT v3, X3 volumetrics, water / smoke / lens flare rows, the campaign post-process, chapter streaming draw.
- The D3D12 presenter / RT bridge.
- GfxPresenter: the newer HUD pieces, e.g. Escalation / campaign widgets, are not drawn.
- The campaign's render-side calls (setMapActorTransform, unloadChapterUnit, setCinematicView, ...) are no-ops in the base. The
  campaign simulation still runs.

## 07: the campaign gate items (sim through 0865b1fc + roller mines 6427ffb6)

`07_campaign_gate_items.patch`: the same paths, from 7f65db0e to 0865b1fc + GAMEPLAY_CAMPAIGN_MINES. Apply it after 01–06. It
carries:
- the campaign transforming world actors (TnSeqAct_TransformWorldProp, TransformingActors::loadChapter);
- the pawn factories' death outputs and the sentries;
- the chapter start at the first checkpoint;
- the roller mines;
- two neighbours that came in with those heads: Rendering's assets/Gltf.cpp actor-key fix (the bind-pose actor nodes keyed by actor)
  and the Frontend's interact-prompt glue (core/Application_Frontend.cpp).

Verified like 01–06:
- the 01–07 chain applies to a fresh af4d0731 and reproduces the tested tree exactly;
- headless against the head build (0865b1fc + MINES), the SIMHASH stream is identical at every step: Streets TDM 4,201, Remnant ESC
  1,675, BrokenHope ESC 2,530.

## 08: the campaign ToggleHidden / Destroy (dffcf31f -> 59cb3162)

`08_campaign_hide_destroy.patch`: `git diff --binary 903b2ab7 59cb3162` over the same paths. 903b2ab7 equals the 01–07 tree (checked by
file), and the diff is GAMEPLAY_CAMPAIGN_HIDE (Campaign.h, World.h, WorldCampaign.cpp, WorldCampaignRuntime.cpp). Apply it after 07.

Verified:
- the 01–08 chain applies to a fresh af4d0731 and reproduces the tested tree exactly;
- headless against the 59cb3162 head build, the SIMHASH stream is identical at every step: Streets TDM 4,201, Remnant ESC 1,675,
  BrokenHope ESC 2,530, and A1_IAC campaign 60 s (3,749 steps), so the base is exact for the campaign too.

## 09: the campaign authored-hidden keys (59cb3162 -> 78caf2bc)

`09_campaign_hidden_keys.patch`: `git diff --binary 59cb3162 78caf2bc` over the same paths. It touches WorldCampaignRuntime.cpp only:
the authored bHidden is sent for drawable classes only, and names that collide across levels are left visible. Apply it after 08.

Verified: the 01–09 chain applies to a fresh af4d0731 and matches 78caf2bc's tree over every patched path (by file). It is
campaign-only, so the versus / Escalation same-match of 07 / 08 is unchanged.

## 10: campaign hides by object path (78caf2bc -> b315e585 + GAMEPLAY_CAMPAIGN_HIDE_PATHS)

`10_campaign_hide_paths.patch`: the same paths, from 78caf2bc to b315e585 + GAMEPLAY_CAMPAIGN_HIDE_PATHS (77b40439). It contains:
- Gameplay's object-path keys: WorldCampaignRuntime.cpp;
- the asset / interface parts of Rendering's ACTOR_PATH: assets/Gltf.cpp reads extras.actor_path, and render/Mesh.h gains the
  SubMesh field.

The renderer-side matching itself stays at package 6 in the base. The sim is unaffected, since hides are render-only. Apply it after
09. Verified: the 01–10 chain applies to a fresh af4d0731 and matches the tested tree over every patched path.

## 11: campaign teleport, character vars, hazards by unit (b315e585 + HIDE_PATHS -> 35ace85b + GAMEPLAY_CAMPAIGN_TELEPORT)

`11_campaign_teleport_hazards.patch`: the same paths, from the 10 tree to 35ace85b + GAMEPLAY_CAMPAIGN_TELEPORT (a7fedd94). It covers:
- SeqAct_Teleport + TnSeqVar_Character;
- campaign hazard volumes only while their unit is loaded;
- headless movie requests ending at once.
Apply it after 10.

Verified:
- the 01–11 chain applies to a fresh af4d0731 and matches the tested tree over every patched path;
- headless against the 35ace85b + TELEPORT build, the SIMHASH stream is identical at every step: Streets TDM 4,201, Remnant ESC 1,675,
  A1 campaign 60 s 3,749.

## 12: campaign movies play once (11 tree -> 6d30fff2 + GAMEPLAY_CAMPAIGN_MOVIE_ONCE)

`12_campaign_movie_once.patch`: the same paths, from the 11 tree to 6d30fff2 + MOVIE_ONCE. It contains:
- Application_Frontend.cpp: the Game.PlayMovie bridge never re-shows a serial it has ended. That ended request used to replay A1's
  FMV_a1intro after a skip, because the end reaches the World asynchronously;
- the Frontend's FrontendRuntime changes that came in with 6d30fff2.
Apply it after 11. Verified: the 01–12 chain applies to a fresh af4d0731 and matches the tested tree over every patched path.

## 13: campaign object lists + tactical volumes (12 tree -> c4155702 + GAMEPLAY_CAMPAIGN_TACTICAL)

`13_campaign_tactical.patch`: the same paths, from the 12 tree to c4155702 + TACTICAL. It covers:
- SeqVar_ObjectList: factory Spawned appends to it, SeqAct_ModifyObjectList, dead pawns leave the lists;
- SeqAct_TnAssignTacticalVolume: assigned AI keep their goal spots inside the volume.
Apply it after 12. Verified: the 01–13 chain applies to a fresh af4d0731 and matches the tested tree over every patched path.

## 14: DLC Escalation maps + split-screen seat 2 (13 tree -> c6fb42bb + GAMEPLAY_ESC_DLC_MAPS)

`14_esc_dlc_split.patch`: the same paths, from the 13 tree to c6fb42bb + GAMEPLAY_ESC_DLC_MAPS. It covers:
- the Escalation data for MP_ESC_Tranquillity and MP_ESC_District (gen_escalation.py over RE's waves / zone order / factions and
  AssetTools' escalation_geometry), compiled into EscalationData.inc; District's AiEscalation_Soldier_Jet_Autobot row;
- WFC_ESCTEST on the DLC maps (waves advance, due enemy rows spawn, a generator is bought);
- split-screen seat 2 (steps 1, 2 and 4: the second local seat, PresentedFrame::seats, the bot-slot join, the split draw owner),
  a no-op without a seat 2 join;
- everything else merged between c4155702 and c6fb42bb on these paths.
Apply it after 13. Verified: the 01–14 chain applies to a fresh af4d0731 and matches the tested tree over every patched path (content
equal; git apply's CRLF conversion aside).

Fix (Experimental's build of edba5bb3): 14 also adds `src/render/SplitScreen.h` (74 lines, std headers only) as a new file. Renderer.h
includes it from 14 on, and src/render is outside the item paths. No other render header the patched files include is missing from
af4d0731. Verified: `git apply --check` of 14 on a fresh full af4d0731 + 01–13 passes, and that tree configures and builds
wfc_rebuild (Release, llvm-mingw) with 0 errors.

## 15: District doors (14 tree -> 0d56afb4 + GAMEPLAY_ESC_DISTRICT_DOORS)

`15_esc_district_doors.patch`: the same paths, from the 14 tree to 0d56afb4 + DISTRICT_DOORS. On these paths 0d56afb4 equals the
14 tree, so 15 is only that patch:
- TransformingActors::load falls back to AssetTools' per-map `maps/<map>/transforming_actors_gameplay.json` when
  mp_transforming_actors.json has no rows for the map (District's 22 doors);
- the regenerated District Escalation data: the generators' Zone_Manager / Zone subsequence variables flattened into actions
  (Open Doors -> transform, Enable Survival Zones -> zone on, Disable Volumes -> TriggerVolume off);
- WFC_ESCTEST's DLC branch buys a door generator and checks its doors and zones.
Apply it after 14. Verified: a fresh full af4d0731 + 01-15 applies (`git apply --check`), configures and builds wfc_rebuild with 0
errors, and its District ESCTEST log equals the head build's line for line.

## 16: DLC Escalation map mechanisms (15 tree -> d2998d23 + GAMEPLAY_ESC_MECHANISMS)

`16_esc_mechanisms.patch`: the same paths, from the 15 tree to d2998d23 + ESC_MECHANISMS. It covers:
- the mechanisms of RE's DLC_ESC_MECHANISMS_RE.md (1104a86) as data + director code (SurvivalMech.h, WorldSurvivalMech.cpp):
  District's laser defense grid (all five pain volumes off at start; bought: on 45 s, the button ends it early, the generator
  re-buyable), sentries (dormant + invulnerable, woken by their generators; ion / repair; 3-use limit), the power outage (the Match
  RNG; no buying for 60 s) and detpacks; Tranquillity's floodgate drain (three purchases), its zones and the base doors' blocking
  volumes as switchable collision;
- hazard volumes can be switched off; HudGameState.svPowerOutage;
- WFC_ESCTEST's DLC branch checks each mechanism;
- the Systems audio changes merged since 15 on these paths (LevelAudio.inc, SoundCues.cpp).
Apply it after 15. Verified: a fresh full af4d0731 + 01-16 applies (`git apply --check`), builds wfc_rebuild with 0 errors, and its
District and Tranquillity ESCTEST logs equal the head build's line for line.

## 17: pad X = TryInteract else reload (16 tree -> 7ebb7680 + GAMEPLAY_X_INTERACT_OR_RELOAD)

`17_x_interact_or_reload.patch`: the same paths, from the 16 tree to 7ebb7680 + X_INTERACT_OR_RELOAD. It covers:
- the controller drops the reload tap of an X press (Interact + Reload down on the same frame) when an interactable is offered
  (World::localInteractAvailable: a carried object, a campaign interact prompt, an Escalation generator, a DLC mechanism button /
  detpack); keyboard E / R stay independent;
- everything merged between the 16 tree and 7ebb7680 on these paths (the solo-host P0 fix, pad actions, subtitles, voice, seat
  routing, QA gate hooks ...).
Apply it after 16. Verified: a fresh full af4d0731 + 01-17 applies (`git apply --check`), builds wfc_rebuild with 0 errors, and a
headless Streets CTF 120 s gives identical MATCH events to the head build (18 / 18).

## 18: non-convex hazard volumes (17 tree -> 74e41849 + GAMEPLAY_HAZARD_NONCONVEX)

`18_hazard_nonconvex.patch`: the same paths, from the 17 tree to 74e41849 + HAZARD_NONCONVEX. It covers:
- hazard (pain) volumes: convex brushes keep the plane test; non-convex ones (73 of 641, all campaign; e.g. A4_ORB
  PhysicsVolume_6864, which killed the player at the chapter start) use a ray-crossing parity test over the brush triangles;
- everything merged between the 17 tree and 74e41849 on these paths.
Apply it after 17. Verified: a fresh full af4d0731 + 01-18 applies (`git apply --check`), builds wfc_rebuild with 0 errors, and its
A4 campaign run (90 s, bot-driven) equals the head build line for line (no game over).

## 19: Escalation spawn finder as the base game + Pulse waves (18 tree -> 9e1e46e5 + GAMEPLAY_ESC_SPAWN_AUTHORED)

`19_esc_spawn_authored.patch`: the same paths, from the 18 tree to 9e1e46e5 + ESC_SPAWN_AUTHORED. It covers:
- the spawn finder uses the zone's authored TnSurvivalSpawnPoint (ground + capsule check, 5 tries) with no navmesh requirement
  (RE DLC_ESC_MECHANISMS_RE.md section 7); the PROV on-nav stand-in is gone;
- the Pulse Escalation data (30 waves) in EscalationData.inc;
- WFC_ESCTEST's DLC branch checks the due enemy TYPES (a one-wave row may legitimately never be drawn by the budget).
Re-baseline, deliberate: Escalation spawn positions change on every ESC map where the stand-in used to move an off-nav point (the
base ESCTEST rows still pass: Remnant 15/15, BrokenHope 36/36); Tranquillity / District failed spawns 1 / 5 -> 0; Pulse 0 -> 134
spawns. Versus is unchanged (ESC only). Apply it after 18. Verified: a fresh full af4d0731 + 01-19 applies and builds with 0
errors; its Remnant and Pulse ESCTEST logs equal the head build's line for line.

## 20: Pulse Power_Generator_Action + the DLC mechanisms' presentation data (19 tree -> 9e1e46e5 + 19 + GAMEPLAY_ESC_PULSE_VISUALS)

`20_esc_pulse_visuals.patch`: the same paths, from the 19 tree. It covers:
- Pulse (RE section 8): the generators' Zone_Manager chains flattened (Enable Survival Zones / Disable Generator Volumes), the 10
  managers (fired by RE's resolved chains incl. the remote events and first-of-two gates) open their doors' collision (per-name
  switchable blockers from the pawn collision) once their matinee has played;
- the presentation actors / matinee positions of every DLC mechanism (the earlier held visuals item, re-cut here);
- ESCTEST: the presentation check and a Pulse manager check.
Apply it after 19. Verified: a fresh full af4d0731 + 01-20 applies and builds with 0 errors; its Pulse ESCTEST log equals the head
build's line for line.

## 21: Static Escalation data (20 tree -> 2c7e8089 + GAMEPLAY_ESC_STATIC_DATA)

`21_esc_static.patch`: the same paths, from the 20 tree to 2c7e8089 + ESC_STATIC_DATA. It covers:
- Static's Escalation data (RE section 9): 20 waves, 11 enemy rows, 34 zones, 72 generators (the DoorGenerators flattened), the
  fire-jet trap (generator 3558, 16 volumes, matinee 7.018 s and cooldown 2.0 s from the export) in EscalationData.inc;
- ESCTEST: the "due" check covers rows with a MinCount (optional rows depend on budget and kill cadence);
- everything merged since 20 on these paths (STATIC_PREP, two locals online, the WIC texture decoder, previews, level audio ...).
District's data regenerates byte-identical after its art-package fix. Apply it after 20. Verified: a fresh full af4d0731 + 01-21
applies and builds with 0 errors; its Static ESCTEST log equals the head build's line for line.

## 22-28: the 300 FPS sim phase (21 tree -> b8963b8e), one chain

Items 22 and up are exact tree steps. The chain tree after 21 had its base paths replaced by each merge commit's, and the staged
diff (`git diff --binary`) is the item. So after item N, the base paths equal commit N's exactly, and each item also carries
everything else merged on those paths in between (as 17-21 did).

The base paths: src/assets, src/audio, src/core, src/frontend, src/game, src/net, src/platform, src/main.cpp, src/render/Renderer.h,
Camera.h, Mesh.h, SplitScreen.h, CMakeLists.txt, data.

Apply in order, after 21, in a git-init'ed tree (see the note above).

- `22_sim_ticklog_esc_harness.patch`: 21 tree -> a25edf62 (24 files, binary). It carries:
  - GAMEPLAY_TICKLOG (WFC_TICKLOG per-step CSV, sub-slots, WFC_SOAK_GOD / WFC_SOAK_KILLEVERY);
  - GAMEPLAY_ESC_HARNESS (WFC_ESC_STARTWAVE / WFC_ESC_INVULN, test only);
  - everything merged on the base paths since 21: the settings preview PNGs, Rendering's campaign skeletal set dressing game half
    (Campaign.h, WorldCampaignMatinee.cpp: D1_GPUSKIN game half, SKEL_VIS, SKEL_PRELOAD), the DLC mechanism presentation game half
    (WorldSurvivalMechPresent.cpp, the World hooks, Renderer.h setMatineePosition).
- `23_first_use_preload.patch`: -> bcd70a61. Melee weapon models, gib meshes and matinee pawn-group clips at map / chapter load.
- `24_nav_blocks.patch`: -> 5975369b. The applyNavBlocks xz grid prefilter and the CSR computePieces. It also carries Rendering's
  DATA_ROOTS (core/DataRoots.h) and the frontend package paths.
- `25_roster_kismet_latent_asset_root.patch`: -> feaf20bd. It carries:
  - the ROBODEF roster table at chapter load and the Kismet latent list (fix F);
  - Rendering's ASSET_ROOT_DEFAULT (8c670e2f): config::kAssetRootDefault becomes config::assetRootDefault().
  - Plus the compatibility hunks outside the base paths, so this step builds: package 6's src/render/gl/GLRenderer.cpp and
    WfcPipeline.cpp, tests/FrontendTests.cpp and tools/fidelity/Rig.cpp call assetRootDefault() (the head's own spelling).
  - This takes the place of Rendering's separate a / b / c / d entries: their game halves are already in 22, 24 and 25 (exact
    head states), and c's renames outside the base paths are these hunks.
- `26_hitch_loads.patch` (Systems): feaf20bd -> 6ddb0abe. SYSTEMS_HITCH_LOADS: a mid-match first-time body load on a worker, and
  the body's cue set / energon colour load with it. It also carries the frontend data resolver (19bc0841).
- `27_hitch_threads.patch` (Systems): 6ddb0abe -> 4098c267. SYSTEMS_HITCH_THREADS: rosterPackage's thread-safe first init, and GPU
  work only on the main thread.
- `28_hull_los_sentry.patch`: 4098c267 -> b8963b8e. The vehicle-death gib hull preload, the any-hit line of sight
  (CollisionWorld::segmentBlocked) and the nearest-first sentry scan.

Verified (2026-10-24):
- A fresh af4d0731 + 01-28 (git apply, git-init'ed tree) applies clean.
- Every base path equals b8963b8e (ignoring CR).
- A fresh full configure + build of wfc_rebuild.exe has 0 errors. The gate launches only the game exe, so wfc_fidelity is not
  part of the check.
- The headless SIMHASH streams are identical to the b8963b8e head build:
  - MP_IAC_Streets TDM, 4 + 5 bots, 60 s: 4,201 steps;
  - MP_ESC_Remnant ESC 120 s: 8,101 steps;
  - MP_ESC_BrokenHope ESC 120 s: 2,484 steps;
  - A1_IAC campaign 120 s: 7,261 steps;
  - D1_ORB campaign 120 s: 7,261 steps.
