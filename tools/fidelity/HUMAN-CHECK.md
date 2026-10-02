# HUMAN CHECK: next integrated build (Milestone 03)

A machine PASS does not tick these. Play the integrated build. Where possible, compare side by side with
original footage. Each item lists the evidence to look at first (paths are relative to the gate output
folder, `work/fidelity/gate/<stamp>/`).

- [ ] **Vehicle weight: heavy/floaty like the original.** The probes show the hull snaps to the terrain
      in one frame (0.5 m step, 0.25 m bumps), has no hover spring (0 oscillation after a 1 m drop),
      recovers a 4 m drop in one frame, and has symmetric 0.45 s accel/decel. `harness.json`
      `vehicle_profiles.*`, `harness_traces/vp_*.csv`.
- [ ] **Hover pitch/roll response.** The vehicle body has yaw only (`vertical.body_pitch_roll`). Does the
      original lean on slopes, bumps and acceleration?
- [ ] **Transformation without a visible freeze or pop.** `transform/*/sheet.png` (switch frame shown
      in orange). The rebuild switches meshes at about 1.017 s (R→V) and 0.583 s (V→R); the authored
      overlap windows are 0.396–0.880 s and 0.098–0.663 s.
- [ ] **Vehicle texture/material appearance** (front/rear/side, transformed vs spawned as vehicle).
      `vehicle_visual/sheet.png`, `vehicle_visual/materials.json`.
- [ ] **Hover/boost FX** (rings, thruster plume, boost/nitro presentation) vs the original.
      `vehicle_visual/{hover_fx,boost,dash,nitro}`.
- [ ] **Vehicle camera** (follow distance, the shoulder swing at the transform: `camera_offset_swing`
      2.8 m).
- [ ] **Vehicle jump.** The rebuild launches at 0 m/s. Does the original jump, and how high?
- [ ] **Fine Aim camera shift.** Unresolved: see `FINE-AIM-EVIDENCE-REQUEST.md`. Is it a 2 m left shift,
      a 2 m dolly-in, or none?
- [ ] **Fine Aim reticle** (HUD crosshair; Rendering M03 adds the Ion Blaster crosshair). Does it match the
      original in normal aim and in fine aim?
- [ ] **Transform audio follows Optimus** (the transform cue is attached to the pawn after the Systems
      merge). `audio/offending_cues.txt`.
- [ ] **Landing audio feels grounded** (robot BL_FS_LRG_BOT land cues; vehicle hover landing).
- [ ] **Ambient Streets audio present** (70 authored ambient actors; currently NOT INSTANTIATED).
      `map/inventory.csv` category `ambient_audio`.
- [ ] **Sustained firing stays smooth** (50-round magazine, run and turn while firing).
      `perf/attribution.csv`; expected about 10–11 ms/frame after the collision fix.
- [ ] **Previously black/flat Streets surfaces** (flat pinkish ground in the chase view; props on
      fallback materials). `map/inventory.csv` WRONG MATERIAL rows and Rendering's `map_audit.json`.
- [ ] **Rotating / moving props** (12 movers drawn static, e.g. rotating deco spheres at 15 deg/s and
      light-beam cones).
