# Checkpoint data: payload, storage, client transfer, start spots

WFC RE lane, 2026-10-18. This closes the "native GenerateCheckpointData packaging" item in CAMPAIGN_RE_INDEX.md; it extends
CAMPAIGN_STREAMING_RE.md §4.

Sources:
- script: HM CheckpointManager, TnCheckpointManager, TnCheckpointSaveData, HmPlayerController / TnPlayerController,
  Engine.Checkpoint, TnSpawnPointFinder;
- natives (headless Ghidra; queries `work/q/ck*.txt`): GenerateCheckpointData 0x8277F1E8, header reader 0x8277F150,
  path-string serializer 0x8277C0D0, RestartFromCheckpoint 0x8277F448, restore 0x8278B2F8;
- property tables from the cooked packages, plus CDOs.

## 1. The payload: `CheckpointManager.CheckpointData` (`array<byte>`)

Built by native **`GenerateCheckpointData(Checkpoint)`** each time a checkpoint becomes active (`SetActiveCheckpoint`, and
`OnStartingFromCheckpoint`). It clears the array and writes a memory archive.

| # | Type | Field | Source |
|---|---|---|---|
| 1 | int32 | **CheckpointVersion** | manager +0x44; HM CDO = **1**, not overridden by TnCheckpointManager |
| 2 | FString | **map name**: the persistent map package | the checkpoint's world |
| 3 | FString | **package name**: the checkpoint's own outermost package (the streaming level it lives in) | the checkpoint |
| 4 | FString | **checkpoint path name** (`PathName(Checkpoint)`) | the checkpoint |
| 5 | blob | **save-data body** | manager virtual +0x138 (archive). It returns GenerateCheckpointData's bool |

- **The field order is CONFIRMED.** The restore logs `"######### loading checkpoint '%s' from package '%s' for map '%s'
  #########"` with fields 4, 3, 2.
- **Strings** are written by 0x8277C0D0, which removes the PIE prefix **`UEDPIE_`** on save and puts it back when loading in PIE.
  Otherwise these are plain FStrings in the archive's string format.
- **The body (field 5) is PARTIAL.** The +0x138 / +0x13C bodies were not found: the manager vtable could not be located
  statically. From the script hooks the body is the save-data object:
  - Save: `PreSaveGenerateCheckpointData()` → `CreateSaveData()` (`SaveDataClass` = **`TnCheckpointSaveData`**) →
    `PopulateSaveData()` → serialized.
  - Restore: +0x13C reads the same archive, positioned after the header → `PostLoadHandleCheckpointData(SaveData)` →
    `DisseminateSaveData()`.
  - We don't need byte compatibility with the original saves. Serialize the object below in any stable form behind the
    header.

## 2. The save-data object: `TnCheckpointSaveData` (CONFIRMED fields and types)

**Top-level fields:**
- `int DataVersion`: set to ClassVersion at populate.
- `int ClassVersion`: **4** (CDO).
- `array<TnCheckpointPlayerData> SavedPlayers`.

**`TnCheckpointPlayerData`**, one per TnPlayerController at save time. Field order is H (declaration order; WFC packages list
children in reverse, as the SeqAct_MoviePlayer bool masks confirm):

| Field | Type | Written from |
|---|---|---|
| PlayerName | string | `PRI.PlayerName` (gamertag) |
| CharacterName | string | `PC.SelectedCharacter.CharacterName` |
| Health | float | `FMax(Pawn._SegmentedHealth.GetSingleSegmentHealth(), Pawn.GetHealth())`; only if the PC has a TnPlayerPawn |
| AbilityResource | float | `Pawn.AbilityManager.GetResource()` |
| CrappyData | float | never written (padding) |
| WeaponTypes | array<string> | weapon class path names, **WeaponType 1 (the equipped type) moved to index 0**; types 5 and 7 skipped, type 6 goes to the vehicle list |
| WeaponReserveAmmos | array<int> | `ReserveAmmoCount + AmmoClipCount`, parallel to WeaponTypes |
| VehicleWeaponTypes | array<string> | the WeaponType 6 weapons |
| VehicleWeaponReserveAmmos | array<int> | parallel |

Saved: player state only. Not saved: Kismet state, pickups, destructibles, AI. Levels reload fresh (streaming doc §4.2).

