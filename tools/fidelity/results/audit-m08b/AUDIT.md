# Latent-bug and regression-risk audit: M08b

**Audited:** integration/milestone-08b `175a6348`, which contains every lane head as of 2026-10-05:
- Frontend 7a6255e, Gameplay ddd8a58, Rendering f5490be, Systems 79148d0;
- AssetTools eb3335b, RE-Workspace f150a6a (read-only).

**Method:**
- Static code / data / log analysis of the exported tree (`work/ab/m8b_175a634`, 48k lines).
- The extracted assets and AssetTools manifests.
- The decompiled original Flash scripts (`RE-Workspace/work/q/gfx`).
- Runtime evidence from the 175a634 FAST-gate runs.

No new graphical runs: other sessions' renderers were active throughout.

**Question:** what is broken, stale or only accidentally working that a short human playtest would not reveal?

**Ranks:**
- **P0:** can corrupt, crash, reset the driver or lose the world.
- **P1:** obvious gameplay / fidelity defect.
- **P2:** hidden correctness issue likely to surface later.
- **P3:** polish / unsupported.

**P0: none found.** The world-loss class is covered: GL entry state was sane after every overlay in the 175a634 runs, and the renderer releases all match-owned GL objects at match end.

## Why a short playtest misses most of these
A normal offline match is **solo**. Opponents exist only through a diagnostic hook (`WFC_MATCH_OPPONENTS`,
"static synthetic participants (drawn boxes)", `core/Application.cpp:186`). That matches the original, which had no
bots in versus. So kill feed, score bars beyond a few points, hit reactions and team play are never exercised by a
human tester. Findings P1-2 and P2-2 hide behind this.

---

## P1: obvious gameplay / fidelity defects

### P1-1 The lobby team never reaches gameplay: the local player is always an Autobot in team modes
- **Owner:** Integration (seam), Gameplay.
- **Visible:** yes. **Confidence:** HIGH.
- **Source:**
  - The frontend picks a team at the final countdown: `frontend/GameFlow.cpp:756` (`pickTeam()`, random) or via
    `Online.SwitchTeam` (`:391`). It stores it in `match_.teamIndex` (`:785`).
  - **Nothing in `src/core` or `src/game` reads `teamIndex`.**
  - Gameplay picks its own team in `Match::addPlayer` (`game/Match.cpp:131`, `randomInt(2)`), from a fixed-seed LCG
    (`game/Match.h:223`), so the first local player always gets team 0.
- **Evidence (175a634 FAST gate):**
  - The frontend chose **Decepticons in 4 of 6 lobbies** (`gamelobby.finalCountdown team=1`).
  - Gameplay spawned the local player **team=0 in 12 of 12 spawns**.
  - The in-match HUD shows the Autobot logo in both cases, so it follows gameplay.
- **Effect:**
  - The game lobby announces the player as a Decepticon, then the match plays them as an Autobot.
  - Switch Team is a no-op.
  - The Decepticon class bodies (preset `Decepticon` chassis, e.g. Breakdown / Barricade) are reachable only in FFA
    (where faction is forced to Decepticon).
- **Fix direction:** pass the frontend team into `Match` (an explicit team for the local player, as
  TnTeamHandlerTwoTeams.PickTeam keeps a current team < 2).

### P1-2 HUD team score bars use goal 10 instead of the match goal (full at 10 points, then overrun)
- **Owner:** Frontend (DataStores).
- **Visible:** in any match past about 10 points; not in short or solo tests. **Confidence:** HIGH for the
  mechanism, MEDIUM for how the overrun looks (masking).
- **Source:**
  - `<CurrentGame:GoalScore>` is not answered: `datastore.unhandled` 9× in the 175a634 runs (`frontend/DataStores.cpp`
    has no handler).
  - The original `Hud_GFX` (DoAction sprites 497 / 499, `RE-Workspace/work/q/gfx/Hud_GFX.as.txt:1754-1805, 1820`)
    does `GoalScore = parseInt(ReadValue('<CurrentGame:GoalScore>')); if (!(GoalScore > 0)) GoalScore = 10;` and then
    bar `_width = 25 + (score / GoalScore) * 141`, clamped below only.
  - TDM goal is 40 and DM goal is 20, so the bars are full at 10 and grow past the 166 px panel afterwards.
- **Fix direction:** answer `GoalScore` from the match settings (the frontend already holds them in MatchValues).
- **Status (2026-10-05):**
  - Fixed on agents/frontend `e1db12b`: answered from the launched match's PointsToWin (GRI.GoalScore = PointsToWin).
    Code reviewed by Experimental.
  - Runtime proof is pending the next integration. `fast-gate.ps1` now FAILs `hud.goalscore.*` on any unanswered
    GoalScore.
  - P2-3: Frontend asked Gameplay for the GRI values / sourced defaults. P2-4: waits for a Rendering IRenderer entry
    point; Frontend will forward it.

