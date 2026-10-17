# Campaign PROVISIONAL readings closed (RE, 2026-10-18)

Labels: **CONFIRMED (C)** = read in script bytecode, the binary, or cooked data; **HIGH (H)** = stock UE3 reading with
supporting WFC evidence, not traced end to end; **CORRECTED** = the provisional reading is wrong or incomplete, the correction is C/H as marked.

Scope: every PROVISIONAL in Rebuild `docs/handoff/CAMPAIGN_*.md` + `MP_COLLISION_PHYSMAT.md` (integration/milestone-09c) and
AssetTools `docs/handoff/chapter_notes/*.md`. Part 1 = the five that gate verdicts (Integration's order: 1, 4, 3, 2, 5).

## Part 1 (verdict-gating)

### 1. reachFlags 2 = R_FLY (airborne_by_design) — **CONFIRMED (data)**, enum value H

- **Data census** over all 185,660 cooked `TnReachSpec` objects (authored.db): reachFlags takes exactly the values
  **1 (76,035), 2 (106,873), 8 (2,191), 9 (560)**. These are the stock `EReachSpecFlags` bits R_WALK = 1, R_FLY = 2, R_JUMP = 8,
  and R_WALK | R_JUMP = 9; no other bit appears.
- **Specs leaving air nodes** (`AirPathNode`, `TnTacticalAirPathNode`) are flag 2 in **58,154 of 58,211** cases (99.9 %).
  Flag 2 is the air network's flag.
- The remaining 48,719 flag-2 specs start on ground node classes (TnTacticalPathNode, NavigationCell, PathNode, Checkpoint,
  pickup factories): the links from the ground graph up into the air graph. Ground-only specs are 1 / 8 / 9.
- The engine-side enum (`APawn::calcMoveFlags` / `UReachSpec::supports`) was not located in the binary. The bit meaning is
  stock (H), but the data pins flag 2 to fly-only reach independently.
- **Verdict:** airborne_by_design's reading holds; the A4_ORB / D2_COR exemptions stand.

### 4. Empty SetCurrentObjective Description = "clear" — **CONFIRMED (script) for empty; CORRECTED for A1's blank `" "`**

Chain: `SeqAct_TnSetCurrentObjective.Activated` → `TnGameReplicationInfoGameBase.SetCurrentObjective` →
`TnObjectiveManager.SetCurrentObjective` → `ClientSetCurrentObjective`.

1. **The action:** `Description != ""` → `Localize("Objectives", Description, "TransGame")`, else `""`; then `GRI.SetCurrentObjective(desc)`.
2. **The manager, empty desc:** `CurrentObjective = -1` → `ClientSetCurrentObjective`:
   - the previous objective (LastObjective) becomes **DeferredCompleteObjective**, i.e. it is shown as completed;
   - `DeferredAddObjective = ""`: no new objective is added;
   - `LastCompletedObjective = LastObjective`; `LastObjective = -1`.
   So "clear" = **complete the current objective and show none**, not a silent wipe.
3. **The manager, non-empty desc:** `GetObjectiveId(desc)`.
   - If found and different, it becomes current; the previous one completes the same way.
   - **Not found:** `LogInternal("Error: objective … not found in world info objective list")` and **no change at all**.
     The CAMPAIGN_DATA_INDEX note "D1 sets one key not in its bank" is therefore a runtime no-op (C), not a clear.
4. **The bank is localized too:** `TnGameReplicationInfoGameBase` fills it with `Localize("Objectives", key, "TransGame")` per entry,
   so lookups compare localized texts.
5. **A blank `" "` Description is NOT a clear (added 2026-10-18, C):**
   - The test is `Description != ""`, so `" "` goes through `Localize`.
   - The native `execLocalize` (`0x82B982D8` → `0x82B906A8` → core `0x82B2D928`) passes **Optional = 0**, so a missing key returns
     the marker string **`<?%s?%s.%s.%s?>`** (format at `0x82253D64`), e.g. `<?INT?TransGame.Objectives. ?>`. It never returns "".
   - That string is not in the bank → "objective not found" → **no change**: the current objective stays on screen.
   - **Census (AssetTools `*_objectives.json`):** of the 41 `clears_objective` setters, **34 have an empty Description** and clear
     (D3B 17, D3A 11, A3 / A4 / A5 / D1 / D2 / D5 1 each). **A1_IAC's 7 use `" "` and are runtime no-ops.**
   - Fix in the schema: `clears_objective` only when the Description is empty; a whitespace-only Description is `no_op`.

### 3. elevated_above_floor exemption (start 3–30 m above walkable floor) — **CONFIRMED (script)**, falling physics H

- **No floor snap at spawn (C):**
  - `TnGame.SpawnDefaultPawnFor` spawns at exactly `StartSpot.Location`, yaw only.
  - `GameInfo.RestartPlayer` then SetAnchor / Possess / PlayTeleportEffect (MakeNoise only).
  - The 5 m floor trace (`Trace … Location + (0, 0, −500)`, then `HitPosition + CylinderHeight + 1`) exists only in
    `TnPawn.OnTeleport`, not on the spawn path.
- **The pawn falls (H):** with no floor under it the pawn takes PHYS_Falling. The stock `Pawn.SetMovementPhysics` sets
  PHYS_Falling unless it is in water, and `TnPawn` robot / transforming states set `SetPhysics(2)` on teleport. The native
  walking-without-floor transition was not traced.
- **No damage from the drop (C):** `TnPlayerPawn.TakeDamage` returns immediately for `DmgType_Fell` and `DmgType_Crushed`.
  So although a 30 m drop exceeds `Pawn.MaxFallSpeed` 1200 uu/s (Engine CDO), a player spawned there lands unhurt.
- **Verdict:** "the spawn drops onto it" holds; the exemption is sound for player starts / checkpoints.

### 2. Hull physmat rule (BodySetup PhysMaterial, else the first section material's) — **CORRECTED** (C for the order; one link H)

**Located:** the hit → script `HitInfo` PhysMaterial resolver **`0x82F1B370`**, called from the trace natives
(`0x82F22140` / `0x82F21F68` / `0x82F1B488`). It fills (Material, PhysMaterial, Item, LevelIndex, BoneName, HitComponent) from the
check result. The PhysMaterial order, **C from the code**:

1. **the hit component's override** (component +0x118, H: PhysMaterialOverride);
2. else **the check result's own PhysMaterial** (+0x2C: what the collision query reported; H: for a simple-collision hit
   this is the BodySetup PhysMaterial);
