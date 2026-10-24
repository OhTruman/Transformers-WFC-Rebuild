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
