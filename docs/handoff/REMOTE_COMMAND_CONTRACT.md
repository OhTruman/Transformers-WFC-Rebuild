# Remote command call site (Gameplay ↔ Systems transport), proposal 2026-10-17

Gameplay owns the World side; Systems owns the transport (lockstep delivery). Follows GAMEPLAY_SESSION_HOOKS (Remote participants,
noteRemoteCommand, the 180 s idle rule) and Frontend's note: the idle clock must reach 180 s on the same tick on every peer.

## The call

```
// One Remote participant's command for sim tick `tick` (World::simTick() + 1 = the step about to run).
struct RemoteCommand {
    int64_t tick = 0;
    MoveIntent intent;          // the peer's movement / jump / transform / dodge / hover etc. for this step (game::MoveIntent)
    uint32_t buttons = 0;       // held buttons this step (platform::Button bit = 1u << (int)Button), fire / melee / grenade / abilities / pickup
    uint32_t pressed = 0;       // buttons that went down this step
    float aimYaw = 0, aimPitch = 0;   // the peer's view (radians)
};
bool World::applyRemoteCommand(int matchPlayer, const RemoteCommand& c);   // false: not a Remote participant / wrong tick
```

- **When:** inside the lockstep step, for tick N, **after** every peer's commands for N have arrived and **before** `World::tick` runs
  step N. The same order on every peer: ascending match player index.
- **What World does:**
  - it drives the participant's pawn (intent, aim, buttons) for that step;
  - it calls `noteRemoteCommand(matchPlayer)` itself, **only for a non-idle command**:
    - any held or pressed button;
    - or a non-zero move axis;
    - or an aim change > 0.5° since the previous command.
  - An empty / repeated command keeps the idle clock running.
- **The transport never calls noteRemoteCommand directly** (it stays public for tests: SESSIONTEST's scripted commands).
- **Missing command for tick N** (late / dropped peer): the transport's stall rule applies (lockstep waits); World is never stepped
  with a hole. If the session decides to drop the peer, it sends a SlotSwap (to Bot) instead.

## Why here

- **Determinism:** the idle clock and the pawn input change only inside the step, from data every peer has for that tick. So the
  RemoteIdle kick (MatchEvent) and the slot swap land on the same tick everywhere.
- **One place decides "idle":** both sides use the same rule.

## Status

- Gameplay implements `applyRemoteCommand` after Systems confirms the shape (the session hooks give the participant and its pawn
  already).
- The button set maps onto what PlayerController reads for the local player, so a Remote pawn behaves like a local one.
