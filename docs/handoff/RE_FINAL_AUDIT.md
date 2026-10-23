# RE final audit: every remaining H / HIGH / PROVISIONAL / PARTIAL / UNKNOWN row (2026-10-23)

**For the final-build checklist (RE section).**

**How this was built:**
- every handoff doc (83) was scanned for the label markers (334 candidate lines);
- each line was checked against later passes and corrections, and anything since closed was dropped.
- Dropped as closed: RelativeToInitial order, the Bink track selection and bins, the AnimNodeSlot flag (HIGH, kept below as such),
  the director restore blend, ActorFactory stepping, FaceFX M / clamping, the togetherness / BuddyLead items, the ObjectiveType
  "None", sort ties (msvc qsort), the backpack friendly fire and the armour pass-through.
- Pure label-definition lines are omitted.

**Columns:**
- **Label:** H = stock UE3 / inference, HIGH = strong data or engine-schema evidence without code, PROV = provisional (metric
  pending), PARTIAL = shape known, UNKNOWN = not established.
- **Impact:** **V** = visible, **A** = audible, **T** = timing / gameplay rule, **–** = none expected. Severity: ●●● likely noticed
  by a player, ●● noticeable in specific places, ● edge case.

## A. Campaign runtime (owner: Gameplay unless noted)

