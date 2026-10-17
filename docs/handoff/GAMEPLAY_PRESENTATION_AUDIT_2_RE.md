# Presentation audit 2: vehicle weapons, abilities, transformation, melee

WFC RE lane, 2026-10-18. This continues WEAPON_PRESENTATION_AUDIT_RE.md (same method). Parts are added in order (4), (1), (3),
(2), with a commit after each.

**Grep classes** (read-only, against `Rebuild/src`, `Rebuild/data`, `Rebuild/work/render/**/*.json`):
- **C** = named in src code;
- **E** = only in the asset JSON embedded in `src/game/CharacterAudio.inc`;
- **R** = in render / data JSON;
- **-** = no hit.

**C means named, not faithful.** A value column is marked *absent* only when no src reader exists.

**E is not "unused" (Systems, 2026-10-18):** our audio code (AbilityAudio / CharacterAudio) plays cues looked up by class and field in the embedded JSON tables (`classSound` / buff sounds), so an E-only cue can still be played (e.g. the cloak loops via TnBuffCloak). Treat **-** (no hit) as the real gap list; E items need a runtime check (Systems' CUELOG).

**Data note:** MP uses `TnWeapon.MultiplayerData` (`GetWeaponDataByType(3)`), not `PlayerData` (campaign). Everything here uses the
MP data. The gun table was corrected the same way; see its §3 note.

## Part (4): vehicle weapons (delta to the gun table)

The 7 MP vehicle weapons (WeaponType 3, every chassis, both factions) are already rows in the gun table: muzzle, tracer, squib,
recoil, camera. This part adds the firing mechanics and the projectile presentation.

**Muzzle alternation (CONFIRMED, `HmWeapon.OnPlayFireEffects`):** each shot runs, in order:
1. `PlayWeaponEvent`;
2. `StartMuzzleFlash`;
3. `PlayTracerEffect` (unless bSuppressTracerEffects);
4. `PlayImpactEffects`;
5. `EjectShellCasing`;
6. **`ChangeSocket()`**: `CurrentSocket = (CurrentSocket + 1) mod MuzzleFlashSockets.Length`;
7. then the next weapon mesh, `_CurrentMesh = (+1) mod _WeaponMeshes.Length`.

So two-socket vehicle guns alternate **WeaponSocket_Primary / WeaponSocket_Primary2** every shot.
- `EjectShellCasing` uses the mesh's `ShellCasingPSCTemplate`, which **no WFC weapon mesh authors**. All shell ejection is
  AnimNotify-driven (gun table §2).
- Ours: `ChangeSocket` and `WeaponSocket_Primary2` are in `Weapon.h` / `PlayerController.cpp` / `ChassisDef.*` [C].

| Weapon (MP data) | Fire | Interval (s) | Sockets | Projectile | Flight FX / explosion FX / flight loop | Speed | Grep (proj class, flight, explosion, loop) |
|---|---|---|---|---|---|---|---|
| AssaultRifleVehicle (AssaultRifle_Vehicle_WEPDATA) | instant hit | 0.065 | Primary / Primary2 | — | tracer Tracer_AssaultRifle_FX (gun table) | — | gun table |
| PlaneAssaultRifle (AssaultRifle_Plane) | instant hit | 0.065 | Primary / Primary2 | — | same | — | gun table |
| PlaneMachineGun | instant hit | 0.06 | Primary / Primary2 | — | same | — | gun table |
| RocketVehicle (Rocket_Vehicle) | projectile | 0.7 | Primary / Primary2 | TnProjectilePlaneRocketTemp | Tracer_HomRocket_FX / Squib_HomRocket_01_FX / BL_WPN_GUN_ROCKET.TRAIL_LP | 18000 | R (data json) / R / C (FxLights) / **-** |
| PlaneRocket (Rocket_Plane) | projectile, **burst 4, BurstDelay 0.75** | 0.15 within a burst | Primary / Primary2 | TnProjectilePlaneRocketTemp | same | 18000 | same |
| HomingRocketVehicle | projectile (2 fire types; the second is none) | 1.25 | Primary / Primary2 | TnProjectileHomingRocketVehicle | same FX | 10000 | R / R / C / - |
| TankCannon | projectile | 2.0 | Primary only | TnProjectileTankShell | Trail_TankCannon_FX / **Martyrdom_Grenade_FX** / BL_WPN_GUN_TANK_CANNON.TRAIL_LP | 20000 | R / C (WeaponTable) / C (FxLights) / - |

**The secondary (vehicle-form) grenades** are per chassis family through `ChassisRestriction` and fire alternately from
`MuzzleFlash_R` / `MuzzleFlash_L`:
- FlakGrenades: trucks;
- KamikazeMines: cars;
- HealGrenades: jets;
- FlashBangs: tanks.

The gun table rows have their projectile FX.

**Findings:**
- Vehicle projectile flight FX: in render data and `weapons_mp.json` [R]. The explosion squibs are coded in FxLights [C].
- **Projectile flight loop sounds** (`*.TRAIL_LP`, `BL_WPN_GRENADE.*_FLIGHT_LP`): **no src hit (-)**. Only ability projectiles
  (GuidedMissile) play a FlightSound in `AbilityAudio.cpp`.
- **The tank-shell explosion is Martyrdom_Grenade_FX** (the projectile CDO), not a tank-specific effect. Ours references it [C].
- PlaneRocket's 4-shot burst with a 0.75 s gap alternates sockets per rocket. Interval and burst are in `weapons_mp.json` [R].
  Runtime use: unknown.

## Part (1): MP abilities (18)

Generator: `tools/ability_presentation_audit.py`, writing `notes/data/ability_presentation_audit.json` / `.md`.

For each ability, the extractor follows every asset reachable from the ability CDO, three references deep:
- the trigger anim, sound and effect;
- buffs (BuffToAdd / BuffsToApply / EffectBuffs);
- spawned inventory, projectiles and pawn factories;
- classes the ability script spawns by literal (e.g. TnBarrierSpawnable, TnRollerMineAbility);
- plus the trigger anim's AnimNotifies.

**Cooldown is indexed by the game's skill-data set, not the form** (CONFIRMED, corrected 2026-10-18): `TnAbility.GetCooldownTime` uses `Cooldown[TnGame.GetCurrentSkillDataIndex()]` (index 0 if the array is short). **TnMultiplayerGame returns 0; TnGame (campaign and others) returns 1.** So the arrays are **[MP, campaign]**. `GetResourceRequired` returns 0 unless the index is 1, so **`ResourceRequired` (the energon cost) applies only in campaign; MP abilities are cooldown-only.** The result is × CooldownReductionModifier while the owner has TnBuffAbilityCooldown.

**Limits:**
- Mesh / material-only visuals of spawned actors (barrier shield mesh, decoy hologram) are not separated out.
- Footstep / CameraShake / ForceFeedback / Dialog notifies are listed by type only.
- **Hover and Dodge** have no ability-side presentation. They call `AcrobaticsManager.Hover / Dodge`, so their FX and sounds are
  per-chassis acrobatics data (part 3).
- **Whirlwind** is `MeleeService.StartMeleeAttack(3)`, so its FX are in the melee data (part 2).

**C means named, not faithful.**

| Ability (specialty) [class grep] | Cooldown / resource | Trigger anim + notifies | FX (all reachable) | Sounds | Materials / lights | Buffs / spawns |
|---|---|---|---|---|---|---|
| Cloaking (Scout) [-] | [15.0, 15.0] / None | -: - | Cloaking_FX [R] | CLOAK_AUTOBOT_OFF [E], CLOAK_AUTOBOT_START_LP [E], CLOAK_DECEPTICON_OFF [E], CLOAK_DECEPTICON_START_LP [E] | none  | TnBuffCloak |
| HardLock (Scout) [CE] | [60.0, 10.0] / None | Skill_MarkTarget: - | Hardlock_FX [-] | HARD_LOCK_INITIATED [E], HARD_LOCK_WARNING_LP [E] | none  | TnBuffHardLocked, TnBuffImprovedHardLock |
| Hover (Soldier) [C] | [35.0, 15.0] / None | -: - | none | none | none  | - |
| DecoyTrap (Scout) [E] | [60.0] / None | Skill_Barrier: - | DecoyTrap_FX [-] | DECOY_TRAP_DEPLOY [E], DECOY_TRAP_EXPLODE [-] | none {} | TnDecoyTrap |
| Dodge (Scout) [C] | [2.0, 0.5] / None | -: - | none | none | none  | - |
| SpawnAmmoCrate (Soldier) [CE] | [60.0, 180.0] / None | Skill_Barrier: - | none | AMMO_DEPLOY [CE], WeaponPickupMountedIonBlasterObtainedDialog [-] | none {} | TnAmmoBeacon |
| Shockwave (Scientist) [C] | [60.0] / 100.0 | Skill_Shockwave: HurtRadius:None@WhirlwindSocket; PlayEffect:DashPulse_1_FX@BoosterSocket_L; PlayEffect:DashPulse_1_FX@BoosterSocket_R; PlayEffect:Shield_BlastWave_A_FX@PositionSocket; PlayEffect:Trails_Bumblebee_FX@ElbowSocket_L; PlayEffect:Trails_Bumblebee_FX@ElbowSocket_R; PlayEffect:Trails_Bumblebee_FX@KneeSideSocket_L; PlayEffect:Trails_Bumblebee_FX@KneeSideSocket_R; PlayEffect:Trails_Bumblebee_FX@WingLrgSocket_L; PlayEffect:Trails_Bumblebee_FX@WingLrgSocket_R; PlayEffect:Trails_Bumblebee_FX@WingSmSocket_L; PlayEffect:Trails_Bumblebee_FX@WingSmSocket_R; Sound:SHIELD_PUSH | DashPulse_1_FX [R], Shield_BlastWave_A_FX [R], Trails_Bumblebee_FX [R] | SHIELD_PUSH [CE] | none  | - |
| Whirlwind (Soldier) [C] | [60.0] / 100.0 | -: - | none | none | none  | - |
| Drain (Scientist) [E] | [60.0] / 100.0 | -: - | Drain_Blast_FX [-] | DRAIN_HEAL [E], DRAIN_INITIATE [E] | none  | TnBuffDrainSource |
| SpawnSentry (Scientist) [C] | [60.0] / 100.0 | ADD_Grenade_Throw: Sound:GRENADE_FOLEY_TOSS_LRG; Sound:GRENADE_FOLEY_TOSS_MED; Sound:GRENADE_FOLEY_TOSS_SML | Impact_OS_Turret_A_FX [R], MuzzleFlash_HomRocket_FX [CER], MuzzleFlash_Turret_A_FX [R], Squib_RepairEnemy_FX [CER], Tracer_AssaultRifle_FX [CER], Tracer_RepairBeam_FX [CR] | EXPL_IMPT_WORLD [E], GRENADE_FOLEY_TOSS_LRG [E], GRENADE_FOLEY_TOSS_MED [E], GRENADE_FOLEY_TOSS_SML [E], HUD_TARGET_LOCK [E], IMPT_WORLD [CE], Overheat [CER], SENTRY_ACTIVATE_LP [E], SENTRY_ACTIVATE_POSTDEPLOY [E], SENTRY_BY [-], SENTRY_IMPT [E], SENTRY_SHOOT [E], SHOOT [CE], SHOOT_DMG_LP [E], SHOOT_HEAL_LP [E], SHOOT_LAST_SHOT [E], SHOOT_LP [E] | TPRO_DeployedTurret_Overshield_MATINST [-] {} | PawnFactory0, TnSentryPawnAbility |
| AbilityJammer (Soldier) [CE] | [60.0] / None | Skill_AbilityJammer: DamageSweep:None@MeleeSocket_SmallRobot; PlayEffect:SwordTrail_Red_FX@WeaponTrailSocket; Sound:H2H_GUN_BUTT_WHSH_01 | SwordTrail_Red_FX [R], Trail_PlasmaCannon_Lrg_FX [ER], WarCry_AOE_FX [-] | ENERGON_SLING_FIRE [E], ENERGON_SLING_HIT [-], ENERGON_SLING_TAIL [-], H2H_GUN_BUTT_WHSH_01 [E] | none  | TnProjectileAbilityJammer |
| Disguise (Scientist) [E] | [15.0] / None | -: - | none | DISGUISE_INITIATE [E] | none  | TnBuffDisguise |
| Poke (SinglePlayer) [C] | None / None | -: - | SwordEquip_Red_FX [CR] | ANIM_EQUIP [E], ANIM_HOLSTER [E], HUM [CE], HUM_CHARGED [E] | none {"LightColor": {"B": 40, "G": 45, "R": 250, "A": 0}} | - |
| GuidedMissile (SinglePlayer) [C] | [45.0] / 100.0 | Skill_GuidedMissile: Sound:SHOOT_BUILDUP | Exp_GenericLvl_LRG_FX [R], Trail_FusionCannon_FX [ER] | EXPL_IMPT_WORLD [E], SHOOT_BUILDUP [E], SHOOT_TRAIL [E] | none  | TnGuidedMissile |
| Warcry (Leader) [C] | [60.0] / 100.0 | Skill_Warcry: Sound:WAR_CRY_CHEST_HIT; Sound:WAR_CRY_CHEST_HIT_SERVO_01; Sound:WAR_CRY_CHEST_HIT_SERVO_02; Sound:WAR_CRY_STATE_START | Chest_FX [CE], Hardlock_FX [-], WarCry_AOE_FX [-], WarCry_Buff_01_FX [-], WarCry_Buff_02_FX [-], WarCry_Buff_03_FX [-] | HARD_LOCK_WARNING_LP [E], WAR_CRY_CHEST_HIT [E], WAR_CRY_CHEST_HIT_SERVO_01 [E], WAR_CRY_CHEST_HIT_SERVO_02 [E], WAR_CRY_STATE_START [CE] | none  | TnBuffWarcryIncreaseDamage, TnBuffWarcryDecreaseDamageTaken, TnBuffWarcry0, TnBuffWarcry1, TnBuffWarcry2 |
| TransformDisruptor (Leader) [CE] | [60.0] / None | Skill_TransformDisruptor: PlayEffect:DashPulse_1_FX@BoosterSocket_L; PlayEffect:DashPulse_1_FX@BoosterSocket_R; PlayEffect:Shield_BlastWave_A_FX@PositionSocket; PlayEffect:Trails_Bumblebee_FX@ElbowSocket_L; PlayEffect:Trails_Bumblebee_FX@ElbowSocket_R; PlayEffect:Trails_Bumblebee_FX@KneeSideSocket_L; PlayEffect:Trails_Bumblebee_FX@KneeSideSocket_R; PlayEffect:Trails_Bumblebee_FX@WingLrgSocket_L; PlayEffect:Trails_Bumblebee_FX@WingLrgSocket_R; PlayEffect:Trails_Bumblebee_FX@WingSmSocket_L; PlayEffect:Trails_Bumblebee_FX@WingSmSocket_R; Sound:SHIELD_PUSH | DashPulse_1_FX [R], Shield_BlastWave_A_FX [R], Squib_TransformDisrupter_FX [-], Trail_PlasmaCannon_Lrg_FX [ER], Trails_Bumblebee_FX [R] | ENERGON_SLING_HIT [-], ENERGON_SLING_TAIL [-], SHIELD_PUSH [CE], TRANSFORM_DISRUPTOR [E] | none  | TnProjectileTransformDisruptor |
| RollerSphere (Leader) [C] | [60.0] / 100.0 | Skill_AbilityJammer: DamageSweep:None@MeleeSocket_SmallRobot; PlayEffect:SwordTrail_Red_FX@WeaponTrailSocket; Sound:H2H_GUN_BUTT_WHSH_01 | SwordTrail_Red_FX [R] | H2H_GUN_BUTT_WHSH_01 [E], KAMIKAZE_FUSE_START [E] | none {} | TnRollerMineAbility |
| Barrier (Leader) [CE] | [20.0] / None | Skill_Barrier: - | none | BARRIER_DEPLOY [E], BARRIER_LP [E], BARRIER_RETRACT [E] | none {} | TnBarrierSpawnable, TnBuffIncreaseBarrierHealth |

**HUD cooldown presentation** (CONFIRMED: TnHUD + Hud_GFX AS, sprite 583 per ability slot):
- **Calls:**
  - `TnHUD.SetAbilityResource(n, CooldownTime, NormalizedCooldownStart)` / `SetAbilityResourceReady(n)` (−1, 0) →
    `_global.UpdateAbilityCooldown(n, t, start)` → `NotifyAbilityType{0,1,2}UpdateCooldown`;
  - slot icons via `NotifyAbilityType{n}Changed(AbilityType)` → `SetAbilityIcon`, which loads the bitmap named by the ability id
    (fallback `IconUnknown`) at 55 % scale.
  - Slots 0 / 1 are the robot abilities; slot 2 is the vehicle ability, labelled by vehicle type Car / Truck / Jet / Tank
    (TnHudDataObserverVehicleAbilityType).
  - `NotifyCurrentFormChanged` shows 0 / 1 + grenades in robot form, or 2 in vehicle form.
- **Going on cooldown** (CooldownTime ≥ 0, start < 1):
  - background → alpha 50, colour 0x999999; cooldown clip → alpha 40, stopped on frame 1; the element drops 10 px; all 0.5 s
    easeout;
  - the **fill mask** is set to `6 + start × 58` px tall, then grows **linearly to 58 over CooldownTime**;
  - the element turns white and its label fades out, unless jammed.
- **Ready:**
  - `highlight_mc` plays;
  - background → alpha 100, colour 0x666666; cooldown clip alpha 60 and plays 'loop'; the element goes back up; the mask goes to
    64;
  - the element colour goes back to AbilityColor and the label to alpha 100.
- **Jammed** (`ApplyBuff` / `UnapplyBuff` for TnBuffAbilityJammed, AbilityJammedKillstreak, DifficultyJamAbilitiesRevived →
  `AbilityType0/1Jammed`; TnBuffTransformDisruptor → `TransformDisrupted`):
  - `jammed_mc` → alpha 100; the element turns red (0xFF3333) and the label hides; reverted on unjam.

**Ours:**
- `src/frontend/Hud.cpp` sends `NotifyAbilityType{n}Changed` / `UpdateCooldown` [C].
- **No hit for `ApplyBuff` / `UnapplyBuff`** (which drive `AbilityType*Jammed` / `TransformDisrupted` inside the movie), so the jam display
  (killstreak jammer, revive jam, Transform Disruptor) is **absent**. `NotifyCurrentFormChanged` **is** sent (Hud.cpp), and the movie's own AS
  does the ShowAbilityType / ShowGrenades slot swap, so the slot swap is covered (corrected per Gameplay).

**Findings (part 1):**
1. **Ability sounds are mostly E (embedded JSON; may be played data-driven, so runtime check, not gaps).** Cloak start / off loops per faction, HardLock, Decoy deploy, Drain
   initiate / heal, Disguise, Barrier deploy / loop / retract, Warcry chest hits, TransformDisruptor and others have no src
   reference. Exceptions: AMMO_DEPLOY, SHIELD_PUSH (Shockwave / TransformDisruptor) and WAR_CRY_STATE_START [C].
2. **Ability FX with no hit at all (-):**
   - DecoyTrap_FX, Drain_Blast_FX, WarCry_AOE_FX, WarCry_Buff_01/02/03_FX, Hardlock_FX, Squib_TransformDisrupter_FX;
   - sounds DECOY_TRAP_EXPLODE, ENERGON_SLING_HIT / TAIL.
3. **Shockwave and TransformDisruptor share the same notify set:** Shield_BlastWave_A_FX at PositionSocket, DashPulse_1_FX at
   both boosters, and Trails_Bumblebee_FX at elbows / knees / wings. Ours has these R only.
4. **HUD:** cooldown fill, icons and the form slot swap are C; the jam / disrupt states (ApplyBuff) are absent (above).
