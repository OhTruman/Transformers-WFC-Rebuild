# HUMAN CHECK: living-map playtest of MP_IAC_Streets

Machines have already measured counts, placement, timing and per-mode state. Evidence is in `tools/fidelity/results/m04-living-map/LIVING-MAP.md`. These items are what only a person can judge. Each one is an observation, **not permission to tune by feel**. Report a finding to the owner with a screenshot (F12), the spawn, and what you expected.

Play the build named in the report, in the default mode (Deathmatch). Visit several spawns, on foot and in the vehicle, and spend at least a minute standing still in a few rooms.

- [ ] **Inhabited or static?**
  - Does Streets now feel like a place with machinery, vents and distant activity, or still like a frozen set?
  - Name the spots that feel most dead.
- [ ] **Ambient sound field.**
  - Walk from the exterior street into the Decepticon rooms, the Autobot rooms, the Neutral base and hall, the stairwell and the train tunnel.
  - Room tone and reverb should change at the doorways.
  - Distant one-shots (explosions and rockets outside; corridor and tunnel sounds inside) should arrive every few seconds and sound far away, not on top of you.
  - Do lights, monitors, pipes and generators hum where you see them?
  - Report anything that cuts out, stacks up or follows you.
- [ ] **Steam and particles.**
  - Do the 8 steam vents read as steam (density, drift, speed, fade), with no popping, sudden disappearance or build-up over time?
  - Note any place where you expected an effect and saw none.
- [ ] **Moving scenery.**
  - The three decorative domes should turn slowly (15°/s), and the SkyBeam light cones and sphere sweep on a 9 s loop.
  - In the current build these are still drawn frozen, because Rendering hasn't drawn the moving pose yet.
  - If a dome is frozen, check whether you can walk into it or get pushed by an invisible rotating collision. Gameplay already rotates the domes' collision.
- [ ] **Lighting cohesion.**
  - Are the major regions (exterior, the four rooms, the tunnel) lit consistently?
  - Look for black or over-bright patches next to doors, flat untextured surfaces, and obviously wrong materials (monitors, energon glass, heated-metal craters, light cones).
- [ ] **Character and world visual cohesion.**
  - Does Optimus (robot and vehicle) sit in the world's light: matching brightness, shadows on the floor, energon colour?
  - Or does he look pasted on?
- [ ] **Boost steering.** While boosting, does steering still feel abnormally locked compared with the original?
  - Gameplay recovered the wheel/tire steering model (Pass 18), but it isn't in the merged build yet.
- [ ] **Pickup presentation.**
  - Ammo crates should spin and show a highlight beam. Health and overshield pickups are shown only by their particle effect in the original.
  - Can you see where the health and overshield pickups are?
  - Taking a pickup should play its sound once, and the pickup should disappear and come back after 30, 60 or 120 s.
- [ ] **Mode-specific objects.**
  - In Deathmatch, no flag or bomb bases, flags, bombs, domination totems or KOTH markers should appear.
- [ ] **Non-original objects.** Note anything that clearly isn't part of the original map, such as the pale test box near the first spawn in older builds.
