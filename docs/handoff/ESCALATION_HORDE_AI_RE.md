# Escalation horde AI, weak points, Jet flight and loot (E3, RE, 2026-10-23)

**Labels:** C = script bytecode / authored data (authored.db, `notes/data/escalation/ai_sets.json`); H = inference / stock.

**Tools:**
- `tools/destructible_dump.py <blueprint>` (new: the states, health, links and effects of any HmDestructibleBlueprint);
- `tools/ai_sets_export.py`.

This consolidates and **corrects** MP_ESCALATION_RE §7 / §9. The corrections are marked ⚠.

## 1. Templates in play (C, TR_AiEscalation_p + TR_SpiderDec_AI_p; spawn lists per MP_ESCALATION §2)

The 15 Escalation templates are:
- Soldier MG / Rocket / Shotgun (Dec and Aut);
- CQCSpecialist (Dec / Aut);
- Soldier_Jet (Dec);
- Soldier_Tank (Dec);
- Titan (Dec / Aut);
- Brute (Dec / Aut);
- HoloBrute (Aut, Remnant);
- plus the Spider (TR_SpiderDec_AI_p).

**There is no Sniper template in Escalation.** Sniper AI exists only in the campaign.

| Enemy | Pawn class | Action set / Goal set / Sensor set | Loot class | Evidence |
|---|---|---|---|---|
| Spider | default | Spider_AiActionSet / Spider_AiGoalSet / Spider_AiSensorSet | SpiderDec (**not in the table → no drop**) | C |
| Soldier MG / Rocket / Shotgun | default | Soldier_AiActionSet / TR_AiGoalSet_Agent / AggressiveSoldier | SoldierDec / SoldierAut | C |
| CQC Specialist | default | CQCSpecialist_AiActionSet / CQC_AiGoalSet / CQC_AiSensorSet | **SoldierDec / SoldierAut** | C |
| Soldier Jet | TnAiJetSoldierPawnContent | Soldier_Jet_AiActionSet / JetSoldier_AiGoalSet / SoldierJet_AiSensorSet | ⚠ **SoldierDec** (not GENERIC as §7.3 said) | C |
| Titan | default | Titan_AiActionSet / Titan_AiGoalSet / Titan_AiSensorSet | TitanDec / Aut | C |
| Brute | default | Brute_AiActionSet / TR_AiGoalSet_Brute / Brute_AiSensorSet | BruteDec / Aut | C |
| HoloBrute | default | HoloBrute_AiActionSet / TR_AiGoalSet_Agent / HoloBrute_AiSensorSet | HoloBrute (**not in the table → no drop**) | C |
| Boss Tank | ⚠ **TransGame.TnTankAiPawn** (the campaign uses TnTankAiPawnContent) | Tank_AiActionSet / TR_AiGoalSet_Agent / SoldierTank_AiSensorSet | SoldierTankDec | C |

**Engage, melee and retreat rules per class:**
- They're unchanged from **MP_ESCALATION §9.1–9.3** (threat evaluators, per-class actions and timers, health monitor), all C from the
  GOAP blueprints.
- In short:
  - every spawn **sees all players at spawn** (SpawnDude → SeeEnemy) and engages at once;
  - melee is a 1 s-interval action whenever adjacent (all bipeds);
  - **there is no general low-health retreat.** Only CQC retreats (out of LOS); Brute has HunkerDown and the backpack reactions.
    Everyone else fights to death.

## 2. Brute backpack: the only way to kill a Brute (C)

**Setup:**
- Health 100,000 (BruteDec / Aut chassis), so body damage is irrelevant.
- The chassis has a `TnTransformerExtensionAttachedDestructible` → **RB_BruteDec_Backpack_DES** (Aut: RB_BruteAut_Backpack_DES; the
  same layout).

**Blueprint modifiers:**
- **Melee × 0.5**;
- **AI damage × 0**: horde or bot fire can't hurt it, only players;
- **radius damage × 0.25** beyond 128 UU;
- DamageAngle 100.
- `TnDestructibleActor` also ignores damage from the pawn it's attached to.