### P1-3 Volume sliders, subtitles and vibration are saved but never applied
- **Owner:** Systems (volumes), Integration (wiring).
- **Visible:** yes. **Confidence:** HIGH.
- **Source:** `core/Application_Frontend.cpp:173-184`: "No owner API exists yet for the volumes … the values are stored,
  persisted and reported" (`profile.apply … owners: pending: Systems volumes`).
- FX / Dialogue / Music sliders (SettingsMenu_GFX `FXVolume` / `DialogVolume` / `MusicVolume`) change nothing audible.
- Gamma and the look settings **are** applied, at boot / apply (`:176-179`) and per match (`:348`).

### P1-4 The Scientist's Repair Ray does nothing when fired, with no feedback
- **Owner:** Gameplay.
- **Visible:** yes, for the Scientist class preset. **Confidence:** HIGH.
- **Source:** RepairRay is `WeaponFire::Other` (`game/WeaponTable.inc:309`). `Weapon::canFire()` requires `simulated()`
  (InstantHit or Projectile, `game/Weapon.h:22,89`).
- The ray is equipped (the weapon test checks preset loadouts equip fully) but the trigger is a silent no-op.
- It is documented only as one line in a 4,887-line FIDELITY.md (`:3444` "Repair rays are flagged unsimulated").
- **Fix direction:** at minimum an on-screen or log "unsupported weapon" indication; then the heal / drain beam
  (Beam2 rendering is the known Rendering gap).
- **Status (2026-10-05):** open. Gameplay will add an unsupported log + HUD flag; `HudGameState.weaponSimulated`
  already reports false.

### P1-5 Original PC controls shown on the in-game Layout card but not wired
- **Owner:** Gameplay / Platform.
- **Visible:** yes. **Confidence:** HIGH.
- The original PC table (`Xe-TransInput.ini` KeyDescriptions + `TransGame.int`), shown by the rebuild's own Controls
  page:
  - `MouseWheel/PageUp/PageDown` = Swap Weapons;
  - `MiddleMouseButton` = Melee.
- The rebuild maps only PageUp / PageDown (`platform/win32/Win32Window.cpp:37,43`) and Q (`:40`).
- `InputFrame::mouseWheel` is consumed only by the Flash menus (`ui/GfxPresenter.cpp:130`); nothing in `src/game`
  reads it.
- Every other card action is wired: Fine Aim / Boost RMB, Ability 1 / 2 Shift / Ctrl, Kill Streak B, Hover C / V,
  Interact / Pick Up E, Change Form F, Grenade G, Reload R, Scoreboard Tab, Pause Esc.
- Detonate Grenade is on the ability button (CONF RE). Turrets exist only on the Escalation maps.
- **Status (2026-10-05):** fixed on agents/gameplay; code reviewed by Experimental.
  - `2089910`: wheel / PageUp / PageDown all run NextWeapon, the shipped single "Swap Weapons" binding;
    WFC_SWITCHTEST 32/32.
  - `bec41cd`: MiddleMouseButton = Melee, polled with the same focus gate as every key (`Win32Window.cpp:103-105`).
  - Runtime proof is pending the next integration.

---

## P2: hidden correctness issues likely to surface later

### P2-1 Every weapon plays the Ion Blaster reload animation
- **Owner:** AssetTools (export), Gameplay (selection).
- **Visible:** subtly. **Confidence:** HIGH.
- `game/Character.cpp:80-81` picks `Shooting_Reload_IonBlaster_ROBO` for every weapon.
- The original animation set has a per-weapon robot reload clip (`mp_content/mp_animation_compat.json` "reload":
  `Shooting_Reload_AssaultRifle_ROBO`, `…Shotgun…`, `…RocketLauncher…`, …).
- The chassis export (27 player chassis) carries only the Ion Blaster clip + `ADD_Shooting_Reload_Fast/Med/Slow_ROBO`.
- The reload pose and length ignore each weapon's `WeaponReloadAnimTime` (Shotgun 2.5 s).

### P2-2 Hit reactions and melee knock-back animations are never played (undocumented)
- **Owner:** Gameplay.
- **Visible:** when hit (hazards / self-damage now; opponents later). **Confidence:** HIGH.
- Every player chassis exports `ADD_HitReaction_React_B/F/L/R` and `Melee_Knock{Back,Forward,Left,Right}_H2H`. There
  are zero references in `src/game`, and no mention in STATUS or FIDELITY.

