# Campaign-only gameplay systems: per-chapter inventory (RE, 2026-10-23)

For Gameplay's campaign runtime. Labels as usual (C = script / data; H = inference).
- Counts: authored.db actors and Kismet ops whose package maps to the chapter (`work/q/pkg_chapter.json`), and AssetTools
  `next_map/<ch>_pawn_factories.json` archetype counts.
- **Every class below has 0 placements in the 10 MP maps.**
- Semantics live in the existing docs, cited per row; this doc is the index and the per-chapter map.

**Docs cited:**

| short name | file |
|---|---|
| SETPIECES | CAMPAIGN_SETPIECES_RE |
| DELTAS | CAMPAIGN_CONTENT_DELTAS_RE |
| KWFC | CAMPAIGN_KISMET_WFC_CLASSES_RE |
| AI | CAMPAIGN_AI_LAYER_RE |
| TOG | CAMPAIGN_TOGETHERNESS_BUDDY_RE |
| AF | CAMPAIGN_ACTORFACTORY_RE |
| MAT | CAMPAIGN_MATINEE_NATIVE_RE |

## 1. Per chapter at a glance

| Chapter | Playable trio (TnSeqVar_Character) | Bosses / special enemies | Campaign-only mechanics present |
|---|---|---|---|
| **A1_IAC** | Optimus, Ratchet, Bumblebee | **StarScream_Boss** (2 factories; jet boss, ScriptedBombingRun) | turrets (IonicDisplacer 13), StartBombingRun ×2, forced vehicle form ×6, AddImpulseToPawnInVolume ×4, hacking terminals 4, tutorial messages 6, PlayerOnlyBlockingVolume 127 |
| **A2_KON** | Optimus, Bumblebee, Sideswipe (+ AirRaid) | **Soundwave_Boss** + **Laserbeak / Rumble / Frenzy** minions; Spider ×13 | BeginPrisonWalk ×2, SpectateActor ×2, turrets (Ionic 17), interact movers 10, AiDisableShootingBehavior ×5, light-energon interacts 2 |
| **A3_COR** | Optimus, Warpath, Ironhide | **Ratchet_VIP** (escort, healer NPC); Spider ×10; Tank ×3 | **SlugRide turret** ×3 + LockPlayerInTurret ×9, rocket turret 1, TnInteractLightEnergon 4, SpecialSpawnLogic 6, scripted AI jumps 5, HealthStatus event; **destructibles 271** |
| **A4_ORB** | Silverbolt, Jetfire, Air Raid (**jets**) | Tank ×3 (TankStatus ×2), Brute / Titan / CQC | **flight**: ForceJetBoostVolume 4, EnableTurnaroundCameraBehaviorVolume 5, forced vehicle form ×11; rocket turrets 3; 60 HmLightMapped skeletal set-piece actors; Together events 7 |
| **A5_TRY** | Ironhide, Optimus, Bumblebee | **Trypticon** (+ Spider ×27) | SetTrypticonStage ×7, TrypticonSentry ×4, TrypticonVolumes, AddImpulseToPawnInVolume ×15 |
| **D1_ORB** | Megatron, Barricade, Brawl | **Neutral faction** soldiers (90 MG + jets / snipers / brutes) | dark-energon interacts 2, RollerMine 1, tutorial messages 5, shielded positions 5, SetActorMatInst* (D1 79 / 44) |
| **D2_COR** | Starscream, Thundercracker, Skywarp (**jets**) | **SecurityDrone** ×12; batteries (Standard / Large / TOUCH _DES, 27) | **flight / air**: TnInteractAirEmote 9, forced vehicle form 3; **detpacks** (TnInteractDetpack 2, DetPack events 4); tactical movers 98 |
| **D3A_IAC** | Megatron, Breakdown, Soundwave (+ Brawl) | **Brawl_VIP** (tank buddy); **HoloBrute** ×16; RollerMine ×13; invisible DeathBeam sentries 7 | detpacks 7, dark energon 4, Tank ×1 (TankStatus), sentry force-target / dormant, LookAt set-piece actors |
| **D3B_IAC** | Megatron, Breakdown, Soundwave | RollerMine ×33; Omega MG / rocket sentries 12; falling TubeDebris props | sentries forced 32 / dormant 12, ToggleHealthDisplay ×2 |
| **D5_OME** | Megatron, Breakdown, Soundwave | **Omega Supreme** (2), DES_OmegaRobotBattery 4; Autobot jets 24 | CorruptOmegaBattery interact / action ×4, SetOmegaStage ×3, OmegaStatus event |

**All chapters:** pawn factories (1,232), objective markers (466), interact emotes (872), tactical / hotspot volumes, togetherness
volumes (152), downed / revive events, collectibles (5 per chapter), loot tables, achievements.

## 2. Systems, with classes and where the semantics are

