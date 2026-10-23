# Campaign RE end state: what the campaign build still assumes (2026-10-23)

This is every item across the campaign RE docs that is still **H** (stock UE3 or inference, not read in WFC code) or **UNKNOWN**,
after the 2026-10-23 pass. Everything not listed here is CONFIRMED in its doc.
The status column says what the build should do today.

## Closed in the 2026-10-23 pass (now C)
- Matinee:
  - curve evaluation (broken-tangent default: no Δ scaling);
  - quaternion / Euler rotation;
  - Lookup keys;
  - IMF_World frame;
  - director cut / restore params;
  - sound termination;
  - AnimNodeSlot MAT_SetAnimPosition / Weights;
  - AnimControl GetAnimForTime (MATINEE_NATIVE §1–7, 9, 11).
- Togetherness volume / events and the BuddyLead emote finder (TOGETHERNESS_BUDDY).
- Conversation native timing (DIALOG_TIMING).
- Callback ids 0x31–0x38 and the named-level step (STREAMING_NOTIFIES).
- Checkpoint body serializer (CHECKPOINT_BODY).
- ActorFactory / PawnFactory stepping (ACTORFACTORY).
- FaceFX M quaternion and face-graph clamping (FACEFX_GRAPH).
- PostProcessVolumeHeightFog (PPV_HEIGHTFOG).

## Still open