3. else **the hit Material's `GetPhysicalMaterial()`** (vtable +0x160; MIC → parent chain → UMaterial.PhysMaterial);
4. else **`0x82C88168`, only when the hit actor `IsA('StaticMeshActor')`** (class looked up by name `L"StaticMeshActor"` in Engine):
   - it walks the StaticMeshComponent's material slots **in order** (`GetMaterial(i)`, vtable +0x208, i < the mesh's element count);
   - it takes **the first slot whose material has a PhysMaterial** (GetPhysicalMaterial, or the base material's +0x38).
   So "the first material" means the first **with** a physmat, not strictly slot 0;
5. else **GEngine.DefaultPhysMaterial** (GEngine +0x25C = `ENV_PHYSMAT_p.Metal_PHYSMAT`, Xe-TransEngine.ini).

**Class hierarchy (C, Engine.xxx export table):** `StaticMeshActor` and **`StaticMeshCollectionActor` are siblings** under
`StaticMeshActorBase`; SMCA is **not** a StaticMeshActor.

**Consequences for AssetTools' `collision_physmat.py`:**
- **StaticMeshCollectionActor members** (most cooked MP / campaign world meshes):
  - a simple-hull hit with no BodySetup PhysMaterial does **not** take the first section material's PhysMaterial;
  - it falls to the hit Material (H: a hull hit reports no material) and then to **Metal**.
  - The `first_material` hull rule should apply only where step 4 applies.
- **StaticMeshActor (+ subclasses) members:** the `first_material` rule holds, refined to "first slot with a PhysMaterial".
- **InterpActor / DynamicSMActor / KActor** derive from DynamicSMActor, not StaticMeshActor (stock hierarchy, H; not read
  here), so they too fall to Metal.
- **Recommendation:** split the 65,842 `first_material` triangles by owning actor class:
  - SMCA / other → `default_metal` (H);
  - StaticMeshActor → `first_material_with_physmat` (C).
  The step-2 link (what the simple-collision line check writes into +0x2C) stays H; its writer was not traced within the 1 h cap.

### 5. Matinee group linked to an empty SeqVar_Object = runtime no-op — **CORRECTED for 23 of 148 groups** (H)

- AssetTools' manifests (`manifests/next_map/*_matinee.json`): 148 groups are `unresolved` via `object_empty` (the doc's 149;
  one differs by the disabled-track filter).
- **Track census (enabled tracks only):**

  | Track classes | Groups |
  |---|---|
  | Move only | 44 |
  | AnimControl only | 38 |
  | AnimControl + Move | 19 |
  | ActorFloatMaterialParam only | 15 |
  | Toggle only | 5 |
  | FloatProp only | 4 |
  | **with an Event track** (Event + Move 8, Event + AnimControl 4, Event + MaterialParam 3, Event + Move + Sound 1, Event + AnimControl + Move 1) | **17** |
  | **with a Sound track** (Sound only 3, + MaterialParam 1, + Move 1, + Toggle 1, + Event + Move 1) | **7** (1 overlaps the Event row) |

