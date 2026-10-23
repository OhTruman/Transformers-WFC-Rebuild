# Escalation waves: composition, cadence, scaling, end screen and XP (RE, 2026-10-23)

**Labels:** C = TnSurvivalGame bytecode (`work/script/decomp_tc/MP_ESC_*_BASE_m`), the spawn lists
(`notes/data/escalation/MP_ESC_*.json`, from TR_Escalation_m), CDOs; H = inference.

Extends MP_ESCALATION_RE §1–2 / §8 with the complete rows. **Wave numbers are 0-based** (the HUD shows +1).

## 1. Spawn rules (C)

**Wave start** (`InWave.BeginState`):
- IncrementWave; Round Begin event; TotalUsedThisWave = 0;
- CurrentWaveData = `Waves[min(CurrentWave, last)]`;
- **FillingWave = true and one enemy spawned immediately.**

**Each tick** (`InWave.Update`):
1. CheckLivingPlayers: all players dead → EndGame("AllDead").
2. **TotalUsedThisWave ≥ Budget:** no more spawns. Once no enemy is alive → BetweenWaves.
3. Otherwise:
   - **start filling when the alive cost ≤ BudgetDeficitThreshold**;
   - while filling, **spawn one enemy every FillWaveReCheckTime** (the timer resets at each spawn: **BrokenHope 0.5 s** (class
     default), **Remnant 3.0 s**);
   - **stop filling once the alive cost ≥ BudgetAlive.**
   - The last spawn can overshoot Budget, because the budget is checked only before each fill.

**Which enemy** (`GenerateListOfPossibleEnemies`, in spawn-list order):
- an entry is eligible if `MinWave ≤ wave`, `(MaxWave == 0 || wave ≤ MaxWave)`, and **its own** living count < MaxCount. The
  count is per entry index, so the two CQC / Shotgun / Titan / Brute rows count separately;
- **the first eligible entry below its MinCount is chosen exclusively**;
- otherwise a **uniform random pick** among the eligible entries;
- no eligible entry → nothing spawns that tick (logged).

**Where:** zone pick by spawn type, MP_ESCALATION §2. The cost goes to both TotalUsedThisWave and the alive cost; a death removes it
from the alive cost.

**Between waves** (`BetweenWaves`):
- RoundEnded: dead players respawn, Round End event, **everyone revived**;
- **TimeBetweenWaves 25 s** (the GRI NextWaveTime counts down) → the next wave.
- PreGame: **BrokenHope 30 s, Remnant 10 s** after the first player spawns.

## 2. Enemy entries (C)

**BrokenHope** (Decepticon horde):

| Entry | Waves | Min–Max alive | Cost | Spawn type | Loot (spawn list) | Power |
|---|---|---|---|---|---|---|
| Spider | 0–4 | **2**–8 | 5 | Standard | GENERIC (none) | 10 |
| Soldier MG | 0+ | 0–8 | 10 | Standard | SoldierDec | 25 |
| Soldier Rocket | 2+ | 0–4 | 20 | LongRange | SoldierDec | 30 |
| Soldier Jet | 4+ | 0–4 | 20 | Air | **GENERIC (none)** | 75 |
| CQC | 2–3 | 0–2 | 20 | Standard | SoldierDec | 50 |
| CQC | 4+ | 0–6 | 20 | Standard | SoldierDec | 50 |
| Brute | 7–8 | 0–1 | 60 | Standard | BruteDec | 100 |
| Shotgun | 5–6 | 0–2 | 30 | Standard | SoldierDec | 75 |
| Shotgun | 7+ | 0–6 | 30 | Standard | SoldierDec | 75 |
| **Tank** | **14+** | 0–1 | 60 | **Boss** | SoldierTankDec | 300 |
| Titan | 9–10 | 0–1 | 60 | Standard | TitanDec | 100 |
| Brute | 11+ | 0–1 | 60 | Standard | BruteDec | 100 |
| Titan | 10+ | 0–2 | 60 | Standard | BruteDec | 100 |

- On waves 0–4 the Spider's MinCount 2 makes the first two spawns of each fill Spiders.
- Wave 10 has both Titan rows eligible (up to 1 + 2).

**Remnant** (Autobot horde):

| Entry | Waves | Min–Max | Cost | Spawn type | Loot | Power |
|---|---|---|---|---|---|---|
| HoloBrute | 0–5 | **3**–6 | 10 | ShortRange | **SoldierAut (4)** | 25 |
| Soldier MG | 0+ | **2**–6 | 10 | ShortRange | SoldierAut | 25 |
| Rocket | 2+ | 0–2 | 20 | ShortRange | SoldierAut | 30 |
| CQC | 4+ | 0–3 | 20 | ShortRange | SoldierAut | 50 |
| Shotgun | 6+ | 0–2 | 25 | Standard | SoldierAut | 75 |
| Titan | 7–9 | 0–1 | 45 | Standard | BruteAut | 100 |
| Brute | 9–10 | 0–1 | 45 | Standard | BruteAut | 100 |
| Titan | 11+ | **1**–2 | 45 | Standard | TitanAut | 100 |
| Brute | 12+ | **1**–1 | 45 | Standard | BruteAut | 100 |

- MinCount rows (HoloBrute 3, MG 2; from wave 11 Titan 1, from 12 Brute 1) are forced first, in list order.
- Remnant has no Jet, Spider or Tank.

## 3. Wave rows (C): Budget / BudgetAlive / DeficitThreshold, then buffs

