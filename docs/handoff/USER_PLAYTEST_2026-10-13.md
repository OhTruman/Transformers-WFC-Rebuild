# User playtest 2026-10-13 (package playtest_slim a17309b7, tag playtest/09c-overnight-a17309b7)

The user's first hands-on test in just under a week. Verbatim intent kept; owners and priorities added by Integration.
Rule for every item: fix on the lane branch, prove with the matching harness + a repro the user described, then Integration merges and repackages.

## P0 - "prioritize ASAP" (user)

| # | Finding (user) | Owner | Notes / suspects |
|---|---|---|---|
| 1 | **First-person weapon wobble when strafing**: sidestepping or rapid left/right makes the gun "wobbly / glitchy ... almost like an interlaced effect going left to right"; weapon only; "extremely distracting and honestly nauseating". | Rendering (Gameplay for the weapon transform) | Prime suspect: the M1 weapon draw-queue change (weapon drawn from a previous-frame / split transform while the camera moves laterally) or the camera-cut / motion-vector path; also check FSR / FG off paths. Repro: strafe fast, watch the gun edges. Prove with a 2-frame capture (gun edge offset per frame) before / after. |
| 2 | **False firing / reload prompts**: the local gun visually fires with no input and no ammo use; seems tied to nearby bots firing; "reload" prompt with full ammo. "cross-talk / state bug where bot / enemy / teammate weapon activity is affecting the local player's weapon effects or ammo / reload presentation." | Gameplay | Suspect: muzzle / reload presentation keyed by weapon class or pawn slot instead of the local pawn; MUZZLE / RMUZZLE / RELOAD harnesses exist. Repro: stand next to firing bots. |
| 3 | **Vehicle-mode ammo**: transforming into vehicle form shows a low-ammo / one-shot-left state and a reload prompt, and reload is impossible in vehicle form. | Gameplay | Suspect: the vehicle weapon's clip / reserve state initialised from the robot weapon's; or the reload prompt reading the wrong weapon. VEHPHYS / XFORM harnesses. |
| 1b | **Semi-transparent gun on the plane (jet) character** (added 15:45): the held gun was faintly see-through, the map visible behind it; noticed on the jet-vehicle character, unknown whether all. | Rendering | Suspects: a blend / depth state leaking into the weapon draw (weapon draw-queue replay after a translucent draw), the jet weapon's material being translucent or alpha-tested, the RT composite / AO over the weapon. Repro: pick a jet-form character, look at the gun against the map. |
| 4 | **Class change during respawn**: switching class while waiting to respawn (or right at the respawn moment) spawned the character but it could not move; "the game continued acting like I was still spawning in". | Gameplay | Bad state interaction: dying -> respawn -> class change -> replacement pawn. CLASSCHANGE harness exists; add the during-respawn timing to it. |

## P1 - UI / settings

| # | Finding (user) | Owner |
|---|---|---|
| 5 | **Game Mode Options**: Private Match -> game mode options already has Team Balancing, Map Selection, Time Limit, Points to Win. It must ALSO contain Player Limit, Autobots, Decepticons, Bot Difficulty, Bot AI; not on the following screen. | Frontend |
| 6 | **Map name centering**: the map name is left-offset; the blue overlay / frame overlaps the text. Center the name with the map preview so the frame never sits on the text. | Frontend |
| 7 | **Bot AI default = Smart; Classic not needed as an option.** "Smart should just be the normal / default bot behavior. Smart bots should use the same XP range / reward behavior as the Classic bots." | Frontend (remove the row) + Gameplay (default + XP parity) | Keep the Classic path in code behind an env var for the byte-identity harness; it leaves the UI. |
| 8 | **FPS counter** in the game settings. | Frontend + Rendering |

## P1 - gameplay

| # | Finding (user) | Owner |
|---|---|---|
| 9 | **Bot pathing** "still needs more work ... could definitely be a lot better. Please continue improving bot navigation / pathing." | Gameplay (bots unpaused for navigation work) |

## P1 - ray tracing

| # | Finding (user) | Owner |
|---|---|---|
| 10 | F9 cycle works; Off = classic; Faithful = mainly the character; Realistic = the whole map / room. "I like the Realistic mode affecting the entire environment." Effect "fairly subtle ... can continue being pushed further ... once more of the ray-traced lighting is implemented. The shadows look good so far. Please keep working on them." | Rendering (Realistic: GI next; keep pushing) |
| 11 | **RT reflections**: "not currently sure whether the ray-traced reflections are actually working ... Please verify them." | Rendering (prove with an on / off capture on a reflective surface; make them visible; roughness gate) |
| 12 | **RT shadow alignment**: on the map tested "some shadows do not line up correctly with where they should be." Map not named yet (asked). | Rendering |

## Checks

| # | Finding (user) | Owner |
|---|---|---|
| 13 | Main-menu globe / planet: "double-check ... make sure all of its textures are properly working and that there are no texture / material bugs. I think it may actually be fine, but I want it checked." | Rendering + Frontend (main-menu capture vs the original reference) |

## Working (user)
- Main menu loads smoothly; Multiplayer loads nicely; loading "looks good".
- XP works: 43 XP for a kill; bots die properly and XP is awarded.
- Overall: "the game is getting closer. It looks good and a lot is working. The main thing now is continuing to iron out bugs and finish the remaining implementations."

## Decisions (Integration, from the feedback)
- Smart AI milestone M2: the user's verdict is "Smart should be the default; Classic not needed" -> Smart default, Classic row removed from the UI (code path kept for identity tests). The remaining Smart work is pathing / navigation quality.
- Package order: P0 1-4 fixed and proven first, then 5-8 and the RT items, then a repackage + verify.

## Addendum 2026-10-14 (package 4)

- **Characters draw in front of smoke / effects even when inside them** (user, 13:00): translucent FX not depth-tested against characters (draw order / depth). Owner: Systems (effects fidelity), Rendering consulted on the pass order. Fix = a baseline change in all modes (new captures, not an identity gate).