| # | Item | Doc § | Label | Impact | Current build default | Evidence needed to close |
|---|---|---|---|---|---|---|
| A1 | Startup events with `Originator = Checkpoint_X` fire only when starting at that checkpoint | KISMET_VM_CORE §3 | HIGH | T ●●● (wrong set pieces / spawns after a checkpoint start) | filter by the active checkpoint | decode `AWorldInfo::PerformPreGameplaySetup` (exec 0x82C4D770 → 0x82D41A70; the decompile drops the CP argument), or a runtime trace of which events fire at a CP start |
| A2 | Root (Main_Sequence) SequenceActivated events fire at level-Kismet begin | KISMET_VM_CORE §3 | HIGH | T ●●● | fire the 150 root ones | native SequenceBeginPlay / level-Kismet init trace |
| A3 | `GameInfo.ResetLevel` re-fire of SequenceActivated "whose Outer is a Level" (matches no exported event literally) | KISMET_VM_CORE §3 | H | T ●● (in-place restart only) | none re-fired (the streamed levels reload anyway) | read the raw bytecode cast target (`Level(…Outer)` vs `…Outer.Outer`) |
| A4 | Sequence +0x94 list = ActivateDelay delayed activations | KISMET_VM_CORE §4 | H | T ● | stock ActivateDelay | decode the +0x94 loop in UpdateOp 0x82F2EEE0 (vtable +0x19C on each entry) |
| A5 | Which ops run in the pre-gameplay pass (UpdatePreGameplayOp filter: `FUN_82B84440(op) != FindPackage("Engine")` defers the op) | KISMET_VM_CORE §3 | H | T ● (PreGameplaySetup chains split across one tick) | run them all at setup | identify 0x82B84440 (an index lookup on op +0x14 >> 20; likely the class's package) |
| A6 | Latent re-queue position vs the next tick's new events (push-top) | REVIEW_KISMET_VM K6 | H | T ● | latent update between the two runs | decode vtable +0x19C QueueSequenceOp(bPushTop) |
| A7 | Conversation `Face_N` (second var per line) use: face / look-at | ENDSTATE; DIALOG_TIMING | H | V ● (69 links, none in A1) | speaker turns to face Face_N | native conversation update |
| A8 | Subtitle canvas placement / font (SubtitleManager) | ENDSTATE; HUD_OBJECTIVES §4 | PARTIAL / H | V ●● (Frontend) | stock: bottom-centred title-safe, wrapped, white + shadow | native SubtitleManager draw trace |
| A9 | Subtitle on / off per line = `UseSubtitles` (story lines yes, barks no) | ENDSTATE pass 2 | HIGH (data) | V ●● (Frontend) | as stated | the native reader of the flag |
| A10 | FaceFX data-derived bone mapping | ENDSTATE | PROV | V ●●● (every talking face; D5 has 13 tracks) | the data mapping | Gameplay's full-skeleton check |
| A11 | Trypticon jaw correlation metric | ENDSTATE | PROV | V ● | data as is | metric re-run |
| A12 | Co-op bleed-out: does PlayerDamageTaken also scale the drain? (stacks multiplicatively) | SQUAD_AI §5; DIFFICULTY §2.2 | H | T ●● (25–67 s windows change by up to ×2.25) | PlayerDamageTakenDowned only | TnBuffManager modifier composition for TnDamageTypeDownedDrain |
| A13 | AddBuff with the same class and a new level replaces rather than stacks (join re-scale) | COOP_SYNC §4 | H | T ●● | replace | HmBuffManager AddBuff path |
| A14 | Completed-objectives list not replicated to co-op joiners | COOP_SYNC §2 | H | V ● (the joiner's pause-menu list) | not replicated | GRI replication list |
| A15 | Pause-menu objective list = GetActiveObjective / GetCompletedObjectives | HUD_OBJECTIVES §2 | H | V ● (Frontend) | as stated | PauseMenu_GFX AS check |
| A16 | Character select offers AI-held characters | COOP_SYNC §2 | H | T ● (Frontend) | offer them | CharacterSelect_GFX + TnUIControllerCoop AS / script |
| A17 | Checkpoint save indicator = SavingContent_GFX during the profile save; there's no checkpoint toast | HUD_ANCHORS §1 | H | V ● (Frontend) | show it while saving | the native caller of SavingContent |
| A18 | Boss: Soundwave pseudo-god at ≥ 200 UU "to revive the cassette"; Ratchet-killed failure path; tank health ≤ 50 remote; DefendAAguns pseudo-god target unbound | BOSSES §§ | H | T ●● | as documented | runtime trace / Kismet chains beyond the authored ops |
| A19 | Difficulty 0 / 1 / 2 naming (Easy / Normal / Hard) | CONTENT_DELTAS | H (UI C elsewhere) | – | as stated | – |
| A20 | Checkpoint save-body binary form and field order | ENDSTATE | H | – (our own format) | any stable encoding | not needed |
| A21 | Streaming notifies 0x36–0x38 consumers; the named-level step (+0x10 = the checkpoint's level); per-frame time-slice | STREAMING_NOTIFIES; ENDSTATE | H | V ● (loading-screen timing, Frontend) | load the checkpoint level first; stock slicing | native listener registration scan |
| A22 | MaterialInstance param actions / Matinee param tracks change the **shared MIC asset** | CAMPAIGN_LIGHTING §2 | H | V ●● (Rendering: all users of a MIC change together) | shared asset | native SetMatInst*Param |

## B. Rendering (owner: Rendering)

| # | Item | Doc § | Label | Impact | Default | Evidence needed |
|---|---|---|---|---|---|---|
| B1 | Material RandomSeed writer (FMeshElement +0xBC) | ENDSTATE pass 3 | UNKNOWN (decided 0) | V ● (2 materials: Cust_E skin, DarkEnergonCrystal) | 0 | runtime capture of +0xBC, or a typed FMeshElement among the 129 candidate writers |
| B2 | Interpolated light maps: the original's fallback for the **198 unbuilt campaign instances**; header u32s (0, 0, 3) | INTERPOLATED_LIGHTMAP | UNKNOWN | V ●● (those instances unlit / dark) | our fallback | native draw path for N = 0 |
| B3 | Lens flare occlusion latency / smoothing, radial normalisation, proxy +0xE4 / +0xE8 | LENS_FLARE §4–6 | UNKNOWN / H | V ●● | stock UE3 FLensFlareSceneProxy | WFC proxy Render / occlusion code |
| B4 | Modulate + distortion caches for BlastShell_MAT / BlastSphere_Death_MAT (not found) | MODULATE_DISTORTION_CACHE | UNKNOWN | V ●● (blast distortion on deaths) | no distortion pass | a broader shader-cache search (startup / global caches) |
| B5 | Light-visibility occlusion for the forced-dynamic light (LVV query sample points) | TIME_MATERIALS_LIGHTS §5 | UNKNOWN | V ● | unoccluded | the native callers of the LVV query |
| B6 | Material time resets per level, pauses, × TimeDilation; FlipBook own clock | TIME_MATERIALS_LIGHTS §2 | H | V ● (phase only) | world time | native view-family time fill |
| B7 | Height fog CPU fill; up to 4 layers, no culling | GORGE_RUST_HEIGHTFOG; MAP_LIGHTING | H | V ●● (Gorge 4, Rust / Berth / Seed / Complex 2) | stock 4-layer | the fog setter (not reached in the 1 h cap) |
| B8 | HDR: no per-frame exposure found; which gather variant is bound | HDR_SCENE_PATH §4 | PARTIAL | V ●● (overall brightness) | fixed exposure, the documented gather | the binder 0x82FF4040 runtime state |
| B9 | Xenos 16_16_16_16 range [−32, 32); NaN / Inf ignored in blend | HDR_SCENE_PATH; HDR_BLEND_CLAMP | H (via Xenia) | V ● | clamp, NaN → 0 | – (Xenia reference) |
| B10 | DOF blur kernel 32 on BrokenHope / Gorge (ours fixed 16) | MAP_LIGHTING | PROV | V ● | 16 | DOF kernel uniform trace |
| B11 | Character light environment EnvironmentTransitionTime 0.5 s blend not modelled | MAP_LIGHTING §3 | H (stock DLE) | V ● | instant | – (implement stock) |
| B12 | Material 4th-row / TEXCOORD1-2 lane offset; CameraWorldPosition fold | MATERIAL_GAPS | H | V ● | as documented | per-shader CTAB check |
| B13 | Decal UVs from CPU-clipped decal geometry; non-flare VF defaults (intensity 1, occlusion 1) | MATERIAL_NODES | H | V ● | stock | – |
| B14 | DLE / static-light interaction for "irrelevant lights" lists | INTERPOLATED_LIGHTMAP | H | V ● | stock | – |

## C. Systems: audio, FX, particles (owner: Systems)

| # | Item | Doc § | Label | Impact | Default | Evidence needed |
|---|---|---|---|---|---|---|
| C1 | FMV mix-bin → speaker names (bins 0 1 4 5 3 2 = FL FR SL SR LFE C) | ENDSTATE pass 6 | H (bins C) | A ●● | Bink Xenon order FL FR C LFE BL BR | the Bink Xenon mix-bin enum / XAudio channel map |
| C2 | FMV volume source (params +0x14) | ENDSTATE pass 6 | H | A ● | user volume settings | the caller filling the movie params |
| C3 | FMV subtitle cue units 1/100 s; rect order X1 Y1 X2 Y2; held last frame until Stop | FMV_PLAYBACK | H | V ● (Frontend) | as stated | the native subtitle parser / player |
| C4 | Reverb: one at a time (last entered), linear crossfade of MASTER_WET, same-category priority combine | MP_REVERB_ROOMS | H | A ●● | as stated | the native reverb / mixer update |
| C5 | Ability cue replication (stock PlaySound replication, owner-only buffs, anim-driven notifies) | MP_ABILITY_CUES | H | A ● | as stated | – (stock) |
| C6 | Particle `inst[+0x94]` gravity scalar (use −2940) | PARTICLE_MODULE_AUDIT | UNKNOWN / H | V ●● (particle fall speed) | −2940 | the writer of the emitter-instance +0x94 |
| C7 | Particle orbit / velocity overrides; DirectSet LOD only via SetLODLevelIndex | PARTICLE_MODULE_AUDIT | H | V ● | stock | – |
| C8 | Gameplay setters of particle colour params (team / character colours) | PARTICLE_MODULE_AUDIT | H | V ● | as wired | a caller survey |
| C9 | Molten blue arc / Trail2 verdict | MOLTEN_BLUE_ARC_TRAIL2 | H (reasoned) | V ● | as stated | runtime check |
| C10 | Skeletal gib spawn rotation; SetSkeletalMesh impl partly truncated | SKELETAL_GIB_INIT | HIGH | V ● | as stated | – |
| C11 | Voice: an FFA player has no Team object → channel −1 | ONLINE_POSTMATCH §3 | H | A ● | all hear all in FFA | TnFreeForAllGame team setup check |

## D. Gameplay: MP / Escalation / vehicles (owner: Gameplay)

| # | Item | Doc § | Label | Impact | Default | Evidence needed |
|---|---|---|---|---|---|---|
| D1 | Tank armour collision after a piece breaks (final state has no mesh) | HORDE_AI §6 | H | T ● | exposed | the destructible final-state collision |
| D2 | Horde never takes weapon pickups (no pickup goal) | E2B §1 | H (goal side) | T ● | never | goal-set survey (no pickup goal found) |
| D3 | Contextual-command priority when generator and pickup commands are both registered | E2B §5.3 | H | T ● | pickup first | ContextualCommander priority |
| D4 | Round-gate zone: the first player leaving turns it off (per-pawn Touch) | E2B §4 | H (stock) | T ● | per event | stock SeqEvent_Touch |
| D5 | Heavy-weapon activation on pickup / switch-to-best when empty | E2B §5.2 | H | T ● | stock | – |
| D6 | Decoy trap: the kill-award source; stun ×1.5 multiplicative; pickup-triggered | MP_DECOY_TRAP | UNKNOWN / H | T ●● | as stated | native kill credit / buff modifier composition |
| D7 | Hover ride height: tank mass from PHYSSYS | HOVER_RIDE_HEIGHT | PARTIAL | T ●● (tank hover height) | the documented formula | the PhysX body mass |
| D8 | Vehicle death: jet fall trajectory, collision channels, damage ignored while Dying | VEHICLE_DEATH_PRESENTATION | H | V ● | as stated | runtime |
| D9 | Guided missile PHYS_Projectile behaviour; no HUD for others | MP_GUIDED_MISSILE | H | V ● | stock | – |
| D10 | Idle kick: stock PlayerTick check; the host's local player is exempt | MP_INPUT_CONTRACT | H | T ● | stock | – |
| D11 | Jet transform blend 1.0 s mapping; melee variant chosen at random | PRESENTATION_AUDIT_2 | H | V ● | random | anim chooser trace |
| D12 | Weapon camera shake per round in a burst | WEAPON_CAMERA_SHAKE | H | V ● | per round | – |
| D13 | Pickup spin continuous; per-weapon PickupMesh overrides | PICKUP_IDLE_MOTION | H | V ● | continuous | – |
| D14 | Killer cam: FFA all players valid; AI killers skipped | KILLER_CAM | H | V ● | as stated | – |
| D15 | Escalation: the threat −1 per attacker is the only player-count effect | ESCALATION_WAVES §4 | H | T ● | as stated | – |
| D16 | PC aim-assist rules absent from the 360 dump | PLAYTEST_2026-10-23 | H | T ●● (a PC design decision) | ours | the PC build, if ever available |
| D17 | Combat authority: RPC reliability flags; view-disagreement resolution | MP_COMBAT_AUTHORITY | H | T ● | as stated | – |

## E. Online / frontend (owner: Frontend)

| # | Item | Doc § | Label | Impact | Default | Evidence needed |
|---|---|---|---|---|---|---|
| E1 | Quick-match host order = Live's search ranking (no ping round) | ONLINE_PC_DEVIATIONS 4 | H / LIVE | T ● | ours (relay) | – (LIVE) |
| E2 | Host drop → error shown, then main menu / party lobby | ONLINE_POSTMATCH §2 | H | V ● | as stated | the GameEngine error display path |
| E3 | Join-in-progress near match end / beacon after ReturnToGameLobby; kill feed has no history | ONLINE_JOIN_IN_PROGRESS | H | T ● | as stated | – |
| E4 | Party: failed member join; leader-loss message (none) | ONLINE_PARTY §1 / §4 | H | V ● | none | – |
| E5 | Iconic-mode / GameTeamStatus condition for the EndGameStats header in Escalation | MP_ESCALATION §8.4 | H | V ● | GameOverMessage when set | EndGameStats AS condition |
| E6 | HUD sprite 589 in every anchor (safe-area marker) | HUD_ANCHORS §2 | H | V ● | hidden | AS / display-list check |
| E7 | Menu sounds depend on the Scaleform host running the AS | PRESENTATION_AUDIT_3 | H | A ● | – | Frontend host check |
| E8 | `PlayerRestartDelay 6.0` (ini) is dead config | ONLINE_RULES_CHECK | H | – | ignore | – |

## Summary
- **Highest impact still open:**
  - A1 / A2: which Kismet startup events fire. HIGH by data, and it affects every chapter and checkpoint start.
  - A10: the FaceFX bone mapping (every talking face).
  - B2: 198 unbuilt light-map instances.
  - B3 / B4: lens-flare timing, and the blast distortion caches.
  - B8: HDR exposure / gather.
  - A12: the co-op bleed-out stacking.
  - C1: FMV speaker naming.
  - C6: particle gravity.
- **Everything else is ● edge cases or stock behaviour with documented defaults.**
- **None blocks a build.** Each row's default is what ships unless the evidence lands.
