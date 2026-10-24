# Human playtest findings, 2026-10-23 evening (package d5bb6863)

The user's own words, lightly condensed; screenshots in `playtest_2026-10-23b/`. The user's verdict: "probably the most
broken playable build I have tested recently ... the major issue appears to be frontend / game-mode / state integration
rather than one isolated graphics bug ... I now want a serious quality-control pass instead of continually layering new
functionality over broken state." Treat every row as a real open issue even where automated tests pass.

Owner key: F = Frontend, G = Gameplay, R = Rendering, S = Systems, RE = RE, A = AssetTools. P0 = blocks play.

## P0 blockers

| # | Finding | Owner |
|---|---|---|
| 16 / 28 | **Scout cannot move**: camera turns, the character flickers / jitters in place as if two transforms fight; seen in Escalation AND normal multiplayer (shot 06). | G (repro from the menus, windowed; check hit reactions 66109cf5, dash / cloak, local-seat code, the pawn / controller transform owner) |
| 9 / 14 | **Escalation > Host a Game opens the TEAM DEATHMATCH lobby** (title, description, TDM settings) and the match loads as TDM. | F (Escalation host path must set the ESC mode identity, lobby, settings, character select, rules, spawn, HUD) |
| 24 / 26 | **Private Match has no Game Settings**: stuck on TDM; cannot change mode / teams / rules / bots; Start Game looks dimmed (shots 04, 05). | F (regression: the lobby menu lost its Game Settings row) |
| 38 / 39 | **Campaign Chapter I "Dark Energon" > Crash Site > pick Megatron -> blank screen, nothing happens; Back misbehaves.** (D1 was never run from the menus; A1 / D5 were.) | F + G (D1 load from the menu; a load with no progress indicator may look like a blank screen: see 33) |
| 17 / 30 | **Menus can trap the player**: Change Character from the pause menu opened the Escalation / menu character select; backing out left a dimmed screen with no control; had to kill the game. Every modal needs a valid Confirm / Back / Cancel / Close path. | F (modal ownership, screen routes, in-match vs menu character-select context) |
| 15 | Escalation asked for the character AGAIN after loading although it was picked pre-match. | F + G (selected character persistence into the match) |
| 31 | QA / admin panel: restart match and kill / force respawn did not work in the broken state; respawn itself looked broken. | G |

## Escalation (18: complete top-to-bottom review)

| # | Finding | Owner |
|---|---|---|
| 8 | Profile creation on first entry worked; menu shows Find Public Game / Host / Join / Escalation Info / Leaderboards / Friends List. | (OK) |
| 10 | Match Game Settings in Escalation: Back landed in the GLOBAL Settings (graphics / audio); after returning, Game Settings stopped working. State cross-talk between match Game Settings, global Settings, Escalation and the MP lobby. | F |
| 11 / 13 | Escalation character select: names and AVAILABLE / UNAVAILABLE overlap and jumble (an infinite wheel whose text layout fails; shot 07). Compare with the original's Escalation character select before redesigning; the overlap must go either way. | F + RE (original behaviour) |
| 12 | Roster: Silverbolt shows no model; Optimus Prime appears twice. Check the roster against the authored list. | F + A |
| 29 | Inside normal multiplayer, Change / Select Character brought up the Escalation character select. | F |
| 18 | Audit: entering Escalation, profile init, menu, public / host / join, Info, lobby, game settings, map, difficulty, player / bot setup, character select, transition, character persistence, spawn, gameplay, pause / settings, change character, leave, return to lobby. | F lead, G for the sim side |

## Multiplayer menus / lobby

| # | Finding | Owner |
|---|---|---|
| 19 | Multiplayer root (party lobby) already shows the player under AUTOBOTS / DECEPTICONS (shot 02). Show the name, but assign a team only inside an actual match context (Find Match / Private / Join). | F (+ RE: what the original party lobby shows) |
| 23 | Private Match lobby: show AUTOBOTS / DECEPTICONS, place the player under one, with a clean arrow control to switch sides that updates the real team at start. | F |
| 25 | Start Game countdown cannot be cancelled. | F |
| 27 | Player / bot names scrunched together at match load (shot 08). | F (scoreboard / name layout) |
| 20 | Create a Character: the Decepticon Chassis button sits too high; align with Autobot Chassis (shot 03). | F |
| 21 | Create a Character colours: mouse + controller selection (keyboard arrows only today); a bigger colour system later. | F (low) |
| 22 | Selected chassis colours too subtle on some models; make the choice clearly visible without blind oversaturation (check how the colour values reach the material params). | R + RE (authored values) |

