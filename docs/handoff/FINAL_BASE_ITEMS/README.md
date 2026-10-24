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
