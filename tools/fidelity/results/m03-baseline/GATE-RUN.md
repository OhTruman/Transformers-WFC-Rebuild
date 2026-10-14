# Milestone 03 gate - agents/experimental @ 2729e3d (20261002-125255)

**PASS 345 / FAIL 0 / KNOWN 79 / INFO 380 / SKIP 0**  (baseline: m03-baseline)

Steps: build reused (report only); build_measure reused (report only); harness reused (report only); transform_capture reused (report only); audio_attach reused (report only); vehicle_visual reused (report only); map_audit reused (report only); perf_profile reused (report only); perf_counters reused (report only)

## PERFORMANCE REGRESSIONS (0)
- none

## VISUAL REGRESSIONS (0)
- none

## AUDIO ATTACHMENT REGRESSIONS (0)
- none

## TRANSFORMATION REGRESSIONS (0)
- none

## VEHICLE REGRESSIONS (0)
- none

## MAP COMPLETENESS REGRESSIONS (0)
- none

## FINE AIM REGRESSIONS (0)
- none

## OTHER REGRESSIONS (0)
- none

## Performance phases (baseline -> now)
```
held.mag1_2_3s           266.04 ->   266.04 ms
held.all                  51.45 ->    51.45 ms
idle.steady                6.06 ->     6.06 ms
burst.firing             321.04 ->   321.04 ms
burst.after_0_1s           9.39 ->     9.39 ms
burst.after_1_3s           6.12 ->     6.12 ms
burst.settled              6.05 ->     6.05 ms
fineaim.steady             5.48 ->     5.48 ms
walk.steady                5.12 ->     5.12 ms
```

## FAIL (0)
- none

## HUMAN CHECK (machine PASS does not resolve these)
- [ ] **Vehicle weight: heavy/floaty like the original.** The probes show the hull snaps to the terrain
- [ ] **Hover pitch/roll response.** The vehicle body has yaw only (`vertical.body_pitch_roll`). Does the
- [ ] **Transformation without a visible freeze or pop.** `transform/*/sheet.png` (switch frame shown
- [ ] **Vehicle texture/material appearance** (front/rear/side, transformed vs spawned as vehicle).
- [ ] **Hover/boost FX** (rings, thruster plume, boost/nitro presentation) vs the original.
- [ ] **Vehicle camera** (follow distance, the shoulder swing at the transform: `camera_offset_swing`
- [ ] **Vehicle jump.** The rebuild launches at 0 m/s. Does the original jump, and how high?
- [ ] **Fine Aim camera shift.** Unresolved: see `FINE-AIM-EVIDENCE-REQUEST.md`. Is it a 2 m left shift,
- [ ] **Fine Aim reticle** (HUD crosshair; Rendering M03 adds the Ion Blaster crosshair). Does it match the
- [ ] **Transform audio follows Optimus** (the transform cue is attached to the pawn after the Systems
- [ ] **Landing audio feels grounded** (robot BL_FS_LRG_BOT land cues; vehicle hover landing).
- [ ] **Ambient Streets audio present** (70 authored ambient actors; currently NOT INSTANTIATED).
- [ ] **Sustained firing stays smooth** (50-round magazine, run and turn while firing).
- [ ] **Previously black/flat Streets surfaces** (flat pinkish ground in the chase view; props on
- [ ] **Rotating / moving props** (12 movers drawn static, e.g. rotating deco spheres at 15 deg/s and

Artifacts: F:\Transformers Rebuild\Rebuild-Experimental\work\fidelity\gate\baseline-run (harness.json, transform\*\sheet.png, audio\offending_cues.txt, vehicle_visual\sheet.png, map\inventory.csv, perf\attribution.csv)
