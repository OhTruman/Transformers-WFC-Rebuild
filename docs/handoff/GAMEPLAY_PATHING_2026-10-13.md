# Smart bot pathing, 2026-10-13 (user playtest P1 #9)

**User:** "Bot pathing still needs more work. It is not terrible, but it could definitely be a lot better."

**Patch:** `docs/handoff/GAMEPLAY_PATHING.patch`, against integration 85f19e7f.
- Smart only. Classic is byte-identical: 24 / 24 Classic NAVQ runs reproduce the old build's summaries exactly, and the Classic DETERMINISM digest is unchanged (4e8b5f344b226b4f).

## What the user will notice (plain language)

1. **Bots no longer pace back and forth for minutes at a railing.**
   - Cause: Smart bots fetching a health pickup measured distance horizontally.
   - A pickup on the floor 18 m below a railing on Streets counted as "2 m away".
   - The trip led into a strip beside the railing that the bot navigation does not cover. Bots could get in but found no way out, and shuttled there (one bot: 565 direction reversals in 5 min).
   - Now: pickup distance counts the height difference double, and a failed pickup trip is skipped for 90 s (was 30 s).
   - A bot that walks a lot but goes nowhere while off the walkable area stops, drops that goal and walks back the way it came. If that happens three times in one life, the existing "stranded" rule applies.
2. **Bots respawn at sensible spots.**
   - Smart's "respawn near my squad" took the team start nearest the squad from *any* spawn cluster of the map, including far or raised starts that the mode does not use.
   - It now picks only among the team's active cluster, the authored spawn set, still the start nearest the squad.
   - This made the biggest difference on Gorge: off-walkable-area time 14.7 % → 5.0 %.
3. **A goal counts as reached only on its own floor** (within 3 m of height). Before, a goal on the floor below was "reached" from above, and the bot never moved on.

## NAVQ: Smart before / after vs Classic

TDM, 4 friendly + 5 enemy HARD bots, 300 s simulated, 6 seeds per row, null-render boot (CPU only).

Columns: % of travel time slow (< 1.5 m/s) / heading reversals per travel-minute / % of travel off the nav mesh / stuck events per bot-minute.

| Map | Smart before | Smart after | Classic |
|---|---|---|---|
| Streets | 6.1 / 11.2 / 6.1 / 2.14 | **5.4 / 3.0 / 3.2 / 1.22** | 6.9 / 3.3 / 5.9 / 1.48 |
| Molten | 3.4 / 2.3 / 2.5 / 0.78 | **2.0 / 2.5 / 1.6 / 0.75** | 1.8 / 2.0 / 1.5 / 0.84 |
| Debris | 6.8 / 3.0 / 5.0 / 1.81 | **4.8 / 1.3 / 2.6 / 0.94** | 3.8 / 1.2 / 2.1 / 1.09 |
| Gorge | 12.1 / 10.0 / 14.7 / 3.06 | **4.7 / 4.8 / 5.0 / 1.57** | 5.9 / 5.0 / 7.0 / 2.31 |

**Target ("Smart at least Classic in all four columns on every map"):**
- **Met** on Streets and Gorge.
- **Close but not met** on Molten (slow +0.2, reversals +0.5, off-mesh +0.1) and Debris (slow +1.0, off-mesh +0.5).
- Smart has fewer stuck events than Classic on all four maps.

## What was tried and dropped (measured, no gain)

- **Corridor pure pursuit:** helped Streets, hurt Gorge and Debris.
- **Stuck sidestep instead of a jump:** worse everywhere.
- **Squad member slots:** worse on Debris.
- **Removing per-bot route noise:** much worse (Gorge reversals 21 / min). The noise spreads bots across corridors.

## Self-tests on the patched build

- BOTTEST 39/39 for Classic and for Smart.
- DETERMINISM 2/2 for both AIs.
- FXOWNER 6/6.
- GL vs null-render DETERMINISM digest identical (8895b43c7ea0927e).

## Tools

- **WFC_NAVQTEST:** the measurement above.
- **WFC_NAVQTRACE=<player>:** that bot's state every 0.5 s.
- **WFC_NAVPORTALAUDIT=1:** nav portals whose floors step more than 1 m.
- **New WFC_SMARTTUNE keys:** `ditherGuard`, `goalFloorM`, `spawnNearScope` (0 / 1 / 2) and `routeNoise`, for A/B.
- The table script is `Rebuild-Gameplay/work/pass29/navq_table.js`.

## Next

- Debris slow time and Molten reversals.
- One remaining long stall per few matches: a bot slow and off the mesh 40 % of its time without reversing (Streets seed 5).

## Set 2: `GAMEPLAY_PATHING2.patch` (against c949ab88)

### Changes

- **Think-time goal re-pick is floor-aware** (Smart), like botSteer's arrival (Experimental review).
- **A dither escape ends any Smart errand** (cover / flank / hunt spot), not only pickup trips. avoidCells cost paths, not spot choice.
- **`squadSlowFactor` tune key** (default 0.4, unchanged).
- **WFC_NAVQTEST prints squad cohesion:** the settled member-to-leader mean distance and the % within 15 m.

### NAVQ (6 seeds, mean ± standard error)

Columns: slow % / reversals per travel-min / off-mesh % / stucks per bot-min.

| Map | Smart (set 2) | Classic |
|---|---|---|
| Streets | 5.1±0.7 / 3.2±0.9 / 3.6±0.6 / 1.25±0.2 | 6.9±1.6 / 3.3±0.8 / 5.9±1.3 / 1.48±0.2 |
| Molten | 2.2±0.4 / 2.0±0.5 / 1.6±0.1 / 0.67±0.1 | 1.8±0.4 / 2.0±0.5 / 1.5±0.3 / 0.84±0.3 |
| Debris | 4.4±0.6 / 1.3±0.2 / 2.5±0.4 / 0.91±0.2 | 3.8±0.8 / 1.2±0.3 / 2.1±0.3 / 1.09±0.1 |
| Gorge | 4.9±1.1 / 3.9±0.4 / 6.4±1.7 / 1.39±0.2 | 5.9±2.1 / 5.0±2.3 / 7.0±2.7 / 2.31±1.1 |

Every remaining Molten / Debris gap is within one standard error: level with Classic within noise.

**Classic identity:** 24/24 summaries identical.

**Self-tests:**
- BOTTEST 39/39 for both AIs.
- 31 v 32 AI cost: 0.40 ms per step with the dither guard, 0.43 ms without (no measurable cost).
- DETERMINISM 2/2 for both AIs.

### DECISION NEEDED: squad respawn scope (cohesion vs movement)

| `spawnNearScope` | Squad member-to-leader distance (settled, 24 runs) | Within 15 m | Gorge slow / rev / off-mesh / stucks |
|---|---|---|---|
| 2 (shipping) | 70.6 m | 19 % | 4.9 / 3.9 / 6.4 / 1.39 |
| 0 (M2 rule) | 55.0 m | 22 % | 13.4 / 9.5 / 14.6 / 2.88 |

Respawning only in the active spawn cluster spreads squads about 15 m further apart. Respawning anywhere keeps them tighter, but Gorge movement is twice as bad.

The user's M2 decision was "squads stay as they are", so this trade is the user's / Integration's call. A middle option is possible: active cluster, plus other-cluster starts that have a short nav path to the squad.
