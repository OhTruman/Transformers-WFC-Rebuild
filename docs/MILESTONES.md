# WFC Rebuild — Milestones

## M0 — Toolchain & project skeleton ✅
- Portable llvm-mingw (Clang 23), CMake 4.4, Ninja in `.toolchain/` (not committed).
- Root CMake project, `build.ps1`, `.gitignore`.

## M1 — First playable graybox ✅ (current)
- Real Win32 window + OpenGL 4.6 context (fixed-function rendering).
- Stable fixed-timestep loop (60 Hz sim, uncapped render), clean shutdown.
- Graybox arena: ground grid + 7 blocks + 3 spinning pickups.
- Placeholder player box, third-person mouse-look follow camera.
- WASD movement, gravity, jump, ground plane.
- **ROBOT ⟷ VEHICLE** toggle (F): different size, colour, speed, accel, jump.
- Debug HUD in the window title: FPS, position, current form.
- Gamepad (XInput) move + look supported.
- Headless smoke mode (`WFC_SMOKE_FRAMES=N`) for automated verification.

Verified: builds clean; 150-frame smoke run exits 0; interactive run stays alive & responding.

## M2 — Solid movement & collision (next, recommended)
- Box/AABB collision vs. `World` blocks (walls, standing on blocks), not just a flat plane.
- Step-up / slopes; proper capsule vs. box.
- Per-form collision volumes; transform blocked when obstructed.
- Ported idea available from earlier prototype: `World::overlaps(feet, radius, height)`.

## M3 — Combat & entities
- Weapon firing spawns projectiles/hitscan; damage → Health; death/respawn at SpawnPoints.
- Simple AI pawn (idle→chase) as a target dummy.

## M4 — Game modes & HUD
- Real on-screen HUD (bitmap text), scoreboard, one full FFA/team mode loop.

## M5 — Asset pipeline (parallel track)
- Inventory + extraction tooling for the local owned game data (see docs/ROADMAP.md).
- Replace graybox with real geometry/animation once importers exist.

## Deferred
- Online/multiplayer transport (separate future track; only a null seam exists today).
