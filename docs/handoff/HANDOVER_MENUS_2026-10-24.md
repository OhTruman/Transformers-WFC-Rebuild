# Menus hand-over, playtest 2 (2026-10-24, Frontend)

Items from `USER_PLAYTEST_2026-10-23b.md` that Frontend owned or shared. **Fixed** = merged (Frontend commit sha), **Base game** =
kept as the original authored it, **Open** = not done. "Seen" = checked in a windowed menu run.

## P0

| # | Status | What |
|---|---|---|
| 9 / 14 | Fixed 9fcb3d54, seen | Escalation > Host a Game opened TDM: a PC shortcut replaced Host in every party lobby. Escalation now shows its original Host Options (Privacy, Create Game) and an Escalation lobby. |
| 24 / 26 | Fixed (same cause), seen | The user's lobby without Game Settings was that Escalation-as-TDM lobby. Private Match has the Game Settings row (lobby flow v2, the user's vote; the base game sets these before hosting). |
| 16 / 28 | Fixed (Gameplay) | No class could move or fire: a solo private match was launched as an online wire seat. Frontend found it from the menus. |
| 38 / 39 | Fixed d93b9a16, seen | Campaign > Crash Site > Megatron blank screen: the original Solo lobby starts the chapter as soon as a character is picked (its lobby is invisible). Ours never started. |
| 17 / 30 | Fixed (by 9fcb3d54 + 1f4feb44), seen | The trap came from the Escalation-as-TDM state. Versus pause > Choose Character > Back > Back resumes. Escalation's pause has no Choose Character (base game). |
| 15 | Fixed 1f4feb44 (Escalation), 05767448 (Campaign), seen | The lobby pick carries into the map (the original keeps it for Escalation and campaign); no second select. Versus asks again after load, as the original. |

## Escalation / multiplayer menus

| # | Status | What |
|---|---|---|
| 10 | Fixed 824816f7, seen | Escalation has no Game Settings (base game). Back from the Private Match Game Settings page left the global Settings menu over the lobby; it now returns to the lobby. |
| 11 / 13 | Fixed 1f4feb44, seen | The wheel listed 31 iconics of both sides on a 13-slot ring (overlapping names). It now lists the map's 13 characters in authored order. Faint names behind the near ones are authentic. |
| 12 | Fixed 1f4feb44 + 824816f7 | Optimus twice: gone with the map list. Silverbolt's portrait: a capital-letter mismatch ("SilverBolt" vs "Silverbolt_Robot"); the lookup now ignores case. |
| 29 | Fixed 1f4feb44, seen | Escalation used the versus controller; it now has its own (TnUIControllerSurvival). |
| 19 | Fixed b851cbcd, seen | The multiplayer root shows one player list, no teams (base game). |
| 23 | Fixed 3d49f60d + 824816f7 | Private Match: the host is shown under a team, with a **Team row** (under Game Settings) to switch sides; that side is the match team (offline / no one joined; online lobbies balance as before). |
| 25 | Fixed 3d49f60d + 48e42af6, seen | **Back during the start countdown cancels it** (the user's addition; the base game cannot cancel). Start works again afterwards: the lobby menu hid Start during the countdown and never showed it again (48e42af6). Proof: cancelled at 8 s, Start again, the countdown ran out and the match loaded. |
| 27 | Open | Probably the pre-match player list (PreGameCountdown); a shot with bots is in Experimental's final gate. The big mode banner at match start is the original's GameAnnouncement. |
| 20 | Base game | Both chassis buttons are authored and nothing moves them; the scaling is uniform, so resolution cannot shift one. Aligned in our capture; the photo likely shows a hovered / focused button. |
| 21 | Fixed 5757dc3c + d91b178a, seen | Create a Character colours: dragging the selector works (it never did in our Flash player), a click anywhere on the colour square moves it there, and the controller stick / D-pad and arrow keys move it too. |

## Settings

| # | Status | What |
|---|---|---|
| 2 | Fixed (56166868), seen | Scroll arrows on the Graphics page: up when rows are above, down when below, both when both; clickable. |
| 3 | Fixed (56166868, 824816f7) | After Apply: "Restart Required: This setting requires the game to be restarted before it takes effect." for Frame Generation; "Some changes take effect when the next map loads." for HD / Ray Tracing changes; else "Settings Applied". |
| 4 | Fixed (56166868), seen | Commit Changes asks "Apply the changed graphics settings?" (Apply / Cancel). |
| 5 | Fixed bab80f23 + Rendering's 12 images, seen | A framed preview beside the Graphics window for Upscaling, HD Textures, Ray Tracing and Frame Generation, changing with the selected value (same-view captures by Rendering). |
| 34 | Fixed 41d04857 + d91b178a, seen | Brightness calibration at the first main menu, once (the user's change; the original shows it on the first Campaign entry). |
| 35 | Fixed 05767448 (+ Rendering), seen | The display gamma now covers the menus, so the calibration logos respond (left logo fades at 0). The near-white logos in the photo were the camera's exposure. |
| — | Fixed 7f0efe32 | Texture Quality works (it did nothing); every graphics setting re-applies after a match; the hidden Water row no longer applies. |
| — | Fixed 7f0efe32 | **Subtitles** row (Audio): campaign dialogue lines now draw at the bottom of the screen when On (default Off, as the original). |

## Notes for the build

- **Calibration screen stuck over gameplay (fixed d91b178a, seen):** the calibration picture is a separate screen that closed only on Accept / Cancel; a jump into a match while it showed (automated runs, or the Campaign-entry calibration) left it on top of the game. It now closes with the menu that opened it, and autoplay runs skip the first-launch calibration.
- **Open, Rendering:** switching HD Textures from Off to Faithful on the Graphics page froze the menu about 9 seconds while the HD level applied.

## Campaign / loading

| # | Status | What |
|---|---|---|
| 36 | Base game | RE confirmed our chapter select matches the original's browse state (all ten chapters in one row, checkpoints under it; D1 has 10 checkpoints). |
| 37 | Base game | A fresh profile unlocks D1 (chapter I) and A1 (chapter VI) by design; the rest unlock by progress. |
| 40 | Fixed (D1 path), seen | Main menu > Campaign > Solo > difficulty > Chapter I > Crash Site > character > load > play works from the menus. |
| 33 | Fixed 35ab6b95 | A loading progress bar on every map load screen (the original has none). |

## New rows and where they are

- **Settings > Graphics > Language** (INT / FRA): menus and subtitles change at once; voices from the next map.
- **Settings > Graphics > Split Screen Layout** (Horizontal / Vertical): used when a second controller presses Start in a lobby.
- **Private Match lobby > Game Settings**: mode, balancing, map selection, limits, bots, bot difficulty, Iconic Characters.
- **Private Match lobby > Team** (`< Autobots >`): switches your side.
- **Settings > Graphics > Recommended Settings**: detects the PC and applies fitting settings.

## DLC maps

The two packs are always owned. Their nine maps join the menus' map lists by their mode: five versus maps (TDM / DM / CTF / KOTH /
DOM / EXT, Private Match and Find Match) and Tranquillity / District / Pulse / Static in Escalation. A map is selectable once its
map data is in the asset root (all nine are in the package's DLC root). Their text (generator names, map names) is in English and
French.