**State chain** (health per state; each state's damage overflow does not carry, H stock):

| # | State | Health | Exit | Effects |
|---|---|---|---|---|
| 0 | intact | **25** | damage → 5 | targetable (aim point) |
| 5 | first damage | **50** | damage → 3 | BruteBackpack_InitialDamage_FX + smoke sound; targetable |
| 3 | overloading | **75** | damage → 1 | Overload FX + smoke; **DeathImminent = Level 1** → AI action **React_To_Backpack_Damage L1** (stop, play the reaction anim) |
| 1 | critical | — | **auto after 3.0 s** → 4 | sparks / beep; **DeathImminent = Level 2** → **ExtinguishBackpack** action (stop the shield, unequip, play the anim). When that action finishes, if the Brute isn't already dead: **Health = −1, Died(TnDamageTypeBruteBackpack)** |
| 4 | explode | — | pawn died → 2 | **radial damage 500, radius 4000 UU, momentum 600,000, type TnDamageTypeBruteBackpack**; **TnKillAttachedPawn** (kills the Brute); energy explosion FX; score 125 |
| 2 | gone | — | — | — |

- **Total backpack health 150** (25 + 50 + 75) of player damage.
- After the 75-HP stage breaks, **death is certain within 3 s**: the extinguish animation's end, or the explosion at 3 s, whichever
  comes first.
- The explosion's 500 radial damage hits everything within 40 m. H: subject to the normal team / friendly-fire rules of the damage type.
- TnDamageTypeBruteBackpack: ragdoll death, GibDistance 100,000.
- **Kill credit:** TnGame counts achievement 28 for a Brute killed by TnDamageTypeBruteBackpack.

## 3. Boss Tank (C; ⚠ corrects §9.2's two-phase description for Escalation)

**The fuel-cell mechanic only exists when the template starts in vehicle form:**
- `TnTankAiPawn.ApplyCharacter`: `StartingForm == TF_Vehicle` → CreateFuelCell; **else → FuelCellDestroyed() + CreateArmor()**.
- **The campaign Soldier_Tank AICDs set `StartingForm = TF_Vehicle`. The Escalation copy (`TR_AiEscalation_p.Soldier_Tank_Decepticon_AICD`)
  doesn't set it, has no archetype, and so defaults to TF_Robot.**
- **So in Escalation the tank spawns as a robot, already in the "wounded" phase:**
  - controller **invulnerability off**;
  - Reaction 3 and HealthStatus Wounded;
  - TnSeqEvent_TankStatus(1) and TnSeqEvent_HealthStatus(1);
  - **robot movement on, vehicle movement disabled**: it never transforms to the tank.
- **The fight is the robot phase only:**
  - the robot cannon (ShootAtTarget cost 25, ShootFromTactical 5);
  - the gatling **ShootingSweep** (2 s, side offset 2000, every 10–15 s, LOS);
  - TauntWithWeapon;
  - **SmashAttack ≤ 1600 UU**;
  - **6,000 HP** with normal damage.
- **Armour** (CreateArmor, from the chassis TnTransformerExtensionTank):
  - **ChestArmor1** (RB_SoldierTankDec_Front_Stage01_DSYS, socket DSYS_Front_Stage01);
  - **ChestArmor2** (Front_Stage02, DSYS_Front_Stage02);
  - **BackArmor** (Back_Stage01, DSYS_Back_Stage01).
  - Each armour actor holds **two pieces**: piece 2 depends on piece 1. Each piece has a **Healthy 100 → Intermediate 500 → broken**
    chain (scores 50 per piece; pieces fly off).
  - Armour modifier: **AI damage × 0.5**.
  - The armour adds no damage multiplier to the pawn. **H:** the armour's collision absorbs the hits that land on it, so it shields the
    chest and back until broken; the pawn's health takes everything else.
  - **Death follows normal health** (6000 → Died, the TnTankDeathMotionController death).
- **For reference (campaign, vehicle start):**
  - the tank is invulnerable (controller `Invulnerable`; the native damage gate, H on the exact check) until its **fuel cell**
    (DES_TankFuelCell, AI damage × 0.5) runs **Healthy 10 → Damaged 300 → HeavilyDamaged 300 → Destroyed**, 610 in total;
  - HeavilyDamaged fires TankStatus(0); Destroyed runs FuelCellDestroyed as above, removes the FuelCell objective marker, and then the
    armour is attached after the transform (`AttachArmorTime`);
  - while the fuel cell lives, the AI's targetable location is the fuel cell.
  - **If Integration wants the campaign-style boss in Escalation, that's a design choice, not the original.**

## 4. Soldier Jet: air spawn, flight and strafe (C)

**Spawn:**
- spawn type **Air**: zones are picked by their Air score, the same top-3 weighted pick as the other types;
- the spawn point's authored position, plus collision half-height;
- **StartingForm is the default (robot)**, so it appears as a robot at the air spawn point and sees all players at once.

**Forms:**
- **Robot:**
  - jet boosters are spawned on the robot sockets (TnSpawnableDestructibleActor, highlighted);
  - an acrobatics **jump** (state 2) → TakeOff_BlastWave_FX → state **Hovering**;
  - the hover ends when the acrobatics state leaves 2 / 3 → back to InRobotForm;
  - **falling gravity × FallingScalar** while in acrobatics state 4;
  - actions: HoverFromTactical (≤ 6 s), ShootingStrafeFromHover (4 s, every 2–3 s), robot HeavyMG-AIHover.
- **Jet (TnPlaneForm, VehicleType 2):** ShootFromAirTactical / ShootAtTarget with PlaneMachineGun; **BombingRun** (≥ Medium range,
  ShootAhead 500, every 7–10 s); **BarrelRoll** when the damage threat is ≥ Low (every 4.5–6.5 s).
- Timers: MP_ESCALATION §9.2.

**JetStrafingRun** (TnAiActionJetStrafingRun, PlaneStrafingRun weapon):
- **Only valid against a target that is itself in jet form (VehicleType 2) with LOS.** It's an air-to-air attack: against ground
  players it never runs.
- Range 5,500–15,000 UU; the jet must be in vehicle form, **in front of the target** (dot ≥ 0) and **facing it** (dot ≥ 0.866, 30°).
- **The run:**
  - move toward the target and aim;
  - fire while aimed;
  - **peel off within 4,500 UU**: choose randomly among the clear options of 8000 forward + 8000 right (yaw +1), + 8000 left (yaw −1),
    + 2000 up (pitch −1), + 2000 down (pitch +1). Each is traced with the skeleton bounds; a hit closer than 2500 UU discards the
    option, and a farther hit is clamped to the hit point;
  - stop firing once out of the target's front 30°;
  - it succeeds when the target is no longer dangerous;
  - a failed move retries unless peeling off or invalid.

**Weak point: the boosters (RB_SoldierJetDec_DES, AI damage × 0.5)**
- **Powered down** (125 HP, no damage link) while walking. **TnCustomDestructionTriggerA** on hover start → the **powered-up** state
  (125 HP).
- **Damage → Destroyed → TnKillAttachedPawn (TnDamageTypeRobotBooster)**, score 125.
- TriggerB on hover end → back to powered down.
- **So shooting a booster for 125 while the jet hovers kills it outright.** Its 250 body HP is the other way.
- Achievement 29 counts robot-form jet kills by TnDamageTypeRobotBooster.

## 5. Loot on a horde death (C, `TR_Escalation_m.Loot_Table`; MP_ESCALATION §7.3)

**Each CommonItems entry rolls its ChanceToDrop independently, then a weighted pick.**

| Enemy (classification) | Drop |
|---|---|
| Spider (SpiderDec), HoloBrute | none (not in the table) |
| Soldier MG / Rocket / Shotgun, **CQC, Jet** (SoldierDec / Aut) | **4 × TnAbilityResourcePickup, 100 % each** |
| Brute, Titan | 7 × AbilityResource, 100 % each |
| Boss Tank (SoldierTankDec) | 12 × AbilityResource + **a 50 % rare roll: GatlingGun or HeavyRocketTurret (1 : 1)** |
| Every kill, extra | +1 AbilityResource; + **5 for a headshot**, +1 for melee, +1 for an explosion kill |

- Each pickup gives **+3.0 ability resource** to a player who touches it.
- Energon (power) comes only from PowerReward, never from loot.
- GENERIC is empty. Health 100 %, Ammo 100 %, Overshield 10 % and ResourceCache 16 × are classifications for other sources, not
  horde deaths.

## 6. The two open rows, now closed (C, script; 2026-10-23)

**Armour hit absorption: yes, and there's no pass-through.**
- `TnDestructibleActor.AttachToPawn` hard-attaches each armour or weak-point actor to the pawn's socket with
  **SetCollision(CollideActors true, BlockActors false)**. So weapon traces and projectiles **hit the armour actor first**.
- Its `TakeDamage` applies the hit to the destructible only:
  - `AdjustDamage` × the damage type's `DestructibleDamageScale`, which is 1.0 for every type except TnDamageTypeOmegaRocket 0.25;
  - then the blueprint modifiers (AI × 0.5 on the tank armour).
  - **Nothing is forwarded to the pawn's health.**
  - Hits from the pawn itself or its own team are ignored (`ShouldIgnoreDamageFromPawnBase`).
- So the tank's chest and back are shielded until their pieces break. A broken piece leaves only flying physics debris, so the area is
  exposed (H: the final state has no static mesh).
- **Splash damage still reaches the pawn:** HurtRadius visits every colliding actor. The armour doesn't block the radius trace, since
  it's not world geometry (the trace uses world-only flags 1 | 32).
- The same rule covers the Brute backpack and the Jet boosters.
- **⚠ Direction gate** (`HmDestructibleActorBase.TakeDamage`): a hit is ignored when the attacker is more than `DamageAngle` from the
  destructible's facing. That's `dot(normal(attacker − destructible), destructible.Rotation) < cos(DamageAngle)`.
  - Backpack: DamageAngle 100° → cos −0.17, so **shots from the Brute's front ~160° arc can't damage the backpack**.
  - Blueprints without DamageAngle take hits from any direction.

**Backpack explosion friendly fire:**
- `HmRadialDamageDestructionEffect` → `HurtRadius(500, 4000, BruteBackpack, 600000, origin, none, **InstigatedBy = the backpack's
  LastHitInstigator**, bDoFullDamage false)`.
- That instigator is the controller of the last hit, normally the player who broke it (AI damage × 0 to the backpack, but an AI hit
  still sets LastHitInstigator).
- **Falloff:** `scale = (1 − clamp(dist − victimRadius, 0, R) / R)²`. Victims need an unobstructed world trace from the origin; there's
  no height limit.
- **Who is hurt:**
  - **horde AI** (the enemy team of the instigator): full falloff damage;
  - **the instigating player himself: yes.** Self-damage isn't filtered for players;
  - **other players: no.** `TnPlayerPawn.TakeDamage` drops any same-team damage that isn't TnDamageTypeAOE, and BruteBackpack derives
    from TnDamageType, not AOE. They get NotifyHitByFriendlyFire only;
  - if the last hitter was a horde AI: the horde is immune (same team), and players take damage.
- The Brute itself is killed by TnKillAttachedPawn regardless.

## Open
- None of substance. Remaining H: the exact broken-armour collision.


**⚠ Correction (2026-10-23, ESCALATION_WAVES_RE):** loot on a horde death uses the **spawn-list entry's** LootClassification
(`TnSurvivalGame.GetLootClassificationFor` reads `SpawnList.Enemies[EnemyTypeIndex].LootClassification`), **not the AICD's**. So:
- **Jet → GENERIC → no class drop** (§1 / §5's "SoldierDec" was wrong; the old MP_ESCALATION §7.3 was right).
- **HoloBrute (Remnant) → SoldierAut → 4 pickups** (not "nothing").
- Spider GENERIC → nothing.
- BrokenHope Titan waves 9–10 = TitanDec, 10+ = BruteDec (both 7).
- Remnant Titan 7–9 = BruteAut, 11+ = TitanAut (both 7).
- Every kill still adds +1, plus headshot +5 / melee +1 / explosion +1.
