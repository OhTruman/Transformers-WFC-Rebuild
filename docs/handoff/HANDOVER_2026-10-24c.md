# Hand-over note 3, 2026-10-24: build 2bf2f152 = the playtest-4 build (MP + Escalation reliability)

Package: F:/Transformers Rebuild/Rebuild-Systems/work/playtest_slim_2bf2f152 (Play.bat; the HD pack beside it as playtest_slim_2bf2f152_HDpack); self-contained; Play.bat now reuses or drops
stale drive mappings, so the W: failure cannot recur).

## Your playtest-4 findings
Multiplayer
- Bot limits follow the team you are on (your side 1 + N-1 bots, the other side N) for every player limit.
- Game Settings after re-entry: root cause found: the row is cloned from the lobby's Create a Character row, and once
  Iconic Characters is turned on the lobby switches to a menu without that row, so Game Settings could never rebuild.
  Fixed (the iconic menu's Choose Character row is the template now); confirmed windowed on both paths (via Escalation, and with Iconic Characters on).
- XP popups show one at a time (same-kill updates merge in place).
- Map escape by double-jumping walls: OPEN. The authored blocking volumes are already in our movement collision (71 on Streets), so the escapes need a per-map audit of wall tops and missing volumes; Gameplay ran out of usage before it. Next build.
- Hit registration: reviewed, no defect (the shot lands within 0.02 px of the crosshair on average).
- Cubes: Energon cubes restore health and are picked up only below full health; ammo crates only when a weapon is short. Nothing else is placed there.
- Vignette: not reproduced at 2560x1440 fullscreen (the dark edges stay on); awaiting your answer whether you mean the
  backdrop shot changing when a ship moves into the foreground.
Escalation
- Generators (vending machines and the wall-switch door terminals) are drawn now: no generator on any Escalation map
  was ever drawn (skeletal actors the world export never carried), which is why "E to open door for 300" floated at an
  empty doorway.
- Doors: the closed door is a transforming skeletal actor; on the DLC maps (Forsaken = Tranquillity, District, Pulse,
  Static) the sim loads them per map (District 22, Static 34, Pulse 2); Forsaken / Tranquillity's export has 0 door actors (an AssetTools gap: its blue barriers are the floodgates, which do load). Door drawing on the DLC maps is the next build.
- Wave 1 stalling: cause found and fixed. Survivors spawned off our bot navigation or in sealed areas, so the wave could never complete; spawns now prefer zones reachable from the players. Pulse clears wave 1 in the harness; on the other maps the horde now reaches the player (one Tranquillity spider still lacks a path). Please confirm on Forsaken.
- Water on Forsaken: later.
Settings
- Previews: one size, kept inside the window, hover zooms the content inside the fixed frame toward a per-setting
  focal point; Volumetric and Lens Flare use text (no honest same-view pair shows them at this size); the Ray Tracing
  Realistic preview is the real rendering (measured against a match).
Disk cleanup: skipped on your word; Experimental freed 25 GB of its own superseded gate builds. The 55 GB candidate list (superseded packages, verify output, old refresh trees in Rebuild-Systems/work) is in MILESTONES_09c.md if you want it later.

## Proof: (pending) scripted MP + Escalation flows (incl. wave 1 completing and wave 2 starting) and the same-match
gate vs the base chain (01-32).
