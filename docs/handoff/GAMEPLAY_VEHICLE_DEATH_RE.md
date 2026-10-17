# Vehicle death presentation, end to end (car / truck / tank / jet)

WFC RE lane, 2026-10-18.

This consolidates `notes/TARGETED_PASS5_2026-10-05.md` §12 (addenda 10, 12, 21–24), SKELETAL_GIB_INIT_RE.md and
DEATH_PICKER_AND_LIGHT_RE.md, and adds wreck collision and lifetime. The sources are script plus authored.db.

Labels: CONFIRMED unless marked; H = engine-default behaviour, not traced.

## 1. Timeline

| t | What happens | Source |
|---|---|---|
| 0 | `TnPawn.PlayDying` → **`VehicleDying`**, a Dying substate. **The form and the collision mode are unchanged.** `Dying.BeginState`: stop anims, end interacts, `bTearOff` (each machine plays the rest locally), body impact sounds on | TnPawn |
| 0 | `TnVehicleForm.OnPlayDeath`: `_DeathPlayer.Activate()` picks the entry (§2) and starts its modifiers; `PlaySound(VehicleBlueprint.DeathSound)`. This is the only vehicle death sound; no entry adds one | add. 12, 21 |
| 0 → TotalTime | The wreck plays the entry anim on the vehicle mesh and keeps simulating as the **live rigid-body vehicle** (§3). Rigid-body impacts still feed `_VehicleForm.RigidBodyCollisionOccurred` (impact sounds). Car / tank: the **death light** ramps up | §2, §4 |
| TotalTime | **Gib** (`Dying.Gib`): hide the robot and vehicle meshes; LifeSpan = 1; collision mode 1, cylinder made non-blocking (§3); explode-on-death buffs; `VehicleMesh.SetAnimTreeTemplate(none)`; `_DeathPlayer.Deactivate()` → gibs DSYS + hull gib + (no) decal; gibbed camera shake for local players within `GibbedShakeEffectDistance` | add. 10, 22 |
| TotalTime + 1 s | The pawn actor is destroyed (LifeSpan). The death light goes with it | add. 25 |
| up to + 5 s | DSYS chunks expire (LifeSpan 5) | add. 22 |
| up to + 3–5 s (car / tank), + 7 s (jet) | The hull gib expires: mesh hidden and collision off, then destroyed | §5 |

## 2. Per-entry content (`TnVehicleDeathPicker`, first entry whose types all match)

**Types:**
- Car / Truck: `Car` + (MovingCar if the rigid-body `LinearSpeed > 50`, else StationaryCar);
- Tank: `Tank` + Moving / Stationary by the same rule;
- Plane: `Plane`.

| Death blueprint (forms) | Entry | Vehicle anim | TotalTime, until the gib (s) | At death (modifiers) | At gib: DSYS | At gib: hull |
|---|---|---|---|---|---|---|
| **SharedCarDeaths** (23 car / truck forms) | MovingCar | Death_Hover_VEH | U(0.3, 0.75) | **A** one of TF_Death_Buildup_FX / TF_Death_MED_FX at TargetingSocket; **B** light | SoldierCarGibs_DSYS | yes for 17 of 23; none for the soldier-car Aut / CQC Aut / CQC Dec forms |
| | StationaryCar | Death_Hover_VEH | U(0.2, 0.5) | A + B | same | same |
| **SharedTankDeaths** (5 tanks: Warpath, Brawl, Megatron, Scattershot, Demolishor) | MovingTank / StationaryTank | Death_Hover_VEH | U(0.3, 0.75) / U(0.2, 0.5) | A + B | SoldierCarGibs_DSYS | yes, all 5 |
| **SharedJetDeaths** (14 plane forms) | Plane | Death_VEH | **U(4, 5)** (AI the same) | **C** both Smoke_Trail_Jet_Fast_FX at VaporTrailSocket_R and TF_Death_MED_FX at TargetingSocket; **no light** | DefaultVehicleGibs_DSYS | yes for 11 of 12 VEHDEFs; none for SoldierJetNeu |
| SharedVehicleDeaths (Laserbeak only) | Car ×2 / Tank ×2 / Plane | Death_Hover_VEH / none | 0.3–0.4 … Plane 1.5–1.6 | A + B / C | DefaultVehicleGibs_DSYS | no templates |

**Modifier B, the death light** (car / tank only):
- a PointLight at the pawn origin, colour (202, 123, 213), Brightness 25, Radius 512, RampIn 0.2 s;
- LightTime 8 s, but it disappears with the actor at TotalTime + 1 s (DEATH_PICKER_AND_LIGHT_RE.md).

**DSYS effect** (piece Notification-triggered at PositionSocket):

| | SoldierCarGibs_DSYS (car / tank) | DefaultVehicleGibs_DSYS (jet) |
|---|---|---|
| Explosion particle | TF_Death_MED_FX | Exp_Death_Lrg_FX |
| Chunks | 4 from RB_Gib_01..11 + 3× SoldierCar_Wheel | 4 from RB_Gib_01..11 + 4× RB_GibTire_01 |
| Chunk speed | U(800, 2000) | U(2000, 4000) |

