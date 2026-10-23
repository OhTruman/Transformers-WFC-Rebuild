# Q&A batch: material Power, Escalation player starts, D3B opening, SeqAct_Teleport (RE, 2026-10-23)

**Labels:** C = shipped shader microcode / script bytecode / authored data; H = stock UE3 behaviour, not traced.

## 1. MaterialExpressionPower = ClampedPow (C; asked by Rendering)
- **Source:** ENV_ForceField_Defrag_MAT (parent of WEP_Barrier_Defrag_MATINST), base-pass PS `31c1c988406c66557daa55af0431828f`
  (A2_KON_BASE_m ShaderCache; `tools/shader_by_guid.py`).
- **Every pow compiles to:** `MAXv r, |x|, cEps` → `LOG_IEEE |r|` → `MUL` by the exponent → `EXP_IEEE` (instructions 42–45, 91–99,
  117–124).
- **Literals:** **1e-6 (0x358637BD, c252.z)** and 1e-4 (c255.w). Some pows clamp max(x, 1e-6), then max(|·|, 1e-4); the 1e-4 is
  probably an authored Max node.
- **For a port:** `pow(max(abs(X), 1e-6), Y)` for **every** exponent, not only negative ones.
  - A negative base gives |X|^Y, not NaN.
  - pow(0, −0.3) is bounded at about 63.

## 2. Escalation player starts and respawns (asked by Gameplay)
**There's no SV-specific start class, tag or rule (C).**
- **Team assignment:** `TnSurvivalGame.PreGameSetupByTeam` puts every human on the CoopInfo faction; the horde is the other faction.
- **Start re-mapping:** every TnTeamPlayerStartBase gets `SetupForAttackDefend(hordeTeamIndex)`.
  - A start authored with SavedTeam 1 gets the horde's team index; the others get the human team.
  - `TnTeamGame.RatePlayerStart` returns −1 for starts on the wrong team.
- **Search:** `FindInitialPlayerStart` and `FindPlayerStart` go through `TnSpawnPointManager.FindSpawnPoint`.
  - **Which cluster:** the team's active TnSpawnCluster.
    - For the first **InitialSpawnTime 15 s** it's the InitialSpawns cluster.
    - After that, the best-scoring cluster by its modifiers. Clusters are re-scored every 0.1 s. The active one switches when another
      leads by ≥ 5, or when the current one has been active 10 s.
  - **Within the cluster:** the SpawnPoints are taken round-robin, with up to **4** tries for a safe capsule.
  - **Fallback:** the stock FindPlayerStart.
- **Facing (H, stock RestartPlayer):** the pawn and controller take the spawn point's Rotation.
  - The original camera pulls in on collision. A prop behind the start only shortens the boom; it doesn't call for a different
    start.
- **Respawns (C):**
  - Downed and revived: the player stays in place.
  - Dead: respawned at **wave end** by TnRespawnHelperRoundEnd → RestartPlayer → the same FindSpawnPoint, from whichever cluster is
    active then.
  - Survivors are revived in place.

## 3. D3B opening: nothing holds the players (C; asked by Gameplay)
**Checkpoints:**
- Checkpoint_9435 "00 - Intro Cinematic" is at (21602, −46534, −90462).
- Checkpoint_3142 "01 - Level Start" is at (21619, −46266, −90389), a few metres away.
- Both streaming states (IntroCine and LevelStart) load **D3B_IAC_BottomLedge_ART_m / _DSN / _VIG**; IntroCine adds LevelIntro_CINE.
- **So the players stand on the BottomLedge_ART ledge.** If pawns fall, that level's collision is missing in our build; it isn't a
  Kismet problem.

**The chain:**
1. PreGameplaySetup_6609 (unbound) → MoviePlayer FMV_d4text.bik → ForceTextures.
2. GameplayStarted_8938 → Switch_11696 (Link 2) → Set_CP0 → SetActiveCheckpoint(9435) → SetStreamingState IntroCine +
   StartIntroMovie.
3. StartIntroMovie (LevelIntro_CINE) runs the sub-sequence "hideplayer/cinemode":
   - **ToggleCinematicMode Enable** on the player;
   - **ToggleHidden** on the three player TnSeqVar_Characters;
   - Omega hidden;
   - then the matinee chain: planes swoop → 1st interior → omega zoom → fire back → "he's coming right at us" → "stay your course".
4. Every end or skip path fires **IntroMovieSkipped** → "unhideplayer/cinemodeoff" (cinematic mode off, unhide) + **IntroMovieDone**.
5. IntroMovieDone (Base) → **SetActiveCheckpoint(3142)** → StartLevel + SetStreamingState LevelStart.
   - BottomLedge_VIG plays "door and steam" and "Get up everyone", with its own cinematic toggle.

**There is no Teleport or AttachToActor for the players at chapter start.**

## 4. SeqAct_Teleport and TnSeqVar_Character
- **Variable links:** Target(s) and Destination. **bUpdateRotation defaults to true** (C, CDO).
- **Behaviour (H, stock):** each target, or a controller's pawn, moves to Destination.Location (collision-adjusted) and takes its
  Rotation.
- **AI destinations (C):** CheckForErrors expects a NavigationPoint destination when teleporting AI, so the AI can re-anchor. D3B's
  Lift1 and MiddleInt2 teleports use Teleporter nav points.
- **TnSeqVar_Character → `FindMatchingPawn` (C):** returns the first TnPawn in AllPawns whose `_CurrentCharacter.ChassisType` path
  equals the variable's ChassisTypePathName.
  - That's whoever plays that character, human or AI.
  - None if no such pawn exists.
- **Special cases (H):**
  - vehicle form: no script special case;
  - TnAiController's teleport hook clears scripted turrets;
  - dead or no pawn: nothing happens.
