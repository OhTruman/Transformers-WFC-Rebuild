# Bot readiness (Pass 21e, 2026-10-04). Survey only; no bot implementation in this pass

Bots in versus play are a **RECONSTRUCTION EXTENSION**: the shipped multiplayer had no bots (RE OVERNIGHT §F).

## What the match architecture already supports
- `Match` is participant-agnostic:
  - players are indices with team, score, alive and respawn queue, spawn through the cluster manager, damage and kill
    attribution, kill feed and scoreboard;
  - the local player is one participant among them.
- `MatchOpponent` (test-only) proves non-local participants can be:
  - spawned, damaged and killed;
  - respawned in waves;
  - counted inside objective volumes (DOM / KOTH).
- Objective ticking takes a generic pawn list (`tickObjectives(dt, pawns, …)`). A bot pawn in a zone scores the same as a
  player.
- Character selection (`Match::selectCharacter`) is per participant, so a bot would select a chassis like a player.

## What a bot needs that is not there yet
1. **A controller abstraction.** `PlayerController` mixes device input with intent generation. A bot controller must
   produce the same `MoveIntent` + fire / transform / boost latches, then run through the same
   `CharacterMovement::update`. That gives bots identical movement rules and no separate physics.
2. **Multiple full pawns.** `World` owns one `Character` with movement. `MatchOpponent` has no movement or animation. The
   pawn update and animation must become per-pawn.
3. **Navigation.**
   - Streets has authored PathNodes / ReachSpecs: 123 nav points, 852 specs, already validated by `WFC_MAPTRAVERSE` for
     robot and vehicle.
   - That is enough for graph routing. There is no navmesh, and none should be invented.
4. **Behaviour.**
   - The campaign buddy / enemy AI (TnAIController families, AICD data) is the only authored AI. Its decision logic is
     not decompiled.
   - **RE request:** the campaign buddy controller states (follow, engage, take cover, transform decisions) and its
     target selection.
   - A versus bot should reuse those states where they apply rather than generic shooter heuristics.

## Recoverable vs needs RE
| Item | Status |
|---|---|
| Nav graph (PathNode / ReachSpec) | recoverable now (authored, loaded) |
| Spawn / respawn / team assignment for bots | recoverable now (Match) |
| Movement rules | shared with the player (no bot-specific physics) |
| Buddy / enemy AI decision logic | needs RE |
| Bot difficulty, aim error | no original data (versus had none): must be labelled RECONSTRUCTION EXTENSION |

## Pass 22 status (2026-10-05)
- **Done: multiple full pawns.**  now owns a . At spawn World applies the participant's
  resolved chassis (body, collision, stats), specialty (health, speed) and loadout through the same
   /  path as the local pawn.
- It moves with the shared  from a  (), transforms through ,
  takes hitscan damage on its real cylinder, dies and respawns through , and interacts with every objective
  (DOM / KOTH / CTF flag / EXT bomb) through the objective layer.
- **WFC_PARTICIPANTTEST 4 / 4.**
- **Still missing for bots:**
  - a controller producing intents and firing; participants do not fire yet: firing is in PlayerController;
  - participant weapon meshes are not drawn (only the local pawn's);
  - navigation over the authored ReachSpec graph;
  - any decision logic. That logic is a RECONSTRUCTION EXTENSION and needs the campaign AI RE first.
