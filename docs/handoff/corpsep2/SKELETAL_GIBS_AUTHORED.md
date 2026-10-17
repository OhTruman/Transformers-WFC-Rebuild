# Skeletal robot gibs: authored data (patch 2, part 2)

Gameplay, 2026-10-17. Read only from `AssetTools/manifests/authored.db`; meshes in
`ExtractedAssets/content/TR_Death_Gibs_p/Skeletal/` (GIB_Body_SKEL, GIB_LowBody2_SKEL; tanks: SkeletalTank/GIB_Tank*).

## Spawner

- **`TR_Deaths_p.SharedRobotDeaths.TnSkeletalGibSpawner_10806`:** two templates, **GIB_Body_SKEL + GIB_Body_PHYSSYS** and
  **GIB_LowBody2_SKEL + GIB_LowBody2_PHYSSYS**.
  - Each has `Impulses` [] and `TransferVelocityMultiplier` 0, so the gibs inherit no velocity and get no impulse.
  - **LifeSpan U(5, 15) s.**
- **BruteRobotDeaths:** the same templates, LifeSpan {Min 15, Max 5} (swapped; a uniform pick gives the same 5-15 range).
- **SpiderDeaths:** LifeSpan only, no templates (no skeletal gibs).
- **Class defaults** (`Default__TnSkeletalGibSpawner`): ChanceToSpawn 1.0, ImpulseConeHalfAngle 45. There's **no default
  RadialForceStrength**, so robots get no radial push; only the car / tank / jet spawners author one.
- **Behaviour, then (corrected by the RE answer below):** **one** of the two gibs (50 / 50) appears at the gib moment and
  **collapses under gravity as a ragdoll**. They don't fly; the chunks
  and the explosion FX carry the violence.

## Physics assets

- **GIB_Body_PHYSSYS: 6 bodies, 5 constraints.**
  - Bodies, by bone: C_Spine02_Lumbar02 (sphyl r 48.5, len 65.5), C_Spine03_Neck01 (sphyl r 16.7, len 3.1), C_Spine04_Head (sphere
    r 29.5), R_Arm01_Clav (sphyl r 27.5, len 20.7), R_Arm02_Shoulder (sphyl r 31.6, len 65.8), R_Arm03_Elbow (sphyl r 30.4,
    len 66.8). UU, with each element's TM in the bone frame.
  - Constraints are cone-twist:
    - Neck → Lumbar02: swing 10 / 20, twist 10, limit stiffness / damping 10,000 (soft);
    - Head → Neck: 10 / 30 / 10, soft;
    - Clav → Lumbar02: 20 / 20;
    - Shoulder → Clav: 30 / 20 / 10;
    - Elbow → Shoulder: swing2 50 only.
- **GIB_LowBody2_PHYSSYS: 7 bodies, 6 constraints.** Read them with the same query when implementing.

## Collision channel (authored)

`Default__TnSkeletalRigidBody.DefaultRigidBodyComponent` (SkeletalMeshComponent):
- RBChannel **RBCC_EffectPhysics**;
- RBCollideWithChannels `{Vehicle: false, EffectPhysics: false}`.

So the gibs collide with the **world only**: not with vehicles, and not with each other or the RB_Gib chunks (both are effect
physics). There is no gib-vs-gib collision to model.

## RE answer (the RE lane via Integration, 2026-10-17, script bytecode; CONFIRMED unless marked)

1. **One gib per death, not two.** `TnSkeletalGibSpawner.Spawn` does one `Owner.Spawn(TnSkeletalRigidBody, , , Location)`, then
   `Templates[Rand(Templates.Length)]`: GIB_Body or GIB_LowBody2 at 50 / 50. It's called once, from `TnDeathPlayer.Deactivate`.
2. **The gate:**
   - `IsSpawnDisabled` = `GetGameThrottle() >= 3`, or Templates empty, or `FRand() > ChanceToSpawn`;
   - ChanceToSpawn is 1.0, so it passes, but the FRand is still drawn.
3. **The chain:**
   - `TnPawn.Dying.Gib` → `_CurrentForm.PlayGib()` → `TnRobotForm` / `TnVehicleForm.OnPlayGib` → `_DeathPlayer.Deactivate`;
   - Location = `Owner.Mesh.GetSocketWorldLocation('PositionSocket')`;
   - then the gibs DSYS, then `SkeletalGibs.Spawn(Owner, Location)`, then the decal.
4. **Rotation** (HIGH, native check pending): none is passed, so the UE3 default, the dying pawn's Rotation.
5. **Initial pose = the gib mesh's REFERENCE pose** at PositionSocket with the pawn's Rotation.
   - InitializeMesh: `SetSkeletalMesh(MeshTemplate)`, `SetPhysicsAsset(PhysicsTemplate, bForceReInit = true)`,
     `SetHasPhysicsAssetInstance(true)`, `WakeRigidBody()`.
   - Nothing copies bones: no AnimTree / AnimSets, `bUpdateKinematicBonesFromAnimation` false, PhysicsWeight 1.0.
   - The ref skeletons have GIB_Body 8 joints and GIB_LowBody2 11.
6. **Velocity: robots start at REST** and fall.
   - `RandomVectorInCone(Z, ImpulseConeHalfAngle)` is always computed (two FRand) but is used only in the Impulses loop, which is
     empty.
   - TransferOwnerVelocityToMesh runs only if the multiplier ≠ 0 (robots 0); RadialForceStrength only if ≠ 0 (robots none).
