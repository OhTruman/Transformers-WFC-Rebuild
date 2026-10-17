# Hover / ride-height controllers: robots in hover, hover vehicles

WFC RE lane, 2026-10-17. Integration copies this to `docs/handoff/GAMEPLAY_HOVER_RE.md`.

Sources:
- script decompiles (`work/script/decomp`): TnSuspension, TnSpring, TnHoverCarSimulation, TnHoverTankSimulation,
  TnHoverPlaneSimulation, HoverPhysics, TnCarForm, TnAcrobaticsManager, TnVehicle*AiControlStrategy;
- authored.db.

Earlier detail: `notes/MILESTONE03_VEHICLE_NATIVE_FIDELITY.md` P1–P2 and `notes/TARGETED_PASS4_2026-10-05.md` A3/A4 (+ addenda).

**There is no native ride-height controller.** Everything is UnrealScript on a PhysX rigid body. Native code supplies only the
trace, `GetGravityZ` and the integrator. No Ghidra was needed.

**Units:** UU (1 UU = 1 cm in the rebuild's convention). Rigid-body gravity is −2940 × 0.66 = **−1940.4 UU/s²**.

## 0. Three different "hover" systems

| Who | Class | Ground probe? |
|---|---|---|
| Hover car / hover truck (car-form Hovering state, all car and truck chassis) | `TnHoverCarSimulation` | **yes:** 4 spring probes |
| Hover tank (tank form) | `TnHoverTankSimulation` | **yes:** 4 spring probes (slightly different force rule) |
| Jet vehicle hover (plane form, hover mode) | `TnHoverPlaneSimulation` | **no:** anti-gravity, free altitude |
| **Robot-form hover** (the Hover ability) | `TnAcrobaticsManager` JumpingToHover → Hovering | **no:** PHYS_Flying at the altitude reached; no trace, no spring |

**There is no "target ride height" variable anywhere.** For the probe vehicles, height is the spring equilibrium (§1.4).

## 1. Hover car / truck / tank: authored values (CONFIRMED)

### 1.1 Which chassis use which blueprint

| Hover blueprint (VEH_SHARED_p) | Used by | SuspensionRadius | Suspension | Stiffness | Damping | RestingLength (= probe length) |
|---|---|---|---|---|---|---|
| HoverCar_Physics | 7 car forms (Car_Physics: Mass 1500, COM (12, 0, −16)) | **100** | HoverCar_Supension | **8000** | **4000** | **200** |
| HoverTruck_Physics | 14 car forms + 6 truck forms on Truck_Physics (Mass 2500, COM (−47, 0, 20)), incl. Optimus | **185** | HoverTruck_Suspension | **10000** (class default) | **4000** | **250** |
| HoverTank_Physics | 5 tank forms | **250** | HoverTank_Suspension | **20000** | **6000** | **250** |
| SoldierTank{Dec,Aut}HoverTankSimulation | AI soldier tanks | **1200** | …HoverSuspension | **400000** | **50000** | **400** |

### 1.2 Other blueprint values

| | HoverCar | HoverTruck | HoverTank | Class default (car / tank) |
|---|---|---|---|---|
| MaxLinearSpeed | 1500 (default) | 1500 | 1500 | 1500 |
| MaxLinearAcceleration | 4000 | 3000 | 2500 | 3000 |
| JumpLinearSpeed / JumpAngularSpeed | 1200 / 1.0 | 1200 / 1.0 | 1200 / 1.0 | — |
| Dash | 0.5 s / 3000 | 0.5 s / 3000 | — (MaxBoostSpeed 2500) | — |
| AI linear speed / accel | 1500 / 3000 | 3000 / 4000 | 1500 / 4000 | — |
| AI angular speed / accel (rad/s, rad/s²) | 12 / 12 | 20 / 20 | 6 / 6 | 3.142 |

Soldier tanks: AI speed 400 / accel 500, angular 0.5 / 1.0.

**Class constants:**
- TerminalVelocity 3500;
- StabilityThreshold 30° (× modifier);
- CosGroundAngle 0.707 (× modifier);
- DefaultAngularDamping 0.1, DefaultLinearDamping 0.1, StopLinearDamping 10 (AI path only);
- IsUpsideDown when body-up.Z < 0.01.

### 1.3 No lift clamp and no tilt limit

- **Max lift force:** none. The per-probe force is only clamped at ≥ 0, so it never pulls down.
- **Tilt limits:** none as hard limits. The 30° StabilityThreshold only gates corrections (§3) and Dash.
- **Trace / probe length** = `RestingLength + WheelRadius`, and WheelRadius = 0 for hover. So the probe is exactly RestingLength
  long: car 200, truck 250, tank 250.

### 1.4 Resulting ride height (CONFIRMED formula; mass link HIGH)

- Equilibrium per probe: `K(L0 − L) + g = 0`, with K = Stiffness / (M/4). So **L_eq = L0 − 1940.4 · (M/4) / Stiffness**, measured
  from the probe points. The probe points are in the plane of the centre of mass.

| Chassis | Sag | L_eq (COM above ground) | Actor origin above ground | ζ (B / 2√K) |
|---|---|---|---|---|
| Hover car (M 1500) | 91.0 | **109.0 UU** | COM z −16 → **≈ 125 UU** | 1.16 (overdamped) |
| Hover truck / Optimus (M 2500) | 121.3 | **128.7 UU** | COM z +20 → **≈ 109 UU** | 0.80 |
| Hover tank | — | 250 − 1940.4·(M/4)/20000 | — | — |

- The tank's mass comes from its rigid body / PHYSSYS, which is not authored on the hover blueprint: **PARTIAL**.
- The mass link: TnCarForm initializes CarSimulation (which sets Mass / COM / inertia from Car_ or Truck_Physics) before the hover
  sim reads `RigidBody.get_Mass()`.
- **Hover ride height is ~1.1–1.3 m. That is unrelated to the Driving 0.63 / 0.54 m wheel contact in GAMEPLAY_RIDE_HEIGHT_DESIGN,
  which applies to Driving only.**

## 2. Per-tick update (CONFIRMED)

### 2.1 Entry points

- **Player:** `TnPawn.Tick` → form `OnUpdate` → `TnCarForm.Hovering.DoUpdate(_PhysicsDeltaTime)` → `TnHoverCarSimulation.Update`.
  - Once per game tick (30 Hz on console).
  - dt = `WorldInfo.PredictNextPhysicsUpdateTime` = n × 1/60, normally 2/60.
  - It queues forces and velocity changes, which PhysX applies in the following simulate, over its 1/60 substeps (H for the substep
    semantics).
- **AI:** `Tn*AiControlStrategy` → `CarForm.UpdateAi` → `Hovering.DoUpdateAi` → `UpdateFromLocationAndRotation`. Same tick.

### 2.2 Suspension (`UpdateSuspension`, identical for player and AI)

```
dir = RB.RotateToWorldSpace((0,0,-1))                 // body-down: the rays tilt with the hull
for i in 0..3:                                       // 4 probes on the DIAGONALS
  P = RB.TransformToWorldSpace(RB.LocalCOM + Normal(1,1,0)*SuspensionRadius rotated by i*90° yaw)
  TnSuspension.Update(dt, P, dir):
    MultiTrace(P -> P + dir*(RestingLength + 0))       // one zero-extent ray per probe; all hits, first ACCEPTED one used
    accept hit if (HitComponent.BlockRigidBody && HitComponent.RBChannel != 6 /*RBCC_EffectPhysics*/) || HitActor is WorldInfo
    contact: len = max(0, |hit - P|); else len = RestingLength, HasContact = false
    TnSpring.Update(dt, len, g = -dir.Z * GravityZ):   // implicit spring-damper incl. gravity compensation
      vel = (len - Length_prev)/dt (0 on the first tick after Reset)
      v'  = (vel + dt*(k*(L0-len) + g)) / (1 + b*dt + k*dt²)  with k = Stiffness/m, b = Damping/m, m = M/4
      F   = m*((v' - vel)/dt - g)
  if contact:
    car / truck: RB.ApplyForce(-dir * max(0, (-dir·N) * F), P)     // scaled by how square-on the surface is
    tank:        RB.ApplyForce(-dir * max(0, F), P)                // no normal factor
ContactNormal = mean of contacting normals; NumContacts = count (car) / OnTheGround = any contact (tank)
IsOnTheGround (car) = ContactNormal.Z > 0.707
```

- The four probes sit at ±(R/√2) on each axis: car ±70.7, truck ±130.8, tank ±176.8.
- `RBChannel != 6` excludes `RBCC_EffectPhysics`, so gibs and debris never hold a hover vehicle up. Pawns and vehicles with
  BlockRigidBody do count.
- **Rigid-body damping:** Activate sets angular and linear damping to 0 for the car (angular only for the tank). Spring damping is
  the only vertical, pitch and roll damping on the player path.

### 2.3 Player path order

1. UpdateSuspension
2. UpdateTerminalVelocity: if vZ < −3500, add Δv so that vZ = −3500
3. Dash or Strafe: horizontal, scaled by up.Z²
4. UpdateRoll: local-X accel = strafe input − local ωz
5. **UpdateTurn**

**UpdateTurn:** `ω_set = axisAngle(q⁻¹·yawTarget) · TurnRate / dt − ω_local`, with TurnRate (0.05, 0.05, 1), applied as a
local-velocity change masked as:
- **car / truck:** all axes if `NumContacts == 0 || upsideDown`, else **yaw only**;
- **tank:** yaw only if `IsStable (|pitch|, |roll| < 30°) && OnTheGround`, else all axes.

So pitch and roll are corrected at 5 % of the error per tick (per 1/30 s) only when airborne or flipped (car), or when unstable or
airborne (tank). Otherwise pitch and roll come from the springs alone.

**SimulationMode 0** (sim off): the player path only cancels gravity; the AI path runs the suspension, then returns.

### 2.4 AI path (`UpdateFromLocationAndRotation`)

1. UpdateSuspension (same as above).
2. Rigid-body angular damping = 0.1. Linear damping = 10 within 10 UU of the target (VSizeSq < 100), else 0.1.
3. `HoverPhysics.MoveToTargetLocation(maxSpeed = AiMaxLinearSpeed, maxAccel = AiMaxLinearAcceleration, mask (1, 1, 0))`:
   - desired v = clamp(Δ/dt, min(√(a·d), vmax));
   - Δv clamped by a·dt;
   - **horizontal only**, so height is the springs' job.
4. `HoverPhysics.RotateToTargetOrientation(target = DesiredRotation, AiMaxAngularSpeed, AiMaxAngularAcceleration, mask)`.
   It is the same law in local angular space.
   - **Car / truck:** mask = `IsUnstable ? (1, 1, 1) : (0, 0, 1)`.
   - **Tank:** mask = `IsStable ? (0, 0, 1) : (1, 1, 1)`.
   - The AI's DesiredRotation is built yaw-only from the focus or path point (`Pitch = 0`; Roll is 0 from `rotator(vector)`), so
     the full-axis case **rights the bot toward level**.
   - Exception: the avoidance "stop" case (`_IsAvoiding == 2`) uses `Pawn.Rotation`, with Pitch then zeroed when not boosting.
     That case can keep the pawn's roll (H).
   - The correction is rate-limited by AiMaxAngularSpeed / Accel (car 12 / 12, truck 20 / 20, tank 6 / 6).

## 3. Self-righting, ramps and ledges (CONFIRMED script; H where noted)

- **The AI self-righting gate is attitude-relative, not contact-relative.** The car / truck AI rights itself only while |pitch| or
  |roll| > 30°, and it does this **even with probe contact**. Below 30° it corrects yaw only, whether or not it is grounded.
- The player car's gate *is* contact-relative: all axes only when NumContacts == 0 or flipped.
- **Correction for package 5:** if our package 5 gate made bot righting depend on contacts, that matches the *player* rule, not the
  AI rule. The faithful bot rule is:
  - righting on when unstable (> 30°), off when stable, ignoring contacts;
  - rate-limited, not a 5 %-per-tick pull;
  - with 0.1 angular damping.
- The tank uses the stable-gate for the AI and stable-and-grounded for the player.
- **Ramps:** the probes tilt with the body (body-down rays), so on a ramp they measure along the hull axis. The car's force is scaled
  by (−dir·N), so a steep face pushes less. There is no ramp-specific or launch logic. Leaving a ramp lip just loses probe contact,
  and the body becomes ballistic, with the terminal-velocity clamp and (player) the airborne 5 % upright pull.
- **Ledges:** a probe past the edge gets no hit, so that corner gives zero force. The hull pitches or rolls over the edge until the
  remaining springs and contact rebalance it.
  - The player gets no pitch/roll correction while any probe still has contact.
  - The AI gets correction only past 30°.
  - There is no edge detection and no probe-length clamp; the probe is always RestingLength long.
- H: the hull's own PhysX collision is the only other support, e.g. a bottomed-out hull on a lip.

## 4. Robot-form hover (`TnAcrobaticsManager`; CONFIRMED)

- `TnAbilityHover` → `PC.Hover` → `AcrobaticsManager.Hover` → **JumpingToHover**:
  - Physics = PHYS_Falling;
  - Velocity = CalculateJumpVelocity(**HoverJumpHeight**);
  - loop sound and MakeNoise(HoverNoiseRadius).
- When descending or on the ground, it enters **Hovering**:
  - **Physics = PHYS_Flying** (no gravity, so the altitude reached is held);
  - MaxAirSpeed = **HoverAirSpeed**, restored on exit;
  - a timer of **HoverDuration** (× TnBuffIncreaseHoverDuration); the hover-damage buff on the authority.
- Expiry or Jump → Falling. **No ground trace, no spring, no ride height.**

| TnAcrobaticsManagerBlueprint (TR_Acrobatics_p) | HoverJumpHeight | HoverDuration | HoverAirSpeed |
|---|---|---|---|
| SharedAcrobatics | 500 | 7 | 500 |
| StarscreamBossAcrobatics | 750 | 7 | 500 |
| JetSoldierAcrobatics / TankSoldierAcrobatics | 1000 | 10 | 200 |
| HeavySmallAcrobatics | 300 (default) | 10 | 200 |
| class default | 300 | 2 | 300 |

All authored ones have HoverNoiseRadius 3500.

## 5. Jet vehicle hover (`TnHoverPlaneSimulation`)

- Every tick it cancels gravity (`ApplyLinearAcceleration(−Gravity)`).
- **No probes.**
- Player: strafe, dash, roll and turn with MotionPhysics lean.
- AI: `MoveToTargetLocation` on **all three axes** plus `RotateToTargetOrientation` on all axes, with AiMotionPhysicsScale 2.
- Height is whatever the controller targets.
- HoverPlane_Physics: MaxLinearAccel 2500, Dash 0.1 s / 1000. AI_HoverPlane_Physics: 2000, roll 0.7 s / 2000.
