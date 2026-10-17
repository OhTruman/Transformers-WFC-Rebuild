# Death-type picker and death light

WFC RE lane, 2026-10-17. Integration copies this to `docs/handoff/GAMEPLAY_DEATH_PICKER_RE.md`.

Sources:
- script decompiles in `work/script/decomp`: TnPawn (Dying state), TnRobotForm / TnVehicleForm (OnPlayDeath), TnDeathPlayer,
  TnDeathFactory, TnDeathPicker, TnRobotDeathPicker, TnVehicleDeathPicker, TnDeathModifier*, TnRampLightManager,
  TnAnimNotify_TriggerDeathLight, TnDamageType;
- authored.db (TR_Deaths_p) and inventory.db.

No native code was needed. Earlier detail is in `notes/TARGETED_PASS5_2026-10-05.md` §12, addenda 18–25; this doc is the
self-contained summary.

## 1. Selection

### 1.1 Order of events at death (CONFIRMED)

1. `TnPawn.Dying.BeginState` aborts melee and interaction and stops the animator, then calls `_CurrentForm.PlayDeath()`.
   - **TnRobotForm.OnPlayDeath** → `_DeathPlayer.Activate()`.
   - **TnVehicleForm.OnPlayDeath** → `_DeathPlayer.Activate()` + `PlaySound(_Blueprint.DeathSound)`.

   The death player, and so the blueprint, belongs to the current form: robot form uses the robot blueprint (SharedRobotDeaths
   etc.), vehicle form uses the vehicle one (SharedCar / Tank / Jet / Vehicle).
2. `TnDeathPlayer.Activate`:
   1. `PickDeath` → `TnDeathFactory.Create`;
   2. plays the entry's Animation through the death motion controller;
   3. **`Modifier.Start` for every modifier of the entry** (each one honours its own `Delay`);
   4. `TimeUntilGibs = RandRange(TotalTime)`. TotalTimeForAI is used for non-player pawns only when it is non-zero, and it is 0 in
      every entry, so AI use TotalTime too.
   5. Starts the gib timer, which calls `Owner.Gib`.
3. **Insta-gib (still inside Dying.BeginState, after PlayDeath):** if `ShouldInstaGib(LastHitBy, LastTakeHitInfo.DamageType)` is
   true, `Gib()` runs immediately. The death entry was still picked, and its modifiers (including the light) have already started.
   - ShouldInstaGib is true when any of these holds:
     - `bKilledWhenDowned`;
     - a Matinee animation is playing;
     - the damage type is KillZ.
   - Otherwise it is false in god mode or when `!Blueprint.CanInstaGib`. 59 blueprints author CanInstaGib = true.
   - Otherwise it returns `DamageType.ShouldGib`: true when the instigator pawn is not the victim and
     `VSize(instigator − victim) <= GibDistance`.
   - GibDistance is authored only on **EmpShotgun 1000** and **BruteBackpack 100000**; others take the class default, which is
     unauthored (0, so effectively never).
   - BruteBackpack and DeathAnim override ShouldGib to always return true.
   - The later gib-timer Gib is a no-op, because `Dying.Gib` returns when LifeSpan > 0.

### 1.2 Picking an entry (CONFIRMED)

- `TnDeathFactory.Create` starts from the default death: TotalTime 1.0, no animation, the blueprint's default gibs, skeletal gibs
  and decal.
- It then takes `Index = ForceDeathTypes.Length > 0 ? DebugPicker.Pick : Blueprint.Picker.Pick`. ForceDeathTypes is never authored.
- If the index is not −1, it copies the entry's Animation, Modifiers, TotalTime and TotalTimeForAI, plus custom gibs, skeletal gibs
  and decal when the matching `UseCustom*` flag is set.
- **`TnDeathPicker.Pick`:** `FindActiveTypes(Owner, ActiveTypes)`, then the **first entry, in authored order, whose every `Types`
  element is in ActiveTypes**. There is no scoring and no randomness in the pick. No match → −1 → the default death.

### 1.3 Active types (CONFIRMED)

**`TnRobotDeathPicker.FindActiveTypes`** (a set, so the order of adds is irrelevant):

| Type | Active when |
|---|---|
| Martyrdom | the pawn has TnBuffExplodeOnDeath or TnBuffExplodeOnDeathKamikaze |
| HeadShot | `LastTakeHitInfo.BoneName == HeadShotBoneName` (`C_Spine04_Head_XB`) |
| Hovering | `RobotForm.IsHovering` |
| Airborne | `IsAirborne` |
| one momentum type | `yaw = Rotator(TearOffMomentum << Owner.Rotation).Yaw`. ±8192 (±45°) → Forward; −24576..−8192 → Left; 8192..24576 → Right; else Backward. Zero momentum → Forward. |
| the killing damage type's `DamageDeathType` | read as `static.default`, inherited through the class tree (addendum 20) |

