# Death bodies: design note (eye-pass item 1, before code)

Gameplay, 2026-10-16. This is the fix for "dead bots vanish on the death step" (`GAMEPLAY_EYEPASS_AUDIT.md`, 338 / 338 deaths). The source is RE-Workspace `notes/TARGETED_PASS5_2026-10-05.md` §12 addenda 10 and 17-25 (C unless marked). It is presentation only:
- The pawn still leaves the sim on the death step (`despawn()`, the kill, the respawn wave), so **SIMHASH is identical and Classic is byte-exact**.
- A separate **corpse** object, owned by World's presentation, plays the death.

## What the original does (per death)

1. **Pick an entry** (TnDeathFactory / picker): the first authored entry whose types are all active; no match gives the default death (no animation, TotalTime 1.0).
   - **Robot types (SharedRobotDeaths, in authored order):**

     | Entry | Matches |
     |---|---|
     | RobotBooster | damage type |
     | Poke | damage type |
     | Hovering | the pawn was hovering |
     | Airborne | **InAir** |
     | HeadShot | **Death_HeadShot**; the head bone scales to 0.1 |
     | Martyrdom | **Death_Martyrdom** |
     | TractorBeam | |
     | Energy | unreachable |
     | SingleShot | damage type's DamageDeathType |
     | RapidShot | damage type's DamageDeathType |
     | Melee | damage type's DamageDeathType |
     | Explosive | damage type's DamageDeathType |
     | Forward momentum | **Death_BackShot** |
     | Backward momentum | **Death_FrontShot** |

     A sideways momentum has no entry, so it gives the default death.
   - **Vehicles (TnVehicleDeathPicker):**
     - Car / truck: MovingCar or StationaryCar (moving means over 50 UU/s).
     - Tank: MovingTank or StationaryTank.
     - Plane.
     - All play **Death_Hover_VEH**, except planes, which play **Death_VEH**.
2. **Play it from the death pose** for **TotalTime**, a random value in [Min, Max]:

   | Death set | TotalTime |
   |---|---|
   | Robot entries | 1.25-3.5 s |
   | Brute | 2-4 s |
   | Cars / tanks | 0.2-0.75 s |
   | Jets | 4-5 s |

   Entry modifiers run during it:
   - small death FX at the head, booster and chest sockets;
   - the death light, which already exists (`spawnDeathLight`);
   - the melee entry's Defrag material (TransitionTime 3 s) and disintegrate sound;
   - the hover entry's impulse;
   - the vehicle "A" modifier: TF_Death_Buildup_FX or TF_Death_MED_FX at TargetingSocket.
3. **Gib at TotalTime** (`TnPawn.Dying.Gib`): the meshes hide, then:
   - **Robots:**
     - the Exp_Death_FX particle;
     - 4 RB_Gib chunks (800-2000 UU/s in a 60° cone up, life 5 s, gib trail FX);
     - the GIB_Body / GIB_LowBody2 skeletal gibs (life 5-15 s);
     - **the DeathDecal_MAT scorch decal**: a square 800-950 UU on the floor, 30 s, skipped on surfaces steeper than 60°.
   - **Cars / tanks:**
     - TF_Death_MED_FX;
     - 4 chunks from RB_Gib + SoldierCar_Wheel;
     - the **skeletal hull wreck**: the vehicle's `DefaultSkeletalGibTemplate`, e.g. VH_Bumblebee_HULL_SKEL. It takes 0.75 × the vehicle's velocity, plus a radial force of 300-700, and lives 3-5 s. It turns to the **Defrag** energy-death material after 0.5-1 s and dissolves over 4.5 s. No decal.
   - **Jets:** Exp_Death_Lrg_FX plus tire chunks; the hull dissolves over 6 s and lives 7 s.
   - The chunks take the owner's Cust_Color_A.
