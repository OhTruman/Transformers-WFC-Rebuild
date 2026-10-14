# Milestone 03 gate - agents/experimental @ 425eb1a (20261002-224901)

**PASS 665 / FAIL 0 / KNOWN 21 / INFO 599 / SKIP 0**  (baseline: m03-baseline)

Steps: harness reused (report only); transform_capture reused (report only); audio_attach reused (report only); vehicle_visual reused (report only); map_audit reused (report only); runtime_probe reused (report only); render_selftests reused (report only); perf_release reused (report only); perf_profile reused (report only); perf_counters reused (report only)

## PERFORMANCE REGRESSIONS (1)
- idle.steady: 6.06 -> 10.41 ms/frame
Improved (4): trace_cost.hitscan_trace_under_1ms: KNOWN -> PASS; held.mag1_2_3s: 266.04 -> 8.12 ms/frame; held.all: 51.45 -> 11.52 ms/frame; burst.firing: 321.04 -> 214.90 ms/frame

## VISUAL REGRESSIONS (82)
- transform\r2v_side\f00056: CHANGED (mean luma diff 5.4, 4% of cells) - review
- transform\r2v_side\f00062: CHANGED (mean luma diff 5.2, 3% of cells) - review
- transform\r2v_side\f00068: CHANGED (mean luma diff 5.2, 3% of cells) - review
- transform\r2v_side\f00074: CHANGED (mean luma diff 5.5, 3% of cells) - review
- transform\r2v_side\f00080: CHANGED (mean luma diff 6.2, 5% of cells) - review
- transform\r2v_side\f00086: CHANGED (mean luma diff 6.8, 6% of cells) - review
- transform\r2v_side\f00092: CHANGED (mean luma diff 7.3, 6% of cells) - review
- transform\r2v_side\f00098: CHANGED (mean luma diff 7.0, 6% of cells) - review
- transform\r2v_side\f00104: CHANGED (mean luma diff 6.1, 3% of cells) - review
- transform\r2v_side\f00110: CHANGED (mean luma diff 5.3, 3% of cells) - review
- transform\r2v_side\f00116: CHANGED (mean luma diff 5.3, 3% of cells) - review
- transform\r2v_side\f00120: missing in this run
- transform\r2v_side\f00122: CHANGED (mean luma diff 6.4, 6% of cells) - review
- transform\r2v_side\f00128: CHANGED (mean luma diff 6.5, 6% of cells) - review
- transform\r2v_side\f00134: CHANGED (mean luma diff 6.5, 6% of cells) - review
- transform\r2v_side\f00140: CHANGED (mean luma diff 6.6, 6% of cells) - review
- transform\r2v_side\f00146: CHANGED (mean luma diff 6.7, 6% of cells) - review
- transform\r2v_side\f00152: CHANGED (mean luma diff 7.0, 7% of cells) - review
- transform\r2v_side\f00158: CHANGED (mean luma diff 7.5, 9% of cells) - review
- transform\r2v_side\f00164: CHANGED (mean luma diff 7.7, 10% of cells) - review
- transform\r2v_side\f00166: missing in this run
- transform\r2v_side\f00168: missing in this run
- transform\r2v_side\f00170: CHANGED (mean luma diff 10.5, 13% of cells) - review
- transform\r2v_side\f00172: missing in this run
- transform\r2v_side\f00174: missing in this run
- transform\r2v_side\f00176: CHANGED (mean luma diff 12.2, 14% of cells) - review
- transform\r2v_side\f00178: missing in this run
- transform\r2v_side\f00182: CHANGED (mean luma diff 15.6, 15% of cells) - review
- transform\r2v_side\f00188: CHANGED (mean luma diff 14.2, 13% of cells) - review
- transform\r2v_side\f00194: CHANGED (mean luma diff 13.0, 13% of cells) - review
- transform\r2v_side\f00200: CHANGED (mean luma diff 15.4, 15% of cells) - review
- transform\r2v_side\f00206: CHANGED (mean luma diff 15.6, 16% of cells) - review
- transform\r2v_side\f00212: CHANGED (mean luma diff 14.6, 16% of cells) - review
- transform\r2v_side\f00218: CHANGED (mean luma diff 14.6, 16% of cells) - review
- transform\r2v_side\f00224: CHANGED (mean luma diff 14.3, 12% of cells) - review
- transform\v2r_side\f00056: CHANGED (mean luma diff 12.3, 11% of cells) - review
- transform\v2r_side\f00062: CHANGED (mean luma diff 11.0, 9% of cells) - review
- transform\v2r_side\f00068: CHANGED (mean luma diff 14.1, 11% of cells) - review
- transform\v2r_side\f00074: CHANGED (mean luma diff 13.0, 9% of cells) - review
- transform\v2r_side\f00080: CHANGED (mean luma diff 11.1, 8% of cells) - review