| Area | Item | Label | Build assumption | Doc |
|---|---|---|---|---|
| Matinee | IMF_RelativeToInitial matrix order (VMX code) | **C** (pass 5) | Row-vector convention: **InitialTM = KeyTM(t0)⁻¹ × (ActorTM × BaseTM⁻¹)**, RemoveScaling; **RefTM = InitialTM × BaseTM**, RemoveScaling; **World = KeyTM(t) × RefTM** | MATINEE_NATIVE §10, ENDSTATE pass 5 |
| Matinee | slerp shortest-path for bUseQuatInterpolation | **C (0x82B82FA8)** | shortest path: second weight negated when dot < 0; plain lerp weights (1 − α, α) when \|dot\| ≥ 0.9999 (0x3F7FF972); no renormalize before the quat → rotator (0x82B8A0F0). A1 uses it on 311 move tracks | MATINEE_NATIVE §2 |
| Matinee | Lookup-key end tangents | **C (0x82BFFD80)** | first / last key → tangents zeroed (memset 12 bytes); interior keys Catmull-Rom with (1 − Tension) · 0.5 | MATINEE_NATIVE §3 |
| Matinee | groups bound to empty SeqVar_Objects (165) | C mechanism / H cause | actor tracks no-op; sound / event / fade tracks still run | MATINEE_NATIVE §7, PROVISIONALS_CLOSED §5 |
| Matinee | AnimNodeSlot "skip weights" flag (+0xC4 bit 31) meaning | **HIGH** | **AnimNodeSlot has exactly one BoolProperty, `bIsPlayingCustomAnim`** (Engine.xxx class children); bit 31 = the first declared bool. So Matinee's MAT_SetAnimWeights skips a slot that **script is driving with PlayCustomAnim**. Build: skip the Matinee weight write while the slot plays a custom anim. Missing for C: the parent-class layout confirming +0xC4 is AnimNodeSlot's first field | MATINEE_NATIVE §9 |
| Dialog | subtitle canvas placement / font | H | stock: highest-priority line, centred bottom title-safe, word-wrapped, white with shadow | DIALOG_TIMING §5 |
| Dialog | UseSubtitles = false lines hidden | HIGH (data pattern) | show only lines with UseSubtitles. The English wave cues have 1,648 true vs 8,697 false: true = story conversation lines, false = combat barks (reload / grenade / taunts). The native consumer (cue +0x10 bool in the subtitle queue) wasn't located; there's no name string in the exe | PROVISIONALS_CLOSED §6 |
| Dialog | conversation's second variable per line | **C (data) + H (use)** | VariableLinks come in pairs per line: `Speaker_N` (the speaking actor, required by CheckForErrors), then **`Face_N`**. Face is linked in only 69 links across the whole campaign, **0 in A1**. H: the speaker faces / looks at that actor. Implement as an optional look-at | DIALOG_TIMING |
| FMV | Bink audio-track selection; speaker mapping; held last frame | **C (selection)** / H (speakers, hold) | **Play tracks 0, 1, 2, 3, 4 + (5 + language)**: INT 5, FRA 6, ITA 7, DEU 8, ESN 9, RUS 10, POL 11 (see pass 3). Never mix all ten. H: tracks 0–4 = the shared bed; the speaker layout isn't traced | FMV_PLAYBACK |
| FMV | subtitle cue times in 1/100 s; rect order X1, Y1, X2, Y2 | H | as stated | FMV_PLAYBACK |
| Streaming | listeners of callbacks 0x36 / 0x37 / 0x38 | not found | use as "restart requested / restore done / mid-restore" UI hooks | STREAMING_NOTIFIES |
| Streaming | named-level step strings: +0x10 = the checkpoint actor's level | H | load that level visible (blocking) first | STREAMING_NOTIFIES |
| Streaming | per-frame streaming time-slice budget | H | stock AddToWorld time-slicing | STREAMING_RE |
| Checkpoint | binary form of struct array items in the body | H | any stable encoding (no byte compatibility needed) | CHECKPOINT_BODY |
| Checkpoint | TnCheckpointPlayerData field order | H | declaration order | CHECKPOINT_DATA §2 |
| AI | hit-physmat fallbacks (override, simple-collision hit) | H | as PROVISIONALS_CLOSED §3 | PROVISIONALS_CLOSED |
| AI | pawn falls when spawned without a floor | H | PHYS_Falling | PROVISIONALS_CLOSED |
| Kismet | stock-native actions: math / compare / counter / set-material / interp-float shape (CurveExponent ease) | H (stock) | stock UE3 semantics | KISMET_WFC_CLASSES |
| Kismet | latent ForceIntoForm finish; pseudo-god; IK look-at bones | **C / C / H** | **ForceIntoForm** (TnSeqAct_ForceIntoForm.Update, TnPawn.OnForceIntoForm): already in form → nothing; else MoveToSafeTransformationLocation → ForceIntoForm (PC.ForceToForm / AiController.Transform); a target that isn't in form and isn't transforming → **AbortFor** (Aborted); the latent finishes when every target is in the form. **Pseudo-god** (Controller.OnTogglePseudoGodMode inputs On / Off / Toggle; GameInfo.ReduceDamage): damage ≥ Health is clamped to **Health − 1** (never dies; that hit skips InvManager.ModifyDamage), and TnPawn refuses insta-gib while in (pseudo-)god. IK look-at bones: still H | KISMET_WFC_CLASSES |
| Kismet | ObjectiveType None matches all | **C: corrected** | an unset ObjectiveType is the **CDO default `TnObjectiveMarkerTypeObjective`**, not None. Add / Remove pass it through, and `TnObjectiveManager.RemoveObjectiveMarker` removes only entries with the **same MarkerBase and the exact same MarkerTypeClass** (replicated 16 slots + local list) | KISMET_WFC_CLASSES |
| HUD | which anchor clip each campaign HUD element sits under; pause-menu objective list | H | Frontend's layout | HUD_OBJECTIVES |
| FaceFX | Trypticon jaw correlation | PROVISIONAL (metric) | data is fine | FACEFX_RE |
| FaceFX | the data-derived bone mapping (needs a full-skeleton check) | PROVISIONAL | Gameplay to confirm | FACEFX_RE |
| Materials | RandomSeed (FMeshElement +0xBC): the reader is C (material PS SetMesh 0x82E982E8 uploads it), the **writer is UNKNOWN** | UNKNOWN | 0 = the first atlas variant | MATERIAL notes / matc.py |
| Materials | 4th-row / TEXCOORD1-2 lane notes; CameraWorldPosition fold | H | as MATERIAL_GAPS | MATERIAL_GAPS |
| Content | difficulty 0 / 1 / 2 = EASY / MEDIUM / HARD | **C** | the UIText.Difficulty strings, profile default 1 (CAMPAIGN_DIFFICULTY_RE). The MP iconic list: unchanged | CONTENT_DELTAS |
| Pipeline | reachFlags 2 = R_FLY airborne exemption | C (data) / was PROVISIONAL | keep | PROVISIONALS_CLOSED §1 |
| Pipeline | physmat simple-hull rule; default Metal fallback | H / C | keep | MP_COLLISION_PHYSMAT |

