# Hand-over note 2, 2026-10-24: build ae63135a = the laptop-ready package with the playtest-3 fixes

Package: F:/Transformers Rebuild/Rebuild-Systems/work/playtest_slim_ae63135a (Play.bat; the HD pack beside it as playtest_slim_ae63135a_HDpack). This one is self-contained:
copy the folder to the laptop and run Play.bat there (no dev folders needed). First launch of a new build compiles its
shaders once (~30-50 s into the first map); every later launch reads the shader cache (~10 s).

## Your playtest-3 findings, by priority (MP first, then Escalation, then shared)

Multiplayer
- Aim at the crosshair: fixed twice over. The camera's pitch step was applied before the shot (every robot shot left
  above the crosshair: Sniper 1 deg, Bazooka / Heavy Pistol 1.5, EMP Shotgun 4), and the trace used the unshaken camera
  while the view drew the shake. Shots now follow exactly the view you see; a windowed pixel proof puts the impact
  within 1 px of the crosshair over 30 shots.
- XP on kills: fixed. A private match you host alone counted as "online private" (which earns nothing in the original);
  a host with no members now keeps the PC offline rule.
- Scout Scatter Blaster lockout: authored. 2.4 s to empty the clip, then one 2.5 s whole-clip reload that fire cannot
  interrupt (RE confirmed the original's reload rule). Not changed.
- Orbital Beacon: fixed. The versus marker setup name differed, so no enemy markers drew; the original effect is
  enemy markers for your team for 30 s plus the announcer line.
- Pickup cubes: they are touch pickups (ammo 15, health Energon 9 at +50, overshield 1 on Streets) and do nothing when
  you are full, as in the original. One real bug fixed: ammo crates refilled only the active weapon.
- The invisible hitbox at the central crater: the mesh's own collision hull is a 27.7 m lens over a 19 m visible crater
  with a knife-edge rim our walk probe caught. That one mesh now collides per polygon (a PC adaptation; the original's
  slope-walking movement model is on the later list).
- Vehicle headlight flicker: fixed. The lamps were placed from the simulated bone while the car is drawn interpolated,
  so they jumped every sim step.
- Performance at max settings: no build regression (same fps on the previous and current builds on a fixed route; your
  82 fps was the 5v5 with ten ray-traced characters). The Escalation 30 s load was the shader prewarm: now cached.
- Join code leaking into Create a Character, "Cancel Start" during the countdown (mouse, pad B, Esc), Start Game grey
  while enabled (the solo-host enable flag), Decepticon Chassis box (it was mirrored 180 degrees), scoreboard rows
  (not reproduced with 5 + 4 bots: send a screenshot if you see it again).

Escalation
- Solo hosting: a private host starts alone (the original's rule). Map list: all six Escalation maps now cycle in
  the lobby map row (the public playlist picked a random map behind a veto a lone host can never win; now Host's Choice). Duplicate Optimus: the DLC Escalation maps had no
  character list so the wheel showed all 31 iconics; fixed. Load time: shader cache (above).

Settings / frontend
- First-boot prompts queue (Brightness, then the HD hint). Graphics opens at the top; arrows sit in fixed slots.
  Preview panel: one standard size, LEFT = baseline OFF, RIGHT = the selected value, hover to zoom; pairs for Upscaling
  incl. FSR 3.1 Q / B / P, HD, Ray Tracing (Realistic's brighter look is real: RT GI + sky light), Volumetric, Lens
  Flare; Frame Generation is text only. Language moved to Audio; Split-Screen Layout at the bottom by FPS Counter.
  Menus stay at 60. Backing out of a solo Campaign returns to the main menu. Main-menu fade: not reproduced windowed (the soft-edge layer holds); tell us your display mode if it persists.

Campaign: deferred as you asked; your findings are recorded in USER_PLAYTEST_2026-10-24.md.

## Proof: tools/fidelity/results/FINAL_GATE_ae63135a.md: PASS. The scripted MP and Escalation menu flows on this build pass every row (76 / 76: countdown cancel and restart, Scout, move, shoot, transform, Change Character and back; the Escalation pick kept, play, return). Same match head = base on all 12 rows vs the base chain
   (FINAL_BASE_ITEMS 01-31), negative control differs everywhere. Stability: campaign 24 / 24, Kismet 10 / 10, voice PASS, 38 TDM runs across all 19 versus maps incl. the nine DLC maps with 0 errors, all six Escalation maps clean. The package itself passed the clean-machine verify (every map row, D1, Remnant, Horizon, Pulse, HD runtime, settings, RT).

## Still open after this package
- The 300 FPS minimum target: the sim side is within budget everywhere except the level-start frame; the render-side
  baseline (worst frame per chapter / Escalation map, 1440p + 4K) is armed and runs next.
- Pulse / Static Escalation extras (healing platform, Nexus, sentries, Trypticon; Static's ambient fire bursts).
- The original's slope-walking movement model (the crater's general case).
- GitHub (origin) carries every merge on integration/milestone-09c.
