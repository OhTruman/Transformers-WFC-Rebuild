# Milestone 03 gate - agents/experimental @ 69ee161 (20261002-051903)

**PASS 344 / FAIL 0 / KNOWN 71 / INFO 374 / SKIP 0**  (baseline: m03-baseline)

Steps: build reused (report only); build_measure reused (report only); harness reused (report only); transform_capture reused (report only); audio_attach reused (report only); vehicle_visual reused (report only); map_audit reused (report only); perf_profile reused (report only)

## PERFORMANCE REGRESSIONS (0)
- none
Improved (1): held.all: 51.45 -> 38.14 ms/frame

## VISUAL REGRESSIONS (25)
- transform\r2v_side\f00182: CHANGED (mean luma diff 7.5, 9% of cells) - review
- transform\r2v_side\f00188: CHANGED (mean luma diff 6.5, 9% of cells) - review
- transform\r2v_side\f00194: CHANGED (mean luma diff 5.3, 8% of cells) - review
- transform\r2v_side\f00200: CHANGED (mean luma diff 7.7, 9% of cells) - review
- transform\r2v_side\f00206: CHANGED (mean luma diff 7.8, 9% of cells) - review
- transform\r2v_side\f00212: CHANGED (mean luma diff 6.5, 9% of cells) - review
- transform\r2v_side\f00218: CHANGED (mean luma diff 6.3, 10% of cells) - review
- transform\r2v_side\f00224: CHANGED (mean luma diff 7.2, 9% of cells) - review
- transform\v2r_side\f00056: CHANGED (mean luma diff 6.7, 8% of cells) - review
- transform\v2r_side\f00062: CHANGED (mean luma diff 7.3, 7% of cells) - review
- transform\v2r_side\f00068: CHANGED (mean luma diff 9.4, 7% of cells) - review
- transform\v2r_side\f00074: CHANGED (mean luma diff 8.5, 6% of cells) - review
- transform\v2r_side\f00080: CHANGED (mean luma diff 5.8, 5% of cells) - review
- transform\v2r_side\f00086: CHANGED (mean luma diff 3.7, 4% of cells) - review
- transform\v2r_side\f00090: CHANGED (mean luma diff 3.9, 4% of cells) - review
- transform\v2r_side\f00092: CHANGED (mean luma diff 3.3, 3% of cells) - review
- vehicle_visual\bright_area\wfc: CHANGED (mean luma diff 25.3, 50% of cells) - review
- vehicle_visual\dash\wfc: CHANGED (mean luma diff 9.2, 17% of cells) - review
- vehicle_visual\hover_fx\wfc: CHANGED (mean luma diff 10.6, 20% of cells) - review
- vehicle_visual\idle_front34\wfc: CHANGED (mean luma diff 12.4, 12% of cells) - review
- vehicle_visual\idle_rear34\wfc: CHANGED (mean luma diff 10.8, 10% of cells) - review
- vehicle_visual\idle_side\wfc: CHANGED (mean luma diff 15.1, 16% of cells) - review
- vehicle_visual\nitro\wfc: CHANGED (mean luma diff 6.4, 8% of cells) - review
- vehicle_visual\transform_mid\direct_vehicle: CHANGED (mean luma diff 7.3, 10% of cells) - review
- vehicle_visual\transform_mid\r2v_end: CHANGED (mean luma diff 7.7, 9% of cells) - review

## AUDIO ATTACHMENT REGRESSIONS (0)
- none

## TRANSFORMATION REGRESSIONS (0)
- none

## VEHICLE REGRESSIONS (0)
- none

## MAP COMPLETENESS REGRESSIONS (1)
- lights: NOT INSTANTIATED 0 -> 1
Improved (2): movers: PRESENT + WRONG EFFECT 12 -> 3; props: PRESENT + WRONG MATERIAL 124 -> 0

## FINE AIM REGRESSIONS (0)
- none

## OTHER REGRESSIONS (0)
- none

## Performance phases (baseline -> now)
```
idle.steady                6.06 ->     6.40 ms
held.mag1_2_3s           266.04 ->   258.44 ms
held.all                  51.45 ->    38.14 ms
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

Artifacts: F:\Transformers Rebuild\Rebuild-Experimental\work\fidelity\gate\rendering-66bea49 (harness.json, transform\*\sheet.png, audio\offending_cues.txt, vehicle_visual\sheet.png, map\inventory.csv, perf\attribution.csv)