## Not RE (data or pipeline gaps, tracked elsewhere)
Pickup factories in design sublevels, the BSP default-face grids, the matc pass for the remaining campaign materials, the airborne
player starts: CAMPAIGN_BACKLOG (AssetTools / Rendering).

## Pass 2 (2026-10-23): A1 first

**Closed to C:**
- quat slerp;
- Lookup end tangents;
- the ForceIntoForm finish rule;
- pseudo-god;
- ObjectiveType (corrected: the default is the Objective marker type, and removal needs an exact match);
- the difficulty names;
- the conversation's second variable (data: `Face_N`, unused in A1).

**UseSubtitles:** raised to HIGH by the data pattern.

**What A1 actually uses** (authored.db, InterpTrackMove in A1 packages):
- 1,163 move tracks, of which 1,078 RelativeToInitial, 311 quaternion and 1,147 with lookup entries;
- FaceFX tracks 25.

So the remaining A1-relevant open item is **IMF_RelativeToInitial order** (H): VMX code that Ghidra can't decode; it affects only
groups whose actor has a Base.

**Missing evidence per remaining A1 row:**
- **RelativeToInitial order:** a VMX128-capable disassembly of the move-frame code (0x82C010B8 callee), or a runtime capture.
- **Streaming listeners 0x36–0x38 and the named-level step:** the listener objects are registered dynamically. Finding them needs
  the vtable of the object at the notify target (one more Ghidra pass: who calls the register at device +0x20 with ids 0x36–0x38).
- **The streaming time-slice budget:** the engine's per-frame budget constant in 0x82D5FB28's caller. Gameplay doesn't need it
  (stock is fine).
- **The checkpoint body binary / field order:** not needed (no byte compatibility).

## Pass 2: D5_OME

**What D5 uses** (authored.db, D5 packages):
- 101 move tracks (22 RelativeToInitial, 2 quat, 99 lookup);
- 13 FaceFX tracks;
- 42 conversations;
- **3 Bink movies:** FMV_d5itext (+ _INT / _FRA), FMV_d5otext (+ _INT / _FRA), and **FMV_d5outro.bik** (TnSeqAct_MissionComplete).

**Row by row:**
- **AnimNodeSlot skip flag:** HIGH (above). Applies to D5's 155 AnimControl tracks.
- **FMV audio:** new data (C, the Bink header of FMV_d5outro.bik):
  - 1280×720, 1,277 frames;
  - **10 audio tracks, all mono 48 kHz, ids 0–9** (flags 0x5000).
  - The text-overlay movies come as separate _INT / _FRA files, so on-screen language is per file.
  - **Still UNKNOWN:** which of the 10 tracks the game enables (H: a 5.1 bed + per-language voice). Missing evidence: the full-screen
    Bink player's sound-track selection call. Its strings exist (`HmCreateRenderingThreadFullScreenBinkMoviePlayerCommand`,
    UCodecMovieBink) but static analysis resolved no references; it needs a dynamic trace or a manual vtable walk.
  - Build: mix all 10 at equal gain until confirmed. It's audible but safe.
