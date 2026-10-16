# Squad formation slots: redesign proposal (after package 6, no code now)

Gameplay, 2026-10-16. Integration closed the Smart follow reversals as a design item (`GAMEPLAY_SMART_ROUND_7d9edf78.md`). This doc covers what a formation slot should be, how members pick and hold one, and the expected cost. It is a proposal only; the shipping `squadTactics = 2` stays until it is built and gated.

## Why the current slot reverses members

Today a member within `squadRegroupM` (15 m) of its leader gets a **wedge slot**.

**The slot itself:**
- It sits 6 m back and 5 m to the side (the third member 10 m back).
- It is placed relative to the leader's **current velocity**, or its last move direction while it stands.
- It is re-derived every think and re-goaled when it moves more than max(4 m, 20 %).

**The problems:**
1. **The slot is heading-relative.** When the leader turns 90° at a corner, every slot swings around it. A member that was behind-left now has a target to its right or ahead, so it turns sharply or back.
2. **The slot is a point goal with a 2.5 m arrival radius**, chased by the path follower. While the leader walks, the point keeps moving away and the member runs, arrives, stops, and runs again.
3. **Squad mates' slots are 5-10 m apart**, inside the 7 m local-avoidance look-ahead. Members steer around each other, and the side flips; REVDIAG ties 39-54 % of reversals to that push.
4. **The member switches between two targets at 15 m:**
   - Inside 15 m it heads for its slot behind the leader.
   - Outside 15 m it heads for the leader's destination ahead.

What was tried and left off (`GAMEPLAY_SQUAD_TACTICS_DESIGN.md`, `GAMEPLAY_SMART_ROUND_7d9edf78.md`):

| Option | Result |
|---|---|
| Trail following (`squadTrail`) | Streets worse |
| Station keeping (`squadStationKeep`) | Worse |
| Slot freeze (`squadSlotFreeze`) | No gain |
| Passing-side hold | Failed |
| Wedge hysteresis | Failed |

Each one patches a symptom of the same design: **a moving point target chased by a path follower.**

## Proposal: slots on the leader's route, held by speed rather than by goal

**1. A slot is a distance along the leader's planned corridor, not an offset from its heading.**

- The leader has a path (waypoints). Member k's slot is the point **d_k metres behind the leader along that path**: d = 6 / 10 / 14 m, plus a lateral offset of ±2 m clamped to the corridor width at that point.
- At a corner the slot follows the corridor around the corner instead of swinging.
- When the leader stands, the slots stay where they are on the corridor, so they don't rotate with its look-around.
- **Why this is not `squadTrail`:** squadTrail took points on the leader's *walked* trail (history: stale, off-mesh after a jump). This uses the *planned* corridor (on the mesh, ahead of the member).

**2. Members do not path-find to the slot while they are near it.** Within 12 m of their slot along the corridor, they stop setting a new goal.

- **Steering:** the member follows the leader's corridor itself, sharing the path and starting from the nearest corridor point.
- **Speed:** it regulates its speed toward the slot.
  - Ahead of its slot: speed scale down to 0, so it stands. It never walks back.
  - Behind: up to run speed plus 20 %, then vehicle regroup beyond 45 m as today.
- **Result:** "arrive, stop, run" becomes continuous speed matching. A member never reverses to reach a slot it has overshot; it waits for the slot to come to it.

**3. One squad corridor.**

- All members follow the leader's corridor, laterally offset, so mates never cut across each other's lines.
- Local avoidance between squad mates becomes a lateral lane: the member keeps its ±2 m side. Avoidance stays for other pawns.

**4. One rule, no switch at 15 m.**

- Within 45 m the member is on the corridor.
- Beyond 45 m, the existing regroup to the leader's destination (vehicle form) applies.
- This removes the wedge / destination flip at squadRegroupM.

**5. Fights keep the current firing line.** The firing line works: in-fight cohesion is 25 % within 20 m.

## What stays the same

- Squad membership and leaders.
- The leader's waits and slowing.
- Spawning near the squad.
- Fights (firing line).
- Classic: untouched, byte-exact.

## Expected cost and how it would be gated

**Code size:**
- New code: about 250-350 lines in `WorldBots.cpp`, in the formation branch and the post-steer speed scale.
- A corridor "distance along path" helper in `BotNav`, about 60 lines.

**Cost per step:**
- Members already have a path; sharing the leader's removes their wedge-goal searches, which is a saving of roughly one search per member every 2-4 s.
- The corridor projection is O(waypoints) per think.
- Expected AI cost: unchanged or lower (today about 0.2 ms per step at 31v32).

**Behaviour risk:**
- **Corridors that jump or drop:** members must take jump links like the leader does. They already follow corridors with jump links, so this is low risk.
- **Strength:** squads move more tightly and arrive together. The Smart win rate (70 % at Hard) may rise and need the AIDUEL band check (57-65 %). Fallback: `squadTactics=1` semantics.

**Gate** (the 24-seed yardstick, z-score per column; `navq_cmp.js`):
- Follow reversals down at z ≤ −2 on at least 3 of 4 maps.
- No movement column (slow / off-mesh / idle5 / stucks) worse at z ≥ 2.
- Cohesion: settled within 15 m ≥ the current 41 %.
- BOTTEST, AIDUEL ladder, DETERMINISM, and the Classic byte-exact check.

**Effort:** about 1-2 days: build, then 2-3 tune rounds of about 30 min of NAVQ each (24 seeds × 4 maps at about 5 s per run), plus AIDUEL.

## Open question for the user (after package 6)

Whether squads should visibly travel as a column along routes, which is what this gives, or keep today's looser wedge. A column reads as more disciplined, but it is a visible behaviour change from what was playtested in package 5.
