# MP input contract vs the original's player-input semantics (RE review of ONLINE_MP_ARCHITECTURE §10.2, a09b4054; 2026-10-18)

Scope: a rules review, not networking code. It compares the original's input semantics (TransGame / Engine script; CONFIRMED (C)
unless marked HIGH (H)) against Systems' remote-command contract (int8 move axes, yaw u16 over 0..2π, pitch i16 over ±π/2, idle =
any held / pressed bit, a non-zero axis, or an aim change > 0.5° vs the previously applied command).

## 1. Aim precision (C): no divergence

- The original replicates the view as **`((Rotation.Yaw & 65535) << 16) + (Rotation.Pitch & 65535)`** in every
  `ServerMove` / `DualServerMove` / `ServerMoveWithOld` (`PlayerController.CallServerMove`).
- UE3 rotators are 16-bit integer units: 65536 = 360°, so 0.0055° per unit, for both yaw and pitch. `ServerMove` rebuilds
  `ViewRot.Pitch / Yaw` from them with Roll 0.
- **Yaw u16 over 0..2π = exactly the original's yaw resolution. Pitch i16 over ±π/2 is 2× finer than the original** (the original
  spends half its pitch range on angles it never uses).
- No feel difference: every peer applies the same decoded numbers, which is what matters.
- Optional for bit-exactness with the original: quantise pitch on the original's 65536-per-360° grid (i16 over ±π) so aim
  arithmetic sees the same steps.

## 2. Move input (C): no feel divergence; two flags to carry

- The original sends **Acceleration × 1000** as a vector (`NewMove.Acceleration * 1000`). The OldMove copy is compressed per axis via
  `CompressAccel(0.05 × Accel + 0.5)`. Acceleration comes from the analog axes (`aBaseY × MoveForwardSpeed`,
  `aStrafe × MoveStrafeSpeed`).
- **So movement is analog in the original.** int8 axes (±127, ~0.8 % steps) keep it analog. Make sure the full stick magnitude (not
  just direction) survives the int8 encode, so half-tilt walking stays half speed.
- **Flags the original replicates with each move** (SavedMove / TnSavedMove `CompressedFlags`), which the buttons word must cover:

| Bits | Flag |
|---|---|
| 0–7 | DoubleClickMove (dodge direction enum) |
| 8 | bRun |
| 16 | bDuck |
| 32 | bPressedJump |
| 64 | bDoubleJump |
| 128 | bPreciseDestination |
| **256** | **bUserInputDetected** (see §3) |
| **512** | **bUsingFineAim** (server calls FineAim / StopFineAim) |
| **1024** | **bPressedHover** (→ `_PressedHover`) |

- Fine-aim and hover are **move flags** in the original (applied in `TnSavedMove.SetFlags` on the server), not separate RPCs. They
  must be held / pressed bits in the command, applied in the same step as the move.

## 3. What counts as "idle" for the 180 s kick (C, with two H)

**The original's rule:**
- **Config:** Xe-TransGame.ini `[Engine.GameInfo] MaxIdleTime = 180`, `bKickLiveIdlers = true`. `GameInfo` floors MaxIdleTime at 20.
- **Activity:** `PlayerController.LastActiveTime` is refreshed only:
  1. on `ServerMove` when **`MoveFlags & 256`** (`bUserInputDetected`) is set;
  2. on `ServerTeamSay` (team chat);
  3. at controller begin play.
- **`bUserInputDetected` = `PlayerInput.UserInputDetectedThisFrame()`:**
  `PressedKeys.Length > 0 || RawJoyUp ≠ 0 || RawJoyRight ≠ 0 || RawJoyLookUp ≠ 0 || RawJoyLookRight ≠ 0`, i.e. **any held button,
  or any non-zero raw move stick or look stick**. It is input-based, not result-based: movement, firing or aim change are not required.
- **The kick:** `GameInfo.KickIdler(PC)` → `AccessControl.KickPlayer(PC, IdleKickReason)`. It is called natively when
  TimeSeconds − LastActiveTime > MaxIdleTime (H: stock UE3 `PlayerTick` check, which skips local players on the host).
- **Private matches never kick:** `TnGame.InitGame` sets `bKickLiveIdlers = false` when `IsPrivateMatch()`. `IsPrivateMatch()` is
  **true when there is no online game interface or no game settings**, else `GameSettings.IsPrivateGame()`.

**Divergences in §10.2 that change when the rule triggers:**
1. **Private / offline matches: the original has no idle kick at all.** No interface or settings → private → bKickLiveIdlers false.
   If our LAN / direct-join / offline-with-bots matches correspond to the original's private (system-link or invite) matches, they
   must **not** kick idlers. The 180 s kick applied only to public (matchmade) Live games, which are out of scope.
   **Recommendation:** disable the idle kick unless a "public match" mode exists, or make it a host option, default off.
2. **Look stick held at a pitch limit:** the original counts any non-zero look-stick deflection as active. Systems' "aim change > 0.5°"
   does not, e.g. holding look-up at the pitch clamp, or turning slower than 0.5° per command (fine-aim creep). Use **"look-axis
   non-zero"** rather than "aim delta > 0.5°", or carry the original's flag-256 bit in the command.
3. **Chat counts as activity** in the original (ServerTeamSay). If voice / quick-chat exist, decide whether they refresh activity;
   the original's text team-say did.
4. **The host's local player** is not kicked (H, stock).
5. **Agreements:** "any held / pressed bit" = PressedKeys; "non-zero axis" = RawJoyUp / Right. Both match, provided the axes are taken
   **before** any game-side zeroing (the original uses the raw axes after the input dead zone, H).

## 4. Summary for Systems / Gameplay

- Aim: OK (pitch is finer than the original; optional grid match). Move: OK if analog magnitude is kept; FineAim / Hover must ride in
  the command as bits.
- **Idle:** implement the original's input-based rule (any button held, or any move / look axis non-zero), refreshed by chat. **Do
  not kick in private / offline matches.** That last point is the one that most changes behaviour.
