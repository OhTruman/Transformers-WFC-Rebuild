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