4. **Corpse removed** about TotalTime + 1 s after death.
5. **Death camera (local player):** `TnPlayerController.Dead`.
   - `CameraStrategyManager.Died` switches to the **Death** strategy (robot) or **Death_Vehicle**. These use the same third-person behaviour classes as OverTheShoulder; their exact orbit values are authored in CAM_*_Strategies_p and not read yet.
   - Spectating starts after MinRespawnDelay 3 s, and respawn at 5 s.

## What the rebuild will do

**Corpse list:** `std::vector<Corpse> corpses_` in World, presentation only and never read by the sim. A corpse is pushed where the death is presented today (`onPawnDeath`, local and participant). It captures:
- the pawn's chassis, form and world transform;
- its last animation pose (the skinned MeshData of that frame);
- its velocity;
- its character colours;
- the damage type and headshot flag;
- the "moving" flag (over 0.5 m/s).

**Entry pick:**
- **Interim (the death-light approximation, Integration 2026-10-15):** robots use the damage type's death type and the headshot flag. Hovering / airborne come from the pawn state at death; momentum comes from the killer's direction.
- **Vehicles:** the moving / stationary rule.
- **Later:** the full picker (TearOffMomentum from the hit) replaces the interim once the hit momentum is kept.

**Robots:**
- The corpse draws with the entry's animation from the pawn's skeleton (Death_* clips in each chassis' ROBO_ANIM set; the clip names are in the exported anim glTFs).
- **Missing clip:** hold the last pose (the default death has no animation either).
- **Head shot:** the head bone scales to 0.1.
- **Melee:** the Defrag sweep through `setDrawEnergyDeath`, which exists (M74).
- **At TotalTime:**
  - Exp_Death_FX;
  - 4 chunk meshes (the RB_Gib static meshes) on simple ballistic arcs with spin, no collision (H: chunks do not collide in the original either, CollisionLifeSpan has no effect), 5 s;
  - the scorch decal through the renderer's decal path, where it exists, else skipped and noted.

**Vehicles:**
- Death_Hover_VEH (cars / tanks / trucks) or Death_VEH (jets) for TotalTime, then the gib FX, the chunks and the **hull wreck**.
- The wreck is the hull skeletal mesh posed at the vehicle transform. It moves as a single rigid body (0.75 × the velocity plus an upward kick, gravity, ground-clamped; no per-bone physics in the rebuild, PROV).
- The Defrag starts after 0.5-1 s and runs over 4.5 s; the wreck is gone at 3-5 s.

**Skeletal gibs** (GIB_Body / LowBody2): optional in the first patch (PROV: rigid, not ragdolled).

**Local player:** the same corpse (it draws as the pawn did). The **death camera** keeps a third-person orbit on the corpse for the 3 s before spectating.
- **Interim:** the current camera framing, aimed at the corpse.
- The authored Death strategy values are read in the same pass, if they are short.

**Randomness:** a presentation-only RNG seeded by (victim, death time), never the sim's.

## Checks

- **SIMHASH and the Classic byte-exact reference:** unchanged on the 24 NAVQ seeds (presentation only).
- **BOTTEST DEATH rows:** every death has a corpse; the corpse's lifetime is within TotalTime + 1 s of its entry.
- **Staged crops (windowed, slot B):**
  - a robot killed by each of head shot, melee and explosive;
  - a car / tank wreck with its Defrag dissolve;
  - a jet.
  - Each as a before / after against base.
- **Cost:** at most about 10 live corpses in a 4 v 5 match (deaths about every 5 s, life up to 3.5 + 1 s), plus wrecks.
  - Per frame: one skinned draw each, plus 4 chunk draws and trail FX.
  - Expected under 0.1 ms CPU at 4 v 5. The GPU cost is the same as the pawns they replace.
  - Measured in the patch on a 16 v 16 Rust match (Integration's slot).

## Effort and order

1. **Patch 1:** robot corpse (animation + hold + headshot + melee Defrag + gib FX + chunks), vehicle wreck (hull + Defrag + chunks), local death camera interim. About 1 day.
2. **Patch 2:** scorch decals, skeletal robot gibs, authored death-camera strategy values, the full momentum picker.
