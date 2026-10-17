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

## AGREED (Systems, 2026-10-17; recorded as ONLINE_MP_ARCHITECTURE.md section 10.2): the proposal with four amendments

1. **Local too:** in an online match the local human's command also goes through `applyRemoteCommand` at tick N, decoded from the same
   wire bytes that were sent, never from the live input, so every peer's sim sees identical numbers. The camera may still turn locally
   every frame; only the sim waits for N.
2. **Wire-exact fields:** `struct RemoteCommand { uint32_t tick; int8_t moveForward, moveRight; uint32_t buttons, pressed; float aimYaw, aimPitch; }`.
   - aimYaw is decoded from a u16 (0..2pi) and aimPitch from an i16 (+-pi/2). They are ABSOLUTE values, so nothing drifts after a missed
     bundle.
   - World builds the MoveIntent from the axes + aim + buttons deterministically, as for local input. Anything MoveIntent needs beyond
     that (e.g. an analog vehicle throttle) gets a byte added by Systems on request.
3. **Buttons:** held + pressed as two u32. `pressed` stays explicit: a tap can start and end inside one tick.
   `static_assert((int)platform::Button::Count <= 32)`.
4. **Idle rule:** compare against the previously APPLIED command of that player (decoded values).
   - Non-idle = any held / pressed bit, a non-zero axis, or an aim change > 0.5 deg.

## AMENDED (RE MP_INPUT_CONTRACT_RE 4a1f8fb; Integration decision (A); Systems 10.2, 2026-10-17)

- **Aim** on the rotator grid: aimYaw u16 / aimPitch i16 in rotator units (65536 = 360 deg), decoded to radians. It is the look direction
  (facing, camera-relative movement, animation, vehicles).
- **aimPoint:** the sender's camera-trace RESULT in ABSOLUTE world space, 3 x int24 at 1 cm, decoded to metres. Absolute because of the
  input delay (the command for N is built at N-D). Present when `kRemoteBitAimPoint` (bit 31) is held. Without it: eye + dir x weapon range.
  - Shots / beams / aimed abilities go from the pawn's eye at tick N toward aimPoint. Fine aim and the hover smoothers stay camera-only.
- **Bits:** 0..28 platform::Button (FineAim = Button::FineAim), 29 Hover, 30 Active (look axis / team chat), 31 AimPoint.
- **Idle** = the original's input rule: any held / pressed bit or a non-zero move axis. No aim delta.
- **The kick** only with the host option ?WfcIdleKick=1 (MatchSettings.kickIdle). The original never kicks in private matches.
- **Online, every human goes through this path** (Integration decision (A)): the local PlayerController sends its command like everyone
  else. Offline keeps the direct path.
- `RemoteCommand { uint32_t tick; int8_t moveForward, moveRight; uint32_t buttons, pressed; float aimYaw, aimPitch; core::Vec3 aimPoint; }`
