# HUMAN CHECK: Milestone 03 merged build (integration/milestone-03 @ 356c352)

Play the merged **Release** exe. A machine PASS does not tick these: each is a human observation against
the original, **not permission to retune by feel**. A finding goes to the owner with a capture (F12 / the
gate's contact sheets). Evidence paths are under `tools/fidelity/results/integration-m03-356c352/`.

## Robot
- [ ] **Robot feel.** Walk / jog / strafe / diagonal / back / orbit / idle pivots. Acceleration and stop feel;
  jump apex ~5.1 m (the native 5.0 m with the known explicit-Euler overshoot, KNOWN Gameplay); landing weight.
- [ ] **Camera feel (robot).** Shoulder offset and orbit; no lag on turns. NOTE (KNOWN, below): the camera
  snaps straight in to the pawn when its collision ray is blocked near the anchor, and back out when it
  clears. Does the original pull in smoothly, or not at all, in the same spots?

## Weapon
- [ ] **Fine Aim feel.** Camera moves 2 m back (orbit-space), no sideways shift, FOV 80 -> 45, move speed x0.5.
- [ ] **Crosshair.** Bloom grows with held fire (+0.005/shot, 0.08 -> at most 0.18; one 50-round magazine
  peaks ~0.167), shrinks linearly (full range in 2 s), doubles in the air (0.25 s ramp, 0.5 s back after
  landing), halves in Fine Aim. Does the on-screen size track the original?
- [ ] **Sustained-fire smoothness.** Hold fire for several magazines incl. auto-reload: no hitch, no audio
  build-up, muzzle and tracer leave the barrel, shells/magazine eject.
- [ ] **Reload presentation.** Reload over strafing legs; magazine drop + flare.

## Transformation
- [ ] **Transform continuity**, both directions, standing and moving: both meshes overlap R->V 0.40-0.88 s,
  V->R 0.10-0.66 s; arm mesh shown while no weapon; no weapon fire before usable; no root, shadow or
  audio jump.
- [ ] **R->V camera while moving.** Most transforms are smooth. In some spots the camera collapses onto the
  pawn for 2-50 frames (camera-collision pull-in; also happens without transforming). See KNOWN.

## Vehicle
- [ ] **Vehicle weight.** COM rests at 1.287 m (native spring K 10000, B 4000, m = M/4). Heavy and planted?
- [ ] **Vertical response.** Springs settle in ~1 s; a 1 m drop settles with mm overshoot; a 10 m drop dips
  to ~0.6 m COM (PROVISIONAL: hull contact not recovered).
- [ ] **Sideways inertia.** Strafe and forward both stop in 0.5 s; velocity re-aims ~0.7 s after a 90 deg turn.
- [ ] **Pitch / roll.** Nose lifts ~4 deg over 0.25 m, ~8 deg over 0.5 m (springs only); lean on slopes,
  kerbs, strafing; no snap over steps.
- [ ] **Hover jump / boost jump / Dash / Boost / Nitro.** Jump +12 m/s (apex ~3.8 m); boost jump
  (+13.5 up, +6.4 fwd); Dash 30 m/s 0.5 s along the body; Boost to 30 m/s, Nitro to ~43.5 m/s
  (rise depends on the PROVISIONAL tire model).
- [ ] **Vehicle materials / hover and boost FX.** Paint, energon, glass vs the original; hover pattern,
  booster glow, smoke, afterburn.
- [ ] **Vehicle loops** stop when leaving the vehicle; no engine left behind.

## World
- [ ] **Lighting cohesion.** Baked lightmaps + dynamic light on Optimus in bright and dark areas.
- [ ] **Shadows.** Robot, vehicle and transform shadows under the composite light; no duplicate or stale
  shadow when moving between lighting cells.
- [ ] **Map completeness.** Streets props, BSP, decals, level FX (light rays, glows), pickups
  (authored factories), the destructible wall panel (placed outside the normal play space), moving props.
- [ ] **Sound placement.** Footsteps / landing / transform cues on Optimus; vehicle loops follow; ambient
  Streets bed and zone reverb.

## KNOWN going in (do not report as new)
- The plain pale box ~10 m in front of the spawn is Gameplay's weapon-test `DamageTarget` dummy
  (`World.cpp` spawns it unconditionally). Not original content; ignore it when judging map completeness.
  (The magenta pillar / green pad in some gate stills is the `WFC_DEBUGCAM` beacon - never in normal play.)
- Camera-collision snap (Gameplay, PROVISIONAL behaviour): unsmoothed pull-in when the
  `PlayerController::cameraPos` segment hits geometry within 0.3 m of the anchor. Seen in the R->V moving
  capture (frames 107-108); reproduces on Gameplay 0762f01 and with no transform at all.
- `ParticleBase_BW_MAT` fails to compile (translator vector-output defect, Rendering; fix deferred past M03).
- Robot jump apex overshoot (+0.14 m), step height 0.35 vs 0.37 m, low-obstacle wall probe, fire tap on a
  zero-step frame (Gameplay KNOWNs).
- Energon hue on the robot reads blue (TnCharacterApplier runtime colour not reconstructed).
