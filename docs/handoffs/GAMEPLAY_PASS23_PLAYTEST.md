# Gameplay Pass 23 — human playtest fidelity handoff (agents/gameplay)

Scope: the five playtest items only (weapon switching, vehicle handling, Scout height, initial score, preserve).
No bots / online / new features. Full detail: FIDELITY.md "PASS 23", STATUS.md "GAMEPLAY PASS 23".

## What changed (code)
| file | change |
|---|---|
| src/platform/Input.h, win32/Win32Window.cpp | `InputFrame::mouseWheel` (same line Integration already has) + WM_MOUSEWHEEL; middle mouse = Melee |
| src/game/PlayerController.cpp / .h | wheel / PgUp / PgDn → NextWeapon (wantSwitch_ = 1); fire latch for sub-tick clicks; `hudAimState().weaponClass` = held class (interned) |
| src/game/Character.h | `requestWeaponSwitch` follows HmWeapon.TryPutDown (reload abandoned, refire wait, retarget, queued re-put-down, melee block) |
| src/game/CharacterMovement.cpp | hover UpdateTurn ω replacement (mask 0.05 / 1); overhead hull probe starts ≥ 0.1 m above the root (boost jump fix) |
| src/game/World.h / .cpp | HudGameState `attackingTeamIndex`, `currentObjectiveCountdown`, `competitiveScoreEnabled` |
| src/assets/SkinnedModel.cpp | skinPose always copies uv / subs (mirror of agents/rendering 79388f5) |
| src/core/Application.cpp / .h | harnesses WFC_SWITCHTEST, WFC_SCORETEST, WFC_HEIGHTTEST, WFC_VEHPHYS (+ WFC_VEHPHYS_ONLY), WFC_POINTPROBE — add them to the direct-boot env list |

## Merge notes
- Input.h: Integration already has `float mouseWheel = 0.0f;` — keep one.
- SkinnedModel.cpp: same two lines as Rendering 79388f5 (my copy keeps the `// UVs are pose-invariant` comment) — take either.
- Keep Integration's MatchLaunch.localTeam / startLocalMatch(s, localTeam) seam (not touched here).

## Playtest expectations after merge
- Mouse wheel and PageUp / PageDown switch primary ↔ secondary for every class, also mid-reload (reload cancelled) and while
  firing (switch after the current shot's refire window); not during melee / transformation / vehicle form.
- Vehicles stay upright after glancing wall hits; jump nose-up spin is levelled in the air; boost jump (Space while holding boost)
  now works (~4.9 m). Hover jump unchanged (~3.8 m, matches the original).
- Scout taller while jogging is the original shared-animation posing (no change).
- Score bars: Frontend's GoalScore fix; Gameplay state is fresh every launch.

## Open (documented PARTIAL)
- Tank glancing-wall tilt up to its 30° stability limit; ramp launches not separately measured against a capture.
- RepairRay still unsimulated (HUD weaponSimulated false); per-weapon reload clips await AssetTools.
- Systems' new per-form vehicle audio inputs (SYSTEMS_M08C_AUDIO_HANDOFF) — next pass.
