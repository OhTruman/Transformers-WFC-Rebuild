# HUMAN CHECK: Milestone 05 (frontend → Streets TDM → return)

Only things a person can judge. Everything measurable runs in `tools/fidelity/m05-e2e-gate.ps1`. Run it first and
keep its `M05-GATE.md` open. Each item names the gate check that frames it. These are observations for the owners,
**not permission to tune by feel**. Report each finding with a screenshot (F12), the step, and what you expected.

Play the merged Release build: launch normally (no `WFC_*` variables) from a fresh folder, so the profile is new.

## Boot and frontend
- [ ] **First launch.** Logos (Activision → Hasbro → High Moon), then the intro, then the main menu.
  - The gate proves the chain runs in order (`STATE.intro_chain_order`).
  - Without a video decoder the movies end instantly (`PRESENTED.intro_movies_played` KNOWN).
  - Judge: does the pause between them feel like loading, or like a broken boot?
- [ ] **Second launch.** No logos; straight to the menu (`STATE.second_launch_skips_logos`). Does it feel right?
- [ ] **Does the frontend look like WFC?**
  - The orbiting Cybertron camera, the fireworks, the menu layout and the fonts.
  - The gate only measures whether anything is drawn (`PRESENTED.frontend_screen`). A black window there is KNOWN
    until the GFx presenter lands.
- [ ] **Menu transitions.** Main menu → Multiplayer → party lobby → private game → game lobby.
  - Are the transitions and the loading screens between levels smooth?
  - Do button presses always land on the screen you are looking at (input focus)?
- [ ] **Selection persistence.** Choose TDM and Streets, back out, re-enter.
  - Is the choice remembered where the original remembers it?
  - After a match, does the lobby preselect Streets?
- [ ] **Menu music and UI sounds.**
  - Frontend music (FRONTEND_MX_ORBIT_01), party lobby music, game lobby music.
  - Does each level switch music cleanly: no doubling, no silence gaps, no music continuing into the match?
  - Streets has no match music.
  - Do button sounds play exactly once?

## Loading
- [ ] **Match loading screen.** It should read "Team Deathmatch", "in Streets", with rotating tips.
  - The gate checks the text (`STATE.match_loading_text`); you judge the presentation.
  - Does the screen stay up until the map is ready, with no flash of an unfinished world?

## Match (Streets TDM)
- [ ] **Pre-game.** The original holds everyone for a 10 s countdown before anyone spawns.
  - Until Gameplay implements PendingMatch, the rebuild starts at once (`MATCH.pending_countdown` KNOWN).
  - Does the start feel abrupt?
- [ ] **Spawn feel.** Do you spawn on your team's side, facing play, and never in sight of an enemy spawn kill?
- [ ] **Player feel.** Walking, aiming and firing as Optimus. Does it feel like the shipped game?
- [ ] **Boost steering.** Does steering while boosting still feel locked?
- [ ] **Transform continuity.**
  - Robot ↔ vehicle at a standstill, driving, boosting and airborne. No pop, camera snap or lost momentum.
  - The stress test makes the pawn **fall through the floor after a vehicle→robot transform at speed in 17% of
    cases** (`transform_stress`: 127/756, boost and nitro only).
  - Does it happen to you in normal play, and where?
- [ ] **Smoke, glass, transparency.** Steam veils at distance, glass floors from above and below, and black
  geometry behind ramps (see HUMAN-CHECK.md for the measured cases).
- [ ] **HUD readability.**
  - Team scores, your score and the MM:SS clock: readable at a glance in bright and dark areas?
  - Does the clock count down from 15:00?
- [ ] **Death and respawn.**
  - Death camera, then the respawn screen, then a respawn after about 5 s.
  - Does it feel like the original, and does the respawn put you somewhere sensible?
- [ ] **Score updates and announcements.**
  - Kills add to your score and your team's score; suicides do not.
  - Announcer calls at 5 / 3 / 1 kills left and at 2 min / 1 min / 30 s.
- [ ] **Match pacing.** Over a full match to 40 (or 15:00): does the flow of fights, respawns and score feel right?
- [ ] **Sound field in the match.**
  - Ambient beds, weapon and transform sounds, pickups.
  - Does the soundscape change naturally between rooms, tunnels and the exterior?
  - Does anything keep playing that should have stopped?

## Pause and leaving
- [ ] **Pause input focus.** Hold W and press Esc. The robot must stop while the pause menu is up, and the world keeps
  running, because multiplayer does not pause (`PRESENTED.pause_input_focus` HUMAN: real key input cannot be
  automated safely).
- [ ] **Match end.** At 40 or at 0:00: end-game stats, the HUD hidden, then a return to the game lobby after about 15 s
  with Streets preselected.
- [ ] **Quit to the main menu** from pause. Is the frontend exactly as it was: music, camera, no leftovers?
- [ ] **Second match** in the same session. Same spawn behaviour, scores reset, nothing left over from match one.
- [ ] **Long session.** Play 4–5 matches without restarting.
  - Does it get slower, stutter on loading, or run out of memory?
  - The gate measures about +1.5 GB per return to the frontend (`LIFETIME.memory_per_cycle`).

## Overall
- [ ] **"This feels like the shipped game."** One honest paragraph: what breaks the illusion first?
