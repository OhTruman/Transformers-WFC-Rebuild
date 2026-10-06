# HUMAN CHECK: Milestone 05 (frontend → Streets TDM → return)

Only things that need eyes, ears or a controller. Everything measurable already ran in `m05-final.ps1`. Keep its
`M05-FINAL.md` open: each item names the check that framed it, and the gate's `R1_sheet.png` shows what every
screen looked like on the build you're testing. These are observations for the owners, **not permission to tune
by feel**. Report each finding with a screenshot (F12), the step, and what you expected.

**Setup.** Use the Release exe named in `M05-FINAL.md`, launched from an **empty folder** (no `wfc_profile.ini`) with no
`WFC_*` variables. Use a controller if you have one, and play a full match to the end at least once.

## 1. Boot and frontend
- [ ] **Logos and intro.**
  - Activision → Hasbro → High Moon → intro film, full screen. The gate proves they decode and play to the end
    (`intro_movies_decoded`, `intro_played_to_end`).
  - Judge: do they look and feel like the shipped boot (pacing, black between them, skip with A / Start)?
  - The movies are silent by design for now (movie audio PARTIAL: track layout unknown).
- [ ] **Second launch.** No logos; straight to "Press START" (`second_launch_skips_intro`).
- [ ] **Does the frontend look like WFC?** Title logo, main menu, Multiplayer party lobby, mode list, host options, game
  lobby. The shipped Scaleform movies run their own code here.
  - Look for wrong fonts, missing icons, misplaced panels, and colours.
  - The 3D scene behind the menus is black, because UI_FrontEnd_m is not exported (KNOWN). Judge the menus,
    not the backdrop.
- [ ] **Menu feel.** Up/down movement, wrap-around (mode list wraps; host-option rows stop at their ends), back (B),
  button sounds, transition speed. Does each press land exactly once?
- [ ] **Menu music and UI sounds.**
  - Frontend medley (FRONTEND_MX_ORBIT_01), party-lobby music, game-lobby music.
  - Each should start cleanly when its screen opens, never double up, never continue into the match.
  - Menu clicks should be audible once per press (`AUDIO.music.*`, `ui_sounds_*`).
- [ ] **Map selection.**
  - The lobby opens on the first selectable map. In the rebuild that's Streets, because only maps with runtime
    data are listed.
  - Left/right steps and wraps between Streets and Gorge.
  - The map panel's name and thumbnail follow the selection.

## 2. Loading
- [ ] **Loading screen.**
  - "TEAM DEATHMATCH / in Streets" with three tips over the looping loading film.
  - The three tips are the same sentence, which is authored that way (KNOWN).
  - Does the screen hold until the map is ready, without a flash of an unfinished world?
  - The text animating in during a blocking load is PARTIAL.

## 3. Match (Streets TDM)
- [ ] **Countdown.** Nobody moves for ~10 s before the match starts (`MATCH.pending_countdown`). Does the countdown read
  and feel right?
- [ ] **Spawn feel.** On your team's side (Autobots 7810 / Decepticons 4159 clusters), facing play.
- [ ] **HUD readability.** Health segments and overshield, ammo, clock counting down from 15:00 (or 10:00), team
  score bars in team colours. Readable in bright and dark areas?
- [ ] **Player feel.** Walking, aiming and firing as Optimus.
- [ ] **Boost steering.** Steering while boosting.
- [ ] **Transform continuity.**
  - Robot ↔ vehicle at a standstill, while driving, boosting, nitro-boosting, turning, and in the air.
  - The stress tests check for falling under the map (`transform_stress`, `XFORMTEST`, `CHAOS`). You judge: pop,
    camera snap, lost momentum, and how long the fold takes.
  - Also transform under a low overhang (a known open RE question).
- [ ] **Vehicle against low geometry.**
  - Drive and boost into crates, low blocks, train coaches, railings and narrow gaps.
  - The truck should stop or deflect, never pass through (`vehicle_collision`).
- [ ] **Smoke, steam, glass.**
  - Distant fog cards and silhouette smoke: do they still veil the geometry?
  - Steam up close: soft and moving, purple where authored?
  - Glass bridges from above, along and below: does anything behind or below the glass appear in front?
  - Ramps and walls seen through transparent surfaces; black geometry showing through.
  - The gate measures whether Rendering's fixes are present in this build (`visual.*.merge`). Whether they now look
    like WFC is your call.
- [ ] **Damage, death, respawn.**
  - Get killed, if a second player or a test opponent is available.
  - Death camera, the respawn screen after ~3 s, back in at ~5 s, robot form with full health and 50/150 ammo.
  - Does it feel like the original?
- [ ] **Score and announcements.**
  - Kills add to your score and your team's score; suicides don't.
  - Announcer at 5 / 3 / 1 kills left and at 2 min / 1 min / 30 s remaining (`time_announcements`).
- [ ] **Match pacing.** Over a whole match: fights, respawns, score.
- [ ] **Sound field.**
  - Streets ambience by area, weapon / transform / pickup sounds.
  - Is anything doubled, missing or left playing (`doubled_sounds_*`)? Is there any frontend music in the match?

## 4. End and leaving
- [ ] **Match end.**
  - At the score or time limit: HUD gone, end-of-match results (Level / Name / Score / Kills / Deaths).
  - About 15 s later you're back in the game lobby (`match_end_ui`, `match_over_return_to_lobby`).
  - Does the result screen look and read right ("Your team won" / "Tie game")?
- [ ] **Second match.** Start again from the lobby.
  - Fresh scores and clock, pickups back, no leftover sounds, effects or HUD state from match one.
- [ ] **Pause and quit.**
  - Esc / Start opens the pause menu, and the world keeps running behind it (multiplayer doesn't pause).
  - **Hold W while pausing: the robot must stop** (`pause_input_focus`).
  - Quit Game → main menu; the music resumes; nothing from Streets is still audible.
- [ ] **Long session.** 4–5 matches without restarting.
  - Does loading get slower, does it stutter, does memory climb (Task Manager)?
  - The soak measures this too (`LIFETIME.memory_stabilizes`).

## Overall
- [ ] **"This feels like the shipped game."** One honest paragraph: what breaks the illusion first?
