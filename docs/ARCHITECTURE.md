# WFC Rebuild — Architecture

Clean-room reconstruction skeleton. All behaviour here is original placeholder code; nothing
is copied or derived from the original executable or its assets. Placeholders are meant to be
replaced incrementally.

## Layering (dependencies point downward only)

```
            +---------------------------+
            |         main.cpp          |
            +-------------+-------------+
                          |
            +-------------v-------------+
            |   core::Application       |  lifecycle, fixed-step loop, title HUD
            +----+----------------+-----+
                 |                |
     +-----------v----+   +-------v-----------+
     |  game (logic)  |   |  render (IRenderer)|  platform-independent
     +-----------+----+   +-------+-----------+
                 |                |
     +-----------v----------------v-----------+
     |            platform (IWindow, Input)   |  neutral interfaces
     +-----------+----------------------------+
                 |
     +-----------v------------+   +----------------------+
     | platform/win32 (Win32) |   | render/gl (OpenGL)   |  concrete adapters
     +------------------------+   +----------------------+
```

**Rule:** `game/*` and `render/*` interfaces never include OS/GL/Win32 headers. Only the
adapter folders (`platform/win32`, `render/gl`) do. This is what keeps gameplay portable to
future targets (Xenia reference testing, Xbox/RGH, native Windows, WebAssembly) by swapping
adapters, not rewriting logic.

## Modules

### core/
- `Math.h` — Vec3, Mat4 (column-major for GL), transforms, perspective/lookAt.
- `Log` — printf-style logging, mirrored to stdout + `wfc.log`.
- `Time` — monotonic clock + `FixedStepClock` accumulator.
- `Config.h` — tunable constants (all placeholder values).
- `Application` — owns window/renderer/world; runs the loop.

### platform/  (neutral) + platform/win32/ (adapter)
- `Input.h` — `Button` enum + `InputFrame` (held/pressed edges, mouse delta, gamepad axes).
- `Window.h` — `IWindow` interface + `createWindow` factory.
- `win32/Win32Window.cpp` — Win32 window, WGL/OpenGL context, keyboard/mouse/XInput polling.

### render/  (neutral) + render/gl/ (adapter)
- `Camera.h` — position + yaw/pitch, builds view/proj matrices.
- `Renderer.h` — `IRenderer` (drawBox / drawGroundGrid / drawLine) + `createGLRenderer`.
- `gl/GLRenderer.cpp` — fixed-function OpenGL 1.1 graybox renderer (no extension loading).

### game/  (all platform-independent)
- `Actor` — base transform + tick/draw.
- `TransformState` — `Form` (Robot/Vehicle) + per-form tuning (the core mechanic).
- `Character` — controllable pawn: form, velocity, health, weapon, ability.
- `CharacterMovement` — integrates a neutral `MoveIntent` (accel, gravity, ground clamp, jump).
- `PlayerController` — maps `InputFrame` → `MoveIntent` + follow camera (only reader of input).
- `Player` — owns a pawn + controller.
- `World` — owns level blocks, spawns, dynamic actors, the player; ticks and draws.
- `GameMode` — match phase/rules (scaffold).
- Scaffolds: `Health`, `Weapon`, `Projectile`, `Ability`, `Team`, `SpawnPoint`, `AI`,
  `Pickup`, `Objective`.

### online/
- `Online.h` — `IOnlineService` seam + `NullOnlineService`. **No networking implemented.**

## Data flow per frame
1. `Win32Window::pump` fills a neutral `InputFrame`.
2. `PlayerController::handleInput` updates camera orientation + buffers `MoveIntent` (per frame).
3. Fixed steps: `World::tick` → `PlayerController::applyToPawn` → `CharacterMovement::update`.
4. `PlayerController::updateCamera` positions the third-person camera.
5. `IRenderer` draws ground grid, blocks, actors, player; window presents.
