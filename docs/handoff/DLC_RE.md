# DLC_RE: what the two WFC map packs add and how the original gates them (RE, 2026-10-23)

**Labels:** C = pack ini / DLC authored.db / shipped script / exe; H = inferred; UNKNOWN.

**Sources:**
- AssetTools/work/dlc/MapPack{1,2}/files (read-only);
- manifests/dlc/authored.db;
- work/script/decomp;
- the Ghidra snapshot.

**Plan:** DLC_RE_PLAN.md.

## 1. Escalation exports (priority; C)
`notes/data/escalation/MP_ESC_{Tranquillity,District,Pulse,Static}.json` have the same schema as BrokenHope / Remnant: spawn_list,
generators, zones and the Kismet events downstream.
- They were produced by `tools/escalation_export.py <outdir> --dlc <dlc authored.db>`.
- The class defaults (`_class_defaults.json`) still come from the base db. The base exports were re-checked and are byte-identical.

| Map | Spawn list | Waves | PreGame / between waves / FillWaveReCheck | Horde | Zones / generators / events |
|---|---|---|---|---|---|
| Tranquillity ("Forsaken", Pack 1) | TR_Tranquillity_p.SpawnList.Tranquillity_Spawn_List | 17 | 25 / 25 / 1.5 s | Decepticon (players Autobot, Faction 0) | 34 / 35 / 37 |
| District (Pack 1) | TR_District_p.SpawnList.District_Spawn_List | 20 | 10 / 25 / 1.25 s | Autobot (players Decepticon) | 28 / 65 / 69 |
| Pulse (Pack 2) | TR_Escalation_Pulse_m.SpawnList.Pulse_Spawn_List | 30 | 15 / 25 / CDO | Decepticon | 33 / 46 / 48 |
| Static (Pack 2) | TR_Static_m.SpawnList.Static_Spawn_List | 21 | 10 / 25 / 3.0 s | Autobot | 34 / 72 / 112 |

- **Waves:** the same mechanics as ESCALATION_WAVES_RE.
  - Budget / BudgetAlive / BudgetDeficitThreshold per wave.
  - Buffs are TnBuffSurvivalDecreaseDamageTaken / IncreaseDamage levels, rising in the late waves. Pulse goes up to level 11 on both.
  - There's no player-count or difficulty scaling.
- **Enemies:** all existing TR_AiEscalation / TR_AiCharacterDefinition AICDs, except one.
- Pulse is the only DLC map with **Soldier_Tank** bosses (EST_Boss): waves 0–1, 13–19, then 20+ ×2.

**The one new enemy (District only, C):** `TR_District_p.AiEscalation_Soldier_Jet_Autobot_AICD`.
- It's an Autobot copy of Soldier_Jet_Decepticon_AICD:
  - the same ActionSet / GoalSet / SensorSet (Soldier_Jet / JetSoldier / SoldierJet);
  - PawnClass TnAiJetSoldierPawnContent;
  - **ChassisType TR_SoldierJetAut_ROBODEF_p.SoldierJetAut_ROBODEF**, which is in the base dump;
  - colours R222 G32 B32 / 192 grey;
  - LootClassification SoldierAut.
- It adds a Primary weapon-class override next to the Vehicle one (both default props).
- Spawn rows (EST_Air, cost 30): waves 3–6 up to 1; 7–10 up to 2; 11+ up to 3.
- **For Gameplay:** no new pawn class or body, just an AICD with an Autobot chassis.

## 2. Pack contents (C)

| | Pack 1 ("Map Pack 1") | Pack 2 ("Map Pack 2") |
|---|---|---|
| Versus maps | MP_KON_Fortress (MapId 512, Presence 21), MP_ORB_Havoc (511, 22) | MP_KON_Sector (529, 39), MP_IAC_Horizon (517, 27), MP_IAC_Metropolis (518, 25) |
| Escalation maps | MP_ESC_Tranquillity (513, 23, Faction 0), MP_ESC_District (514, 24, Faction 1) | MP_ESC_Pulse (516, 26, Faction 0), MP_ESC_Static (531, 41, Faction 1) |
| Character unlocks (MP) | Tank4 Scattershot, Truck6 Onslaught, Tank5 Demolishor, Car6 Jazz, Jet7 Shockwave | Car7 Deadend, Truck5 Zeta Prime |
| UI | UI_DLCThumbnails_p (map images) | UI_DLC2Thumbnails_p |
| Languages | INT, FRA, DEU, ITA, ESN (TransGame.* + per-map _LOC_*) | same |

