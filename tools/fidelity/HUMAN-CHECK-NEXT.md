# HUMAN CHECK: next integration build (after the M05 playtest)

Automation already measured everything it can. Use `final-gate.ps1`'s `FINAL.md` for the numbers and the sheets
(`R1_sheet.png`, `R7_sheet.png`, `A1_sheet.png`, `A2_loading_sheet.png`, `A3_sheet.png`, `jitter/*.png`).

**Setup.** Play the Release exe named in `FINAL.md`, launched from an empty folder, with no `WFC_*` variables. Use a
controller for one full pass and keyboard + mouse for another. Report each item as OK / wrong (screenshot, F12) /
not reached.

1. **Frontend appearance.**
   - Intro logos and film: picture **and sound** (`accept.intro.audio`).
   - Title and menus over the live Cybertron scene, not black (`accept.title.background`; RE PT 5).
   - Fonts, panels, icons, colours.
2. **Menu feel.**
   - Start on the title. Enter does nothing there in the original; keyboard Start is F3 in the rebuild.
   - Up / down wrap in lists; rows stop at their ends.
   - B / Esc always returns to the parent screen (`accept.nav.*`).
   - Map selector wraps.
   - Every button sound plays once.
3. **Loading quality.**
   - The loading film and the "TEAM DEATHMATCH / in Streets" overlay keep moving during the load. The original never
     freezes (`accept.loading.updates_during_load`).
   - Nothing of the loading screen remains once the match starts.
4. **Character motion quality.**
   - Walk, strafe, turn, fire, jump with Optimus, at your monitor's real refresh rate.
   - No jitter, no parts separating, no "interlacing" (`jitter.pacing.*`, the Gameplay 21a fix).
5. **Vehicle feel.**
   - Hover strafing; boost steering (right stick / mouse); nitro.
   - Ramps and lips: the truck should not hard-stop on a ramp (`vehicle_collision.ramp_hard_stops`).
   - Boost → robot at speed never drops you under Streets.
   - Hover, boost and nitro look and sound different and steady.
6. **HUD readability.**
   - Clock, team scores, health / overshield, ammo, crosshair. Readable, and matching what happens?
   - Where the HUD isn't drawn yet, note it as missing (`accept.hud.*` WAITING).
7. **Kill feed.** "Killer [weapon] Victim" style messages in team colours, about 3 s each, if presented
   (`accept.hud.kill_feed_event`).
8. **Death and respawn.** Death animation and camera, the respawn screen after about 3 s, back in at 5 s at your team's
   side.
9. **Match-end presentation.**
   - Results screen at the score or time limit (Level / Name / Score / Kills / Deaths).
   - Back in the lobby after about 15 s.
   - The experience panel is known to show undefined / NaN (no XP service).
10. **Map fidelity.** Smoke, fog, steam, glass bridges, ramps behind glass, lighting and shadows on Optimus. Any **large
    change** from the M05 build that looked strong is suspicious.
11. **Sound field.** Streets ambience by area, weapons, transform, vehicle, pickups. Nothing doubled, nothing left
    playing in the menus, menu music back after every match.
12. **Second match and long session.**
    - Start another match from the lobby, then a few more without restarting.
    - Fresh scores, clock and pickups.
    - No slow-down or growing memory (Task Manager: about 2.8 GB in the lobby, 3.5 GB in a match).
13. **Pause.** Hold W, press Esc. The robot must stop, the world keeps running, and Resume restores control.