**`TnVehicleDeathPicker.FindActiveTypes`:**
- Car / Truck: `Car` + (`MovingCar` if `TnCarForm.LinearSpeed > 50`, else `StationaryCar`).
- Tank: `Tank` + Moving / Stationary by the same rule.
- Plane: `Plane`.

**Effective DamageDeathType** (addendum 20):

| Death type | Damage types |
|---|---|
| RapidShot | AssaultRifle, BurstRifle, CarMachineGun, Environment(NoDowned), GatlingGun, (Heavy / Mounted) IonBlaster, RepairEnemy, SentryGun |
| SingleShot | EmpShotgun, HeavyMG, HeavyPistol, Shotgun, SniperRifle |
| Explosive | all rockets / grenades / mines / shells / OmegaBeam / PlasmaCannon / EnvironmentExplosion (17) |
| Melee | Melee, MeleeBerzerk, Rammed, ShoulderSlam, WeakMelee, Whirlwind |
| Poke, RobotBooster, FlashBang | their own classes |
| none | 45 classes, incl. StandardPistol, BurstPistol, Bazooka, GuidedMissile, TruckRocket, NullRay, EnergyBlast, Energon, ShieldPush, Shockwave |

No damage type resolves to Energy or TractorBeam.

### 1.4 SharedRobotDeaths entry order (HoloBruteRobotDeaths is the same) (CONFIRMED)

| # | Types | Anim | TotalTime | Modifiers | Custom gibs |
|---|---|---|---|---|---|
| 0 | RobotBooster | — | 1.25–1.5 | local force on L_Leg03_Ankle_XB + world force | — |
| 1 | Poke | — | 1.5 | **FX pair + LIGHT** | gibs PokeGibs_DSYS, **no skeletal gibs, no decal** |
| 2 | Hovering | — | 1.5–2.5 | **FX pair + LIGHT** + ApplyForce impulse | — |
| 3 | Airborne | InAir | 1.25–2.5 | **FX pair + LIGHT** | — |
| 4 | HeadShot | Death_HeadShot | 1.25–2.0 | ControlBone: head scale 0.1 | — |
| 5 | Martyrdom | Death_Martyrdom | 1.25 | — | gibs MartyrdomGibs_DSYS |
| 6 | TractorBeam (unreachable) | — | 1.25 | — | gibs TractorBeamGibs_DSYS |
| 7 | Energy (unreachable) | EnergyRanged | 1.5–3.5 | SetMaterial (Defrag 3 s) + TF_Death_Energy_Med_FX | no gibs, no skeletal gibs, no decal |
| 8 | SingleShot | ProjectileSingle | 1.25–2.5 | **FX pair + LIGHT** + Exp_Death_Sm_FX at Chest_FX | — |
| 9 | RapidShot | ProjectileMultiple | 1.25–2.5 | **FX pair + LIGHT** | — |
| 10 | Melee | — | 1.5–2.75 | SetMaterial (Defrag 3 s) + PlaySound hud_melee_death_disintegrate | **no gibs, no skeletal gibs, no decal** |
| 11 | Explosive | Explosion | 1.25–2.0 | **FX pair + LIGHT** | — |
| 12 | ForwardMomentum | Death_BackShot | 1.25–2.0 | — | — |
| 13 | BackwardMomentum | Death_FrontShot | 1.25–3.0 | — | — |
| — | no match (sideways momentum, e.g. FlashBang or a "none" damage type pushed sideways) | none | 1.0 | — | default |

- "FX pair" = `TnDeathModifierPlayEffect_3123`: Exp_Death_Sm_FX at **Head_FX** and at **BoosterSocket_C**, after Delay U(0, 0.5).
- "LIGHT" = the single shared object `TnDeathModifierTriggerLight_9386` (§2).
- **Random elements:** only TotalTime (RandRange), the FX delay, the ApplyForce impulse, and the vehicle PET_Random effect choice.
  The pick itself is deterministic.

**Other robot blueprints:**
- **BruteRobotDeaths:** TnCustomDeathPicker, always Custom → Titan_Death, TotalTime 2–4, no modifiers, so no light.
- **SpiderDeaths and SecurityDroneDeaths:** they also use `TriggerLight_9386` with the same values; their entry tables were not
  re-listed here.

## 2. Death light

### 2.1 Which deaths light up (CONFIRMED)

| Robot (SharedRobot / HoloBrute) | Light |
|---|---|
| Poke, Hovering, Airborne, SingleShot, RapidShot, Explosive | **yes** |
| RobotBooster, HeadShot, Martyrdom, Melee, Forward / Backward momentum, default death | **no** |
| Energy, TractorBeam | no; unreachable anyway |

