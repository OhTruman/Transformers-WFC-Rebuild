# Smart squad tactics: formation in and between fights (design)

**User decision 2026-10-13 23:35:** "Yeah you can definitely implement that!" (squad-level tactics for Smart bots).

**Scope:** Smart only, behind `WFC_SMARTTUNE` keys. Classic stays byte-identical, re-proven 24/24.

## Problem

Squads split in fights. Settled member-to-leader distance is 70.6 m, with 19 % within 15 m.

Today each member runs its own errands (hunt / flank / cover / retreat) anywhere on the map. Squad play is only "take the leader's goal or target", so a fight scatters the squad. Re-forming at respawn (option A) closed only 7 m.

## Position model

Every think (0.25 s), each member that is not the leader gets a **slot** relative to its leader.

### Between fights (travel)

- A wedge behind the leader, along the leader's move direction (its corridor heading, else its facing).
- Slot k of up to 3 members: back 6 m, then lateral −5 / +5 / 0 m for k = 0 / 1 / 2 (the third trails at 10 m).
- Members walk to the slot point; the leader's corridor is the route.
- When the leader is far (> 25 m), members path to the leader's destination instead (existing behaviour).

### In a fight (the leader or the member has a target in sight, or the leader is hunting)

- A **firing line** across the threat axis, from the leader toward the leader's target (else the member's own target).
- Slot k sits 6-10 m to the side of the leader, alternating sides, perpendicular to the axis, plus 2 m back.
- The slot point must:
  - be on the nav mesh;
  - be reachable in a straight walkable line from the leader;
  - have line of sight to the target from eye height.
- Candidates are tried at 6 / 8 / 10 m and on both sides. With none valid, the member holds near the leader (within 8 m).

### Hysteresis

- A slot target is kept while it stays valid and moves less than 4 m.
- New paths are searched only on a real change, through the shared search queue as today.

## When a member breaks formation

| Case | Rule |
|---|---|
| Its own target in sight and its slot has no line of sight | Fight from where it is (Classic combat steering). Re-slot next think. |
| Hurt (existing cover / retreat rules) | Cover spots are searched within 20 m of the leader only; retreat goes toward the leader, not away from the squad. |
| Flank | At most one flanker per squad: the slot-0 member, and only while the leader is in a fight within 25 m. Others stay in the line. |
| Hunt of a remembered enemy | Only the leader hunts. Members follow in formation (slots on the hunt axis). |
| Leader dead | The next live member by order leads. Slots re-form around it. |
| Objective modes | Unchanged (squads are TDM only today). |

## Interaction with existing errands

- **Cover / HoldCover:** allowed, but the spot is constrained to 20 m from the leader. Holding cover counts as being in formation.
- **Flank / hunt:** restricted as above.
- **Leader slow / wait:** kept. With formation, members close up faster, so waits get shorter.
- **Pickups:** allowed within 20 m of the leader, else skipped while in a squad fight.

## Tune keys (all 0 = today's behaviour)

- `squadFormation`: 1 = formation slots on.
- `squadSlotBackM`, `squadSlotSideM`: wedge / line offsets.
- `squadLeashM`: cover / pickup / flank radius around the leader (20).

## Measurement and gate

### Cohesion

- Settled member-to-leader mean ≤ 55 m and within 15 m ≥ 22 % (the M2 level).
- Plus a new **in-fight cohesion**: member-to-leader distance sampled only while the leader is engaged. Report the mean and the % within 20 m.
- If the formation works, in-fight cohesion is the clearer signal of "squads stay a unit through fights". The gate holds if settled cohesion meets the M2 level OR in-fight cohesion is clearly better (≥ 10 percentage points within 20 m vs shipping) and settled cohesion does not get worse.

### Other conditions

