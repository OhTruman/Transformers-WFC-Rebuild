# HUMAN CHECK: MP_IAC_Streets playtest

Only things a person can judge are listed here. Everything objectively measurable now runs automatically.
Run `tools/fidelity/checkpoint-suite.ps1`, then read the latest `results/` folder. Each item names the
measurement that frames it. Report findings to the owner with a screenshot (F12), the spawn, and what you
expected. These are observations, **not permission to tune by feel**.

Play the merged Release build in Deathmatch. Visit several spawns, both on foot and in the vehicle.

- [ ] **Smoke and steam at distance.**
  - Does steam, or the silhouette-smoke sheets, turn into a large translucent veil when seen from 30–60 m?
  - Measured: `playtest.sheets.BckSillouhetteSmoke_*` covers 56–100% of the view at 30–60 m, and several
    steam emitters cover 47–100% at 30 m.
- [ ] **Smoke up close.**
  - Walking into steam should feel like walking into steam, not like a flat grey wall appearing.
  - Measured: steam covers 100% of the view at 2–6 m.
- [ ] **Red/purple smoke.**
  - Does any smoke or fog read as the wrong colour?
  - Measured: fog sheets authored *Blue* contribute red in many views (`playtest.sheets.FogBlue_*.hue`).
- [ ] **Living effects.**
  - Do the environmental effects move like the original? Fog sheets measure nearly static (`*.motion` < 0.5).
- [ ] **Glass bridges and transparent floors.**
  - Stand on them, look down, look along them, and look at them from below. Does anything behind or below
    the glass appear in front of it?
  - Measured: the light glass floors contribute nothing from above and 59–80% from below
    (`playtest.glass.*`). Is that the original's look?
- [ ] **Black geometry.** Look behind ramps and into dark corners. Is anything solid black that should be lit?
- [ ] **Moving scenery.** Domes, the SkyBeam and the totems (DOM): do they move naturally and stay in sync
  with their collision?
- [ ] **Lighting and character cohesion.**
  - Is Optimus (robot and vehicle) lit like the world around him, in bright and dark areas
    (`sweep/sheet_robot.jpg`, `sheet_vehicle.jpg`)?
- [ ] **Ambient sound field.** Does the soundscape change naturally between rooms, tunnels and the exterior?
- [ ] **Boost steering.** Does steering while boosting still feel locked?
- [ ] **Pickup presentation.** Do crates spin and highlight, and do health and overshield read as pickups?
- [ ] **Mode objects (CTF / EXT / DOM / KOTH).**
  - Do the bases, totems and KOTH ring look right in their modes (`modes/sheet_*.jpg`)?
  - Flag and bomb effects could not be separated by measurement (`modes.flag_*`, `modes.bomb_*`).
- [ ] **Boost → robot.**
  - The stress test reproduces the pawn falling through a 9 cm floor after a boost → robot transform
    (`transform_stress`).
  - Does it happen in normal play, and where?