- **RandomSeed writer:** UNKNOWN, but its scope is now known. **Only two materials in the whole game use
  MaterialExpressionRandomSeed:** `TR_AllShader_p…CHR_Transformer_NormSpec_Cust_E_Mat` (custom-character skin) and
  `ENV_AllShader_p…DarkEnergonCrystal_MAT`.
  - D5 is affected only if its characters use Cust_E. Build default 0 (the first variant).
  - Missing: the FMeshElement +0xBC writer (a store scan over the mesh-element fill paths).
- **FaceFX bone mapping:** PROVISIONAL. D5's 13 FaceFX tracks drive Megatron / Soundwave / Breakdown faces; it needs Gameplay's
  full-skeleton check. The Trypticon jaw row doesn't apply to D5.
- **RelativeToInitial order:** 22 tracks in D5, same open item as A1.

## Pass 3 (2026-10-23): Bink sound-track selection (C)

**The Bink library in the exe:**
- `BinkOpen` = **0x83697C30** (both "Error reading Bink header." / "Not a Bink file." loads).
- `BinkSetSoundTrack(count, ids*)` = **0x836977C0**: caps count at 32, copies the ids to the globals 0x838F7AA4 (count) / 0x838F7AA8
  (ids).
- BinkOpen without flag **0x4000** (BINKSNDTRACK) forces **count 1, track 0**.

**Three callers:**
1. **The full-screen / FMV path** (0x82CE0F00): `BinkSetSoundTrack` with the list built by **0x82CDD4B0** =
   **[0, 1, 2, 3, 4, 5 + Lang]**.
   - `Lang` = 0x82CBEEE0 on the current language code: **FRA 1, ITA 2, DEU 3, ESN 4, RUS 5, POL 6, otherwise (INT / JPN) 0**.
   - The strings are at 0x821F6554–0x821F6588 and 0x82250FA4.
2. **The texture-movie paths** (0x82C078D0 flags 0x1104400; 0x82BE81C0 flags 0x4000400): `BinkSetSoundTrack(0, 0)` with
   BINKSNDTRACK, so **no audio tracks**. In-world movie textures are silent.

**The data fits:**
- the story FMVs (FMV_a1intro / a1outro / a2outro / d5outro, …) have **10 mono 48 kHz tracks**, i.e. 0–4 + five language voice tracks
  5–9 (INT, FRA, ITA, DEU, ESN);
- the per-language text movies (`*_INT.bik` / `*_FRA.bik`) have **6** tracks (0–5).

**For the build (English):** play **tracks 0–5** of every FMV.
- H: tracks 0–4 are the shared music / effects bed (five mono channels; their speaker assignment isn't traced, so a front L / R / C +
  surround L / R guess is reasonable), and track 5 is the English voice.
- **Never play 6–9** (the other languages).
- The per-language text movies get the same list. In French that would request track 6, which doesn't exist in their 6-track
  files (H: no voice there).

## Pass 3: RandomSeed writer (still UNKNOWN; narrowed)

**The reader** (C, 0x82E982E8, the material pixel-shader SetMesh):
- if the shader has the RandomSeed parameter (shader-param slot +0x7C), it uploads **one float4 register = `*(u32*)(MeshElement + 0xBC)`**,
  raw bits as stored.
- Next to it, `MeshElement + 0xC0` bit 30 selects the ±1 "two-sided sign" constant.

