# MP reverb rooms: how the original picks reverb (RE, 2026-10-18)

Labels: CONFIRMED (C) = cooked data / Kismet graph / script; HIGH (H) = inference, not traced natively.
Export: `notes/data/mp_reverb/<MAP>.json` + `_default_and_summary.json` (`tools/mp_reverb_export.py`). It lists every reverb / mixer
Kismet action, its preset name, the Kismet path back to the originating event (Touch / UnTouched, through
AmbientAudioZone / Delay / RemoteEvent), the trigger volume, and the preset's mixer + DSP definition.
Trigger-volume polygons are already in AssetTools' `ExtractedAssets/VerticalSlice/Maps/<MAP>/audio.json` (`zones`).

## 1. No ReverbVolumes: reverb is a Kismet-driven mixer preset (C)

- **There are zero placed `ReverbVolume` actors in the game** (only the class default). No WorldInfo reverb settings are authored.
- WFC replaces the stock UE3 volume reverb with High Moon's mixer:
  - **`SeqAct_Reverb`** ("(High Moon) Environmental Reverb"): one input *Activate*, plus `ReverbMixerPreset` (a
    SoundMixerPresetName).
  - **`SeqAct_AmbientAudioZone`** ("(High Moon) Ambient Audio Zone"): *Enter*; its *Scene 0 Begun* output activates the reverb
    and the zone's ambience.
  - Also `SeqAct_Mixer` (*Enable / Disable Preset*) for ducks / PA.
- Presets are defined once in **`SoundConfig.SoundMixerProperties`**:
  - `MixerPresets[547]`: Name, **Priority**, **FadeInTime / FadeOutTime**, Duration (−1 = until disabled), IsPlayerPOV.
  - `SoundCategories[47].DSPPresets[name]`: per-category DSP (Volume, LowPass, Echo, Reverb, …).
  - **Every MP reverb preset sets DSP on exactly one category, `MASTER_WET`.** So reverb applies to all "wet" sounds (SFX_WET_*,
    DX_WET_*) and never to the DRY categories (HUD, music, DX_DRY, radio).
- **Reverb fields** are FMOD Ex I3DL2: Room / RoomHF / ReflectionsLevel / Level in mB, DecayTime s, DecayHFRatio,
  ReflectionsDelay / Delay s, Diffusion / Density %, HFReference Hz, RoomRolloffFactor. Plus an **Echo** (Delay ms, DecayRatio,
  WetMix, DryMix) on the same category.

## 2. Default when outside every zone (C)

- `MASTER_WET`'s "Default" DSP has **Reverb Room / RoomHF / Reflections / Level = −10000 mB** (silent) and Echo WetMix 0. Outside
  any zone the mix is **dry**.
- **No reverb fires at level load in any MP map:** every reverb action traces back to a `SeqEvent_Touch`. A player starts dry until
  their pawn touches a zone volume. `bForceOverlapping` makes a spawn inside a volume count as a touch.
- Starts outside every zone (AssetTools `player_start_zones`): BrokenHope 3 / 4, Rust 28 / 111, Seed 7 / 102, Streets 8 / 84,
  Berth 4 / 120, Gorge 3 / 116, the rest 0.

## 3. Who triggers it (C)

- **The trigger volumes are local and client-side:** `bLocalPlayerOnly`, `bClientSideOnly`, `bForceOverlapping`,
  `MaxTriggerCount 0` (unlimited), ReTriggerDelay 0–0.1, ClassProximityTypes Pawn, **IgnoredClassProximityTypes TnCamera**.
  So it's the **local player's pawn** (not the camera) entering a volume. Each client picks its own reverb, and nothing is
  replicated.
- **Wiring patterns:**
  1. **Zone volumes (most maps):** Touch.Touched → AmbientAudioZone.Enter → *Scene 0 Begun* → SeqAct_Reverb(room).
  2. **Direct volumes (Rust 5, Remnant 1):** Touched → SeqAct_Reverb(A), **UnTouched → SeqAct_Reverb(B)**. These are archway /
     doorway transitions that restore the outer room on exit. E.g. Rust "ARCHWAY 2" (TriggerVolume_15840): exit → RUST_FOYER.
  3. **Delayed / remote (Debris 29, Complex 19, Remnant 26 via Delay; RemoteEvent chains):** same, with a Delay or
     RemoteEvent in between. Debris uses UnTouched → AmbientAudioZone for 11 exits.

**Per map:**