- **Movement:** NAVQ slow / reversals / off-mesh / stucks within 1 SE of the shipping set on all 4 maps (6 seeds).
- **Strength:** WFC_AIDUEL Smart vs Classic win rate 57-65 % (the user's accepted band).
- **Identity:** Classic 24/24. BOTTEST 39/39 for both AIs. DETERMINISM 2/2.
- **Delivery:** a patch against the current integration head, before the package-4 cut (user playtest ~2026-10-21).

## Results (2026-10-14): shipped as the Smart default, `squadTactics = 2`

Integration's ship call; the user judges the formation fidgeting in the playtest.

### Toggle

| `squadTactics` | Behaviour |
|---|---|
| **2 (default, A)** | Formation, plus: leader stays on foot while strung out; slots kept off catwalk edges (1.2 m); members on foot within 30 m of the leader (vehicle regroup beyond 45 m, with hysteresis); leader waits 14 s / 4 s cooldown; leader slows at 18 m |
| 1 (B) | Formation, leader on foot, slot edges (movement only) |
| 0 | The pre-tactics squads |

Explicit `WFC_SMARTTUNE` keys still override the preset.

### Measured (null-render NAVQ, 6 seeds × 4 maps; AIDUEL 26 matches per map)

**Cohesion** (shipping → A):

| Metric | Shipping | A | Gate |
|---|---|---|---|
| Settled, within 15 m | 19.0 % (70.6 m mean) | **41.2 %** (43.6 m mean) | ≥ 22 % |
| In fight, within 20 m | 9.7 % (91.5 m mean) | **25.2 %** (61.5 m mean) | ≥ 19.7 % |

**Movement for A** (slow / reversals / off-mesh / stucks):

| Map | A | Of which reversals: pathing / follow / fidget |
|---|---|---|
| Streets | 5.9 / 5.3 / 4.0 / 1.56 | 4.7 / – / 0.6 |
| Molten | 1.8 / 3.4 / 2.0 / 0.69 | 2.8 / – / 0.7 |
| Debris | 4.7 / 3.0 / 1.9 / 1.08 | 2.4 / – / 0.6 |
| Gorge | 4.6 / 4.6 / 5.0 / 1.23 | 4.05 / – / 0.55 |

- Slow, off-mesh and stucks are at shipping level. Gorge is better.
- Reversals are above the shipping band on Streets / Molten / Debris.
- The fidget part (a member within 6 m of a leader slower than 1 m/s) is only about 0.6 per travel-min. Most of the rise is formation-following with a walking leader.
- NAVQ now reports reversals split three ways: pathing / follow / fidget.

**Strength** (Smart vs Classic AIDUEL, Hard):

| Version | Smart win rate |
|---|---|
| A | 70.2 % (104 duels) |
| B | 54.8 % |
| Tactics off, current code | 65.4 % |

Kill share for A is 52 % (aim unchanged). **Difficulty ladder for A** (matched difficulty, 56 duels each; ±6.6 points SE):

| Difficulty | Smart win rate |
|---|---|
| Easy | 51.8 % |
| Medium | 60.7 % |
| Hard | 70.2 % |
| Expert | 57.1 % |

Easy is ≤ 55 %, so no Easy trim was needed.

**Other gates:**
- Classic identity: 24/24.
- BOTTEST: 39/39 for both AIs (a Classic 6.16 ms max under load was noise; the rerun passed 39/39).
- DETERMINISM: 2/2 for both AIs.
- 31 + 32 AI cost: 0.20 ms per step.

### Tried and left off by default (tune keys kept)

| Key | What it does | Why it is off |
|---|---|---|
| `squadDynamic` (+ `squadVehicleRegroupM`) | Dynamic squads on respawn | 63.7 m cohesion |
| `squadSlotFreeze` | Freeze slots while the leader is still | No gain |
| `squadStationKeep` | Deadband / velocity matching | Worse |
| `squadTrail` | Leader-trail following | Streets worse |
| `spawnNearScope = 3` | Respawn-scope middle rule | Movement worse |