**Corrections to the package 5 approximation:**
1. Hovering and Airborne deaths light up **whatever the damage** (they are matched before any damage type). So does Poke.
2. **Head shots are dark even from guns**: entry [4] is matched before SingleShot / RapidShot.
3. Gun kills whose damage type has **no** DamageDeathType are **dark**: StandardPistol, BurstPistol, NullRay, Bazooka, GuidedMissile,
   TruckRocket and others go to momentum or the default death.
4. There is no reachable "energy" death.

**Vehicles:**
- **SharedCarDeaths and SharedTankDeaths:** every entry has `TriggerLight_5223`.
- **SharedJetDeaths:** no light.
- **SharedVehicleDeaths** (Laserbeak): car / tank entries only (addendum 21).

### 2.2 Authored values (CONFIRMED)

The CDO defaults (`TnRampLightProperties`) fill any unauthored field.

| | Robot `TriggerLight_9386` | Car / Tank `TriggerLight_5223` | CDO default |
|---|---|---|---|
| Brightness | **10** | 25 | 25 |
| Radius | **2048** | 512 | 512 |
| FalloffExponent | 10 | 10 | 10 |
| LightColor (R, G, B) | **(230, 230, 255)** | **(202, 123, 213)** | (255, 255, 255) |
| RampInTime | **2.0** | **0.2** | 0.5 |
| RampOutTime | 0 | 0 | 0 |
| LightTime | **2.0** | **8.0** | 10 |
| Translation | (0, 0, 0) | (0, 0, 0) | (0, 0, 0) |
| Delay | none (starts at death) | none | — |

- The robot colour has A = 1 authored. SetLightProperties takes a Color, and the alpha is not used for lighting (H).

### 2.3 Curve: `TnRampLightManager.UpdateLight(dt)`, called from `TnPawn.Dying.Tick` (CONFIRMED)

```
CurrentTime += dt                          (TriggerLight sets CurrentTime = 0)
RampOutStart = LightTime - RampOutTime     (= LightTime here)
if CurrentTime > LightTime:  SetEnabled(false); CurrentTime = -1; stop
elif CurrentTime >= RampOutStart:  B = (1 - (t - RampOutStart)/RampOutTime) * Brightness
                                   (RampOutTime 0 → 0/0 only at exactly t == LightTime; one-frame edge)
else:  B = min(t / RampInTime, 1) * Brightness
SetLightProperties(B, LightColor)
```

- **Robot:** brightness rises linearly from 0 to 10 over 2 s, then the light **cuts off abruptly** at t > 2 s. It never holds at full.
- **Car / tank:** the light reaches 25 at 0.2 s and would hold until 8 s, but the actor is destroyed first (§3).

## 3. What the light is, and what happens at gib time

### 3.1 It is a PointLightComponent on the pawn (CONFIRMED)

- The light is **`DeathLight0`**, a **PointLightComponent in the pawn's own Components** (TnPawn CDO and every pawn subclass).
- Component defaults:
  - `bEnabled false`;
  - `CastShadows / CastStaticShadows / CastDynamicShadows false` (no shadows);
  - `bForceDynamicLight true`;
  - RadiusOfInfluence 1013.76 and LightColor (250, 220, 120). Both are overwritten: TriggerLight sets Radius, FalloffExponent and
    Translation and enables the light; each tick sets brightness and colour.
- Translation is 0, so the light sits at the **pawn actor origin** (collision cylinder centre). It is not attached to a bone or
  socket.
- `TnPawn` wires it at blueprint init: `_DeathLightManager.Initialize(DeathLight)` and service `'TnDeathLightService'`.
  `TnDeathModifierTriggerLight.OnStart` calls `GetService('TnDeathLightService').TriggerLight(LightProperties)`.
- **It is not Kismet and not a particle light.**
- `TnAnimNotify_TriggerDeathLight` (an anim-notify path to the same service) exists as a class, but has **zero authored instances**
  in inventory.db. It is unused.
- Like the rest of the death, it runs on every machine.

### 3.2 At gib time (CONFIRMED from script; timings derived)

- `Dying.Gib` hides the meshes, sets **LifeSpan 1** and runs the gibs, but does **not** touch the light.
- `Dying.Tick` keeps calling UpdateLight, so the light carries on until LightTime expires or the pawn actor is destroyed, 1 s after
  the gib.
- **Robot lit entries:** the gib comes at ≥ 1.25 s, so destruction is at ≥ 2.25 s. The 2 s ramp therefore always completes and cuts
  off at 2 s. It is still ramping (5–8 at the gib) while the gib explosion plays.
- **Insta-gib (any entry):** destruction at 1.0 s, so a lit entry's light is cut at about half brightness (5).
- **Car / tank:** gib at 0.2–0.75 s, destruction at 1.2–1.75 s, so the light is full for about 1–1.5 s and then disappears with the
  actor. The 8 s LightTime is never reached.