## RENDERING REGRESSIONS (0)
- none

## WEAPON REGRESSIONS (0)
- none

## AUDIO ATTACHMENT REGRESSIONS (0)
- none
Improved (39): audio_attach.moving_transform.wave.TRANS_ION_UNEQUIP_SERVOS.wav: KNOWN -> PASS; audio_attach.robot_footsteps.wave.TRANS_ION_UNEQUIP_SERVOS.wav: KNOWN -> PASS; audio_attach.robot_footsteps.wave.GUN_RIFLE_BOLT_ACTION.wav: KNOWN -> PASS; audio_attach.jump_land.wave.TRANS_ION_UNEQUIP_SERVOS.wav: KNOWN -> PASS; audio_attach.boost.wave.AUTO_OPTIMUS_BOOST_START_LR.wav: KNOWN -> PASS; audio_attach.boost.wave.WHSH_BOOST_FLARE_LR_03.wav: KNOWN -> PASS; audio_attach.boost.wave.SYNTH_ENGINE_ACCEL.wav: KNOWN -> PASS; audio_attach.boost.wave.SYNTH_AIR_RELEASE_04.wav: KNOWN -> PASS; audio_attach.boost.wave.SYNTH_AIR_RELEASE_08.wav: KNOWN -> PASS; audio_attach.nitro.wave.AUTO_OPTIMUS_BOOST_START_LR.wav: KNOWN -> PASS; audio_attach.nitro.wave.WHSH_BOOST_FLARE_LR_03.wav: KNOWN -> PASS; audio_attach.nitro.wave.SYNTH_ENGINE_ACCEL.wav: KNOWN -> PASS ...

## TRANSFORMATION REGRESSIONS (0)
- none
Improved (24): transform_momentum.weapon_restore_frac_elapsed_to_robot: KNOWN -> PASS; transform_timeline.r2v_stand.both_meshes_visible_in_fold: KNOWN -> PASS; transform_timeline.r2v_moving.both_meshes_visible_in_fold: KNOWN -> PASS; transform_timeline.v2r_stand.no_usable_weapon_without_muzzle: KNOWN -> PASS; transform_timeline.v2r_stand.both_meshes_visible_in_fold: KNOWN -> PASS; transform_timeline.v2r_moving.no_usable_weapon_without_muzzle: KNOWN -> PASS; transform_timeline.v2r_moving.both_meshes_visible_in_fold: KNOWN -> PASS; transform_timeline.separate_arm_mesh: KNOWN -> PASS; transform_analyzer.r2v_stand.overlap_inside_authored_window: KNOWN -> PASS; transform_analyzer.r2v_stand.target_mesh_first_visible: KNOWN -> PASS; transform_analyzer.r2v_stand.source_mesh_hidden: KNOWN -> PASS; transform_analyzer.r2v_moving.overlap_inside_authored_window: KNOWN -> PASS ...

## VEHICLE REGRESSIONS (0)
- none
Improved (3): movement.vehicle_rest_com_height: KNOWN -> PASS; vehicle_profiles.step.BOOST.never_below_surface: KNOWN -> PASS; vehicle_profiles.step.NITRO.never_below_surface: KNOWN -> PASS

## MAP COMPLETENESS REGRESSIONS (2)
- movers: NOT INSTANTIATED 0 -> 4
- lights: NOT INSTANTIATED 0 -> 1
Improved (6): movers: PRESENT + WRONG EFFECT 12 -> 3; props: PRESENT + WRONG MATERIAL 124 -> 0; level_emitters: NOT INSTANTIATED 8 -> 0; pickups: NOT INSTANTIATED 5 -> 0; destructibles: NOT INSTANTIATED 1 -> 0; ambient_audio: NOT INSTANTIATED 70 -> 30

## FINE AIM REGRESSIONS (0)
- none
Improved (2): fine_aim_presentation.start_end_audio: KNOWN -> PASS; fine_aim_probe.start_end_audio_wired: KNOWN -> PASS

