# Systems M08 audio handoff (agents/systems) — multi-map, mode, character, movie audio

Gameplay owns match state, and Systems owns playback. Every hook below is a single call on `World`. If no call is
made, the audio keeps its current behaviour: Optimus, the Ion Blaster, and no objective lines. No Systems-side
timers duplicate Gameplay state.

## Gameplay hooks

| when (Gameplay) | call | plays |
|---|---|---|
| the local player's body is chosen / changes | `world.setPlayerCharacterAudio(chassisKey)` (roster chassis key: `Truck`, `Car`, `Tank`, `Jet`, `Car2`…) | footsteps / exertions / landing / take-off / transform / vehicle engine / boost / ram / death from that chassis's SoundEventSets and clip notifies |
| the held weapon changes | `world.setPlayerWeaponAudio("TransContent.TnWeaponHeavyPistol")` | fire / low-ammo fire / tail / fine aim / dry fire from that class's WeaponSounds |
| match begins / final stretch / time or kills announcement / ends | `world.matchAudio().onMatchStarted(modeTag, team)`, `onGameNearlyComplete()`, `onProgressAnnouncement(sw)`, `onMatchEnded(winner, localWon)` | the M07 match audio, unchanged |
| flag event (TnFlagMessage switch 0 returned, 1 picked up, 2 dropped, 3 scored) | `matchAudio().flagMessage(sw)` | the HUD stinger CTF_FLAG_* plus the announcer line |
| bomb event (TnBombMessage 1 pickup, 2 drop, 3 detonated, 4 defused, 5 planted; `team` of RelatedPRI_1) | `matchAudio().bombMessage(sw, team)` | the announcer line (team-specific for pickup / detonated) |
| domination (switch = point × 10 + type; 0/1 team takes, 2/3 team capturing) | `matchAudio().dominationMessage(sw)` | MP_*Captur*_A..E lines |
| CTF / Code of Power intro (local team attacks?) | `matchAudio().ctfMessage(attacks)` | Capture / Defend Code of Power |
| round-based message (0 Autobots win, 1 Decepticons win, 2 time up, 3 switching sides) | `matchAudio().roundMessage(sw)` | the line, plus COP_ROUND_OVER / switching-sides music |
| KOTH hill activated / defender changed (0, 1, 254 contested, 255 neutral) | `matchAudio().kothZoneActivated(matchOver)`, `kothDefenderChanged(team, ignoring, matchOver)` | MP_Hill* lines |
| a gameplay-owned Kismet event fires (e.g. TnSeqEvent_SurvivalEvents "Round Begin") | `world.levelAudioEvent("Game:<class>:<output>")` | whatever the map's sound graph links from it |

Every switch → cue rule is ported from the decompiled script (`ClientReceive` / `GetColoredString` /
`DefendingTeamChanged`), and the cue names come from the class defaults [CONF].

## Frontend

* **Language:** movie audio now follows RE 433ef9e. Tracks 0..4 always play, plus the centre track 5 + L, where L is
  derived from GLanguage. Set `WFC_LANGUAGE` (INT/FRA/ITA/DEU/ESN/RUS/POL) where the build learns the language.
* **Volume:** the three logos (MoviesToAlwaysPlaySound) play at Bink volume 0.8. Other movies follow the FX Volume
  option (GetMovieVolume, RE CONFIRMED). Call `frontendAudio.setMovieFxSlider(fxSlider0to100)` when the profile
  settings are applied; the volume is slider / 100. The default is the profile default 80 → 0.8.
* **File choice:** RE §E2 gives the native order, which the Frontend movie player should follow:
  1. `<Name>_360_<LANG>`
  2. `<Name>_360_INT`
  3. `<Name>_<REGION>_<LANG>`
  4. `<Name>_<LANG>`
  5. `<Name>_INT`
  6. `<Name>_360`
  7. `<Name>`
* The M07 movie patch is otherwise unchanged.

## AssetTools

* 169 dialogue waves referenced by character SoundEventSets are absent from the extraction. They are mainly the
  WL_DX_CARD01 / CARA01 groups. Those lines are skipped at load (they log as missing cues); nothing crashes.