- **Stock `USeqAct_Interp::InitInterp` (H; RE 27d1ee0 rated it HIGH for WFC):**
  - a group with no actor still gets a group instance;
  - **Event tracks fire their Kismet outputs regardless of the group actor;**
  - **Sound tracks without an actor play at the listener.**
- **So:** the 125 groups with only actor-targeted tracks (Move / AnimControl / Toggle / material / property) are no-ops (H).
  The **23 groups with an Event or Sound track are not**: their event outputs fire (e.g. A1_IAC `EndPiece` ×4, `DataCube`,
  `Elevator`) and their sounds play.
- The manifests should keep those 23 as **live** for the event / sound tracks.

## Part 2

### 6. UseSubtitles false = the HUD shows only flagged lines — **HIGH (data)**; consumer not located

- **Where the field lives:** `UseSubtitles` is a WFC field inside each `SoundNodeWaveEx.Subtitles[]` cue (beside Text / Time).
- **Census:** 15,016 cues; **1,649 true / 13,367 false**.
- **Sampled content:**
  - true = story / mission dialogue ("Autobots--a new age is upon you! I, Megatron…", "My scans indicate that door should lead us…");
  - false = combat barks ("Let's dogpile on that guy.", "That hit the spot!", "Got some Cloakers -- look sharp!").
- **The reading fits.** The per-cue native filter (the subtitle queue, stat names QueueSubtitles / FindHighestPrioritySubtitle)
  was not traced to the field.
- The player switch is separate (C): `HmPlayerController` → `SetShowSubtitles(ProfileSettings.GetSubtitleConfigOption())`;
  `TnUIController` Paused states call `DisableSubtitles()` and restore them on exit.

### 7. 32 characters with no map-package reference load at runtime from AI character definitions — **CONFIRMED, two paths**

- **Enemies / bosses / buddies (C):** campaign maps reference them indirectly. The chain is
  `SeqAct_TnPawnFactory` (1,263) → `TnActorFactoryAI` (1,038) → **`AiCharacterDefinitionTemplate`** → `TR_AiCharacterDefinition_p.*_AICD`
  → `AiCharacterDataResolved.ChassisType` → the `TR_*_ROBODEF_p` package.
  - 53 AICDs are in use, e.g. `Trypticon_AICD`, `StarScream_Boss_AICD`, `Soundwave_Boss_AICD`, `Frenzy_AICD`, `Rumble_AICD`,
    `Spider_AICD`, `SecurityDrone_AICD`, `CQCSpecialist_*`, `Brawl_VIP_AICD`, `AirRaid_Buddy_AICD`.
  - So the ROBODEFs are reached through an object reference, not a name. A walk that follows AICD → ChassisType finds them.
- **Playable iconic characters (C):**
  - Arcee, Jazz, Slipstream, Zeta Prime, Shockwave, Onslaught, Demolishor and the others come from
    `TnProfileSettings.PresetIconicCharacters` (30 `TR_SPPlayerCharacterData_p.*_PCD_SP`).
  - Each PCD's `ChassisTypes` → the TransCustomization.ini `TnDataProvider_Chassis` sections (UniqueIconicId) → ROBODEF.
  - Each chapter's selectable set is `TransLevels.ini` `CampaignCharacters` (e.g. A4: AirRaid / Jetfire / SilverBolt).
- **Correction:** not "presumably from AI definitions". The AI ones come through AICD objects; the playable ones come through PCD_SP + the chassis ini.

### 8. D3A / D3B = Decepticon chapters 3 and 4 — **CONFIRMED (TransLevels.ini)**

| Map | MapId | PresenceId | Checkpoint ids | Thumbnails | Faction | CampaignCharacters |
|---|---|---|---|---|---|---|
| D3A_IAC_Base_m | 103 | 7 | `D3_CP*` | | 1 (Decepticon) | Megatron / Soundwave / Breakdown |
| D3B_IAC_Base_m | 104 | 8 | **`D4_CP01…D4_CP19`** | `UI_CheckpointThumbnails_p.D4_CP*` | 1 | Megatron / Soundwave / Breakdown |

So D3B is chapter 4 (matching `FMV_d4text_*.bik`); there is no `D4_` package because chapter 4 reuses the IAC level as D3B.

### 9. High Moon custom Kismet classes "with no RE decomp file" — **CONFIRMED for all 15**

- **Why there's no file:** the 15 classes have **no script functions of their own** (data-only SequenceActions / SequenceEvents), so
  `ue3dis --class` emits nothing. Their behaviour is the target's handler (actions) or the activating code (events):

