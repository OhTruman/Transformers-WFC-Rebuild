# Campaign-only gameplay content vs MP: items, destructibles, interactives, player mechanics

WFC RE lane, 2026-10-18.

Sources:
- script: TnPawn / TnPlayerPawn / TnPlayerController, TransContent TnCampaignGame, Hm destructible classes, TnDestructibleActor,
  TnMountableTurretBase, TnCollectableAchievementItem, TnPickupRewardManager / TnLootTable, TnBuff*;
- data: authored.db (placed actors per map, CDOs, interact sequences, loot tables), Coalesced ini (MP data providers).

New data exports:
- `notes/data/campaign_player_characters.json`: the 30 SP iconic loadouts;
- `notes/data/campaign_loot_tables.json`: the 11 loot tables.

Everything is CONFIRMED script / data unless marked.

## 1. Items: what campaign has that MP doesn't

### 1.1 Player loadouts: no new player weapons or abilities

- Campaign players pick from **`TnProfileSettings.PresetIconicCharacters`**: 30 `TR_SPPlayerCharacterData_p.*_PCD_SP` (Optimus, Optimus
  Prime, Bumblebee, … Zeta Prime, plus Autobot / Decepticon / Neutral CarSoldier).
- MP uses `TR_MPPlayerCharacterData_p` class presets (Scout / Soldier / Scientist / Leader) plus custom characters.
- **Every WeaponTypes / VehicleWeapons / Abilities / Skills id in the 30 SP loadouts is also an MP data-provider id**
  (TransWeapons / TransCustomization.ini). Campaign adds no player-usable weapon, ability or skill class; only the combinations
  (iconic loadouts) are campaign-only.
- **H:** MP's iconic mode (`GRI._bIconicMode`, `FindIconicCharacter` + `CanUsePresetCharacter`) reads the same list.

### 1.2 Placed pickups

| Factory | Campaign | MP | Delta |
|---|---|---|---|
| TnWeaponPickupFactory | 206 | 33 | **campaign-only InventoryType: TnWeaponAssaultRifle**; all others are also in MP |
| TnPickupFactory | 34 | 1 | campaign items: FlashBangs, FlakGrenades, GrenadeLauncher, PlasmaCannon, AssaultRifle, HeavyPistol, **TnAmmoCratePickup** (7), **HM_Engine.HmProjectileBag** (an engine base class, H: misauthored or unused) |
| TnHealthPickupFactory | 49 | 67 | same class; campaign RespawnTime is per placement (30–180 s), MP is mostly 60 |
| TnOverShieldPickupFactory | 7 | 14 | same class; campaign keeps the class default RespawnTime, MP sets 120 |
| TnAmmoCratePickupFactory | 81 | 81 | same class; RespawnTime 5–60 in campaign, 30 in MP |

**Pickup values** (identical in both modes):
- TnHealthPickup AddedHealth 50;
- TnAbilityResourcePickup (energon shard) AddedResource 3;
- TnAmmoCratePickup refills WT_Primary / Secondary / Vehicle.

### 1.3 Campaign-only loot drops (`TnPickupRewardManager` + `TnLootTable`)

**This system exists only through the campaign game.**
- `TnCampaignGame.DiscardInventory`: a pawn with a loot classification **spawns rewards instead of dropping its inventory**.
- `ShouldDiscardInventoryWhileDying` forces this.

**Classifications per AI death** (`GetLootClassificationFor`):
1. the AICD's `LootClassification`: SoldierAut / Dec / Neu, BruteAut / Dec / Neu, TitanAut / Dec / Neu, SoldierTankAut / Dec,
   SpiderDec, HoloBrute; 39 AICDs have none;
2. always `AbilityResource`;
3. `HeadShotAbilityResource` if the last hit was a headshot;
4. `MeleeAbilityResource` if the death damage type's trigger is Melee;
5. `ExplosionAbilityResource` if it is Explosion.

**Table lookup** (`GetLoot(class, modifiers, difficulty)`):
- For each sub-table in CommonItems, then RareItems, where `GameDifficulty` is −1 or the current one, `RandomFloat(100) <=
  ChanceToDrop + bonus` → one weighted pick.
- Bonus = `GetLootModifiers()`, zero in shipped script.
- Each drop: `Spawn`, `DropFrom(location, RandomVectorInCone(up, 60°) × toss speed)`. Toss speed is `DefaultTossSpeed 1000` unless
  overridden.

