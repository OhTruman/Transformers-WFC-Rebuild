# DLC plan (after the final build)

User 2026-10-23 ~20:20: "F:\Transformers Rebuild\4156084C: the DLC I have for the game. Add it after the current build;
then split the DLC packs across the lanes so each AI builds and adds its part."

## Source (read-only, like Game Dump / ExtractedAssets)
`F:\Transformers Rebuild\4156084C\00000002\` = two Xbox 360 LIVE / STFS marketplace packages (title 4156084C, content
type 00000002), named in their headers:
- `4FF07171C37B52D0DA136DF8D005465E00DBC8B241` (395 MB): "Transformers: WFC - Map Pack 1" = Map and Character Pack #1
- `09BCE0B69F21E41C920BB9B7C632F9B679681A0A41` (520 MB): "Transformers: WFC - Map Pack 2" = Map and Character Pack #2
Never modify / extract INTO this folder; extraction goes to AssetTools' own work/ (STFS -> the packs' .xxx / .tfc etc.).

## Order
1. Finish and hand over the current build (gate -> refresh.sh -> verify -> hand-over). The DLC is NOT in it.
2. AssetTools extracts both packs (STFS) and inventories them: maps (versus + Escalation), characters, chassis,
   abilities, audio, localisation, any gamedata / ini deltas. RE reads the packs' manifests / Kismet / class data and
   writes DLC_RE.md (what each pack adds and how the original gates it: entitlements, menu entries, playlists).
3. Split by content (both packs in parallel), one owner per item; integration merges in the usual loop:
   - AssetTools: per-map render data + inputs (the same chain as the campaign / MP maps), character bodies / weapons /
     anims export, Escalation data for the new ESC maps, DLC assets into the outside-render list.
   - Rendering: render data check + eye pass per new map (Off / Realistic), lightmaps / RT world, any new material.
   - Gameplay: new characters (classes, abilities, chassis), new map nav / spawns / pickups / transforming actors,
     Escalation waves for the new ESC maps, SIMHASH identity for the existing maps.
   - Frontend: map list + playlists + Quick Match pools + Escalation map list + character select entries, DLC always
     owned (no entitlement check: the user owns the packs), credits.
   - Systems: audio for the new maps (banks / reverb / music), packaging rule (DLC in the base package), two-PC MP check.
   - Experimental: identity + regress rows for the new maps; the final gate grows from 10 to 10 + N maps.
4. One combined timed gate on the DLC head, refresh.sh, verify, hand-over (the DLC build).

## Open
- The pack contents (map / character names) come from AssetTools' inventory after the freeze; this file is updated then.

## Also in the next build (user 2026-10-23 ~20:35)
- A Language option in Settings (Frontend): every language the dump / DLC packs ship (at least INT and FRA; RE / AssetTools
  list the rest), applied to the GFx text, the subtitles and the dialog banks where they exist; INT stays the default.

## Inventory (AssetTools, 2026-10-23 ~21:00; manifests/dlc/dlc_inventory.json; files in AssetTools/work/dlc/MapPack{1,2}/files)
- **Pack 1 (DLCMaps, 375 MB):** versus MP_KON_Fortress "Fortress" (MapId 512), MP_ORB_Havoc "Havoc" (511); Escalation
  MP_ESC_Tranquillity "Forsaken", MP_ESC_District "District". Unlocks: Scattershot, Onslaught, Demolishor, Jazz, Shockwave.
- **Pack 2 (DLCMaps2, 494 MB):** versus MP_IAC_Horizon "Horizon", MP_IAC_Metropolis "Metropolis", MP_KON_Sector "Sector";
  Escalation MP_ESC_Pulse "Pulse", MP_ESC_Static "Static". Unlocks: Deadend, Zeta Prime.
- **No new playable characters:** the "character packs" are TransCustomization.ini unlocks of base characters (all already
  exported). One new ESC enemy: TR_District_p.AiEscalation_Soldier_Jet_Autobot_AICD. Two new set-dressing skeletal meshes.
- **Languages:** the packs ship INT / FRA / DEU / ITA / ESN for DLC strings only; the base is INT + FRA, so the Settings
  Language row offers INT + FRA.
- **Other:** TransLevels.ini MapInfo deltas (MapId, PresenceId, game types, thumbnails), DLCMaps2.ini PackagesToFullyLoadForDLC,
  BL_LVL_* / WL_LVL_DLC_* audio + dialogue, TR_* Escalation data, UI thumbnails, GuidCache.

## Assignments (started 2026-10-23 ~21:00)
- AssetTools: the 9-map MP chain (Pack 1 first; one heavy lane while Systems packages) + ESC data + outside-list rows.
- RE: DLC_RE.md (DlcIds, playlists, map info, presence, ESC waves, unlock gating, Frontend short-circuits).
- Gameplay: the 7 unlocks playable everywhere incl. bots; the District ESC enemy; per map nav / spawns / pickups /
  transforming actors / ESC waves.
- Frontend: unlocks owned everywhere; map + playlist + Quick Match + ESC list entries + thumbnails (after DLC_RE.md);
  the Language option (INT + FRA).
- Rendering: per map render check + lightmaps / RT / calibration + eye pass; the 2 new skeletal meshes.
- Systems: DLC audio / dialogue banks per map; the Language audio setter; packaging (DLC in the base).
- Experimental: final-gate map list 10 -> 19; identity rows per new map.

## Also in the next build (user 2026-10-23 ~21:30): split screen on PC
Two local players on one PC (the original's 2-player split screen: campaign co-op, Escalation and local versus, per RE):
- RE: SPLITSCREEN_RE.md: which modes the original splits, the layout (horizontal / vertical, HUD placement per view),
  the second player's sign-in / profile rules, camera / audio listener rules, what the lobby does with two local seats.
- Rendering: two viewports per frame (two cameras, split render targets or scissored halves, post-process per view, the
  RT path per view), the HUD composite per half.
- Frontend: second local profile sign-in (press Start on a second pad), per-view GFx HUD instances, menus with two local
  seats (party of two locals), settings per profile.
- Gameplay: a second local human participant (the sim already has multi-human seats: the local loop gets two input
  owners), pad 2 input routing, campaign co-op with two locals, Escalation / versus with two local seats in online too.
- Systems: two audio listeners (the original's rule); input device routing = the user's requirement: two controllers, OR keyboard+mouse for player 1 and a controller for player 2 (any pad takes seat 2 on Start; KB+M always belongs to seat 1; pads can be swapped).
- Experimental: a split-screen identity + perf row (two views cost ~2x).
Order: RE first, then Rendering + Gameplay in parallel, Frontend + Systems after; it ships with the DLC build if ready,
else in the one after (the user decides at the cut).

### Split screen: RE result + decisions (2026-10-23 ~21:50)
RE (SPLITSCREEN_RE.md): the original has NO split screen (no second LocalPlayer in script; every mode is online / system
link, one seat per console). So the modes and layout are ours. Decisions (Integration, under the user's autonomy):
- Modes: every mode: campaign co-op (2 locals + squad AI), Escalation, local versus with bots, and online sessions with
  two local seats (two PRIs on one connection, counted against MaxPlayers, per RE).
- Layout: horizontal (top / bottom) default, vertical as a Settings option; title-safe per view as the stock CDO.
- Seats: seat 1 = keyboard / mouse or the first Start pad; seat 2 = the next other pad pressing Start (the stock
  CreatePlayer(ControllerId) rule); each seat keeps its own profile remap, customization, XP; saves / achievements on seat 1.
- Render: full quality per view (beauty first); per-view post-process and RT; perf reductions only if visually lossless.
- Audio: one listener per player, nearest-listener attenuation (stock).
- The 45 GetLocalPlayerController call sites must use the owning PC (Gameplay / Frontend).
