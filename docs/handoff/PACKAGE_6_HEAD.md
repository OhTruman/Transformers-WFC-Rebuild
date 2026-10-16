# Package 6: integration <HEAD> (<DATE>)

<!-- SKELETON (Systems, drafted 2026-10-16 07:59 from docs/MILESTONES_09c.md up to the 07:59 entry). Integration fills
     <HEAD>, <DATE>, sizes, the Rendering rows marked [PLACEHOLDER] and the gate table at build time; rename the file to
     PACKAGE_6_<head>.md. Items marked (verify) were read from the log, not re-checked on the final head. -->

Launch with Play.bat (maps the package to W: on the dev PC). Package: work/playtest_slim (<SIZE> GB: <N> files kept +
<N> .xpr twins; HD pack D b0-31 3,506 files / 21.27 GB; glTF overlay <OVERLAY>, <N> files; promoted <DATE>). This is the
hand-over build the user playtests. Fallbacks: work/playtest_slim_398b27ef (package 5, internal checkpoint),
work/playtest_slim_30b2fa74 (package 4). Tag: playtest/09c-package6-<HEAD>.

Reboot before a long session: the AMD driver's nonpaged pool grows with GPU load and only a reboot clears it.

## New since package 5 (398b27ef)

### Realistic lighting (ray-tracing plan v3 and the extra X rows; Realistic only, Off / Faithful unchanged)

- **Characters take the RT sun** (row 1, bd749603, WFC_RT_CHARSUN): robots and movers in sunlight get the traced sun
  and its shadow, matching the world they stand in.
- **Lamp calibration on characters** (row 8, 5ab0a45c, WFC_RT_CHARLAMPCAL).
- **Sun-share shadows** (row 2, 8b29ff13, WFC_RT_SUNSHARE).
- **Transient lights in reflections** (row 6, 9007d194, WFC_RT_REFLTL): gun blasts and death glows show on reflective
  surfaces.
- **Probe fill** (row 5, 70310b02, WFC_RT_PROBEFILL).
- **Row 4** [PLACEHOLDER: Rendering - what it is, commit, switch].
- **Row 7** [PLACEHOLDER: Rendering - what it is, commit, switch].
- **Tiled transient lights** (1c94d62e, WFC_TLTILED=0 off): up to 64 dynamic lights per frame in Realistic in 32-px
  tiles (was 8); the first 4 keep RT shadows. Effect lights no longer push muzzle lights out of the cap; fixes the
  package 5 "transient lights cost at RT off" known issue [PLACEHOLDER: confirm the RT-off cost on the final regress].
- **X1 grade / exposure / bloom** [PLACEHOLDER: Rendering - final grade round, adaptation, bloom, per-map grade].
- **X2 specular**: a Realistic gain on UE3's directional-lightmap specular fed by the RT lightmap (x1.0 in Off /
  Faithful) [PLACEHOLDER: final gain, commit].
- **X4 bounce colour** (GI saturation 2.5 incl. probes) [PLACEHOLDER: commit].
- **X5 contact shadows / character AO** (X5-1: robots darker under cover) [PLACEHOLDER: commit, caster list from
  SYSTEMS_X5_CASTERS - sentries first, barrier never casts].
- **X3 volumetrics** [PLACEHOLDER: only if the decision rule moved X3 into package 6 - sun shafts / lamp halos / lit fog].
- **RT-era effect lights** (Systems 709df0f5, WFC_FXLIGHTS): explosions, impacts, vehicle boost and pickups give off
  light, calibrated per template from the decoded fade curves (SYSTEMS_RT_LIGHT_HOOKS) [PLACEHOLDER: default-on in
  Realistic after Rendering's calibration batch - final gain]. The original has no such lights (a deliberate Realistic
  addition).
- **Vehicle headlights** (Systems 709df0f5, WFC_HEADLIGHTS): an RT-shadowed spot per vehicle at invented positions
  (SYSTEMS_VEHICLE_BEAMS; the original has no vehicle light components) [PLACEHOLDER: default state, colour].
- **Enhanced materials (plan item 6)**: 56 compiled materials added to every MP map's render data (existing entries
  byte-identical on all 10 maps); the sentry turret draws its authored hologram (package 5 known issue closed); the
  Modern energy calibration [PLACEHOLDER: Rust +19.3 % / Molten +6.7 % vs +-5 % target at 06:48 - final numbers].
- **Item 7 polish** [PLACEHOLDER: Rendering - temporal reflections / lightmap super-sample, if any shipped].

### Everything else

- **HD textures actually load.** Packages 4 and 5 silently ignored the pack (the loader read a row's byte count as a
  float; 89,478,612 rounded). Fixed 66eb7d73; the exe logs "HD texture pack available: 3505 textures"; verify_slim has
  an HD runtime row that requires that line. First build where the HD row shows.