**No data source:**
- no authored property anywhere holds a seed (authored.db props scan);
- no Engine / HM_Engine / TransGame class has a RandomSeed-like property for primitives (only SpeedTree / foliage / terrain seeds,
  which these materials don't use).
- So the writer is native mesh-element setup code.

**The candidate set** (`opscanr` stores to both +0xBC and +0xC0 in 0x82D00000–0x83100000): **129 functions** (list in
`work/q/rs_s2.txt`, not committed). Static analysis can't pick the right one without the FMeshElement type.

**Missing evidence:** a runtime capture (PIX / memory watch on +0xBC of the mesh element passed to 0x82E982E8), or typing FMeshElement
in Ghidra and following the static / skeletal proxy element fills.

**Impact is small:** only two materials use it (`CHR_Transformer_NormSpec_Cust_E_Mat`, `DarkEnergonCrystal_MAT`).
- Build: keep **0** (every instance shows the first variant).
- Or, as a **PC adaptation**, a stable per-component hash, if Rendering prefers visible variety. Label it as such.

## Pass 4: every chapter, which open / closed rows it exercises (C, authored.db; `work/handoff/campaign_endstate_usage.json`)

| Chapter | move tracks | RelativeToInitial | quat | AnimControl | FaceFX | conversations | ForceIntoForm | pseudo-god toggles | AddMarker | movies | MissionComplete |
|---|---|---|---|---|---|---|---|---|---|---|---|
| A1_IAC | 1163 | 1078 | 311 | 1047 | 12 | 72 | 14 | 0 | 48 | 2 | 1 |
| A2_KON | 720 | 638 | 183 | 661 | 24 | 134 | 8 | 0 | 33 | 1 | 1 |
| A3_COR | 617 | 333 | 112 | 695 | 9 | 99 | 16 | 3 | 48 | 1 | 1 |
| A4_ORB | 762 | 460 | 224 | 345 | 3 | 95 | 16 | 0 | 50 | 2 | 1 |
| A5_TRY | 324 | 154 | 29 | 295 | 5 | 25 | 0 | 0 | 15 | 3 | 1 |
| D1_ORB | 759 | 469 | 74 | 741 | 19 | 56 | 5 | 0 | 35 | 2 | 1 |
| D2_COR | 736 | 547 | 178 | 247 | 2 | 81 | 3 | 0 | 30 | 1 | 1 |
| D3A_IAC | 516 | 340 | 66 | 796 | 14 | 155 | 18 | 5 | 62 | 1 | 1 |
| D3B_IAC | 523 | 144 | 39 | 456 | 19 | 118 | 12 | 1 | 59 | 1 | 1 |
| D5_OME | 101 | 22 | 2 | 77 | 13 | 42 | 0 | 1 | 4 | 2 | 1 |

(Pseudo-god here counts SeqAct_TogglePseudoGodMode only; TnSeqAct_SetAiGodStatus GOD_PseudoOn is separate, CAMPAIGN_BOSSES.)

**The remaining open rows don't depend on the chapter.** Every chapter exercises the same set:
- **RelativeToInitial order** (H, only Base-attached groups);
- **FMV speaker layout** (H);
- **the RandomSeed writer** (UNKNOWN; only Cust_E characters and the dark-energon crystals in D1 / D3A);
- **the FaceFX bone mapping** (Gameplay);
- **streaming listeners 0x36–0x38 and the named-level step** (not needed);
- **the checkpoint binary / field order** (not needed);
- **the HUD anchor clips** (Frontend).

Everything closed in passes 2–3 applies to all chapters.

## Pass 5 (2026-10-23): IMF_RelativeToInitial matrix order (C, VMX128 decoded)

**Method:** `tools/vmx128dis.py` is a minimal VMX128 decoder for the blocks Ghidra marks "bad instruction data" (lvx128 / stvx128,
vmrghw128 / vmrglw128, vdot4fp128 = vmsum4fp128, plus the common integer / float ops), following the public Xenia opcode tables.
- Each product was checked by **symbolic execution** of the decoded block: named matrix elements in, the stored results read back.
- Every product has the same shape: a 4×4 transpose of the right operand by two merge passes, then 16 dot4 of the left operand's rows
  against those columns, then merges back to rows. That is `Out[i][j] = Σk L[i][k]·R[k][j]`, i.e. **Out = L × R**, row-major,
  row vectors (UE3 FMatrix).

**The three steps** (offsets: actor Location +0xD8, Rotation +0xE4; track instance InitialTM +0x50, InitialQuat +0x90):

1. **CalcInitialTransform 0x82C3B260** (the instance setup; Ghidra has no function there, found by a raw `bl` scan):
   - `ActorTM = RotTrans(Actor.Rotation, Actor.Location)` (0x827913B8);
   - `BaseTM` = 0x82BF2478 (the Base actor, or its bone, with scaling removed; identity without a Base);
   - `A = ActorTM × BaseTM⁻¹` (0x827912B8 = inverse);
   - the track's **local key at time t0** through the vtable +0x144 (GetKeyframe position / rotation). **t0 = 0 when bForceStartPos,
     else the SeqAct_Interp's current Position** (+0xCC), 0 with no SeqAct;
   - `InitialTM = KeyTM(t0)⁻¹ × A`, then **RemoveScaling (1e-8)** (0x82791350: normalizes rows 0–2 when |row|² > 1e-8);
   - `InitialQuat = FQuat(InitialTM)` (0x82B82738) → +0x90.
2. **GetMoveRefFrame 0x82C010B8** (vtable +0x14C):
   - IMF_World → `RefTM = BaseTM` (no RemoveScaling);
   - **IMF_RelativeToInitial → `RefTM = InitialTM × BaseTM`, then RemoveScaling (1e-8)**.
3. **Key → world 0x82C00CC0** (vtable +0x150): `KeyTM(t) = RotTrans(key rot (rotator → quat 0x82B82078), key pos)`;
   **`World = KeyTM(t) × RefTM`**. The location and rotation are then extracted from World.

**Consequences (C):**
- At t = t0 the actor stays exactly where it was: `KeyTM(t0) × KeyTM(t0)⁻¹ × ActorTM × BaseTM⁻¹ × BaseTM = ActorTM`.
- The keys are applied **in the actor's initial local frame**, so a key translation (x, 0, 0) moves along the actor's initial
  forward axis, and key rotations compose before (to the left of) the initial rotation.
- With a Base, the frame follows the base's current transform every evaluation (BaseTM is re-read in GetMoveRefFrame), so an attached
  actor's relative animation rides on a moving base.
- **Key times aren't rebased:** a Matinee started mid-way (the SeqAct Position isn't 0, without ForceStartPos) captures InitialTM so
  that the current pose equals the key at that position.

