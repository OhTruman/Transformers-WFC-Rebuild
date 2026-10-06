# Validation tiers

The project is in rapid active development, so **FAST is the default after every normal integration milestone.**
TARGETED and FULL run only when someone asks for them.

| tier | when | command | cost |
|---|---|---|---|
| **FAST** | every normal integration milestone (default) | `fast-gate.ps1 -Ref <integration> -Prev <last validated>` | ~20-30 min renderer time |
| **TARGETED** | a major subsystem changed and needs deeper proof | the matching `m07-*.ps1` suite(s), see below | 15-90 min per suite |
| **FULL** | explicit request: stabilization checkpoint / release candidate | `m07-gate.ps1 -Ref <integration> -Build` | several hours |

## FAST: "did this integration obviously break the game or the systems changed in this milestone?"
FAST is not a certification. It covers the following.

- **Builds:** Debug + Release of the exact sha, plus headless ctest (frontend + fidelity) for both.
- **Gameplay self-tests:** `WFC_WEAPONTEST`, `WFC_CHASSISTEST`.
- **Golden path (Release):** presentation-gate `route,direct` + the Create a Character watchdog:
  - cold boot -> title -> party lobby -> Create a Character preview (emblem state);
  - Streets TDM with the selected character;
  - world / HUD;
  - pause / resume, results, lobby;
  - second match, frontend return;
  - the frontend-launched world vs the same-start direct boot.
- **Map switch:** Streets -> lobby -> Berth -> lobby -> Streets (default profile only). Checks the world per visit and
  the in-play draws against the direct boot.
- **Renderer state:** the world frame after every overlay (normal variant). The negative controls run only in
  TARGETED / FULL.
- **Mechanics:** move, jump, fire, reload, repeated transform. Weapon FX: ribbon / beam (Trail2 / Beam2) is reported as
  KNOWN Rendering, separately from other FX errors.
- **Audio duplication and resource explosion:** read from the runs above. No plateau proof.
- **GPU:** waits at most 20 min (`WFC_GATE_GPU_WAIT_MIN`) for other sessions' renderers. A step it could not run is
  `UNKNOWN (DRIVER / GPU CONTENTION)`, never a product failure.

## TARGETED: choose by what changed
`FAST-GATE.md` lists "changed since" per area: the files changed since the previous validated commit. Use it to pick
the suites below.

| changed area | suite(s) |
|---|---|
| frontend menus / movies / navigation | `m07-frontend.ps1`, `presentation-gate.ps1` (all watchdogs) |
| renderer GL state / UI pass | `m07-renderstate.ps1` (with negative controls), `map-loss-gate.ps1` all profiles |
| characters / roster / selection | `m07-characters.ps1` (33 chassis via `WFC_CHASSIS` + class presets) |
| maps / render data / collision | `m07-maps.ps1` (every launchable map + frontend chain) |
| game modes / match rules | `m07-modes.ps1` |
| memory / resource lifetime | `m07-soak.ps1` (+ `WFC_MEMCYCLE`) |
| match lifecycle / quit routing | `m05-e2e-gate.ps1 -Full`, `playtest-acceptance.ps1` |

**Reuse evidence for unchanged systems.** If an area has no changed files since a commit where a suite passed, cite
that result (ledger below). Don't rerun it.

## FULL
`m07-gate.ps1` runs every suite: all maps, all modes, all 33 chassis, 4 display profiles, the negative controls and
the multi-pass soak. Run it only when explicitly asked.

## Ledger
Every gate run is recorded in `VALIDATION-LEDGER.md`: tier, integration commit, the previously validated commit, what
changed, the verdict, and where the evidence is. Later gates use it to choose relevant tests and to reuse evidence.