- **Death bodies** (Gameplay fcc567dd / a6e9ea67): a killed robot plays its picked death animation from its last pose
  (melee sweep, explosion, headshot, fall / crawl) then gibs; vehicles leave a hull wreck with chunks and smoke that
  dissolves. Package 5 showed only the kill-feed icon. Presentation only (SIMHASH identical).
- **Kamikaze mines drawn** (Gameplay 050a7796, closed dc5f5a61): mines and their trails render; the detonation effect
  (Exp_2500cb_FX) renders (staged check, Systems fd447694).
- **Bot turning speed** (Gameplay 022e5e34): the original's AiMaxAngularSpeed - robot facing snaps 133 -> 0, vehicle
  heading snaps 1,016 -> 173. Classic re-baselined on this commit (by design: every Classic run diverges from the old
  reference from step ~602; new reference docs/handoff/vehyaw/).
- **Audio** (Systems 9711d354, b9e6f3d8): the original's FMOD rule that a sound quieter than -64 dB at the listener holds
  no channel (virtual, resumes when audible); 128 real voices in ExtendedPlayers matches (16 v 16 / 32 v 32, our
  extension), the original's 96 otherwise. 63 bots, paced: refused sounds Streets 1,700 -> 529, Gorge 2,726 -> 779; the
  player's own sounds are never refused.
- **Impact effects with a degenerate surface normal** now draw (one per map was dropped; Systems 9711d354).
- **Effect data decode** (AssetTools 7b1908a, switched 07:59): homing-rocket explosion embers fly (they sat still);
  16 Orbit LODs complete; VelocityOverLife modules carry their Absolute flag. 2,007 LODs identical, 0 unexpected
  changes (docs/handoff/fx_regen/).
- Harness-only: WFC_NOINTERACT exact client size (7d9edf78, 2560x1440 verified), voice_ab.ps1 -Paced (audio counts
  from lockstep + 60 fps), eye-pass pipeline (27ab8332), package-gate.sh.

## Known issues

- **Debris Smart cost**: [PLACEHOLDER: numbers] - Smart bots on Debris lose time at vehicle corners; the next Smart item
  (vehicle corner anticipation) after package 6.
- **Death bodies, patch 2 deferred**: decals, skeletal gibs, the authored death camera and the full momentum picker come
  after package 6; the death-type choice is still an approximation.
- **X3 / X6 / X7 = package 7** unless pulled in: volumetric light (X3, unless the decision rule moved it in), more
  reflective surfaces + damp sheen (X6), 2x RT lightmap (X7).
- **Explosion close-ups saturate**: Exp_2500cb_FX (mine) is authored HDR yellow (modulate glow x60/60/20); several at the
  camera fill the screen flat yellow-green. Data, not a bug; the HDR clamp point vs the 360's 7e3 scene target and soft
  particles for all translucent particles are Rendering items [PLACEHOLDER: shipped or not].
- **Two vending-machine materials** (Ammo / EnergonCube MATINST) fail the runtime GLSL compile and keep the glTF
  fallback (campaign-style props) [PLACEHOLDER: fixed or not].
- **Vehicle muzzle lights saturate nearby walls** (authored brightness 25 / 5 m, as the original) - carried from
  package 5; WFC_TLIGHTSCALE is the lever.
- **Squad follow reversals** (Smart) - carried from package 5.
- **Driver nonpaged pool** grows with GPU load (AMD); reboot before a long session.
- [PLACEHOLDER: anything the final gate or eye pass adds]

## Gates (final head <HEAD>)

<!-- package-gate.sh table; base = 398b27ef + yaw 022e5e34 (same match across the 3 arms), perf-only vs 398b27ef. -->

| gate | result | notes |
|---|---|---|
| regress-8map (3 arms x 8 maps, RT off, Classic, lockstep) vs 398b27ef+022e5e34 | [PLACEHOLDER] | lights-off arm limit; tiled-light cost |
| Off / Faithful identity (Rust, Classic): SIMHASH + frames | [PLACEHOLDER] | expected diffs: death bodies, mines, effect data |
| Realistic perf, 1440p 63 bots (p50 / p90 per map) | [PLACEHOLDER] | floor ~60 fps at 1440p; budgets are reports |
| R4 leak rows (GPU dedicated over 5 map changes) | [PLACEHOLDER] | |
| Eye pass: same-frame Off / Realistic / Realistic+HD sheets, 5 user maps | [PLACEHOLDER] | night-and-day bar |
| verify_slim -SubstDrive X: (maps, HD static + HD runtime row, bin vs build_slim.json, overlay, Settings, RT gate) | [PLACEHOLDER] | HD runtime row must PASS (fails package 5 by design) |
| FAST gate | [PLACEHOLDER] | |
| BOTTEST / NAVQ / SIMHASH (both AIs) | [PLACEHOLDER] | 46 / 46 at fcc567dd |
| Settings survive map change (WFC_SETTINGSTEST) | [PLACEHOLDER] | new renderer switches in release()'s keep list |