**Kismet:**
- `TnSeqAct_SetLootTable` picks the table per chapter (`Campaign_LootTable_A1…A5, D1…D5`, `CrateLootTable`).
- `TnSeqAct_SpawnPickupRewards(LootClassificationToSpawn)`: loot crates (`DES_LootCrates_p` prefabs: Grenades, Health,
  LightWeapons, ResourceCache, Overshield, Ammo).
- `TnSeqAct_LootPickupFactoryModifier` (TnLootClassificationPickupFactory: set classification / RespawnTime).

**Table A1, for example** (all chances 100 unless noted; one line = one roll):
- Soldier: 4 shards.
- Brute: 7 shards, plus 50% ammo or health.
- Titan (Dec): 7 shards + a Gatling gun; Titan (Aut): a Gatling gun.
- Spider: 1 shard.
- Base AbilityResource: 1; Headshot: +5; Melee: +2; Explosion: +1.
- ResourceCache: 20 shards.
- Crates: LightWeapons = AssaultRifle 1 / Shotgun 1 / HeavyPistol 0.5; HeavyWeapons = RocketLauncher / PlasmaCannon; Grenades =
  FlakGrenades.

The full tables are in `campaign_loot_tables.json`.

### 1.4 AI-only weapons (never player-spawned in MP)

Cited from `notes/data/ai_definitions.json` / `ai_weapon_data.json` (38 AI WEPDATA):
- mounted turret weapons: MountedHeavyIonBlaster, MountedRocketTurret, MountedRepairRay;
- boss weapons:
  - Omega: TractorBeamArm, RocketArm, ShootingFrenzyArm;
  - Starscream: StarscreamRocketVehicle, Starscream EMPShotgun / HoverRockets;
  - BossRepairRay, AudioBlast, CeremonialStaffHolo;
- HalberdAxe;
- soldier-tank cannons and Gatlings: TankRobotCannonHeavyDecepticon, TankRobotGatlingGunDecepticon, TankCannonHeavy;
- plane strafing / bombing runs: PlaneStrafingRun, BombingRunVehicle, BombingRunLaser, LaserVehicle;
- HeavyMGAIHover, BruteRocketLauncher, the Minion EMPShotgun / HeavyPistol.

**The player gets two of these only through turrets** (§2.3): MountedHeavyIonBlaster / MountedRocketTurret while mounted. Ripping
the turret gives the MP-pool **GatlingGun / HeavyRocketTurret**.

## 2. Destructibles and interactives

### 2.1 Destructible damage pipeline (Hm script; the same code in MP)

**Classes:**
- campaign-only placements: **TnDynamicDestructibleActor** (554; movable, bNoDelete);
- **TnStaticDestructibleActor** (3917 campaign / 116 MP; bWorldGeometry, not movable);
- **TnSpawnableDestructibleActor** (runtime: boss parts, fuel cells, armor; bKillDuringLevelTransition).

**Damage path:**
1. **`TnDestructibleActor.TakeDamage / TakeRadiusDamage`:**
   - ignored if **`_Invulnerable`**;
   - ignored if the actor is based on a pawn and the instigator is that pawn or its teammate;
   - then `AdjustDamage`: × the damage type's `DestructibleDamageScale` (1.0 for all, 0.25 for TnDamageTypeOmegaRocket).
2. **`HmDestructibleActorBase`:**
   - point damage from outside the **DamageAngle** cone (blueprint default 180°, so all directions) is ignored;
   - `ModifyDamage`:
     - × `DamageModifiers[type].ModifierValue`;
     - radius damage beyond `MinRadiusDamageDistanceForModifier` (50) × `RadiusDamageModifier` (1.0);
     - AI instigator × `AIDamageModifier` (1.0);
   - records `LastHit`.
3. **Component:**
   - a damage type whose trigger is an **`HmKismetDestructionTrigger` subclass** (Aux1–3, InstantKillAi, **Corruption**,
     **Energon**) hits **every piece**;
   - otherwise only the piece owning the hit component.
   - **Radius damage per piece:** scale = `1 − dist_to_piece_bounding_sphere / radius` (or 1 if bFullDamage); skipped if ≤ 0.
4. **Piece** (`UpdateStateFromHit`):
   - only if the active state has a **link accepting the damage type's DestructionTrigger**: an exact class = 2, a subclass = 1;
   - `Health −= damage`; `AccumulatedDamage += min(Health, damage)`;
   - the observer `OnDamage(normalized)` feeds the health bar;
   - **Health ≤ 0 → next state:** a random exact-match link, else a random subclass-match link;
   - entering a state:
     - effects begin;
     - Health = that state's `InitialHealth`;
     - a stimulus `HmStimulusType_Destructible` (AI hears it);
     - optional **automatic transition timer** (server);
     - replicated `ReplicatedPieceStates[piece]`.
   - Other entry points: `NotifyTrigger(damage, trigger)` (Kismet / scripted) and dependent pieces (`DependentPieceHandles` take
     the same damage).

