# TnTransformingWorldActor on MP maps: behaviour, triggers, collision

WFC RE lane, 2026-10-18, for Gameplay package 7 (collision fix).

Sources:
- script `TransGame.TnTransformingWorldActor` / `TnSeqAct_TransformWorldProp`;
- the authored Kismet in authored.db;
- AssetTools `manifests/maps/mp_transforming_actors.json` (32 actors).

## 1. What the actor does (CONFIRMED script)

- **Mesh:** a skeletal mesh (`HmInterpolatedLightMappedSkeletalMeshComponent`, also the CollisionComponent) with **one
  AnimNodeSequence** (e.g. `IAC_InteriorDoor_TransOut`) and its PhysicsAsset.
  - **Untransformed = anim position 0. Transformed = position 1** (the end of the sequence).
- **States:** Untransformed → Transforming → Transformed → Untransforming → Untransformed.
  - **Transforming:** `PlayAnim(rate 1)`; when the normalized position reaches ≥ 1 → Transformed.
  - **Untransforming:** plays the same anim at rate −1 from its end; at ≤ 0 → Untransformed.
  - A reversed request mid-way swaps direction.
  - Each start raises an AI stimulus (`TnStimulusType_WorldTransformation`).
- **Setup:** `InitialTransformState` sets InitialState (Untransformed or Transformed), the anim position (0 or 1) and the lightmap
  interpolation percentage (0 or 1).
- **Gates:** the actor gates its NavigationPoints, opening or closing them per state and per gate Behavior.
- **Triggers:** only `SetDesiredTransformState`. It is called from:
  - `TnSeqAct_TransformWorldProp`: input 0 "Untransform", input 1 "Transform"; output "Finished" when no target is still
    transforming;
  - `OnToggle` (SeqAct_Toggle: On = transform, Off = untransform, Toggle).
- **There is no proximity, use or touch trigger.**
- **Replication:** `DesiredTransformState` replicates (bAlwaysRelevant, SimulatedProxy), so clients play the same transitions.

## 2. The 32 MP actors (CONFIRMED from Kismet links)

| Map | Actors | Initial | What changes them in a match |
|---|---|---|---|
| **BrokenHope** (ESC) | 13 IAC_InteriorDoor | Untransformed (closed) | `TnSeqEvent_SurvivalGenerator` "New Power Level" → **Transform (open)**, one or two generators per door. |
| **BrokenHope** | 5 NEU_BridgeStraight | **Transformed** | `SeqEvent_PreGameplaySetup` → **Untransform** at setup, i.e. the bridges start retracted. Later RemoteEvents `TransformBridge1/2/3` (fired by SurvivalGenerator "New Power Level") → Transform. `UntransformBridgeN` events have no activator. |
| **Remnant** (ESC) | 12 ORB_InteriorDoor | Untransformed | SurvivalGenerator "New Power Level" → SeqAct_Toggle → Transform. Two doors (3920, 797) are chained from another door's TransformWorldProp "Finished". |
| **Rust** | IAC_TubeDoor + IAC_InteriorDoor | Untransformed | **`SeqEvent_SequenceActivated` (main sequence, level start) → Transform in every mode.** Three `SeqCond_GameRuleActive` nodes (BombingRun / Flags / KingOfTheHill) also link to Transform but have **no inputs**: dead nodes. |

**Answers:**
- **(2) They are not static.**
  - **Rust:** both doors open (animate to Transformed) right at match start, in every mode.
  - **BrokenHope and Remnant (Escalation maps):** doors start closed and open, and the bridges extend, only when Escalation
    generators are powered (TnSurvivalGenerator power levels). With no generator events, as in any non-Escalation use, they stay
    in their setup state:
    - doors closed;
    - BrokenHope bridges retracted, because PreGameplaySetup untransforms them.
  - The "Transformed" initial state of the bridges is therefore not what players see.

## 3. Collision (HIGH: stock UE3 semantics plus script)

- The PhysicsAsset is the actor's collision via the skeletal component:
  - COLLIDE_BlockAll, BlockActors, BlockNonZeroExtent, BlockRigidBody;
  - no PhysicsAssetInstance is authored.
- UE3 skeletal line and point checks test the PhysicsAsset bodies at the component's **current SpaceBases**, i.e. the **animated
  pose**.
- `bUpdateSkelWhenNotRendered = true` keeps the bones and the collision updating off-screen.
- On each end state, `SetValuesForState` also calls `ForceSkelUpdate()` + `UpdateRBBonesFromSpaceBases(false, true)`.
- **So collision follows the animated bones during and after a transform, not the reference pose.**
- **Rebuild:** build the PhysicsAsset bodies per bone and drive them from the anim at the actor's current normalized position: 0
  Untransformed, 1 Transformed, animated in between.

## 4. Versus vs Escalation (2026-10-18, for Gameplay's door collision / bot nav)

**Answer: Remnant and BrokenHope are Escalation-only maps, so their doors are never in a versus match (CONFIRMED).**

- `Coalesced_ini` `TnDataProvider_MapInfo`, `CompatibleGameTypes`:
  - `MP_ESC_Remnant_BASE_m` (MapId 506) = **["SV"]** only;
  - `MP_ESC_BrokenHope_BASE_m` (505) = **["SV"]** only;
  - `MP_IAC_Rust_BASE_m` (504) = DM, TDM, CTF, KOTH, EXT, DOM (Complex, Streets etc. the same).
  - SV = Escalation (survival). The original never loads Remnant or BrokenHope for TDM / CTF / Conquest / Power Struggle /
    Countdown, so "sealed corridors in versus" never occurs.
- **Within Escalation** (§2): the 12 Remnant doors and 13 BrokenHope doors start closed and open only on
  `TnSeqEvent_SurvivalGenerator` "New Power Level". That's generator power-ups bought between / during waves, not round start, a
  timer or player Touch. BrokenHope's 5 bridges are retracted at PreGameplaySetup and extend on the same generator events via
  RemoteEvents `TransformBridge1–3`. **Nothing ever closes them again** (the `UntransformBridgeN` events have no activator; no
  door Untransform is wired).
- **Rust (the only versus map with transforming actors):** both doors (IAC_TubeDoor, IAC_InteriorDoor) get Transform (open) from
  `SeqEvent_SequenceActivated` at level start, **in every mode**. They animate open once and stay open. The three GameRuleActive
  conditions linked to them are dead nodes (no inputs).
- **Timings:** each transition is one play of the actor's AnimNodeSequence at rate 1 (open; reverse = rate −1). Sequences:
  IAC_InteriorDoor_TransOut, NEU_BridgeStraight_TransOut, ORB_A4InteriorDoor_FastTransOpen, IAC_TubeDoor_TransOut. Their
  SequenceLength was not extracted here (the AnimSequence exports are not in the MP packages' export tables; AssetTools' animset
  export has them). There is no hold / auto-close timer: the state persists until another SetDesiredTransformState.

**For Gameplay:**
- If Remnant / BrokenHope are offered in versus, that's a rebuild extension. The original has no versus door state for them.
- Escalation-faithful: closed until the matching generator power level, then open for the rest of the match.
- Rust: open from match start in every mode, so nav should treat both Rust doors as open after the opening animation.