### P2-3 The objective-mode HUD timer and objective HUD data are never answered
- **Owner:** Frontend.
- **Visible:** in DOM / KOTH / EXT. **Confidence:** HIGH (data), MEDIUM (exact look).
- These go unanswered on 175a634:
  - `<CurrentGame:CurrentObjectiveCountdown>`: Hud_GFX `objectiveTime_mc`, `Hud_GFX.as.txt:4109-4121`, so the hill /
    bomb countdown never shows;
  - `<CurrentGame:AttackingTeamIndex>` (128 reads), `ActiveObjectives`, `CompetitiveScoreEnabled`;
  - `_CurrentWave` / `_NextWaveTime` (Escalation only).

### P2-4 The HUD's post-process requests are dropped
- **Owner:** Rendering (chain), Frontend (bridge).
- **Visible:** when badly hurt or jammed. **Confidence:** MEDIUM.
- `Hud_GFX` calls `Self.ActivatePostProcessChain(1)` for the hurt / downed state and `(0)` for the HUD scramble
  (ability jammer / transform disruptor) (`Hud_GFX.as.txt:2489, 4544`).
- The rebuild emits `bridge.unhandled` for them (26× in the 175a634 runs). The Flash `mc_hurt` overlay still shows;
  the native screen effect under it does not.

### P2-5 A failed map render-data load silently falls back to the legacy renderer with the Streets fog
- **Owner:** Gameplay (caller), Rendering.
- **Visible:** not now (all 10 maps have render data). **Confidence:** HIGH.
- `game/World.cpp:88` ignores `loadMapRenderData()`'s result.
- On failure the fixed-function `world.glb` path draws with the GL fog hard-coded to Streets' HeightFog
  (`render/gl/GLRenderer.cpp:84-92`).
- The `legacy fallback` marker (`GLRenderer.cpp:643-649`) is reported only under `WFC_VISUALCHECK`.
- **Recommend:** LOG_ERROR on failure in a normal run, plus a frontend-visible note.

### P2-6 The test design hides team and combat bugs
- **Owner:** Experimental / Integration.
- A normal match is solo (authentic: no versus bots).
- Recommend a supported opponent hook that draws real chassis instead of boxes, so FAST / TARGETED gates (and humans
  on request) can exercise score progression past 10, kill feed with real kills, hit reactions and team spawns.

---

## P3: polish / unsupported / logging
| # | issue | where | owner |
|---|---|---|---|
| P3-1 | **Fixed on agents/gameplay bec41cd** (fire press latched for the next sim step). Was: Fire is sampled per render frame (`wantFire_ = isDown`). At ≥144 Hz a click shorter than one 16.7 ms sim step can miss a tick; all other presses are latched | `game/PlayerController.cpp:161` | Gameplay |
| P3-2 | Hidden Ion Blaster fallbacks: `syncShownWeapon` uses id "IonBlaster" when the weapon has no def; HUD weapon / icon / inventory default "IonBlaster"; static Ion mesh drawn when a skinned load fails. Not exercised now; recommend LOG once when taken | `game/World.cpp:1043,1476-1480,1827` | Gameplay |
| P3-3 | Low-ammo cue threshold 5 (Ion Blaster WEPMESH) for every weapon | `game/Weapon.h:74` | Gameplay / Systems |
| P3-4 | `decodeImage` has two silent failure returns (zero-size / oversize); World logs failed textures only as a count, never by name (0 failures in the 175a634 runs) | `platform/win32/Win32Image.cpp:10,15`; `game/World.cpp:111` | Rendering |
| P3-5 | Sound-cue package aliases are hard-coded for Ion Blaster, Foley, Optimus Prime and Soundwave vehicles only; other packages rely on short names (0 `not in table` warnings in the 175a634 runs, but only a few chassis exercised) | `game/SoundCues.cpp:98` | Systems |
| P3-6 | Remnant's `PostProcessVolumeHeightFog` is not exported (Escalation map; Debris has no HeightFog in the original: authentic) | AssetTools `manifests/maps/MP_ESC_Remnant*` | AssetTools |
| P3-7 | Scoreboard on Tab is a toggle; the original PC behaviour (hold vs toggle) is UNKNOWN | `frontend/FrontendRuntime.cpp:870` | Frontend |

## UNKNOWN (needs a short graphical run; not run because other renderers were active)
- **Material / particle compile fallbacks on the 8 maps other than Streets / Berth on M08b.**
  - Streets and Berth on 175a634 had **0** `failed to build; using glTF fallback` and **0** `textured fallback`.
  - The 194 such lines found in older Integration logs are pre-M35 (LogoAUT / LogoDEC, fixed by f5490be) or
    particle templates.
  - **Check:** one direct boot per map, about 30 s each:
    `WFC_BOOT=match WFC_MAP=<map> WFC_SMOKE_FRAMES=600`, then grep those two strings and `not drawn yet`.