**Damage type → trigger:**
- WeaponFire: assault rifle, pistols, MGs, EMP shotgun, ion blasters …;
- Explosion: bazooka, grenades, rockets, mines, turret rockets, environment explosions;
- TankCannon: tank shell;
- Melee: melee;
- Shockwave: shockwave, shield push;
- OmegaRocket: Omega beam / rockets;
- DeathBlossomRocket;
- GenericDamage: guided missile;
- Corruption, Energon: Kismet-class, hit all pieces;
- PawnBaseDied: a pawn-attached destructible when its pawn dies;
- Heal, TractorBeam, Touch, TimerExpired, Notification;
- CustomA / B: the boss scripts.

**Kismet:**
- **`SeqEvent_HmDestructibleStateNotification`** fires on every piece state change, with string vars **"Piece Name"** and
  **"State Name"** (instigator = the damage instigator). This is the single Kismet output; maps branch on the state name.
- **`SeqAct_TnToggleDestructible`:** inputs 0 vulnerable, 1 invulnerable, 2 toggle.
- **Highlight** only when `_ShouldBeHighlighted && !_Invulnerable`. Fuel cells and boss parts get objective markers.

**Debris and effects** are per-state effect factories (blueprint data):
- Debris, FracturedMesh, Gibs, Particle, Sound, Dialog, RadialDamage, DamageDependents, EnableLink, StateNotification,
  Targetable, AnimatedMesh / CollidableMesh / PhysicalMesh / StaticMesh, CompetitiveScore.
- The death-path and initial-state notes cover their behaviour (MILESTONE03 P5B; TARGETED_PASS5 §12).

### 2.2 Interactives (data-driven `TnInteractActionSequence` lists on the CDO; event types BeginAdjust / BeginUse / EndUse / ForceEndUse / Touch / Untouch / Enabled / Disabled)

| Class (campaign-only placements) | Use conditions | BeginUse sequence (in order) | Kismet output |
|---|---|---|---|
| **TnInteractDetpack** (9) | touch 500 | shuffle anim (adjust) → hide placement mesh → show base → *user*: `Detpack_Release` → EndUse event → release user; *base*: `Countdown_Detpack_*` (blocking) → **TnInteractActionFireDetpackEvent** → hide base → disable | `TnSeqEvent_Interact` (EndUse, when the user anim ends); **`TnSeqEvent_DetPack` "BaBoom"** after the countdown anim |
| **TnInteractCorruptOmegaBattery** (4) | touch 500; starts **Disabled** (Kismet enables) | the same detpack-style placement; `Transform_Detpack`; EndUse; then FireDetpackEvent | EndUse + BaBoom (Kismet then corrupts the battery, set-pieces doc) |
| **TnInteractButton / ButtonReusable** (85 / 4) | ≤300 UU, ±45° | holster → `Interact_Button` + base `AP_ButtonInteract_TransIn` → unholster → **EndUse** → release → disable (Button) → collision off | `TnSeqEvent_Interact` EndUse |
| **TnInteractHackingTerminal** (15) | ≤200, ±45°, user facing (dot −1…−0.5) | holster, character colors, **hide HUD**, console in / loop anims, camera anim, big screen + `InteractAccessingMovie_GFX`, then out anims | sequence-driven; the HUD progress bar is the shared one (HUD doc) |
| **TnInteractLightEnergon** (4) | **Optimus Prime only**, ±45°, touch 800 | camera anim, equip **ButterflyAxe** melee, `Interact_Energon_ROBO`, unequip, EndUse, disable | EndUse |
| **TnInteractDarkEnergon** (8) | **Megatron only**, ±45°, touch 600 | camera anim, `Interact_DarkEnergon` | sequence events |
| **TnInteractShieldedPosition** (15) | **AI only** | deploy / raise / lower anims (AI cover) | — |
| TnInteractEmote / AirEmote / PathNode / MoverPathNode | AI use | emote anims (buddy lead, AI layer doc) | — |

### 2.3 Mountable turrets (campaign-only placements: IonicDisplacer 44, Rocket 7, SlugRide 3)

- **Mount:** holster → BeginTurretControl → in anims and camera (HUD hidden, then shown) → enable fire → force-rotate → **mount
  weapon** (`WeaponClass`: MountedHeavyIonBlaster / MountedRocketTurret) → contextual commands **End use (1)** and **Rip turret
  (2)**.