- **Versus game types:** all DM / TDM / CTF / KOTH / EXT / DOM.
- **ESC maps:** SV, with CampaignCharacters = the 13 Autobots (Faction 0) or 13 Decepticons (Faction 1). These lists are the same
  as the base ESC maps' character pools.
- **No new playable characters.**
  - The seven unlocks are **base-game chassis** (their ROBODEF / ROBO / VEH packages are all in the dump).
  - The base TransCustomization.ini ships them with `LockedChassis=true` / `LockedCharacter=true`.
  - The pack's TransCustomization.ini sets both to **false**.
- **Base TransLevels.ini already lists Fortress, Havoc and Tranquillity** (MapId / PresenceId, no ImagePath). Pack 1 re-adds them
  with `+` and its ImagePath. Pack 2's maps aren't in the base ini.

## 3. How the original gates and lists DLC (C unless marked)
- **Mount:**
  - the exe enumerates marketplace content (XContent, roots named `DLC%d`, Function_0x829E3D08);
  - it merges each pack's `<Pack>__<IniName>.ini` / `.int` into the matching config (H, the stock UE3 DLC ini merge);
  - it adds the pack's packages to the package path.
  - `Engine.PackagesToFullyLoadForDLC` makes UI_Lobby_m / UI_CampaignLobby_m fully load the thumbnail package.
  - Removal or corruption → `DLCContentRemovedMessage` / `CorruptDLC*` popups. A storage-device change re-reads all DLC.
- **Map lists:** the TnDataStore_MenuItems map providers = the TransLevels MapInfo sections. **A merged pack's maps simply appear**,
  because the ini sections are their only definition.
  - `TnDataProvider_MapInfo.HasRequiredAssets` is **native** (exec table 0x837090AC). **H:** it checks that the map package is
    present. `HasRequiredMapAssets(MapId)` and the lobby use it.
  - So the three base-listed Pack 1 maps are hidden or disabled without the pack.
- **Playlists:**
  - `IsProviderDisabled` = !VisibleInMenu || !HasRequiredContent;
  - HasRequiredContent gathers the **DlcId** of every map whose assets are present, then `HasRequiredContentForPlaylist`: every
    ContentId of the playlist must be among them.
  - `MapCanBePlayed`: the map's DlcId must be among the selected playlist's ContentIds.
  - **Shipped TransPlaylists.ini:** every playlist has `ContentIds=(0)`, and the MapInfo CDO has DlcId 0. **DlcId is set nowhere
    in script or ini.**
    - **H:** it's assigned natively per mounted pack, or stays 0.
    - **UNKNOWN:** the live (Demonware-downloaded) playlist file, which probably added DLC playlists with ContentIds such as (0, 1).
      It isn't in the dump or the packs.
  - With the shipped data, DLC maps with DlcId 0 are allowed in every playlist.
- **Character select:** `TnCharacterScriptBinding.IsCharacterUnlocked` / `IsChassisUnlocked` check the chassis provider's
  `LockedCharacter` / `LockedChassis`. If set, the profile's `CustomCharacterData.IsChassisUnlocked(UniqueId)` decides. **With the
  pack merged the flags are false, so the characters are always available.**

## 4. What the rebuild short-circuits (for Frontend; DLC always owned)
1. Merge both packs' TransLevels / TransCustomization / DLCMaps2 ini sections into the base data. Pack 1's `+` entries replace the
   three base entries and add the ImagePaths.
2. `HasRequiredAssets` / `HasRequiredMapAssets`: true for all nine DLC maps.
3. Every DlcId = 0. Keep the shipped ContentIds=(0), so HasRequiredContentForPlaylist and MapCanBePlayed pass with no change.
4. LockedChassis / LockedCharacter false for the seven unlocked chassis. Character select needs no other gate.
5. No DLC-removed or corrupt popups. The thumbnails come from UI_DLCThumbnails_p / UI_DLC2Thumbnails_p.
6. Escalation map list: the four ESC maps with their Faction. Quick Match / playlist pools: the versus maps join the existing
   game-type pools (no separate DLC playlist exists in the shipped data).
