# Gameplay split-screen plan (two local humans), 2026-10-23

Status: a plan only. It goes into code after the DLC section 1 items, on Rendering's view-list interface. Decisions are from
docs/DLC_PLAN.md: all modes; seat 1 = KB/M or the first Start pad; seat 2 = the next pad that presses Start; per-seat profile and XP;
online = two PRIs on one connection.

## The approach: seat 2 is a local remote seat

The World has one local player object (`player_`: PlayerController + pawn). Today ~240 `localPlayer_` and ~244 `player_` uses sit in
src/game. Rewriting all of them into an N-seat local loop is a large, risky refactor.

The World already simulates other humans through the **seat path**: online peers and leave-to-bot seats. Each one is a
MatchOpponent participant driven by a per-tick `RemoteCommand` (move, buttons, presses, aim yaw / pitch, aim point) through
`applyRemoteNow` → `applySeat`. Online, the identical simulation on every peer is proven by SESSIONTEST and the online sessions.

**Plan:** seat 2 is a participant of kind Remote (or a new Local2), whose RemoteCommand is built each step **locally** from pad 2's
input, through the same mapping the online client uses to send its own command. That gives:
- simulation, rules, damage, respawn, scoring, campaign "humans", Escalation seats, buffs, abilities and vehicles all for free,
  because they already treat any non-bot participant as a human;
- online with two local seats: the connection sends both seats' commands. The session protocol already carries per-seat commands,
  so it becomes two PRIs on one connection.

## What changes in Gameplay

1. **Seat 2 join:** a pad's Start in the lobby or in a match calls `World::addLocalSeat(padIndex, selection)`, which adds the
   participant on the host's team rules (campaign: replaces a squad AI member as co-op does; versus: takes a bot slot). Leaving gives
   the slot back to a bot (leave-to-bot exists).
2. **Input routing:** `platform::Input` gives per-device frames: KB/M + pad 0 → seat 1 (as today), pad N → seat 2. Each step,
   `Application` converts seat 2's frame into a RemoteCommand (the online client's `buildCommand`) and queues it like a received
   command (`remoteQueue_`), stamped with the local tick.
3. **Seat-2 camera / aim:** a second PlayerController-equivalent camera state for seat 2: yaw / pitch, fine aim, the vehicle camera
   rig, zoom. The seat path takes aimYaw / aimPitch from the command, and the camera rig code is in PlayerController. Plan: a second
   `PlayerController` instance used only as a camera / aim state machine, fed pad 2's look input, producing the command's aim. That
   is the one real refactor: PlayerController must run without owning `player_`'s pawn (it reads the seat-2 pawn).
4. **Presented frame:** `PresentedFrame` gains `seats[2]`, each with camera, HUD state (health, ammo, weapon, abilities, markers,
   damage direction, killer cam) and per-seat events (hit markers, kill feed own-lines). Today's single-seat fields become seat 0.
   Rendering's view list then draws a view per seat; the Frontend draws a HUD per seat.
5. **Local-only effects** that are local-seat-scoped today (hazard pain timer, damage-taken direction, hit reactions on the local
   rig, the local weapon presentation): per seat. Most read `localPlayer_`, so the seat-2 equivalents read its participant through
   the existing participant code paths.
6. **Campaign:** co-op with two locals plus squad AI is already the model, because the squad fills the remaining trio slots. Interacts:
   seat 2's presses come through the command (`seatInteract_` exists for remote seats). Game over when every human is down / dead
   (co-op rule).

## Order

1. PlayerController as a camera / aim state without the owned pawn, plus `addLocalSeat` + pad routing (seat 2 plays, drawn by
   Rendering's second view);
2. per-seat PresentedFrame HUD fields;
3. online: two local seats on one connection (the command batching);
4. campaign / Escalation / versus checks: one headless run each with an injected seat-2 command stream (a WFC_SEAT2BOT test switch:
   the bot brain drives seat 2's commands).

## Risks

- **PlayerController coupling:** about 76 `player_.controller()` sites read the one camera. Seat 2 needs its own instance; the
  refactor is mechanical but wide.
- **Same-match:** seat 2 is an extra participant, so versus identity and SIMHASH are unchanged unless seat 2 joins.