- **Damage to the turret: × `TransferDamageToUserRatio` 0.5 to the user**, except the user's own damage.
- **End use:**
  - the user is god-moded during exit, collision is restored, the user goes to a free spot (`FindSpot`), and falls in robot form;
  - the mounted weapon is removed and the robot weapon restored.
- **Rip:** `RemoveWeapon` creates **`RemovedWeaponClass`** in inventory (GatlingGun / HeavyRocketTurret), marks the turret
  weapon-removed, and plays dialog.
- **SlugRide:** `_LockedInTurret` (no end or rip), attaches during adjust; `TnSeqAct_LockPlayerInTurret`.
- **Downed** is impossible while using a turret (`CanBeDowned`).

### 2.4 Other campaign-only actors

**TnCollectableAchievementItemAutobot / Decepticon (26 / 25)**, destructibles:
- When destroyed (normalized health ≤ 0) by a player, every player gets **`ClientSetBitOnAchievement(AssociatedAchievement,
  CollectableIndex 0–24)`**.
- Hidden at start if every local player already has the bit.
- Data quirk: the Autobot CDO names `ACC_CP_COLLECT_DECEPTICON`.

**TnRollerMine** (1 placed + the RollerSphere ability): Fuse 10, DamageRadius 1000, DamageAmount 25, Momentum 1000, Health 30.

**Volumes:**
- `TnDisableTransformingVolume` (23);
- `TnForceJetBoostVolume` (4);
- `TnEnableTurnaroundCameraBehaviorVolume` (5);
- `HmPlayerOnlyBlockingVolume` (469);
- `TnTogethernessVolume` / `TnTacticalVolume` (AI doc).

## 3. The player's campaign-only mechanics

### 3.1 Downed / revive (player side)

**Gate** (`GlobalCanBeDowned`):
- `DownedEnabled && GlobalDownedEnabled`;
- **a player pawn is never downed when the game has ≤ 1 human player** (solo: you die);
- `GameClass.AllowDowned(P)`: **TnCampaignGame true**; TnMultiplayerGame only with skill `TnSkillCanBeDowned`;
- never while on a turret.

**Going down** (health ≤ 0, not KillZ or `EnvironmentNoDowned` damage):
- `_IsDowned`, Health = 0;
- `ClientStartDowned`: force-end any interact, PRI notify, AI teammates notified;
- **`TnSeqEvent_Downed` output 0**, PRI AddDowned.
- **Further damage keeps lowering Health. At `Health ≤ −DownedHealth (400)` the player is killed** (`bKilledWhenDowned`).

**While downed:**
- drain **`DownedHealthDrainRate` 40/s** (`TnDamageTypeDownedDrain`), so **10 s** to bleed out with no damage;
- the drain pauses while being revived (`_DrainHealthWhenDown=false`);
- the player can't move (AccelRate 0) and plays `Revive_Fall`;
- **3 s of downed invulnerability** (`DownedInvulnerableLength`);
- **DownedSuicide** (button) = instant DownedDrain kill;
- in plane form, the PlaneDowned state blocks jump, boost and transform;
- the HUD shows `NotifyNormalizedDownedHealthChanged(Health / DownedHealth)`.