Shared chunk settings:
- angular speed U(12000, 20000) in a 60° half-cone about +Z, spawned on a 100 UU sphere 100 UU up;
- trail TF_Death_GibsTrail_FX each;
- the owner's `Cust_Color_A` tint;
- LifeSpan 5;
- the chunk count is halved, quartered or zeroed by the game throttle;
- spawned locally on each machine.

**No decal for any vehicle death** (the death decal is robots only).

## 3. Collision of the wreck

1. **Death → gib (the 0.2–0.75 s car / tank window; 4–5 s for jets):**
   - `VehicleDying` does not change `_CollisionController`. The pawn stays in **mode 4, PhysicalVehicleMesh**: the vehicle
     skeletal mesh is the collision component with `SetBlockRigidBody / SetActorCollision(true, true) / SetTraceBlocking(true,
     true)`, PHYS_RigidBody.
   - **The wreck blocks pawns, projectiles and traces, and keeps tumbling or sliding as a rigid body** with the momentum it died
     with. H: the jet's 4–5 s fall is this rigid body under its form's simulation without input; the trajectory wasn't traced.
   - H: damage taken in this window is ignored (Dying); projectiles still impact and explode on it.
2. **After the gib (the invisible 1 s before destroy):**
   - collision mode 1 (`AnimatedCylinderRobotMeshHybrid`), then `Dying.Gib` sets the cylinder to **`SetBlockRigidBody(false)`,
     `SetActorCollision(true, false)`**;
   - mode 1 also left the cylinder `SetTraceBlocking(false, true)`, and the vehicle mesh collision is disabled;
   - **so nothing blocks pawn movement or zero-extent traces.** The shell is only touchable. H: an extent-swept projectile could
     still register on the cylinder for that second.
3. **Hull gib (`TnSkeletalRigidBody`, inherits HmSkeletalRigidBody):**
   - **CollideActors, BlockZeroExtent, BlockRigidBody**; `RBChannel = RBCC_EffectPhysics`;
   - RB collide-with channels: **not Vehicle, not EffectPhysics**, others the engine default (H: world, blocking volumes and
     gameplay physics; not pawns);
   - BlockActors / BlockNonZeroExtent not set (H false). **Pawns pass through it. Hitscan and zero-extent projectiles hit it**
     (no damage handling on it);
   - it doesn't collide with other gibs or with vehicles.
4. **DSYS chunks (`HmDestructibleSpawnedChunk`):**
   - `bBlockActors false`, component `BlockActors / BlockNonZeroExtent false`, RBChannel EffectPhysics, not colliding with
     EffectPhysics;
   - **no pawn blocking.** They bounce on the world only. `CollisionLifeSpan` is a no-op.

## 4. Hull gib (`TnSkeletalGibSpawner`, at the gib)

- **Template:** the vehicle form's `DefaultSkeletalGibTemplate` (e.g. VH_Bumblebee_HULL_SKEL + HULL_PHYSSYS). It spawns at
  PositionSocket with the pawn's rotation, in the **hull's reference pose**. It is skipped if the throttle is ≥ 3 or the form has
  no template.
- **Velocity:**
  - every bone gets **0.75 × the dying vehicle's rigid-body velocity** (StarscreamBoss 0.7);
  - plus a **radial force U(300, 700)** at the bounds centre for car / tank. The jet is authored {700, 400}, which yields
    400–700.
- **Defrag material** (`TnSkeletalRigidBodyModifierSetEnergyDeathMaterial`):
  - after the delay, all elements switch to the vehicle `EnergyDeathMaterial` (TR_Defrag_MAT child) with character colours;
  - scalar **`Defrag` ramps 1 → 0 over TransitionTime**;
  - the visible sweep along U runs from 0.2·T to 0.7·T, with an edge glow (EdgeEnergyColor default (0, 0.5, 2)).

| | Delay | TransitionTime | Dissolve visible | Hull LifeSpan |
|---|---|---|---|---|
| Car / tank | U(0.5, 1.0) | 4.5 s | delay + 0.9 … delay + 3.15 s | U(3, 5): it can vanish before the sweep finishes |
| Jet | 0 | 6.0 s | 1.2 … 4.2 s | 7 |

## 5. How long each piece stays (after the gib)

| Piece | Lifetime |
|---|---|
| Pawn actor (invisible) + death light | 1 s |
| DSYS chunks (4) | 5 s. H: destruction waits until the chunk hasn't been rendered for 0.5 s |
| Hull gib | car / tank U(3, 5) s, jet 7 s. Then mesh hidden, collision off, destroyed (no DestroyedEffect authored) |
| Explosion particle | plays out naturally |
| Decal | none |

## 6. Data vs code

**Code (script):**
- the picker rule (speed > 50);
- the state flow (VehicleDying keeps PhysicalVehicleMesh collision; the gib switches to the non-blocking cylinder, then LifeSpan 1);
- velocity transfer and radial force application;
- the Defrag ramp.

**Data** (death blueprints `TR_Deaths_p.Shared*Deaths`, `*_DSYS`, the form blueprints):
- entries, TotalTime, anims, FX and sockets;
- light values;
- DSYS chunk pools and speeds;
- the hull template per form;
- the spawner multiplier, radial force, LifeSpan and Defrag delay / time;
- `EnergyDeathMaterial` per form;
- `DeathSound`.
