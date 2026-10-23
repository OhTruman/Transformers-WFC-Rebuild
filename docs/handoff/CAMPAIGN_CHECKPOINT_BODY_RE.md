# Checkpoint save-data body: the native serializer (RE, 2026-10-23)

Closes CAMPAIGN_CHECKPOINT_DATA_RE's field 5 ("save-data body", PARTIAL).
Labels: C = WFC binary (headless Ghidra); H = inference.

## How the body is reached (C)
- **The checkpoint manager's virtuals:**
  - TnCheckpointManager's vtable is at 0x82202658.
  - **+0x138 = 0x8275E460 (save)**: get the save-data object (script interface 0x8308F570), then **SaveData vtable +0x12C (archive)**.
  - **+0x13C = 0x8275E4E0 (load)**: create the save-data object (0x83086A90), then **SaveData +0x130 (archive)**, then
    `PostLoadHandleCheckpointData(SaveData)` (0x83075048).
- **UHmCheckpointSaveData's vtable** (0x8209F558, from its internal constructor 0x82798CB8):
  **+0x12C = 0x82769350 (write)**, **+0x130 = 0x827694E0 (read)**.

## Body format: a self-describing property list (C)
**Write (0x82769350):** for every property in the save-data object's class property chain **whose owner is the object's own
class** (`TnCheckpointSaveData`, so no inherited properties), and that passes the serialize filter (0x82791738: not native
0x1000, not transient 0x2000 for this archive, the usual editor / deprecated exclusions):
1. **FString property name**;
2. **FString property type name** (the property's type string, vtable +0x144);
3. **the value**: `Property->SerializeItem(Ar, this + Offset)`, in the engine's binary item form.

**Read (0x827694E0):** loop **until the archive is at its end**:
- read the name and the type string;
- `FindField(class, name)`;
- **if the type string matches**, SerializeItem into the property; otherwise nothing is consumed (H: a type mismatch would
  desync; the original never hits it).

**So the body is, in this order** (the class property chain; H: declaration order):
- `DataVersion : int`;
- `ClassVersion : int`;
- `SavedPlayers : array<TnCheckpointPlayerData>`.

Each is written as (name, type, value). Array items are the struct's binary serialization: the TnCheckpointPlayerData fields in
the order of CAMPAIGN_CHECKPOINT_DATA_RE §2 (H: binary struct form, declaration order).
- `TnCheckpointSaveData` declares exactly these three properties (C, cooked TransGame export list).
- `HmCheckpointSaveData` declares none.

## The whole checkpoint blob
```
int32  CheckpointVersion (1)
FString MapName
FString CheckpointLevelPackage
FString CheckpointPathName           (UEDPIE_ prefix stripped)
repeat until end: { FString PropName; FString PropType; <binary value> }   // DataVersion, ClassVersion, SavedPlayers
```

**For the rebuild:** byte compatibility isn't needed. A name-tagged, typed property list is the original's own shape, so a
JSON or tagged encoding of the same three fields is a faithful equivalent. **There's no world-state section**: Kismet, pickups
and destructibles come back from the level reload (CAMPAIGN_STREAMING_RE §4.2), not from the body.