| System | Classes | Chapters | Semantics |
|---|---|---|---|
| **Bosses (script, no native boss code)** | TnOmegaSupremePawn + OmegaStatus / SetOmegaStage / CorruptOmegaBattery; TnTrypticonPawn + TrypticonEvents / SetTrypticonStage / TrypticonVolumes / TrypticonSentry; TnTankAiPawn + TankStatus | D5, A5, A4 / A3 / D3A | SETPIECES §1–3 |
| **Starscream boss (A1)** | StarScream_Boss_AICD: Soldier_Jet_Boss action set (ShootFromAirTactical, RocketBarrageDeathBlossom, BarrelRoll, **ScriptedBombingRun**, ThrowGrenade, transform both ways) + TR_AiGoalSet_Boss (adds StayInTacticalVolume, HealSelf); TnSeqAct_StartBombingRun | A1 | AI layer; action list C (this doc); KWFC (StartBombingRun) |
| **Soundwave boss + minions (A2)** | Soundwave_Boss_AICD (SoundwaveBoss set: **SpawnCharacter**, **AudioBlast**, Revive, HealFromCurrentLocation, Retreated; Engineer_Boss goals); Laserbeak (air: BombingRun, BarrelRoll), Rumble (**Stampede**, AreaOfEffectAttack), Frenzy (**AbilityJammer**, Hover attack): TnMinionAiPawn | A2 | AI layer; action lists C (this doc) |
| **VIP / escort buddies** | Ratchet_VIP (NPC_Healer sets), Brawl_VIP (tank buddy), AirRaid_Buddy | A3, D3A, A2 | AI §2 (buddy), TOG §2 |
| **Neutral faction** | *_Neutral_AICD soldiers | D1 | AI layer (faction tables) |
| **Turrets (mountable)** | TnMountableTurretIonicDisplacer 44, TnMountableTurretRocket 7 (content subclass, no script), **TnMountableSlugRideTurret 3** + TnSeqAct_LockPlayerInTurret 9 (A3); SeqAct_TnAiUseTurret 53 | A1–D5 | DELTAS §2.3 |
| **Sentries (AI turrets)** | *_TURRETDEF via TnActorFactorySentryAI; SeqAct_TnAiSentryForceTarget / SetDormant | most | AI layer; AF |
| **Flight / jet sections** | player jet trios (A4, D2); TnForceJetBoostVolume, TnEnableTurnaroundCameraBehaviorVolume (A4); TnInteractAirEmote (D2: an air emote point, content-only class); TnSeqAct_ForceIntoForm (TF_Vehicle: A1 6, A2 1, A4 11, D2 3, D3A 3) | A4, D2 (+ forced-vehicle beats) | DELTAS §2.4 / §3; KWFC (ForceIntoForm) |
| **Special movement** | TnDisableTransformingVolume 22, TnSeqAct_AddImpulseToPawnInVolume (A5 15, A1 4), SeqAct_TnAiJump (A3), TnInteractMoverPathNode (A2 / A3), TnSeqAct_BeginPrisonWalk (A2) | various | DELTAS; KWFC |
| **Spectate** | TnSeqAct_SpectateActor (A2 ×2): input 0 → each target PC `SpectateActorSubject(actor, or the controller's pawn)`; input 1 → clear. Saved for join-in-progress (C) | A2 | this doc |
| **Destructibles** | TnDynamicDestructibleActor 554 (A3 271), battery DES (D2), Omega batteries (D5), TnSeqAct_TnToggleDestructible | all | DELTAS §2.1 |
| **Detpacks** | TnInteractDetpack, TnSeqEvent_DetPack | D2, D3A | SETPIECES §4; DELTAS |
| **Interactives** | TnInteractButton(Reusable), HackingTerminal, ShieldedPosition + ShootFromShieldedLocation, LightEnergon(NonPrime), DarkEnergon | all | DELTAS §2.2 |
| **AI-scripting Kismet** | DefendActor 144, AttackFromActor 97, SetSniperSearchLocations 89, AssignTacticalVolume 414, MoveToActor 380, UseInteract 335, ShootAtActor 105, SetAiGodStatus 35, SetTargetableByAi 50 | all | KWFC; AI |
| **Tactical / hotspot volumes** | TnTacticalVolume 354, **TnHotSpotVolume 137**: at Initialize, every tactical PathNode inside the volume (same level) is flagged IsHotSpot with the volume's Faction / AllFactions; nodes outside are cleared (C) | all | this doc; AI |
| **Set-piece skeletal actors** | HmLightMappedSkeletalMeshActor 92 (A4 60), TnSkeletalMeshLookAtActorMAT 22 + SetSkeletalMeshLookAtActorTarget 20 | A3, A4, D1, D3 | MAT (AnimControl); KWFC (LookAt) |
| **Co-op / flow** | TnCampaignPlayerStart 144, SetCoopSpawnPointSet 5, togetherness | all | TOG; STREAMING |
| **Score / achievements** | AddCompetitiveScore 43, UnlockAchievement(InVolume), AchievementEventCounter, collectibles 51 | all | KWFC; DELTAS |

## 3. Gaps
- **Content-only classes with no script behaviour of their own** (behaviour is from the parent plus data): TnMountableTurretRocket,
  TnInteractAirEmote.
- **The boss action implementations** (AudioBlast, SpawnCharacter, Stampede, AbilityJammer, RocketBarrageDeathBlossom,
  ScriptedBombingRun) are TnAiAction* script classes.
  - The action sets above are C; the per-action parameters are in the sets' blueprints (use `tools/ai_sets_export.py` with
    these AICDs).
  - **Next RE item if Gameplay wants it:** a per-action decode like MP_ESCALATION §9 for the A1 / A2 bosses and minions.