This closes the last chapter-independent H in the Matinee move path. No runtime capture is needed. If Gameplay wants a regression
check anyway: at Matinee start, record an actor's Location / Rotation, the Base transform, and the first evaluated world transform of a
RelativeToInitial group with a Base (A1 DecoInt robot arm). The world transform must equal the start pose.

## Pass 6 (2026-10-23): FMV speaker layout (C for the routing, H for the bin names)

**Routing:** after BinkOpen, the FMV path (0x82CE0F00) calls **0x82CCA028(bink, trackList, volume)**. For every enabled track it calls
BinkSetVolume (0x8369A6E8, the track's +0x164 backend hook) and **BinkSetMixBins (0x8369A780, +0x178) with a single bin**:

| Bink track | mix bin | speaker (Xenon channel order FL FR C LFE BL BR, H) |
|---|---|---|
| 0 | 0 | front left |
| 1 | 1 | front right |
| 2 | 4 | back / surround left |
| 3 | 5 | back / surround right |
| 4 | 3 | LFE |
| 5 + language (the voice) | 2 | **center** |

- So the **track order is FL, FR, SL, SR, LFE, C(voice)**, which matches the order Systems cited.
- The bin numbers are C. Their speaker names assume Bink's standard Xenon speaker order (H). The internal consistency (the voice
  goes to bin 2 and the one sub-like track to bin 3) supports it.

**Volume (C):**
- `volume = (int)(params->Volume [+0x14] × 65536)`, clamped to 0..65536.
- **Any movie listed in `[Engine.MovieSettings] MoviesToAlwaysPlaySound` gets 0xCCCC (0.8) instead**: Logo_Activision,
  Logo_Hasbro, Logo_HighMoon (Xe-TransEngine.ini).
- H: the source of the +0x14 volume (likely the user's volume settings) isn't traced.
