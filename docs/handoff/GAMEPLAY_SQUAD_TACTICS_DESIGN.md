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
