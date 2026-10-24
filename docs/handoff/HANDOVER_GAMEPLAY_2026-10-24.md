# Gameplay hand-over, 2026-10-24 (user playtest 2026-10-23b + the DLC completion)

**Scope:** the user's playtest rows that Gameplay owned or shared, the broad headless proof, the Escalation re-baseline, and what is
still PROV or open on each DLC map. Merge shas are on `integration/milestone-09c`.

## The user's rows

| Row | Finding | Status |
|---|---|---|
| **16 / 28** (P0) | "Scout cannot move": the camera turned, the character flickered in place. Every class, every solo-hosted match. | **Fixed.** Cause: a private match or Escalation hosted alone launched as an online session with 0 remotes (`gl.online = !m.onlineSlots.empty()`), so the local player was a wire seat with no lockstep driver. Fix `gl.online = !gl.remotes.empty()`: f0d500cc (Integration, from the Gameplay / Frontend root cause); SOLOLAUNCHTEST tightened: 609cd27d. Proof, menu path, windowed: the fixed exe walked about 110 m and emptied the clip; the unfixed one stayed at spawn with a full clip. The Frontend's gate: 43 m, 20 shots, a transform, jitter 0. |
| **31** | The QA panel's restart / kill / respawn did nothing; respawn looked broken. | **Fixed by the same cause** (they run through the seat path). FLOWSIMTEST MP with WFC_QA=1: character change Scout (Car2) → Soldier (Tank3), kill / respawn in 4 s, a relaunch spawns: 7f35fb82 + 508b29ce. The panel's Restart is a menu relaunch (Frontend). |
| **15** | Escalation asked for the character again after loading. | **Fixed, sim side.** The launch carries the pick (Frontend's pick persistence, 7a7b8ba6). FLOWSIMTEST ESC: offline launch, the human spawns, moves 112 m, 78 shots, 2 transforms. |
| **38 / 39** | Campaign Chapter I > Crash Site > Megatron gave a blank screen. | **Sim side clean**: D1 loads, starts and reaches Crash Site headless. The blank screen was the menu side (the solo lobby, 1edec0c2, Frontend). |
| **A4 (found in the sweep)** | The player died at t 1.0 s on chapter start: 10,000 damage from hazard PhysicsVolume_6864. | **Fixed: 658fb346.** Cause: the convex-only hazard test; 73 of 641 hazard brushes are non-convex (all campaign). Those now use a ray-crossing parity test; convex ones keep the exact test. A4 plays 90 s with no game over. |
| Pad X | X both interacted and reloaded next to an interactable. | **Fixed: 9c94b50d** (TryInteract else reload). |

## Broad headless proof (no card; the bot-driven human; logs in Rebuild-Gameplay/work/cp_sweep, esc_sweep, dlc_sweep)

**Campaign** (90 s from the chapter start, WFC_CAMPAIGN_ROOT=AssetTools/out/campaign_render, head 74e41849 + the hazard fix):

| Chapter | Started | Checkpoints | Game over | Errors |
|---|---|---|---|---|
| A1_IAC | yes | 1 | – | 0 |
| A2_KON | yes | 1 | – | 0 |
| A3_COR | yes | 2 | – | 0 |
| A4_ORB | yes | 1 | – (was t 1.0 s) | 0 |
| A5_TRY | yes | 1 | – | 0 |
| D1_ORB | yes | 1 (Crash Site) | – | 0 |
| D2_COR | yes | 5 | – | 0 |
| D3A_IAC | yes | 2 | 90.4 s: combat death (no hazard / fall) | 0 |
| D3B_IAC | yes | 2 | 71.8 s: the bot walked off a ledge (KillZ; the known post-build item) | 0 |
| D5_OME | yes | 1 | 79.5 s: combat death in the Omega fight | 0 |

**Escalation (WFC_ESCTEST, head + 19 + 20 + 21):**

| Map | Result | Notes |
|---|---|---|
| MP_ESC_Remnant | 15 / 15 | rule checks |
| MP_ESC_BrokenHope | 36 / 36 | rule checks |
| MP_ESC_Tranquillity | 9 / 9 | 6 waves, 257 spawns, 0 failed; floodgates + base doors + presentation |
| MP_ESC_District | 12 / 12 | 6 waves, 89 spawns, 0 failed; lasers, sentries, detpack, outage, door generator, presentation |
| MP_ESC_Pulse | 9 / 9 | 6 waves, 134 spawns, 0 failed; Zone_Manager doors |
| MP_ESC_Static | 9 / 9 | 6 waves, 170 spawns, 0 failed; door generator 10255 (3 / 3 doors, 4 / 4 zones); the fire-jet trap |

**Versus DLC (60 s TDM, 9 bots):**

| Map | Result | Nav cells | Respawns |
|---|---|---|---|
| MP_KON_Fortress | spawn ok, 0 errors | 11,918 | 12 |
| MP_ORB_Havoc | spawn ok, 0 errors | 15,458 | 9 |
| MP_IAC_Horizon | spawn ok, 0 errors | 7,358 | 10 |
| MP_IAC_Metropolis | spawn ok, 0 errors | 8,809 | 11 |
| MP_KON_Sector | spawn ok, 0 errors | 15,268 | 5 |

## Escalation re-baseline (deliberate, b6ddf3bc / FINAL_BASE_ITEMS 19)

- **The change:** the spawn finder now uses the zone's authored TnSurvivalSpawnPoint with a ground + capsule check and no navmesh
  requirement (RE DLC_ESC_MECHANISMS_RE.md §7). The old PROV rule replaced an off-nav point with an on-nav one, or spent the try.
- **Effect:**
  - Escalation spawn positions change wherever that rule used to move a point, so ESC same-match against older heads differs.
  - The base ESCTEST rows still pass.
  - Failed spawns: Tranquillity 1 → 0, District 5 → 0, Pulse 2,968 → 0 (0 → 134 spawns).
- **Test change:** ESCTEST's DLC "due" check is now per enemy type (a one-wave row may legitimately never be drawn by the budget).
- **Versus:** unchanged.

## PROV / open per DLC map

| Map | Built | PROV / open |
|---|---|---|
| Tranquillity | waves, floodgates (3-purchase drain), zones, energon off, base doors (switchable blockers), presentation data | the 11 destructibles unhidden at Drain Completed are presentation only (no collision change); FogOn presentation |
| District | waves, the Autobot jet, doors, laser defense grid, sentries (ion / repair, 3-use), power outage, detpacks, vending (flattened), presentation data | button range 3 m and detpack countdown 3 s (anim lengths not measured); the sentries are not drawn by Gameplay (Rendering); the outage's dark grade / screens are presentation (HudGameState.svPowerOutage) |
| Pulse | waves, the authored spawn rule, Power_Generator_Action (10 managers: zones, generator volumes, door collision after the matinee), presentation data | **open:** FinalRoomUnlocked (logged, its effect not modelled); the healing platform, Nexus_Attack, the repair / rocket / eye sentries, Tank_AI_Helper_Logic, SpaceVignettes, Trypticon dialogue; the managers' NavMeshCells (our versus nav has no gating) |
| Static | waves (20 rows, Autobot horde), the DoorGenerators (40 root Toggle chains, flattened; zones stay on once opened), the fire-jet trap (generator 3558: 16 volumes off at start, on when bought for the 7.018 s matinee, re-buyable after 2.0 s, both from the export), presentation data: 2400c037 + a61249c0 | **open:** the ambient FIREJETS01-16 / FIREEXP01-10 touch-triggered fire bursts and the camera Kismet |
| Fortress / Havoc / Horizon / Metropolis / Sector | versus: spawns, pickups, objectives, nav | none found in the sweep |

## Also delivered in this window

- **Split screen offline:** seat 2 on the remote-seat path, per-seat presented frame, bot-slot join, the split draw owner.
- **Online two locals:** Systems' 85c4cd06, with the World glue per the agreed contract.
- **Section 43 sim half:** FLOWSIMTEST MP 10 / 10, ESC 6 / 6, CP 6 / 6.

## Static and District data (2026-10-24, later)

- **Static** (RE section 9): STATIC_PREP 2400c037 (the trap mechanism, gen derivation, ESCTEST row) + STATIC_DATA / FINAL_BASE_ITEMS 21 a61249c0.
  - Static's export needed an extractor fix first, and its Escalation layer (escalation_geometry, escalation_navmesh) was produced by Integration's extras step.
  - The trap's matinee length and cooldown then came from the real export, so no PROV defaults remain.
- **ESCTEST rule (honest re-baseline):** the DLC "due" check covers enemy rows with a MinCount only.
  - The director spawns a type below its MinCount exclusively, and the test clears the horde every 2 s.
  - So optional rows depend on budget and kill cadence. On Static the optional Rocket / CQC / Shotgun rows never got a free pick in the test; in real play the horde lives and they do.
  - The director itself is unchanged.
- **District after the art-package extractor fix** (MP_ESC_District_Art_m had one wrong 4 KB block): the chain re-ran and gen_escalation's District data is **byte-identical**, as are BrokenHope, Remnant, Tranquillity and Pulse.
  - The fix touched art only; the Escalation data comes from the BASE level.