**Restore** (`OnDisseminateSaveData`, per TnPlayerController):
1. **`FindSaveData`** fails if:
   - `DataVersion != ClassVersion`;
   - there is no PRI;
   - **no entry matches `PRI.PlayerName`**;
   - the entry's CharacterName is empty;
   - or the PC already has a current character with a different name.
2. Found:
   - `PC.SelectCharacter(CharacterName, 1)`;
   - if `Health != −1`: `_CheckpointOverrideCharacterData = {Health, AbilityResource, WeaponTypes (FindObject'd classes; unknown
     names dropped), ammo arrays, vehicle lists}`, applied at the player's next spawn.
3. Not found: the override is cleared (`EmptyCharacterData`), so the player spawns with the character's defaults.

## 3. Where it is kept

**Storage files, not the profile:**
- `TnCheckpointManager` CDO:
  - `CheckpointFilenames = CheckPoint0.sav, CheckPoint1.sav`;
  - icons `AUTOCHECKPOINT.png`, `DECEPCHECKPOINT.png`;
  - subtitles "Autobot Checkpoint" / "Decepticon Checkpoint".
- **Index 0 = Autobot campaign, 1 = Decepticon campaign**. This matches `StartLoadFromCheckpointLoadingMovie` using
  ResumeAutobot for 0.
- Written by `SaveActiveCheckpoint(index)` through `HmQueuedDataReaderWriter.SaveData(filename, CheckpointData, description,
  icon)` on the selected storage device.
- **When:** Kismet `SeqAct_SaveActiveCheckpoint`, which saves only if datastore `CheckpointData.bShouldSave` is non-zero.
- Existence is checked per file (`CheckCheckpointFileExistence`, rerun on storage-device change). Statuses come from
  ECheckpointFileStatus.

**The profile holds only progress:**
- `TnCampaignGame.NotifyReachedCheckpoint` → `PC.ClientReachedCheckpoint` → profile `ReachedCheckpoint(map, index)` +
  `SaveProfileSettings`. H: used by chapter select.
- Mission completion is in the profile too.

**Loading a file:** `TnCheckpointManager.LoadAndRestartFromCheckpoint(index)`:
1. shows the "Loading checkpoint" popup;
2. `LoadData(file)`;
3. on success → `RestartFromCheckpoint(bytes)`:
   - the header's checkpoint path isn't found in memory → travel to the map with `?LoadCheckpoint?DontLoadInitialStreamingState`;
   - otherwise restart in place (streaming doc §4.2).

## 4. Co-op transfer to clients (CONFIRMED script signatures; H the native split)

During the in-place restore the host sends the payload to each client:
1. `ClientPrepareForCheckpointData(TotalDataSize)`;
2. **`ClientSendCheckpointDataPiece(piece)`**, in pieces of **`kCheckpointDataPieceSize = 460` bytes**;
3. `ClientRestartFromCheckpoint(FinalCheckpointDataPiece)`. TnPlayerController starts the "Resume Autobot / Decepticon" loading
   movie before calling super.

Clients rebuild the same byte array and run the restart.

## 5. Start spots and auxiliary spawn points (CONFIRMED script)

**`Checkpoint.GetStartSpotForPlayer(PC)`** (the restore sets each PC's StartSpot, then `TeleportPlayerToCheckpoint`):
- **Local player controller → the checkpoint itself.**
- **Others:** `++CurrentAuxilliarySpawnPointIndex`, wrapped to 0 when it reaches `AuxilliarySpawnPoints.Length`, and that aux
  point is returned.
  - Because the increment comes first, with the minimum of 2 aux points the first remote player gets **Aux[1]**, the next Aux[0],
    then Aux[1] again.
  - The index is not reset between restarts.
- **No aux points →** the checkpoint, with a logged error ("clients will fail to spawn due to encroachment").

`TeleportPlayerToCheckpoint`: SetLocation / SetRotation (StartSpot), `ClientSetRotation(rot, true)`, `Pawn.Teleport`, camera Reset.

**Authoring rules (`CheckForErrors`):**
- unique Tag;
- a StreamingState;
- **at least `MinNumAuxilliarySpawnPoints = 2`** aux points;
- no empty entries, and not the checkpoint itself.

**Other spawns at the checkpoint** (respawn and buddy spawn: `TnSpawnPointFinder.FindFromActiveCheckpoint`):
1. The checkpoint if `IsSafeSpawnLocation(location, default radius, height)`.
2. Else **the first safe aux point in array order**.
3. Else the checkpoint, with a logged error.
