# Gameplay Pass 24 — human playtest fidelity II handoff (agents/gameplay)

Commits c5c992c (24a) .. dfc20f8 (24f), on top of aa0dfd1. Full detail: FIDELITY.md "PASS 24", STATUS.md "GAMEPLAY PASS 24".

## What changed (code)
| file | change |
|---|---|
| src/game/Character.h / .cpp | presentation-only `drawYawOffset_` in meshMatrix (fast-turn stutter); per-chassis ToggleHidden in `meshVisible`; `VehicleState::quickTurnSerial` |
| src/game/ChassisDef.h / .cpp | `toVehRobotHide` / `toVehVehicleShow` / `toRobotRobotShow` / `toRobotVehicleHide` from character.json notifies; `vehicleWeapon2` (WeaponSocket_Primary2) |
| src/game/PlayerController.cpp / .h | presentation yaw between 60 Hz steps; tank 180 quick turn (TnQuickTurnCameraBehavior 0.3 s, 1.2 s cooldown); per-weapon fine-aim profile (FOV / orbit / offset / look, 0.5 s blend); vehicle muzzle alternation + TnPlayerPawn start-trace for the vehicle MG; Repair Ray dispatch; QA noclip |
| src/game/Weapon.h | `beam()` (RepairRay), `alternatesMuzzle()`, `muzzleSocket` |
| src/game/World.h / .cpp | Repair Ray beam (`fireRepairBeam` via `repairBeamHook`); projectile FX (FlightEffect / ExplosionEffect / grenade body mesh; renderer particle API detected at compile time); `noteVehicleShot`; DEV / QA `qa*` API (WFC_QA=1 only) |
| src/game/ChassisTests.cpp | notify-time parse checks |
| src/core/Application.cpp / .h | harnesses WFC_HEADJIT=<Hz>, WFC_VSOCKET, WFC_XFORMVIS, WFC_FINEAIMTEST, WFC_QATEST (+ WFC_QA=1), WFC_PROJFXTEST, WFC_MUZZLETEST; VEHPHYS tank quick-turn block; PARTICIPANT Repair Ray check |

## HudState additions (for glue)
- `repairBeam`, `repairBeamHealing`, `repairBeamStart`, `repairBeamEnd`, `repairBeamTarget` (-1 = no pawn) → Systems `onBeamWeapon(cls, firing, healing ? 1 : target >= 0 ? 2 : 0)` (Systems addendum).
- `vehicleShotSerial` (+1 per vehicle shot), `vehicleShotSocket` (0 = WeaponSocket_Primary, 1 = _Primary2), `vehicleShotMuzzle` (socket world position) → the vehicle muzzle flash / tracer (Systems WeaponFx) should play at this socket when the serial changes.
- `Character::vehicleState().quickTurnSerial` → Systems special180 (Auto_180_Turn).

## Merge notes
- Projectile FX: World.cpp calls `spawnParticleEffect` / `setParticleEffectTransform` / `stopParticleEffect` through SFINAE helpers. With Rendering's Renderer.h in the tree they switch on automatically; verify with `WFC_PROJFXTEST=1` → "renderer particle API: present" and 3/3.
- Rendering's `prewarmDynamicMesh` call (Character model assignment) is Integration's to add, as agreed with Rendering.
- DEV / QA: Frontend's QA window (F10, WFC_QA=1) calls `World::qa*`; `addMatchOpponent(name, drawn = true)` for a visible dummy.

## Playtest expectations after merge
- Fast left/right turns: body steady against the camera at 60 / 144 / 240 Hz (robot, hover, boost; the jet keeps its authored servo lag).
- Transforms both ways on every chassis use that chassis' authored hide / show times; no invisible frames.
- Tank Shift in vehicle form: one 180° turn in 0.3 s; no repeat while held; 1.2 s cooldown.
- Fine aim (right mouse toggle): Null Ray FOV 20 (slow look), Heavy Pistol / Burst Rifle 30, others 45; half move speed.
- Repair Ray heals teammates 60 HP/s, damages enemies 60/s.
- Projectiles show their authored trails and explosions (no cubes); thrown grenades show their mesh.
- Scout / jet / truck vehicle weapons alternate left / right gun each shot; tank cannon unchanged.

## Open (documented PARTIAL)
- Robot-form instant-hit trace still starts at actor + eye height; RE confirmed TnPlayerPawn.GetWeaponStartTraceLocation (crosshair ray point nearest the pawn) applies to robots too — pending a decision.
- Repair Ray lock-on target pick; Null Ray scope sway; PlasmaCannon charge-level visuals; grenade spin; fine-aim HUD scope symbol (Frontend).
- Car2 / Car4 chaos stress: 1 under-deck flag each (same as Pass 23).
