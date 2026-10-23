# Review: Escalation E1, the survival director (integration effecacc, Gameplay d89a41f0) vs the original (RE, 2026-10-23)

**Checked:**
- `src/game/WorldSurvival.cpp`, `Survival.h`;
- `tools/gameplay/gen_escalation.py`;
- `data/gameplay/escalation_MP_ESC_{BrokenHope,Remnant}.json`.

**Against:**
- the TnSurvivalGame bytecode (`work/script/decomp_tc/MP_ESC_BrokenHope_BASE_m.TransContent.txt` 1199–1470);
- the TnSurvivalSpawnFinder bytecode (989–1160);
- `notes/data/escalation/*.json` (the RE export);
- MP_ESCALATION_RE §1–9.

The known PROVISIONAL items (bot bodies, health, weapons, power, generators, downed, buffs) are not findings.

**Verdict: correct except one timing finding (F1). F2–F3 are hooks the later E-items need.**

## Matches (C)
- **InWave.BeginState:** IncrementWave (clamped to the last row), TotalUsedThisWave = 0, FillingWave = true, and the first enemy at
  once.
- **InWave.Update order:**
  1. CheckLivingPlayers;
  2. budget spent → BetweenWaves only once no enemy is alive, with no filling meanwhile;
  3. AliveCost ≤ BudgetDeficitThreshold re-arms filling;
  4. one FillOutWave per FillWaveReCheckTime;
  5. filling stops at AliveCost ≥ BudgetAlive.
- A failed spawn just waits for the next recheck. TotalUsedThisWave may overshoot the budget, as in the original.
- **GenerateListOfPossibleEnemies:** the MinWave / MaxWave (0 = open) window on CurrentWave, LivingCount < MaxCount, and the
  first entry below MinCount chosen exclusively, else a uniform pick.
- **BetweenWaves:** the dead respawn at the start (RoundEnded); TimeBetweenWaves from 0.
- **AllDead:** every human dead → EndGame (no winner).
- **The spawn finder** (C, as answered 2026-10-23):
  - score = current + universal + self[type];
  - ∓999 per touching pawn, ZoneChanges ×±1;
  - re-sort only the named types (the stale-order quirk), using the export's per-entry type list exactly as AffectedTypes does;
  - top 3 weighted by |score| with no floor, a fall-through → index 0, total 0 → the first zone;
  - 5 tries, each re-rolling the zone and advancing that zone's round-robin point; a 0-point zone spends the try;
  - SeeEnemy on every player.
- **Touches:** human player pawns only (a dead pawn untouches).
- **Data:** generated = RE export for both maps.
  - PreGameTime 30 / 10; TimeBetweenWaves 25 / 25; FillWaveReCheckTime 0.5 (the CDO default) / 3.0.
  - Waves 17 / 20, all Budget / BudgetAlive / Deficit identical; enemies 13 / 9 with every field identical; zones 30 / 52.
  - Active flags (BrokenHope 23 active, Remnant 20; the class default Active = true, explicit false on the rest), self-scores,
    ZoneChanges.
  - PossibleSpawnPoints counts = AssetTools spawn-point positions per zone; the authored points are used, not the PROV ring.
  - The 0-point zones match authored.db (PossibleSpawnPoints empty). Remnant has 13 such zones active with all-zero
    self-scores, which only get picked when they're raised by ZoneChanges or when the top-3 total is 0. Faithful.

## Findings

**F1 (timing, C for the mechanism, H for the exact offset): the PreGame clock starts at level start, not at match start.**
- The PreGame and BetweenWaves clocks are `TnSurvivalGame.ElapsedTime`, a variable incremented by **the class's global `Tick`**
  (not a state function), so it also runs during the pre-match PendingMatch countdown.
- The first reset is in `FillOutWave` / `BetweenWaves.BeginState`. `PreGame.Update` tests
  `HordeTeam && FirstPlayerSpawned && ElapsedTime >= PreGameTime`.
- **So wave 1 starts at about max(StartMatch, level start + PreGameTime)**, not PreGameTime after StartMatch.
  - With TnSurvivalGame's MatchAutoStartCountdown of 15 s: **BrokenHope ≈ 15 s after the match starts (30 − 15)**; **Remnant
    (10 s) at once**, as soon as the first player has spawned.
  - Ours counts `phaseTime` only while the match is InProgress: 30 / 10 s after the start.
- **Fix:** run the PreGame clock from the level's first sim tick (PendingMatch included); keep the BetweenWaves clock as is.
- Also: `FirstPlayerSpawned` is a latch (set at a player's first spawn), whereas ours requires a human alive at that moment.
  That only matters if everyone dies before wave 1, which is impossible with no horde.

**F2 (hook for the generators item; Remnant depends on it):**
- 32 of Remnant's 52 zones start inactive, and **generator "New Power Level" Kismet toggles them** (TnSeqEvent_SurvivalGenerator
  → SeqAct_Toggle on TnSurvivalZoneVolume; MP_ESCALATION_RE §5, the Remnant table).
- **Original:** Toggle → `NotifyZoneActivated` (add to Zones, **SortAllArrays: every type**) / `NotifyZoneDeactivated` (remove,
  re-sort all).
- Their CurrentScore / universal adjustments keep accumulating while the zone is inactive: AddToZoneScore and touches still
  apply. Ours already stores them per zone, so only an activate / deactivate entry point plus a full re-sort is needed.
- Without it, Remnant runs on its 20 starting zones forever.

**F3 (hook for Kismet):**
- `ActivateSequenceEvent(1)` at InWave.BeginState and `(0)` at RoundEnded fire **TnSeqEvent_SurvivalEvents "Round Begin" /
  "Round End"**.
- BrokenHope uses them: a TransformWorldProp, remote events, a delay.
- Expose wave start / end events to the Kismet runner when map Kismet runs in Escalation.

**Notes (no change required):**
- **Spawn placement:**
  - ours snaps the authored point to the ground and rejects any live pawn within 2 m + radius + 1 m;
  - the original lifts the point by |enemyHalfHeight − pointHalfHeight| and uses its LocationValidator (`IsSafeSpawnLocation`,
    native: H).
  - The same in practice for points on the floor.
- **Sort ties:** stable sort here; the original's SortArray tie order is unknown (H). It only matters for equal scores, which the
  |score| weighting makes moot.
