# M05 end-to-end gate: first run on agents/frontend a55a4e9 (2026-10-03)

The tree is an isolated `ab.ps1` export of origin/agents/frontend a55a4e9 (int-04 a03d7f7 fast-forwards to it), in
Release. Full table: `M05-GATE.md`. **PASS 47 / FAIL 2 / KNOWN 3 / INFO 3 / SKIP 29 / HUMAN 1.**

## What works (STATE layer, against RE / AssetTools)
The whole route runs through the real UI bridge calls and matches the RE values:

boot `UI_FrontEnd_m` → intro chain in order → main menu → `Online.OpenPartyLobby(GTS_TeamGame)` → party lobby URL →
TDM `publishGameInfo` ("Team Deathmatch") → game lobby URL keys → map 508 Streets → 10 s countdown with 1 s ticks →
team pick → match URL **identical to RE 3.1** → loading "Team Deathmatch / in Streets", 3 tips → match.launch
TDM / 40 / 900 → HUD on → pause → `QuitToMainMenu` → frontend without the intro.

Also passing:
- Every UI transition is in the TnUIController table, and every state opens its authored movie.
- No duplicate or stale screens.
- The profile persists, and the second launch skips the logos.
- 3/3 loops complete, with the flow state reset after each return.
- Player state is fresh in each match; match parameters are stable; one world load per match.
- Handles are flat (+4.5 per cycle).

## FAIL
| check | finding | owner |
|---|---|---|
| `LIFETIME.memory_peak` | Private memory reaches **6.2 GB after 3 loops**, at +1.58 GB for every return to the frontend (2.5 → 4.0 → 5.7 GB at successive returns). This is the documented "renderer recreated, previous map's GL objects leak" gap (`memory_per_cycle` is KNOWN), but at this rate a normal session of 4–5 matches exhausts a 16 GB machine. | Rendering (map unload) / Frontend |
| `DATA.frontend_unit_tests` | The frontend lane's own suite: 58 pass, 1 fail, `catalog.tdm_selectable_maps_now` (expects only Streets; got 2). The same run passes `extensibility.rust_enabled`: the catalog finds a Rust runtime directory, so the assertion looks stale for data where Rust is available. Owner decision: update the test or the catalog rule. | Frontend |

## KNOWN (documented gaps)
- `intro_movies_played`: no video decoder, so the 4 intro movies end instantly. The data exists (DATA PASS); the user
  sees nothing.
- `pending_countdown`: there is no PendingMatch. The PROVISIONAL adapter goes straight to InGame (RE: 10 s).
  Owner: Gameplay.
- `memory_per_cycle`: see FAIL above.

## PRESENTED: what the user sees
- On this run the in-game control frame captured as uniform white (desktop not composited, probably locked), so every
  screen check is SKIP by design.
- An earlier run on the same build with working capture is in `captures_probe_valid.png`:
  - **main menu, party lobby, game lobby, match loading screen and return-to-menu are fully black windows**;
  - the in-game view and the pause (game view only, no pause menu drawn) render the world.
- So the trace says these screens are open, and the user sees black. This becomes KNOWN on any run where capture works,
  until the GFx movie presenter lands.

## INFO
- Boot loading trace: `loading.start` reports Generic / TF_LoadingScreen, then the boot switches to InitialStartup
  without an event. The effective state is correct (PASS).
- Match load takes 2.6–4.4 s.

## SKIP: ready for the next branches
- The MATCH lifecycle: spawn after pending, teams, damage, death, respawn 5 s, scores, 40 / 900 limits, tie, end UI,
  reset, second match.
- Return to the game lobby after the match.
- Menu music and UI audio ownership; audio PCM back to base.
- Per-level resource counters.

See `../../m05/README.md` (what each incoming branch turns on).
