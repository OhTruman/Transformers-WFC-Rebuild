# Smart squads: regroup after respawn (design note only, no code; for a user decision)

## Why

The pathing fix (`spawnNearScope=2`: squad respawns only among the team's active spawn cluster) made Smart move better than Classic. It also spread squads out.

Settled member-to-leader distance, 6 seeds × 4 maps (NAVQ):

| Rule | Mean distance | Members within 15 m |
|---|---|---|
| Respawn near the squad at any team start (M2) | 55 m | 22 % |
| Active cluster only (shipping) | 71 m | 19 % |

A middle rule (active cluster plus starts within a ~40 m path of the leader) gained only 3.5 m and cost movement quality. It failed Integration's bar.

The user's playtest asked for **both** better pathing **and** visible squad play. The gap opens *after* a respawn: the member appears at the active cluster, and its leader may be 50-100 m away and still moving.

## Options

| | Idea | Expected effect | Risk |
|---|---|---|---|
| **A. Dynamic squads on respawn** | A respawning Smart bot joins the squad whose live leader is nearest its spawn (squads re-form by proximity) instead of a fixed squad by player order. | Biggest cohesion gain without extra travel. Squads form where bots are. | Squad sizes drift. Needs a cap (4) and a rule for empty squads. Changes which bots fight together. |
| **B. Rally point** | The leader names a rally cell on its route (from the active cluster toward its goal). Respawned members path there, and the leader slows near it. | Members meet the squad on the way instead of chasing it. | One more goal layer. Rally cells must avoid exposed ground (threat field). |
| **C. Earlier vehicle regroup** | Members over 45 m away already regroup in vehicle form. Lower that to ~30 m and keep vehicle form until within 15 m. | Cheap tuning. Shortens the chase. | Vehicle-form travel was the weaker form in duels (M2 notes). |
| **D. Predictive follow** | Members steer for the leader's predicted position (position + velocity × ETA), not its current one. | Removes the "constant gap" chase. | Little gain when the leader is not moving straight. |

## Recommendation

**A, with C as a cheap complement.** Measure before / after with:
- the NAVQ cohesion and movement columns (6 seeds × 4 maps);
- the WFC_AIDUEL win rate vs Classic, so Smart is not weakened (the user accepted about 60-63 %).

Classic is untouched in every option.

## Gate (proposal)

- Settled member-to-leader mean ≤ 55 m (at least the M2 level) and within-15 m share ≥ 22 %.
- All four NAVQ movement columns within 1 SE of the current shipping set.
- AIDUEL Smart win rate still 57-65 %.