## Matrices
### Characters (27 player chassis; AI soldiers Car8 / 9 / 10 and Minion1 / 2 / 3 excluded)
| item | status | evidence |
|---|---|---|
| robot + vehicle body | PASS 33 / 33 exported; 33 / 33 spawn with their own bodies | M07 characters on Gameplay 22a (`results/m07-dryrun/gameplay_1216e80_chassis.csv`) |
| clips: melee / grenade / ability / transform / jump / death | PASS (9-15 / 2-3 / 9-11 / 5-8 / 4 / 3 per chassis) | robot.glb animation names |
| clips: per-weapon reload | **FALLBACK** to Ion Blaster (P2-1) | |
| clips: hit reaction / knock-back | exported, **MISSING** at runtime (P2-2) | |
| team / faction from the lobby | **FALLBACK** to Autobot in team modes (P1-1) | |
| team colours | PASS (FFA Decepticon, team energon; Integration M08 34aa084) | M08b soak |
| icons / HUD weapon | PASS (provider-driven; Ion default only when no def, P3-2) | |
| audio | PASS (per-weapon / per-chassis, M08; 0 missing cues) | 175a634 logs |
| effects | PARTIAL: Trail2 / Beam2 ribbons not drawn (KNOWN Rendering) | STATUS M08b |

### Class presets (default loadouts)
| class | primary | secondary | grenade | status |
|---|---|---|---|---|
| Scout | Shotgun (InstantHit, SP data*) | HeavyPistol (InstantHit) | FlashBangs | PASS |
| Scientist | BurstRifle (InstantHit, SP*) | **RepairRay (Other: does not fire)** | HealGrenades | **PARTIAL (P1-4)** |
| Leader | IonBlaster | GrenadeLauncher (Projectile) | KamikazeMines | PASS (projectiles drawn as box markers [PROV], FIDELITY:3444) |
| Soldier | AssaultRifle (InstantHit, SP*) | HomingRocket (Projectile) | FlakGrenades | PASS (box markers) |

\*SP data (PlayerData used when a weapon has no MultiplayerData) is **CONFIRMED ORIGINAL** (`game/WeaponDef.h:2-3`,
RE TARGETED_PASS3): 30 of 52 weapons.

### Maps (data present in the export and the 175a634 render data)
| map | geometry | collision (pawn / weapon / fallback) | gameplay / spawns / mode actors | audio | materials / lighting / LVV / FX / movers / PP | fog | status |
|---|---|---|---|---|---|---|---|
| Streets, Berth | Y | Y / Y / Y | Y | Y | Y | Y | PLAYABLE (FAST gate, pixels + GL state) |
| Seed, Rust, Complex, Gorge, Molten | Y | Y / Y / Y | Y | Y | Y | Y | data complete; **runtime fallbacks UNKNOWN on M08b** (last runtime proof: 06b dry run, all mechanics PASS) |
| Debris | Y | Y / Y / Y | Y | Y | Y | none (authentic: 0 HeightFog actors) | as above |
| BrokenHope, Remnant | Y | Y / Y / Y | Y | Y | Y | Remnant: PPV height fog not exported (P3-6) | Escalation maps; versus not offered |

The movement-collision fallback (`collision.glb`) was taken 0 times. Every map has `collision_pawn.glb`.

## Checked and ruled out (not defects)
- **Fog / lighting carried from map A to map B:** `Pipeline::release()` resets the whole pipeline
  (`*this = Pipeline()`, `WfcPipeline.cpp:583`).
- **Stale GL texture handles in World caches across matches:** `unloadMatch` destroys and reconstructs World
  (`Application_Frontend.cpp:716-717`).
- **Spawn ignores a failed chassis apply** (`World.cpp:1692`): the pre-spawn `chassisCheck_` uses the same
  `chassisAssets(id)->ok` test (`World.cpp:1335`).
- **Look settings lost after a match rebuild:** re-applied per match (`Application_Frontend.cpp:348`).
- **Function statics holding GL handles** (`previews`, `HudMarkers`): diagnostic-only (`WFC_SCENEPREVIEW`,
  `WFC_MARKERTEST`).
- **Input edges lost at high refresh:** presses are latched into `want…` flags consumed by the fixed 60 Hz tick
  (except Fire, P3-1).
- **Flash HUD / menu animation frame-rate dependence:** time accumulator per movie (`ui/GfxHost.cpp:107-112`).
- **Kill-feed ally / enemy colouring:** uses the gameplay team (`Application_Frontend.cpp:566`).
- **GL resource growth across matches:** non-UI GL objects flat at every match end; the renderer frees 67-78 match
  textures each time with 0 left; only Flash screen textures grow once per newly visited screen (bounded).
- **Hard-coded 1280 × 720 / 1120 × 720:** window defaults and the authentic Flash stage size.
