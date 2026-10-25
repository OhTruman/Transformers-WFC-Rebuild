# Hand-over note 3, 2026-10-24: build (pending) = the playtest-4 build (MP + Escalation reliability)

Package: (pending: Rebuild-Systems/work/playtest_slim_<sha> + _HDpack; self-contained; Play.bat now reuses or drops
stale drive mappings, so the W: failure cannot recur).

## Your playtest-4 findings
Multiplayer
- Bot limits follow the team you are on (your side 1 + N-1 bots, the other side N) for every player limit.
- Game Settings after re-entry: root cause found: the row is cloned from the lobby's Create a Character row, and once
  Iconic Characters is turned on the lobby switches to a menu without that row, so Game Settings could never rebuild.
  Fixed (the iconic menu's Choose Character row is the template now); confirmed windowed on both paths (via Escalation, and with Iconic Characters on).
- XP popups show one at a time (same-kill updates merge in place).
- Map escape by double-jumping walls and getting stuck outside: (pending Gameplay's boundary-volume import)
- Hit registration review: (pending Gameplay's report)
- Cubes: the Energon / ammo pickups act only when you are hurt or short; (pending the cubes line)
- Vignette: not reproduced at 2560x1440 fullscreen (the dark edges stay on); awaiting your answer whether you mean the
  backdrop shot changing when a ship moves into the foreground.
Escalation
- Generators (vending machines and the wall-switch door terminals) are drawn now: no generator on any Escalation map
  was ever drawn (skeletal actors the world export never carried), which is why "E to open door for 300" floated at an
  empty doorway.
- Doors: the closed door is a transforming skeletal actor; on the DLC maps (Forsaken = Tranquillity, District, Pulse,
  Static) those actors never loaded because the runtime read only the base maps' list. (pending Gameplay's per-map load)
- Wave 1 stalling / enemies behind the blue barriers / the spider in the wall: (pending Gameplay's wave-1 flow fix)
- Water on Forsaken: later.
Settings
- Previews: one size, kept inside the window, hover zooms the content inside the fixed frame toward a per-setting
  focal point; Volumetric and Lens Flare use text (no honest same-view pair shows them at this size); the Ray Tracing
  Realistic preview is the real rendering (measured against a match).
Disk cleanup: (pending your approval of Systems' deletion prompt: ~55 GB of superseded packages / verify output.)

## Proof: (pending) scripted MP + Escalation flows (incl. wave 1 completing and wave 2 starting) and the same-match
gate vs the base chain (01-32).
