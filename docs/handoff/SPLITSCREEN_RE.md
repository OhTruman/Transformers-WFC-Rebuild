# Split screen (RE, 2026-10-23; for the PC two-local-player item)

**Labels:** C = shipped script / config / CDOs; H = stock UE3 behaviour, not traced in the exe.

## 0. Headline (C): the original has no split screen
- **One local player only:** nothing in TransGame / HM_Engine / TransContent script calls `GameViewportClient.CreatePlayer`,
  `SetSplitscreenConfiguration` or `LayoutPlayers`. Only the stock engine class and its exec `DebugCreatePlayer` exist.
- **Menus:** no movie has a split, guest or second-player element (all 38 GFx scanned).
- **Single-seat assumptions everywhere:**
  - `TnWorldInfo.GetLocalPlayerController()` returns the **first** local TnPlayerController (45 calls in 19 classes: HUD, UI,
    interacts, checkpoints…);
  - `SeqAct_SaveActiveCheckpoint` uses `GamePlayers[0]`;
  - `TnGameViewportClient.AssignControllerToLocalPlayer` only ever sets `GamePlayers[0]`;
  - one HUD movie (Hud_GFX, `UpdateLocalPlayerName`).
- **Every multiplayer mode is online / system link**, one seat per console:

| Game class | MaxPlayers |
|---|---|
| TnFrontEndGame | 1 |
| TnCampaignGame (co-op) | 3 |
| TnSurvivalGame (Escalation) | 4 |
| Versus / party lobby | 10 |

- **So everything below is "what the original does with its one seat"** plus the stock UE3 split machinery the rebuild can mirror.
  The split modes and layout are a **rebuild decision**; there's no original to match.

## 1. Pad-to-seat assignment in the original (C)
- **Binding the pad:** at the press-start screen, `TnGameViewportClient.MapInputKey` binds the controller that pressed a button to
  `GamePlayers[0]`, while `_HasAssignedControllerId` is false and `IsControllerSelectionAllowed(key)` passes.
  - On console, selection must be allowed (Allow / DisallowControllerSelection), the key must be **`XboxTypeS_Start`**, and no
    message box may be open.
  - On PC builds any key is accepted (`!IsConsoleBuild` → true).
- **Re-pick:** `ForceControllerSelection` clears the flag (back to press-start: sign-out, profile change), so the next Start re-binds.
- **Other pads:** `IsValidController` drops their input (no LocalPlayer for that ControllerId), and also drops the bound pad's input
  while the system UI is open (`_IsExternalUIOpen`).
- **Per-pad remap from the profile:** `MapInputKeyForController`, in game only:
  - **SwitchRightThumbstickAndB** swaps B ↔ RightThumbstick;
  - **AlternateControlScheme**: Y → LeftThumbstick, LeftThumbstick → none, DPad_Up → Y.
- **For the rebuild (mirroring that rule):**
  - **Seat 1** = keyboard / mouse, or the first pad that presses Start at press-start;
  - **seat 2** = the next *other* pad that presses Start where joining is allowed. Stock: `CreatePlayer(ControllerId, OutError,
    bSpawnActor=true)`. On a client it sends `SendSplitJoin` to the server (H, stock).
  - Each seat keeps its own profile remap. Ignore input from unbound pads, as the original does.

## 2. Stock layouts (C, `Engine.Default__GameViewportClient`)

| SplitscreenType | Views (origin x, y, size x, y) |
|---|---|
| 0 eSST_NONE | (0, 0, 1, 1) |
| **1 eSST_2P_HORIZONTAL, the default 2P** | **P1 (0, 0, 1, 0.5) top; P2 (0, 0.5, 1, 0.5) bottom** |
| 2 eSST_2P_VERTICAL | P1 (0, 0, 0.5, 1) left; P2 (0.5, 0, 0.5, 1) right |
| 3 / 4 (3P favour top / bottom), 5 (4P quad) | as in the CDO |

- `SetSplitscreenConfiguration` forces type 0 for one player. For 2 players it keeps 1 or 2, else uses Default2PSplitType
  (horizontal).
- **Title-safe zone (CDO):** max 0.9 / recommended 0.8 of the screen. H, stock: applied per view.
- A 16:9 PC suggests vertical (left / right), which keeps each view at 8:9. Horizontal is the stock default. **Rebuild choice.**

## 3. Per-seat rules to carry over from the single-seat code (C; what the second seat needs)
- **Profile and customization:**
  - each seat has its own `TnProfileSettings` (input remap, sensitivity…), its own `CustomCharacterData` (IsChassisUnlocked) and
    its own XP manager.
  - The original reads these from the bound PC's profile. Seat 2 needs a second profile slot; on PC, a local "Player 2" profile.
  - The original's sign-in was a 360 gamer profile per pad, so there's nothing to port.
- **HUD:**
  - one Hud_GFX per PlayerController, sized to its view;
  - the scoreboard / player list highlights `LocalPlayerName` per movie;
  - **every `GetLocalPlayerController()` caller must take the owning PC instead of "the first one"** (the main porting work for
    Gameplay / Frontend).
- **Camera:** one camera per PlayerController (stock, H). The third-person camera and its collision pull-in run per view
  unchanged. Cinematic mode and matinee director tracks apply to every PC the Kismet targets (`SeqVar_Player` with bAllPlayers →
  both seats).
- **Audio listener (H, stock UE3):** one listener per local player. Each sound plays once, attenuated against the **nearest**
  listener. Music and VO are global. The original never had a second listener.
- **Checkpoints, saves, achievements:** the original keys these to `GamePlayers[0]`, the primary seat. Keep it that way (the
  campaign save belongs to seat 1).
- **Lobby (C):** the party / game lobby counts players by PRI. Two local seats are two PRIs on one connection and count against
  MaxPlayers (campaign 3, ESC 4, versus 10). The original offers no "add local player" lobby action; it's a rebuild addition.

## 4. Render toggles
- **The original has none:** there's no split code path, and no split-specific LOD / shadow / FX config in the cooked ini.
- **Stock UE3:** every view renders the full scene (shadows, particles, post-process per view). LOD and particle
  significance use each view's own FOV and screen size, so half-height views pick lower LODs automatically.
- **For Rendering:** any reductions (shadow resolution, particle caps, motion blur) are performance choices, not fidelity items.
  - Post-process volumes are evaluated **per view** (CAMPAIGN_POSTPROCESS §0: the containing volume of *that* view's point).
  - Matinee Fade / ColorScale go per PlayerController.
