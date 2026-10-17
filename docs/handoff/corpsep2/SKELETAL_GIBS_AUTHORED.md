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
- **Behaviour, then:** both gibs appear at the gib moment and **collapse under gravity as ragdolls**. They don't fly; the chunks
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
- **Estimate:** 8-12 h (was 4-6 h, which assumed rigid gibs).
