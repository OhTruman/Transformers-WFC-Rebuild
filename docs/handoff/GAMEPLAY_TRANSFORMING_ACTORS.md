# MP transforming world actors: collision (package 7, after the hover item)

Gameplay, 2026-10-17. Sources:
- `docs/handoff/MP_TRANSFORMING_ACTORS.md` + AssetTools `manifests/maps/mp_transforming_actors.json`: 32 actors, their physics assets,
  poses, block flags and 118 boxes;
- the RE lane's `RE-Workspace/work/handoff/MP_TRANSFORMING_ACTORS_RE.md` (3e338ed).

## The gap (af4d0731)

Nothing blocks on them. Map movers get collision only from per-actor triangles in collision_pawn.glb
(`MapState::registerCollision`), and these skeletal actors' PhysicsAsset bodies were never exported there. So pawns, vehicles and
projectiles pass through:
- MP_ESC_BrokenHope: 12 IAC_InteriorDoor + 6 NEU_BridgeStraight;
- MP_ESC_Remnant: 12 ORB_InteriorDoor;
- MP_IAC_Rust: 2 doors (a user map).

## Original behaviour (RE, CONFIRMED)

1. **The pose is one animation:** a single AnimNodeSequence at normalized position 0 (Untransformed) or 1 (Transformed), animating at
   rate ±1 in between. **Collision = the PhysicsAsset bodies at the animated bones,** not the reference pose.
2. **Rust:** both doors Transform (open) at match start, in every mode (SequenceActivated → Transform).
3. **BrokenHope / Remnant doors:** start Untransformed (closed); they open only on the Escalation SurvivalGenerator's "New Power
   Level".
4. **BrokenHope bridges:** authored Transformed, but PreGameplaySetup untransforms them, so they start retracted. The generator's
   RemoteEvents TransformBridge1/2/3 extend them.
5. **Triggers:** Kismet only (TnSeqAct_TransformWorldProp or SeqAct_Toggle). DesiredTransformState replicates.

## Plan

- **Per actor and per PhysicsAsset body:**
  - a box (from the listed boxes, in the body's bone frame) as a dynamic collision set, in both the pawn and weapon worlds, through the
    existing `addDynamicSet` / `setDynamicPose` path;
  - posed every tick from the actor's transform × the bone's global at the current anim position (the transform animation sampled at
    0..1).
- **State:**
  - a per-actor `desired` (0 / 1) and `pos` advancing at ±1 per animation length;
  - start states as above (Rust: Transform at match start; doors closed; bridges retracted);
  - the Escalation triggers (generator power levels, TransformBridge1..3) are wired where the rebuild runs Escalation. Outside it the
    start state holds for the whole match.
- **Needed from the data:** the transform animation per actor (its AnimSet / sequence) and the bodies' bone names. If the json lacks
  the animation, ask AssetTools for the export.
- **Draw: also a gap.** The renderer's skeletal map-actor path (`WfcMapFx.cpp` ~1169) loads only HmSkeletalMeshActor, in bind
  pose; TnTransformingWorldActor isn't matched. So these actors are most likely not drawn (or are frozen, if world.glb has them as
  static nodes). To confirm with a capture.
  - **Proposal to Integration:** Gameplay owns the state and exposes per actor its anim position + pose; Rendering draws the skeletal
    mesh at that pose. One shared state, so what you see is what blocks.
- **Nav:** the actor gates its nav points (blocked while closed / retracted). It also raises an AI stimulus on transform (RE).

## Gates

- Maps without these actors: byte-identical (SIMHASH).
- On the 3 maps:
  - a walk-into test against a closed door / retracted bridge gap: blocked, and passing when open;
  - projectiles stopped;
  - NAVQ: Rust is a user map, so before / after on Rust, plus the identity note on the maps touched.

## Batched with it: dying-vehicle collision (Integration 2026-10-17: one identity change, one re-baseline)

The source is the RE lane's `VEHICLE_DEATH_PRESENTATION_RE.md` (2babd61).

- **VehicleDying keeps the live vehicle's rigid body and full collision until the gib:** it blocks pawns, projectiles and traces for
  TotalTime (car / tank moving U(0.3, 0.75) s, stationary U(0.2, 0.5) s; jets U(4, 5) s).
  - Today the dying pawn despawns at death, so nothing blocks.
  - **Plan:** a sim-side collision body at the vehicle's death pose (its hull collision), alive until the gib, in the pawn and weapon
    worlds, through the same dynamic-set path as the transforming actors.
- **The hull gib collides with the world and hitscan, not pawns (H) or vehicles.**
  - **Plan:** a hitscan-only body following the hull wreck for its lifetime (3-5 s, jets 7 s).
- **After the gib:** the actor is touch-only (non-blocking), so nothing blocks.

## Expected changes for the gate (both parts)

- **SIMHASH changes on every map** where a vehicle dies (the dying-vehicle bodies) and on BrokenHope / Remnant / Rust (the transforming
  actors).
- **Visual:** the doors and bridges drawn and animated (Rendering's half); death frames otherwise unchanged.
- **NAVQ before / after:** on the 4 user maps (vehicle deaths) and Rust (doors).

## Data delivered (AssetTools, 2026-10-17; Integration's copy)

- **GLBs:** `manifests/maps/transforming_actors/{IAC_InteriorDoor, IAC_TubeDoor, ORB_InteriorDoor, NEU_BridgeStraight}_SKEL.glb`
  (skinned, one clip each).
- **`mp_transforming_actors.json`** (old keys unchanged): meshes{glb, clip, clip_length_s (doors 2.333, tube door 1.667, bridge 0.967),
  clip_direction, bodies[{bone, boxes[{box_extent_*, box_local_*}]}], body_bone_keyframes (unused: I sample the GLB)},
  actors[*].drivers[], actors[*].nav_nodes_in_volume.
- **Posing:** world_box = actor.gltf_matrix × bone_global(t) × box_local_gltf, with half extents = box_extent_gltf / 2.
  - t = 0 is Untransformed (doors closed, bridge retracted at 12.3 m); t = clip_length is Transformed (doors open, bridge 20.5 m).
  - Checked by AssetTools: the bone globals match the umodel anim exactly, and the box AABBs match the skinned mesh at both ends.
- **Drivers:**
  - BrokenHope / Remnant doors: Transform ← TnSeqEvent_SurvivalGenerator (Escalation only).
  - Bridges: TransformWorldProp ← TransformBridgeN / UntransformBridgeN, plus Setup's Untransform.
  - **Rust: Transform ← SeqEvent_SequenceActivated, but the op's comment reads "KOTH or BOMB Active OPEN DOORS".** Check that
    sequence's mode gating against RE's "every mode" before implementing (an open question for the RE lane).
- **IMPORTANT, bridges:** their only body bone (L_Robo01_XT) does not animate, so the 3 boxes always cover the EXTENDED deck. The
  original switches that collision in Kismet:
  - UntransformBridgeN / Setup: SeqAct_ChangeCollision COLLIDE_NoCollision + Hide;
  - TransformBridgeN: ChangeCollision with the preset unauthored (the enum default, PROVISIONAL: blocking) + UnHide.
  - **So a retracted bridge has no collision and is hidden.** The plan follows the ChangeCollision / ToggleHidden ops, not posed boxes,
    for the bridges.
- **Nav:** no nav point references these actors in the cook. nav_nodes_in_volume lists the nodes inside each actor's box union
  (0 for Rust; up to 24 at Remnant), and those are the nodes to gate.