7. **Vehicles:**
   - `CreateDefaultDeath` → `AddDefaultSkeletalGibTemplates` adds the form's DefaultSkeletalGibTemplate (the hull; 39 blueprints).
   - TransferVelocityMultiplier 0.75 (StarscreamBoss 0.7): every bone gets `AddLinearVelocity(Owner.Mesh.GetRBLinearVelocity() ×
     mult)`.
   - Then `AddRadialForce(Bounds.Origin, Bounds.SphereRadius, U(300, 700), falloff 0)` (jets {700, 400}), then ApplyColorsToMesh.
8. **Modifiers:**
   - car / tank: SetEnergyDeathMaterial, Delay U(0.5, 1), TransitionTime 4.5;
   - jet: no delay, 6;
   - cloned per gib, ramping 'Defrag' 1 → 0.
   - **Robots: no modifiers**; the gib's own material + ApplyColorsToMesh (the owner's colours).
9. **Lifetime:** SetLifeSpan → LifeSpanExpired: hide, collision off, DestroyedEffect if any, Destroy (no fade).

**Rand sequence:** our gib uses the body's own `frand()`. Mirroring the original's draw order (the ChanceToSpawn FRand, the template
Rand, the cone's two FRand, the LifeSpan) only matters for parity with the original's RNG, which we don't reproduce. So the gib takes
**its own hashed stream**, like the decal, and the existing chunk / hull draws stay identical.

**Matches the existing vehicle hull** (implemented in patch 1): transfer × velocity + a radial kick (PROV rigid) + the Defrag ramp.

## Starting points for the RE lane (the original question, now answered above)

**Question:** when `TnSkeletalGibSpawner` spawns GIB_Body_SKEL / GIB_LowBody2_SKEL for a dying robot, what is each ragdoll's initial
pose?
1. the gib mesh's ref pose at a socket of the pawn (PositionSocket, as the vehicle hull), or at the actor location; or
2. bone transforms copied from the dying pawn's skeletal mesh by bone name. The gib skeletons use the robot bone names
   C_Spine02_Lumbar02_XB … R_Arm03_Elbow_XB, so a name match is possible.

Also wanted:
- the initial linear / angular velocity (Impulses is empty and TransferVelocityMultiplier is 0, but the code may still copy the
  pawn's bone velocities);
- the spawn rotation;
- whether ChanceToSpawn (1.0) and ImpulseConeHalfAngle (45) are used when Impulses is empty.

**Known names (classes and properties, from authored.db and the class table):**
- Classes:
  - `TransGame.TnSkeletalGibSpawner`: properties Templates[] {MeshTemplate, PhysicsTemplate, Impulses[], TransferVelocityMultiplier},
    LifeSpan {Min, Max}, ChanceToSpawn, ImpulseConeHalfAngle, RadialForceStrength {Min, Max}, Modifiers[];
  - `TransGame.TnSkeletalRigidBody`: components DefaultRigidBodyComponent, SkeletalMeshComponent0 (+ AnimNodeSeq0),
    FaceAudioComponent, DefaultActiveEffect, MyLightEnvironment;
  - `TransGame.TnSkeletalRigidBodyModifier` (DelayTimer, an HmTimer);
  - `TnSkeletalRigidBodyModifierSetEnergyDeathMaterial` (TransitionTime, CurrentScalarValue).
- Spawner objects:
  - `TR_Deaths_p.SharedRobotDeaths.TnSkeletalGibSpawner_10806`;
  - `TR_Deaths_p.BruteRobotDeaths.TnSkeletalGibSpawner_11714`;
  - the vehicle spawners `TR_Deaths_p.SharedCarDeaths / SharedTankDeaths / SharedJetDeaths.TnSkeletalGibSpawner_16032`.
- Spawn site: `TnPawn.Dying.Gib` → the death form's PlayGib (the gibs blueprint's effect state). The same path also spawns
  HmGibsDestructionEffect (the 4 RB_Gib chunks) and the Exp_Death_FX particle.
- The vehicle variant is already implemented from `DefaultSkeletalGibTemplate` (e.g. VH_Bumblebee_HULL_SKEL). There the hull keeps
  the vehicle mesh's frame at death (PROV), which is the analogous answer for vehicles.

## Faithful implementation (revised estimate)

- **A small position-based ragdoll:**
  - one rigid body per BodySetup (capsule / sphere, mass from volume);
  - cone-twist joint limits from ConstraintSetup;
  - gravity;
  - collision of the capsules against the world collision. Ground contact via `collision_`, plus wall pushes via the character
    capsule sweeps.
- Fixed 60 Hz sub-steps, presentation only: no sim RNG, and nothing read back into the sim.
- **Draw:** the gib mesh GPU-skinned from the bodies' bone transforms (the corpse `drawSkinnedGpu` path, so the casters batch), with
  the RB_GibExtra material and the owner's Cust_Color_A.
- **Open (RE needed):** the initial pose. Either the ref pose at the pawn's PositionSocket, or bone transforms copied from the
  dying pawn by name (the bone names match the robot skeleton: C_Spine*, R_Arm*). The ReVa Ghidra bridge is down, so this is to be
  read via a headless export into work/, or decided by comparison with the original's footage.
- **Estimate:** 8-12 h before the RE answer. Now **6-9 h**: one ragdoll per death, from rest in the ref pose, so no bone copy, no
  initial velocity and no gib-vs-gib collision. The solver, capsule-vs-world collision and GPU-skinned draw remain.
