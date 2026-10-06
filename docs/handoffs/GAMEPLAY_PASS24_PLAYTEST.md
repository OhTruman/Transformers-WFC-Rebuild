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
- Repair Ray lock-on target pick; Null Ray scope sway; PlasmaCannon charge-level visuals; grenade spin; fine-aim HUD scope symbol (Frontend).
- Car2 / Car4 chaos stress: 1 under-deck flag each (same as Pass 23).

## Addendum: 24h-24k (91672f1..4177d5e)

Integration merged up to 24g (91672f1). This addendum covers the commits after it.

| commit | change | files |
|---|---|---|
| 2f258b6 (24h) | cache + prewarm the eight default MP chassis at startLocalMatch; cache each selected body during PendingMatch (load scheduling only) | World.cpp |
| 7dbf60f | comment fix (the chassis cache lives per World) | World.cpp |
| d2c17db (24i) | robot hitscan / Repair Ray traces start at TnPlayerPawn.GetWeaponStartTraceLocation (crosshair-ray point nearest the pawn); the Repair Ray ribbon still starts at the muzzle | PlayerController.cpp, World.cpp |
| ba0fb4c (24j) | robot projectiles spawn at the held weapon's MuzzleFlash socket (`World::heldWeaponMuzzle` via `heldWeaponMuzzleHook`); weaponFireHook no longer adds 1.5 m (vehicle rockets started 3 m past their socket) | World.h/.cpp, PlayerController.cpp |
| 7eafecb (24k) | Plasma Cannon charge levels (TnChargeWeapon: hold / release, 3 levels, ShotCost 25/50/100, drain at full charge, cancel on switch / reload / melee / transform); thrown grenade spin (−549°/s pitch until at rest); per-class projectile visuals ("<id>#<k>") | Weapon.h, PlayerController.cpp, World.h/.cpp |
| 4177d5e | WFC_DROPTEST diagnostic (10 m hover drop trace) | Application.cpp/.h |

### Merge notes
- PlayerController.cpp: the robot fire block is restructured. The shot body is the `fireRobotShot` lambda, called by the normal
  path and the new `w.charge()` branch. Systems / Integration hunks in that block need re-placing inside the lambda.
- World.cpp: `loadProjectileVisuals` now loops over every projectile class; `spawnProjectile` picks the visual by `w.projClass`;
  grenade spin is in the grenade tick and in draw. Systems' projectile audio hunks sit beside these; keep both.
- World.h: `heldWeaponMuzzleHook` (installed in World::load next to repairBeamHook); new Projectile fields `yaw0 / pitch0 / spin /
  spinRate`; test accessors `projectilePos / Vel / Damage / Spin / Resting`.

### HudState additions (for Frontend / Systems)
- `weaponChargeState` (0 idle, 1 charging, 2-4 levels 1-3) and `weaponChargeMessage` ("CHARGING" / "READY" / "") for the Plasma
  Cannon (TnChargeWeapon.GetHudMessage).
- Charge presentation not done here: material glow 0 / ⅓ / ⅔ / 1 and WP events 9-12 (charge loops, release) belong to
  Rendering / Systems.

### New harnesses
WFC_RMUZZLETEST (4/4), WFC_CHARGETEST (7/7), WFC_DROPTEST (diagnostic, no pass / fail).

### Validation (agents/gameplay 4177d5e)
WEAPON 19, SWITCH 32, PARTICIPANT 22, TDM 43, CTF 12, SCORE 9, FINEAIM 3, MUZZLE 5, RMUZZLE 4, CHARGE 7, PROJFX 2 (3/3 with the
renderer API). In TDMTEST, all eight default chassis load before "match: launched".

### Playtest expectations
- Plasma Cannon: a tap does nothing; hold 0.75 / 2.0 / 3.5 s for level 1 / 2 / 3 (small / medium / large trail); the HUD message
  reads CHARGING, then READY.
- Robot shots go where the crosshair points from the barrel; rockets / grenade launcher rounds leave from the muzzle.
- Thrown grenades tumble end over end until they settle.
- No hitch the first time a bot or player uses a chassis mid-match.

### Open
- Experimental's vehicle checks (see their tools/fidelity): drop10 is a harness false positive (WFC_DROPTEST trace sent);
  hover_jump.pitch_kick = 0 is intended (RE pass 4 §A4); upright.* = −1 is a harness blind spot; step_025 attitude is open
  (question with RE).

## Addendum 2: 24l-24m (1fa3ad9..ed08d93)

Integration has 24h-24k up to 1fa3ad9 (08j). This covers the rest.

| commit | change | files |
|---|---|---|
| ac2db5f (24l) | Plasma Cannon charge presentation state: `weaponChargeGlow` (MaterialGlowAmount 0 / 1/3 / 2/3 / 1), `weaponChargeSerial` (+1 per charge state change), `weaponChargeFizzle` (release before level 1) | Weapon.h, PlayerController.cpp, World.h/.cpp |
| c804fe0 (24l) | `weaponChargeShotLevel` (1-3) for the level-specific fire sound | same |
| ed08d93 (24m) | hover grounded pitch / roll per RE pass 4 A4 (corrected); suspension probes start outside walls; chassis preload narrowed to participants' bodies; charge shot level set before the shot copy (Systems' finding); WFC_RISERTEST, WFC_VEHWALLTRACE | CharacterMovement.cpp, World.cpp, PlayerController.cpp, Application.cpp/.h |

### Merge notes
- Systems M08k (agents/systems 4a84f86, charge audio) reads the 24l HudState fields; take 24l and 24m first, then Systems'
  patches in their order.
- CharacterMovement.cpp: the hover UpdateTurn block (the grounded branch no longer touches pitch / roll w) and the suspension
  probe loop (the mount clamp after `mount = ...`). No other lane edits these.
- World.cpp: the startLocalMatch 8-default preload loop is removed; World::tick now caches every participant's resolved body
  in any match state, not only PendingMatch.

### Behaviour changes to expect
- Hover vehicles now tilt over bumps and kerbs (springs drive pitch / roll on the ground): about 1-2.5 deg crossing a 0.15 m
  kerb at full hover speed, settling within about 1 s. The hover jump shows its authored nose-up kick and levels in the air
  at 5 % per tick (no longer a one-step snap).
- Glancing wall slides keep the car level: 0 deg in hover, 0-8 deg in boost (Pass 23 left 6-31 deg).
- Memory: 2 chassis loads in TDMTEST (was 9 per match on 08i); every participant's body still loads before its spawn.

### Validation (ed08d93)
WEAPON 19, SWITCH 32, PARTICIPANT 22, TDM 43, CTF 12, SCORE 9, XFORMVIS 16, CHARGE 9, MUZZLE 5, RMUZZLE 4, PROJFX 2 (3 with
the renderer API), FINEAIM 3, VEHPHYS 27; HEADJIT and DROPTEST unchanged. Stress: 0/760 transforms under the map on Car2 / Car4
/ Truck3 / Tank3 / Jet4. Chaos A/B against the old behaviour:
- Car4 ammo-crate under-floor: pre-existing (reproduced with the old behaviour).
- Remaining stuck runs: prop pockets (horizontal blocking, level attitude), not 24m.

### Open
- Experimental's synthetic step_025 / step_050 against RE's pitch estimates (requested on this head).
- Wall sliding friction (RE: mu 0.10-0.14 against walls) is not modelled; contacts keep all tangential speed [PARTIAL].
- Match-start spawn frame CPU spike (52-66 ms, Integration 08i RENDERSTATS): not yet profiled.
