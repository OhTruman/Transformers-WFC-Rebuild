# Bot ground-vehicle ledge guard: findings, deferred to package 7 (no patch)

Gameplay, 2026-10-16.

**The problem:** the third Streets slow spot. Bot vehicles carry a corridor corner at 14-15 m/s off a ledge at about (123.3, −705.9, −439.1) into a dead-end trench. In robot form they then walk to its end at (125.9, −721.0, −439.9), under a waypoint they can't reach. POCKETENTRY replays: Classic seeds 2 / 6, 3 entries, all the same.

**The guard tried:** bot ground vehicles only, both AIs, steering only. Above 6 m/s, if the point reached in 0.3 / 0.6 s is off the nav mesh with no floor within a drop, the bot takes the stick off for that step: no boost, brake against the velocity, steer back toward the corridor. It is exempt when the next waypoint is a link or lies below the drop. [PC ADAPTATION: the original's vehicle AI navigation is not recovered; a conservative rebuild rule.] The edit scripts are in `docs/handoff/ledgeguard/`.

## Results

| | v1 (drop 2.5 m, exact cell test) | v2 (> 1 m off the mesh, drop max(2.5, 0.35 × probe distance)) |
|---|---|---|
| Trench entries (seeds 2 / 6) | 0 (was 1 / 2) | 0 |
| NAVQ 24 × 4, both AIs | no map worse | every \|z\| < 2 |
| BOTTEST, 12 seeds | 48 / 48 | 48 / 48 |
| MAPTRAVERSE vehicle tours | identical | identical |
| Activity per seed | 7-20 s, 127-423 episodes (2-4 % of vehicle travel) | 0.9-1.7 s, 8-28 episodes (0.2-0.4 %) |

## Why it is not in package 6

The bar was "rare, at real drops you can name". The top clusters per map (WFC_LEDGELOG, 4 maps × 2 seeds × both AIs, each checked with the WFC_STUCKSPOT walk):

| Map | Cluster | Real drop? |
|---|---|---|
| Molten | (120, −50), y 9 | **Yes**: 14.9 m down in every direction |
| Streets | the trench ledge (120, −430 / −440) | **Yes** |
| Streets | (110, −510), y −719: 39 of 89 episodes | **No**: at most −2.9 m over about 12 m |
| Debris | (60, −80), y 112 | **No**: flat |
| Gorge | (−80, 30) | **No**: ±2 m |

About half the activations are false positives. The floor test (`groundHeight` from the vehicle floor + 1 m) finds no triangle over collision holes and just past mesh edges.

## Package 7 plan

1. Cast a downward segment from the probe point, so a hole in the collision mesh reads as a hole, not a drop.
2. Require the probe's own floor to be both off the nav mesh and lower than the vehicle's floor by the drop.
3. Re-gate with the same bars plus the named-drop check.

Meanwhile the trench costs 2-5 s per entry; bots escape it since the walkable-floor / rise-snap fixes.