## OTHER REGRESSIONS (0)
- none
Improved (25): collision.wall_slide_speed: KNOWN -> PASS; native_vehicle.rest.com_height: KNOWN -> PASS; native_vehicle.free_fall.contacts: KNOWN -> PASS; native_vehicle.free_fall.rb_gravity: KNOWN -> PASS; native_vehicle.hover_jump_stationary.dvz: KNOWN -> PASS; native_vehicle.step_025.no_one_frame_snap: KNOWN -> PASS; native_vehicle.step_025.attitude_follows_terrain: KNOWN -> PASS; native_vehicle.step_025.rest_com_on_top: KNOWN -> PASS; native_vehicle.step_050.no_one_frame_snap: KNOWN -> PASS; native_vehicle.step_050.attitude_follows_terrain: KNOWN -> PASS; native_vehicle.step_050.rest_com_on_top: KNOWN -> PASS; native_vehicle.drop10.settles_to_rest: KNOWN -> PASS ...

## Performance phases (baseline -> now)
```
held.mag1_2_3s           266.04 ->     8.12 ms
held.all                  51.45 ->    11.52 ms
idle.steady                6.06 ->    10.41 ms
burst.firing             321.04 ->   214.90 ms
burst.after_0_1s           9.39 ->     8.37 ms
burst.after_1_3s           6.12 ->     7.93 ms
burst.settled              6.05 ->     7.90 ms
fineaim.steady             5.48 ->     7.01 ms
walk.steady                5.12 ->     5.79 ms
```

## TOOL ERRORS (0) - gate/suite problems, not product results
- none

## FAIL (0)
- none

## HUMAN CHECK (machine PASS does not resolve these)
- [ ] **Robot feel.** Walk / jog / strafe / diagonal / back / orbit / idle pivots. Acceleration and stop feel;
- [ ] **Camera feel (robot).** Shoulder offset and orbit; no lag on turns. NOTE (KNOWN, below): the camera
- [ ] **Fine Aim feel.** Camera moves 2 m back (orbit-space), no sideways shift, FOV 80 -> 45, move speed x0.5.
- [ ] **Crosshair.** Bloom grows with held fire (+0.005/shot, 0.08 -> at most 0.18; one 50-round magazine
- [ ] **Sustained-fire smoothness.** Hold fire for several magazines incl. auto-reload: no hitch, no audio
- [ ] **Reload presentation.** Reload over strafing legs; magazine drop + flare.
- [ ] **Transform continuity**, both directions, standing and moving: both meshes overlap R->V 0.40-0.88 s,
- [ ] **R->V camera while moving.** Most transforms are smooth. In some spots the camera collapses onto the
- [ ] **Vehicle weight.** COM rests at 1.287 m (native spring K 10000, B 4000, m = M/4). Heavy and planted?
- [ ] **Vertical response.** Springs settle in ~1 s; a 1 m drop settles with mm overshoot; a 10 m drop dips
- [ ] **Sideways inertia.** Strafe and forward both stop in 0.5 s; velocity re-aims ~0.7 s after a 90 deg turn.
- [ ] **Pitch / roll.** Nose lifts ~4 deg over 0.25 m, ~8 deg over 0.5 m (springs only); lean on slopes,
- [ ] **Hover jump / boost jump / Dash / Boost / Nitro.** Jump +12 m/s (apex ~3.8 m); boost jump
- [ ] **Vehicle materials / hover and boost FX.** Paint, energon, glass vs the original; hover pattern,
- [ ] **Vehicle loops** stop when leaving the vehicle; no engine left behind.
- [ ] **Lighting cohesion.** Baked lightmaps + dynamic light on Optimus in bright and dark areas.
- [ ] **Shadows.** Robot, vehicle and transform shadows under the composite light; no duplicate or stale
- [ ] **Map completeness.** Streets props, BSP, decals, level FX (light rays, glows), pickups
- [ ] **Sound placement.** Footsteps / landing / transform cues on Optimus; vehicle loops follow; ambient

Artifacts: F:\Transformers Rebuild\Rebuild-Experimental\work\fidelity\m3v\gate (harness.json, transform\*\sheet.png, audio\offending_cues.txt, vehicle_visual\sheet.png, map\inventory.csv, perf\attribution.csv)
