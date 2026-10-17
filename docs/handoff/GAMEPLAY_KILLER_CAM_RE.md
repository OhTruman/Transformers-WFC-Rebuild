# Killer cam / MP spectating after death

WFC RE lane, 2026-10-17, for Gameplay. This follows `DEATH_CAMERA_RE.md`.

Sources:
- script decompiles: TnCameraStrategyManager, TnSpectateTargetChooser, TnSpectateCameraInitialViewCameraBehavior,
  TnOrbitRotationCameraBehavior, TnPlayerController, Engine.GameInfo / PlayerController, TnRespawnHelper*;
- authored.db and Xe-TransInput.ini.

## 1. Which strategy (CONFIRMED)

- `Dead` lasts 3.0 s, then `PlayerSpectating.BeginState` → `TnCameraStrategyManager.StartSpectating`.
  - This always uses **`SpectatingCameraStrategyAsset` = `CAM_Strategies_p.SpectateOverTheShoulder_STRATEGY`**.
  - The asset is cloned once.
- **`SpectateActor_STRATEGY` is used only by `SpectateActorSubject`.** Its callers are Kismet `TnSeqAct_SpectateActor`,
  `PC.InitializeSpectateActor` (Kismet / debug) and TnDebugManager. **Never by death.**

## 2. Initial view (CONFIRMED)

1. **Every target change is a hard cut.** `TnSpectateTargetChooser.SetNewTarget` → `SetNewSpectateTarget` →
   `Camera.SetActiveCameraStrategy(SpectateOTS, target)` + **`Camera.Reset()`**.
   - The reset teleports, so the transition time is 0. The strategy's 1.5 s TransitionTime is not seen on target changes.
2. **Orbit after the reset:** `TnOrbitRotationCameraBehavior.Reset` sets **`OrbitRotation = (Pitch 0, Yaw = target.Rotation.Yaw,
   Roll 0)`**: directly behind the target, level.
   - `Player = TnPlayerPawn(target)`. If the target isn't a TnPlayerPawn, the yaw is kept and only pitch and roll are zeroed.
3. Then the spectator **can orbit with their own right stick:** TnOrbitRotation applies the spectator's input (LookSpeed rows, pitch
   ±75°).
4. **`TnSpectateCameraInitialViewCameraBehavior` does nothing for pawn targets.** Its `SetActor` casts the target to `CameraActor`.
   It only acts when the target is the level camera (§3 fallback): for the first 0.1 s it copies that camera's (pitch, yaw) into
   the orbit.
5. **So the view is not "the killer's view" and not "corpse → killer".** It is the standard spectate over-the-shoulder rig, snapped
   behind the target's facing.

## 3. Cycling and invalid targets (CONFIRMED)

- **Buttons:** `SwitchSpectatorTarget ±1` (Xe-TransInput.ini):
  - **LB = +1, RB = −1** (shared with Ability1 / Ability0);
  - keyboard: `]` = +1, `[` = −1.
- **Valid target** (`IsValidTarget`): IsA `SpectateClass` (TnPawn), not dead, and (**same team as the spectator, or == the killer**).
  - **Teammates only, apart from the killer.**
  - In FFA, every team number is the same, so H: all players are valid.
- **Order:** `WorldInfo.AllPawns` iteration order, wrapping in both directions. Pressing a button clears the killer exemption
  (`Killer = none`), so the killer leaves the cycle.
- **Start:** `Activate` sets `Killer = TnPlayerPawn(_LastPawnWhoKilledMe)` and picks it if valid; otherwise `SwitchTarget(0)` = the
  first valid pawn.
  - `_LastPawnWhoKilledMe = Killer.Pawn`, or none when there is no killer controller. It is reset on Possess.
  - Suicide: the own pawn is dead, so it is invalid. KillZ: no killer. Either way you get the first valid teammate.
  - A non-TnPlayerPawn killer (H: AI) is not chosen, which also gives the first valid teammate.
- **Recheck:** a timer every **1.0 s** (`ValidSpectatingTargetRecheckTime`). If the current target died or left, it calls
  `SwitchTarget(0)`, i.e. the first valid pawn.
- **No valid pawn:** it uses `TnWorldInfo.LevelCamera` if the map has one; otherwise it keeps the current view.
- **HUD:** `_global.SetSpectateTarget(PlayerName)` on each change.

## 4. Respawn while spectating (CONFIRMED / HIGH)

- **Respawn is not input-gated.** `TnRespawnHelperWave` queues the player at death with a per-player 5 s timer, then `Respawn` calls
  `Game.RestartPlayer`. Instant-respawn reward: 0 s.
  - Death to respawn is therefore about 3 s of death cam plus about 2 s of spectating.
  - `TnPC.PlayerSpectating` doesn't override fire.
  - U: the state's parent class wasn't resolved from the decompile. If it is Engine `Spectating`, fire would call
    `ServerViewNextPlayer`. It does not respawn.
- **Camera on respawn: a hard cut.**
  1. `GameInfo.RestartPlayer` → `Possess` → `PlayerWalking.BeginState` → `StartForm(0)` → the RobotForm / OTS strategy.
  2. Then `ClientSetRotation(Pawn.Rotation, true)` → `TnPC.ClientSetRotation` → **`PlayerCamera.Reset()`**, a teleport.
  3. The orbit resets behind the new pawn. There is no blend from the spectate view.

## 5. PCS rows while spectating (CONFIRMED)

- **The target's state is never consulted.**
  - `Spectating.BeginState` → `ClearPCS()`.
  - The Spectating state has no OnUpdate.
  - `UpdateWeaponPCS` / `Update` act on the manager's own `_TransformerPawn`, which is the spectator's dead pawn or none.
  - `HasAllPlayerCameraStates` reads the spectator's camera PCS set, which stays empty.
- **So only the default rows apply:** orbit 1600, FOV 80, default look speeds and offsets. A sniping killer does **not** zoom the
  spectator to FOV 30 / distance 100.
