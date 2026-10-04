# Handoff to Frontend (and Gameplay): the in-match HUD (Hud_GFX) and the Canvas layer

**Human report:** no useful time remaining, team score, player score context, kill feed or match-end UI.

**Integration M05:** "the Scaleform HUD movie (Hud_GFX: clock and score) is not drawn in play, only the renderer
reticle. Ownership is an open Frontend / Rendering handoff."

## Ownership: one presenter per original layer

Sources: RE `MILESTONE05_FRONTEND_GAMEPLAY_BLOCKERS.md` §G / §H, `MILESTONE05_GAMEPLAY_UNKNOWNS.md` §5, and AssetTools
`frontend_hud.json` / `future_hud_handoff.json` (TnHUD CDO callbacks, TnUIControllerMultiplayer movies).

| element | original layer | data source (Gameplay) | rebuild owner |
|---|---|---|---|
| match clock (MM:SS, ≤ 10 s red) | **Hud_GFX** (Scaleform), pull `<CurrentGame:CurrentCountdown / IsCountingDown>` | `hudState` → `MatchValues` (exists) | **Frontend** GfxHost: open Hud_GFX in the InGame UI state |
| team scores (bars, ally/enemy colours, `25 + Score/GoalScore × 141`) | Hud_GFX, poll `<CurrentGame:Teams / GoalScore>` every 500 ms | MatchValues (exists) | Frontend |
| player score | `<PlayerOwner:Score>` (scoreboard: Score / Kills / Deaths) | MatchValues (exists) | Frontend |
| health / overshield / ammo / grenades | Hud_GFX pushes `NotifySegmentedHealthChanged`, `NotifyOverShieldChanged`, `NotifyWeaponClipAmmoChanged`, `NotifyWeaponReserveAmmoChanged`, `NotifyGrenadeAmmoChanged` | Gameplay HUD observers | Frontend calls the `_global` functions on change |
| crosshair | Hud_GFX `mc_crosshairIonBlaster` (`NotifyWeaponSpreadChanged`, `NotifyTargetTypeChanged`, `NotifyWeaponReticuleTypeChanged`, `NotifyLockOnStateChanged`) | Gameplay | Frontend. When Hud_GFX is active, set `IRenderer::ReticleState.visible = false`: the renderer's reconstruction of the same clip must not draw twice |
| kill / score notifications ("kill feed") | Hud_GFX `_global.GameMessage` (TnHUD `GameMessageCallBack`), `PointEvent` (`KillTransactionObserver`), `RewardAnnouncement`, `GameAnnouncement` | Gameplay kill / score events | Frontend. Placement, rows, lifetime and fade are Hud_GFX timeline / AS2 behaviour: running the movie reproduces them; nothing is re-authored |
| pre-game countdown | PreGameCountdown_GFX | exists | Frontend (done in M05) |
| death → spectate → respawn timer | `SpectatingUI` = MultiplayerRespawn_GFX, `<PlayerOwner:TimeToRespawn>` | exists | Frontend (done in M05) |
| match end, scoreboard, results | `EndGameUI` = EndGameStats_GFX (+ PlayerList_GFX); `ScoreboardMovie` on TnHUD for the in-match scoreboard | exists | Frontend (done in M05; the in-match scoreboard toggle input is RE UNKNOWN) |
| player tags / objective markers / tombstones | **Canvas** (`TnObjectiveMarkerType*.Draw`, UI_HudMarkers_p materials, MarkerFont) | Gameplay supplies which pawns / objectives and their rule visibility | **Rendering**: `render::HudMarkers` (this branch) |
| radar / minimap | — | — | **UNKNOWN**. No radar / minimap object exists in the authored data (AssetTools). RE is verifying; nothing is drawn |

## Composition (renderer contract, agents/rendering)
Order within a frame:
1. scene and post;
2. Canvas material tiles (markers);
3. renderer reticle (when visible);
4. `drawScreenTriangles` batches in submission order.

If the GfxHost draws Hud_GFX through `drawScreenTriangles`, or with its own GL after `endFrame`, it lands above the
Canvas layer. UE3 draws the Canvas HUD before the GFx movies, so that order matches the original.

## Gameplay → Rendering for the Canvas layer
Build `render::MarkerRequest`s each frame and call `HudMarkers::draw(renderer, camera, w, h, requests, dt)`. Example:
`WFC_MARKERTEST` in `Application.cpp`.

Each TDM player tag request:
- type `TnObjectiveMarkerTypeTransformerVersus`;
- setup `AllyMarkerSetup` or `EnemyMarkerSetup` (Disguised = ally setup);
- `base` = pawn location;
- `labelZ` = collision half-height;
- `label` = PlayerName.

Gameplay omits the markers the rules hide:
- the viewer's own pawn;
- dead pawns;
- enemies without `TnBuffSeeEnemyObjectiveMarkers` / HardLock / RevengeMarker;
- labels without line of sight, when cloaked.

It sets `drawHealthBar` only for a Scientist viewer. Focus, sizes, on/off-screen and label colours come from the authored
setups.