## Settings

| # | Finding | Owner |
|---|---|---|
| 1 | Graphics settings menu: good; keep. | (OK) |
| 2 | Scroll indicators: a DOWN arrow when rows exist below, UP when above, both when both. | F |
| 3 | Settings that need a restart / extra action: a proper popup ("This setting requires the game to be restarted before it takes effect."), not small side text. | F |
| 4 | Commit Changes: a confirmation dialog (Apply / Cancel) and clear feedback that the settings applied. | F |
| 5 | A preview panel on the right for the newer features (upscaling modes, ray tracing, enhanced textures ...): OFF vs ON / quality images that update with the selection; zoom / inspect if practical. | F + R (preview images) |
| 6 | Menus run at 72 FPS: hard-lock the frontend to 60 FPS; in a match use the player's frame-rate limit. | R / S (app loop) |
| 7 | Audio / voice settings look good; add a key / controller binding to switch voice channels on the fly. | S + F |
| 34 | Brightness calibration belongs in the first-launch setup, not only on entering Campaign. | F |
| 35 | Brightness calibration preview broken: the slider changes the game's brightness but the reference logos don't respond (the left logo must fade out at low brightness; shot 09). | R + F |

## Campaign (40: complete top-to-bottom review; the user's list was cut off after "chap")

| # | Finding | Owner |
|---|---|---|
| 36 | Chapter selection UI doesn't look right; review against the original layout (shot 10). | F + RE |
| 37 | Fresh profile: chapter VI shown highlighted / unlocked while starting at chapter I; later chapters must stay locked until progression unlocks them (unless the original unlocks VI = the Autobot start by design: RE confirms). | F + RE |
| 38 / 39 | See P0: Crash Site blank screen; Back broken afterwards. | F + G |
| 40 | Audit: first-launch brightness, Campaign root, Solo, Co-op, difficulty, chapter select, mission select, character select, load, play, checkpoints, pause, restart, leave, return. | F lead, G |

## Loading

| # | Finding | Owner |
|---|---|---|
| 33 | Load times still too long: optimize, or show a real progress indicator / bar; never a static screen. | R + F (progress bar from the loader's stage counts) + A (data size) |

## Rules for this pass (user, 2026-10-23)

- "Please do a broad quality / stability pass across the entire frontend-to-gameplay flow rather than only fixing
  individual screenshots one at a time."
- Do not treat d5bb6863 as a stable milestone.
- Frontend and Gameplay: NO new feature work (DLC menus, split screen) until the P0 rows and the two audits are closed.
  Rendering / AssetTools / Systems continue the DLC data work (not state-related) plus their rows above.
- Each lane: reproduce from the MENUS with a windowed run (the card is free while the user is not playing; ask
  Integration for a slot), fix the state / ownership cause, one scripted menu run per flow as the proof, patch vs the head.

## The user's own words (after the summary above, 2026-10-23 ~23:55)

"The entirety of the build is broken. I need to complete quality pass making sure settings are doing what they're
doing, things are where they're supposed to be relative to either what I said, or what the base game is. Always default
to the base game if I didn't say anything about it. All the settings I told you to add after I said we got this past
done, and then do a complete performance overpass to make sure everything is working the way it's supposed to be, and
also is running how it's supposed to be."

Order, therefore:
1. QUALITY PASS (now, every lane): every setting does exactly what its row says; every screen / flow / rule matches the
   user's explicit instructions, else the BASE GAME (RE is the reference; no invented behaviour).
2. The new settings items the user asked for in this pass (scroll arrows, restart popups, commit confirmation, preview
   panel, menu 60 FPS, voice-channel binding, first-launch calibration) AFTER the quality pass is done.
3. A complete PERFORMANCE pass: everything working as it should AND running as it should (timed rows per mode / map /
   setting, load times, menus).
DLC and split screen resume after 1-3 (AssetTools / Systems data work may continue in the background; no Frontend /
Gameplay feature patches until the QC pass is closed).