**Reviving** (the reviver's side):
1. `UpdateRevivablePawn` picks the **nearest downed teammate within `ReviveDistance` 500** (robot, on the ground) or
   **`PlaneReviveDistance` 1000** (plane), skipping one another teammate is already reviving. It registers the Revive contextual
   command.
2. `TryRevive` → `GiveRevive`: plays **`Revive_Heal` (robot) / `Revive_Heal_VEH`**, replicated.
3. **The revive time = that animation's length** (`ClientStartReviveCountdown`). The HUD bar shows `_RevivePercentDone`.
4. Anim end → `GetRevive`:
   - Health += `RevivalHealth` (**115**) − the current negative health, i.e. you come back at 115;
   - or `HealthMax` if FullRevive (Kismet `TnSeqAct_DownRevive` input 1);
   - `TnHealTypeRevive`; `Revive_Getup`; **`TnSeqEvent_Downed` output 1**; PRI AddRevive / AddRevived.
5. An interrupted anim → fail, and the drain resumes.

**On revive, campaign-only** (`TnPlayerPawn.OnGetRevive`), self-buffs at the difficulty level (§3.3):
- **`TnBuffDifficultyPlayerDamageTakenRevived`**: damage-taken modifier 0.15 / 0.25 / 0.35 (per difficulty) for 10 / 7 / 5 s;
- **`TnBuffDifficultyJamAbilitiesRevived`**: abilities jammed 5 / 7 / 10 s.

**Game over** (TnCampaignGame):
- **A human player killed by anything but `DmgType_Suicided`** (including bleeding out, being killed while downed and DownedSuicide)
  → `CampaignGameOver`. The same if an active AI squad member bleeds out.
- `NotifyDownedPawn` also calls `CampaignGameOver` when no other-team player pawn exists and **every same-team pawn is downed**.
- In `InProgressOver`:
  - after **1 s** (`_DelayBeforeShowingGameOverScreen`): `GRI._CampaignGameOver`, `PC.OnCampaignGameOver`, UI event 14;
  - all AI plan runners and the scheduler suspended;
  - **all damage zeroed**; no respawns (`PlayerCanRestart` false).
- Recovery is a checkpoint restart (`StartMatch` → InProgress).

### 3.2 Energon, overshield, health

- Same pickup classes and values as MP (§1.2).
- **The campaign-only part is the energon shard economy:** loot drops (`AbilityResourcePickup` +3 each), fed by kills, headshots,
  melee and explosions (§1.3).
- `TnBuffDifficultyAbilityResourceModifier` scales the ability resource (§3.3).
- AI overshields (`TnAiHealthMonitorOvershield`, `TnAiActionRechargeOvershield`) are AI-side.
- `ApplySpecialtyBuffs` is **false** in campaign (true in TnGame/MP), so MP specialty buffs don't apply.

### 3.3 Difficulty scaling tables

- **Level** = `max(0, GameDifficulty × (MaxDifficultyLevel 2 + 1) + (NumHumanPlayers − 1))` → 0–8. That is 3 difficulties × 1–3
  players.
- H: difficulty 0 / 1 / 2 = Easy / Normal / Hard.
- Applied to every pawn at spawn and on `OnSetDifficulty`:
  - players get `DifficultyBuffsPlayer`;
  - AI get `DifficultyBuffsAi`.

| Buff (target) | L0 | L1 | L2 | L3 | L4 | L5 | L6 | L7 | L8 |
|---|---|---|---|---|---|---|---|---|---|
| PlayerDamageCaused (player) | 1.5 | 1.25 | 1.0 | 1.0 | 0.9 | 0.8 | 0.7 | 0.6 | 0.5 |
| PlayerDamageTaken (player) | 0.5 | 0.75 | 1.0 | 1.0 | 1.25 | 1.5 | 1.75 | 2.0 | 2.25 |
| PlayerDamageTakenDowned (player, condition: downed) | 0.5 | 0.4 | 0.3 | 0.5 | 0.4 | 0.3 | 0.25 | 0.2 | 0.15 |
| AbilityResourceModifier (player) | 0.75 | 0.65 | 0.55 | 1.0 | 0.9 | 0.8 | 1.25 | 1.0 | 0.9 |
| DamageCausedAiVsAi (AI, condition: H, AI target) | 0.45 | 0.35 | 0.25 | 0.3 | 0.2 | 0.1 | 0.25 | 0.15 | 0.05 |
| On revive: DamageTakenRevived modifier / time (s) | 0.15 / 10 | 0.15 / 10 | 0.15 / 10 | 0.25 / 7 | 0.25 / 7 | 0.25 / 7 | 0.35 / 5 | 0.35 / 5 | 0.35 / 5 |
| On revive: JamAbilities time (s) | 5 | 5 | 5 | 7 | 7 | 7 | 10 | 10 | 10 |

- All are permanent (`BuffTime −1`) except the revive buffs.
- Buddy extras are per AICD `Buffs` (e.g. Optimus buddy `TnBuffHealthOnBlock`).

## 4. Data vs code summary

**Code (script):**
- the destructible damage / state machine;
- interact action classes;
- the turret mount / rip;
- the downed / revive state and the game-over rules;
- the loot classification rules (§1.3 list);
- the difficulty level formula.

**Data:**
- iconic loadouts;
- loot tables (per chapter) and crates;
- destructible blueprints (states, links / triggers, InitialHealth, modifiers, effects);
- interact sequences (CDOs);
- turret weapon / removed-weapon classes;
- buff tables;
- DownedHealth 400, drain 40, RevivalHealth 115, revive distances 500 / 1000, invulnerable 3 s;
- pickup respawn times.

**Kismet:**
- SetLootTable, SpawnPickupRewards, ToggleDestructible, DownRevive, LockPlayerInTurret;
- interact Enabled / Disabled;
- the destructible state notification branches.
