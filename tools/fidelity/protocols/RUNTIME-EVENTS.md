# Runtime event protocol for Experimental's match and frontend validators

Experimental validates behaviour it can observe. The product exposes match and frontend state to
`match-validator.ps1` and `frontend-validator.ps1` through **one log line per event** in `wfc.log`, written
with `LOG_INFO`. The validators never require a value that RE has not recovered. Where an expectation is
UNKNOWN, the measured value is reported as INFO.

This is a proposal for the owning lanes (Gameplay for MATCH, the frontend owner for FRONTEND). The names
are the contract; the wording after each `key=` field is free. Emit an event only when it really happens:
do not emit placeholder screens or scores.

## MATCH (Gameplay)

```
MATCH init mode=<DM|TDM|CTF|KOTH|EXT|DOM> rules=<comma-separated TnGameRules_* names> teams=<n> time_limit_s=<n|none> score_limit=<n|none>
MATCH spawn player=<id> team=<n|-1> start=<PlayerStart actor> pos=<x>,<y>,<z>
MATCH timer remaining_s=<seconds>                      (at least once per simulated second while running)
MATCH kill killer=<id> victim=<id> killer_team=<n> victim_team=<n> weapon=<name>
MATCH score team=<n> score=<total>                     (TDM: team score; FFA: team=-1 and player=<id>)
MATCH death player=<id> pos=<x>,<y>,<z>
MATCH respawn player=<id> start=<PlayerStart actor> delay_s=<seconds since death>
MATCH end reason=<score_limit|time_limit|other> winner=<team|player|draw> t=<simulated seconds>
MATCH cleanup
MATCH restart
```

## FRONTEND (frontend owner)

`checkpoint` is one of the stages below. `movie` is the authored GFx movie (`UI_Gfx*_p/<Name>_GFX`) or
`.mkv` file that is on screen, exactly as AssetTools lists it.

```
FRONTEND screen=<checkpoint> movie=<authored movie> state=<enter|exit> t=<seconds since exe start>
FRONTEND select playlist=<PlaylistId> mode=<GameModeTag> map=<MapFilename>
FRONTEND load state=<begin|end> map=<MapFilename> movie=<loading movie> t=<seconds>
FRONTEND error what=<text>
```

| checkpoint | authored anchor (AssetTools manifests) |
|---|---|
| startup | `TF_InitialStartup` legal screen while the first map loads (`frontend_loading.json` `startup_order`) |
| startup_logos | `Engine.MovieSettings.MoviesToAlwaysPlaySound`: Logo_Activision, Logo_Hasbro, Logo_HighMoon (`frontend_flow.json`) |
| title | `UI_FrontEnd_m` (startup map, `[URL]`) with `UI_GFxFrontEnd_p/FrontEnd_GFX` |
| main_menu | `UI_GFxFrontEnd_p/FrontEnd_GFX` |
| multiplayer | `UI_GFxLobbies_p/PartyLobby_GFX` / `GameLobby_GFX` |
| mode_select | playlists visible in the menu (`frontend_modes.json` `menu_visible_playlists`) |
| map_select | maps compatible with the mode (`frontend_maps.json`; Streets = `MP_IAC_Streets_Base_m`, MapId 508) |
| loading | `TF_LoadingScreen` / `UI_GFxLoading_p/LoadScreen_GFX` / `LoadScreenAlpha_GFX` |
| gameplay | the selected map, with `UI_GFxHud_p/Hud_GFX` |
| return | `UI_GFxEndGameStats_p/EndGameStats_GFX`, then a lobby or menu |

The *exact* screen sequence between the authored anchors (for example, whether `title` and `main_menu`
are separate states of `FrontEnd_GFX`) is UNKNOWN until RE recovers it. The validator therefore checks
the ordering of the stages that do appear, and that every movie it sees is authored. It does not require
a stage the product never claims.