**Buff levels:**
- DT = `TnBuffSurvivalDecreaseDamageTaken`: horde damage taken × 0.9 / 0.8 / 0.7 / 0.6 / 0.5 for levels 0–4;
- DD = `TnBuffSurvivalIncreaseDamage`: horde damage dealt × 1.25 / 1.5 / 1.75 / 2.0 / 2.25.

| Wave | BrokenHope | Buffs | Remnant | Buffs |
|---|---|---|---|---|
| 0 | 100 / 50 / 20 | – | 100 / 50 / 30 | – |
| 1 | 150 / 80 / 40 | – | 150 / 70 / 40 | – |
| 2 | 200 / 100 / 50 | – | 200 / 80 / 50 | – |
| 3 | 300 / 125 / 100 | – | 300 / 110 / 70 | – |
| 4 | 400 / 150 / 120 | – | 400 / 130 / 90 | – |
| 5 | 500 / 200 / 170 | – | 550 / 160 / 110 | DT0 |
| 6 | 600 / 200 / 180 | – | 700 / 180 / 130 | DT1 |
| 7 | 800 / 230 / 200 | – | 900 / 210 / 150 | DT2 |
| 8 | 1000 / 230 / 200 | – | 1100 / 230 / 170 | DT3 |
| 9 | 1200 / 230 / 220 | DT0 | 1200 / 230 / 190 | DT4 |
| 10 | 1200 / 230 / 220 | DT1 | 1200 / 230 / 190 | DT4 DD0 |
| 11 | 1200 / 230 / 220 | DT2 | 1200 / 230 / 190 | DT4 DD1 |
| 12 | 1200 / 230 / 220 | DT3 | 1200 / 230 / 190 | DT4 DD2 |
| 13 | 1200 / 230 / 220 | DT4 DD0 | 1200 / 230 / 190 | DT4 DD3 |
| 14 | 1300 / 230 / 220 | DT4 DD1 | 1200 / 230 / 190 | DT4 DD4 |
| 15 | 1300 / 230 / 220 | DT4 DD2 | 1200 / **250** / 190 | DT4 DD4 |
| 16 | 1300 / 230 / 220 (**repeats from here**) | DT4 DD3 | 1200 / 230 / 190 | DT4 DD4 |
| 17–19 | (= 16) | | 1200 / 230 / 190 (19 repeats) | DT4 DD4 |

- The "alive cost 50" of wave 0 rises to **230** by wave 7 / 8.
- The horde buffs are applied at spawn (ApplyWaveBuffs). Already-alive enemies keep the buffs they spawned with.

## 4. Player count and difficulty: nothing scales (C)
- **Nothing in TnSurvivalGame reads the player count** for budgets, alive cost, cadence, enemy choice or buffs. The only
  player-count read is the stock `GRI.PRIArray.Length ≤ 1` MVP / XP gate.
- **No difficulty:** TnSurvivalGame derives TnTeamGame → TnMultiplayerGame → TnGame, not TnCampaignGameBase. So
  `TnPawn.ApplyDifficultyBuffs` returns early: **no difficulty buffs on players or horde**. GetSkillDataIndex is fixed at 1.
- **1 to 4 players face identical waves** at start and after joins (consistent with ONLINE_JOIN_IN_PROGRESS).
  - H: the only implicit effect is targeting. The threat sensor's −1 per other attacker spreads the horde over more players.
- The weapon-pickup and power economy don't scale either (PowerReward per kill to the killer).

## 5. End-of-Escalation screen, score and XP (C)
- **Trigger:** all players dead at once → `TnSurvivalGameOverMessage(DisplayWave + 1)` → **GameOverMessage "You survived until wave
  N"** → MatchOver:
  - audio only (OnCampaignGameOver);
  - UI event 9 → GameEnded → **EndGameStats_GFX** (TnUIControllerCoop EndGameUI) → **15 s → ReturnToGameLobby**.
- **EndGameStats content:**
  - it opens on the **Earnings** view (unless iconic mode). **Escalation earns 0 XP** (`TnGameReplicationInfoSurvival.CanGainXp`
    false, ShowXpEvents false), so the per-specialty bars show no gain and no level-up;
  - **X → Scoreboard:** the header is **"You survived until wave N"** (GameOverMessage); per player Level · Name · **Score** ·
    **Kills** · **Deaths**, sorted Score↓ Kills↓ Deaths↑ Name. The level column is the player's normal rank, unchanged;
  - the HUD's in-wave score and "wave N" counters are the live equivalents.
- **Score** (competitive scoring, TR_Escalation_m.ScoreDefinition):
  - per kill by type: Spider 10, MG 25, Rocket 30, CQC 50, Jet 75, Shotgun 75, Brute / Titan 100, HoloBrute 25, **Tank 300**;
  - headshot / melee kill events;
  - **assists 0**, **no multiplier** (MaxMultiplier 1.0);
  - shard pickups score PickupShard;
  - per player, no team total.
- **XP / rank: none in Escalation.** No XP, no rank-ups, no challenge progress from XP; MVP is skipped (it needs CanGainXp).
  - **Achievements:** wave 16 displayed → 45; power spent → 44; plus the horde-kill achievements (Brute by backpack 28, etc.).
  - **Leaderboard write** (TnStatsWriteSV): Score, Games, Time Played, Kills, Deaths, Revives, Downed (LIVE). The headline result is
    the wave reached.
  - Quitting mid-match warns "Your leaderboard progress will be lost."