| Map | Reverb actions | Presets (Priority) | Notes |
|---|---|---|---|
| BrokenHope | 49 | BH_EXTERIOR_01 (256), COMPLEX_CORRIDOR / CROSSROAD / LOW_ENT / MID_ENT / UPPER_ENTRANCE (196–202), DESOLATIONROW_CORRIDOR (197) | + matinee duck / mute (383–384), INTERCOM_PA (413) |
| Remnant | 14 | REMNANT_CORRIDOR / HANGAR / LARGE_ROOM / MED_LARGE_RM / MED_ROOM / OPEN_HANGAR (210–215), COMPLEX_MID_ENT (196), ORBITAL_SPACE (66) | + INTERCOM_PA |
| Berth | 20 | BERTH_ALCOVE / BASEMENT / FLAG_ROOM / FOYER (161–165) | fades 0 |
| Rust | 35 | RUST_ALCOVE / COMPOUND / CORRIDOR / FOYER / TOWER / TUNNEL / UNDER_BRIDGE (174–180) | fades 0; archway exit restores |
| Seed | 49 | SEED_DOORWAY / EXT_ALLEY / EXT_COURTYARD / EXT_FOYER / INT_CORRIDOR / INT_HALL / PLANETARIUM / UNDER_BRIDGE (184–191) | + DUCK_EXT_AMB (386) |
| Streets | 9 | STREETS_* ×9 (182–223) | |
| Molten | 22 | COMPLEX_* ×6, DESOLATIONROW_MAINROOM / EXTERNAL, STREETS_TRAIN_TUNNEL, STREET_EXTERIOR (181–221) | reuses other maps' presets |
| Debris | 7 | ORBITAL_CORE / INTERIOR / SHIP / SPACE / TUNNEL (63–67) | + ORBITAL_INTERIOR_DUCK (385) |
| Complex | 12 | COMPLEX_CANYON / CORRIDOR / LOWER_CANYON / LOW_ENT / MID_ENT / UPPER_ENTRANCE (196–203) | + WATERFALL_DUCK (382) |
| Gorge | 12 | GORGE_CORRIDORS / EXT / RAMPROOM / TALLROOM / UNDERDECKS (205–209), COMPLEX_CORRIDOR (199) | |

The full DSP values per preset are in the json; DecayTime ranges from 0.3 s (corridors) to 4.8 s (Streets AUTO_ROOM_02).

## 4. How the active reverb is chosen (C for data, H for native)

- **One reverb at a time, the last zone entered** (H, strong).
  - SeqAct_Reverb only has *Activate*; no MP graph ever disables a reverb preset. Exits are handled by activating another
    room's reverb (the UnTouched → outer-room pattern).
  - With enable-only semantics, presets would pile up and the highest Priority would stick forever. So SeqAct_Reverb must
    **replace** the current reverb preset. AssetTools' note on AmbientAudioZone ("entering a new zone ends the previous scene")
    agrees.
- **Crossfade:** the switch uses the preset's FadeInTime / FadeOutTime: 0 (Berth, Rust, Gorge rooms), 0.25 s (most), 0.5 s
  (Remnant hangars, Gorge ext), 0.3–0.6 s (Debris) (H: linear crossfade of the MASTER_WET DSP parameters).
- **Priority:** unique per preset (MP reverbs 63–256; ducks / PA 382–413). It matters when several presets are active together,
  i.e. a reverb plus a duck / PA (H: on the same category, the higher Priority's DSP wins per parameter, or they combine). Since
  only one reverb is live at a time, Priority does not pick between rooms.
- **No geometry containment test or interpolation by position:** the switch is event-driven at the volume boundary.
  Overlapping volumes resolve by touch order (last touched wins). A player standing in two volumes keeps the last one entered.

## 5. WFC-specific overrides (C)

- `HmPlayerController` swaps a camera-defined `CamAudioSettings.MixerPreset` when the camera strategy changes. `TnPlayerPawn`
  enables Wounded{Low, Medium, High} presets by health. `TnPlayerController` enables `GameOverMixerPreset`.
  `HmAudioCategoryEffectsManager` enables presets when a category's channel count exceeds a threshold.
- These are separate presets on other categories / volumes, active alongside the reverb.
- There is no stock `ReverbVolume`, `AudioDevice.SetReverbSettings` or `SeqAct_SetReverb`-style path in MP.

## 6. For Systems (proximity voice + match audio)

- **Model:** per local listener, a current-room reverb set by trigger-volume touch events. Dry at spawn unless spawned inside a
  volume. Switching crossfades over the preset's fade time. Apply the reverb to the "wet" SFX / dialogue bus only.
- **Voice chat:** the original's VOIP isn't routed through these categories (it's not in SoundCategories; H). Putting proximity
  voice through the room reverb is a design choice. If you do, use the **listener's** current room, as the original does for all
  wet sounds: the reverb is per listener, not per source.
- Data: the per-map json has the presets and wiring; AssetTools' audio.json has the volume polygons.

**Caveat for AssetTools:** authored.db stores 860 `SoundMixerPresetName` objects with empty props (no
PresetName). The cooked tagged property holds the name, and `tools/mp_reverb_export.py` reads it from the package. This is why
audio.json lists no presets for Remnant / Molten / Debris.