| Class (package) | Behaviour lives in |
|---|---|
| SeqAct_TnAiJump, SeqAct_TnAiMoveToActor, SeqAct_TnAssignTacticalVolume (TransGame) | `TnAiController` handlers |
| TnSeqAct_DownRevive (TransGame) | `TnAiController`, `TnPlayerController` |
| TnSeqAct_ActivateTogethernessVolume (TransGame) | `TnTogethernessVolume` |
| TnSeqAct_MissionComplete (TransContent) | TransContent game code (decomp_tc) |
| TnSeqEvent_DetPack | `TnInteractActionFireDetpackEvent` |
| TnSeqEvent_Downed | `TnPawn` |
| TnSeqEvent_HealthStatus | `TnAiHealthMonitor`, `TnTankAiPawn` |
| TnSeqEvent_Interact | `TnInteractBase` |
| TnSeqEvent_OmegaStatus | `TnOmegaSupremePawn`, `TnAiActionGrab`, `TnAiActionLockOnTarget` |
| TnSeqEvent_TankStatus | `TnTankAiPawn` |
| TnSeqEvent_TrypticonEvents | `TnTrypticonPawn` |
| **SeqAct_TnAiUseTurret** (TransContent) | **own script, CONFIRMED.** It is cooked into the design sublevels that use it (A1_IAC_Strts0_Design_m, A2_KON_WallCrawler_Design_m, A4_ORB_Flying2_DSN_m). Input 0 → `SetScriptedTurrets` + UseTurret goal / action sets; input 1 → clear + reset + CancelCurrentPlan. See `CAMPAIGN_KISMET_HANDLERS_RE.md` (93790c1) |
| **TnSeqAct_AiDisableShootingBehavior** (TransContent) | **own script, CONFIRMED** (A2_KON_BASE_m). `DestroyInventory()` on both inputs, then NoWeapon_Buddy sets (0) or the template's own sets (1). See `CAMPAIGN_KISMET_HANDLERS_RE.md` |

**Correction (2026-10-18):** the first version of this item said these two handlers were not located. They were already decoded in
`CAMPAIGN_KISMET_HANDLERS_RE.md` (93790c1), and they were re-verified today from the packages above. So all 15 are resolved.

### 10. A4 `Checkpoint_14441` ("02a -Watchdog Part 2") truly air, by design — **CONFIRMED**

- **Spawn form:** the checkpoint's action `TnStartingFormOverrideCheckpointAction` has **StartingFormOverride = TF_Vehicle**.
  `Process()` → `TnCampaignGameBase.set_PendingStartingFormOverride`, so the player spawns in vehicle form.
- **Spawn points:** the auxiliary spawns are two **AirPathNodes** (`AirPathNode_2348`, `_7858`). The checkpoint itself has no
  reach specs.
- **Characters:** A4's CampaignCharacters are **AirRaid, Jetfire, SilverBolt**, all jets.
- So the restart spawns a jet in flight at (236044, 69344, 233996), by design.

### 11. FaceFX node clamp to [min, max] is the face graph's own — **HIGH** (1 h cap reached)

- **The base node (C):** `FxFaceGraphNode`, registered `0x832FDE18`, size 0x44. Its constructor `0x832FDE98` initialises
  **+0x10 = 0.0 and +0x14 = 1.0** (the node's min / max defaults) and the input-link array at +0x1C. The subclasses
  (`FxCombinerNode` vtable `0x8238D8E0`, Delta / GenericTarget / MorphTarget / BonePose) share it.
- **No clamp downstream (C, prior):** the bone pass has none (FACEFX_BONE_BLEND_RE.md).
- **Not found:** the node-value evaluator (where input links are summed and clamped). Within the cap, no FaceFX-library function pairs
  +0x10 / +0x14 loads with a compare or fsel.
- The stock FaceFX 1.x semantics (node value = Σ link outputs + track value, clamped to [min, max]) remain the HIGH reading.

### 12. CAMPAIGN_MATINEE_SCHEMA runtime semantics — see item 5

The doc's only runtime PROVISIONALs are the actorless-group rules. They are covered by item 5: the stock InitInterp behaviour is HIGH,
and the empty-object groups are corrected (23 with Event / Sound tracks are live).

### Not closable by RE (stated as such)

- **Trypticon's low jaw-voice correlation** (CAMPAIGN_FACEFX_RE): a measurement / lip-sync-timing question, not a data reading.
- **CAMPAIGN_FACEFX_RE "mapping from the data … for Gameplay to confirm on the full skeleton":** it needs the in-engine skeleton hook.
- **CAMPAIGN_STREAMING_RUNTIME.md (design note, PROVISIONAL):** our runtime design, not an original-behaviour claim.
- **CAMPAIGN_ASSET_INVENTORY's work estimate ("about a day … PROVISIONAL"):** a planning estimate.
- **Pilot / summary document titles marked PROVISIONAL:** they inherit the verdict gate. That gate is resolved by items 1 and 3
  (the A4 / D2 PASS readings hold).

**X1 tone re-check (Integration's side item):** Rendering confirmed the "intentionally not driven" list against
`RENDERING_RT_PLAN_V3.md`. No change.
