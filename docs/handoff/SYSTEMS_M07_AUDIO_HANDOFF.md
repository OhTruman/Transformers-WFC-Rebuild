# Systems M07 audio handoff (agents/systems) — movie audio, match / announcer audio, lifecycle

Two small patches against `integration/milestone-05`. Both were verified on a merge preview of `agents/systems`
onto `integration/milestone-05` (a real cold boot through the intro chain, then lobby → Streets → quit cycles).

| patch | owner | lines | what |
|---|---|---|---|
| `SYSTEMS_M07_frontend_movie_audio.patch` | Frontend / Integration | ~25 | the movie's own sound starts with its video and stops at its end / skip |
| `SYSTEMS_M07_gameplay_match_audio.patch` | Gameplay / Integration | ~20 | Match events → announcer + mode music |

## 1. Boot movies are silent — cause and fix

**Cause (current executable):**
* `platform::IMoviePlayer` (Win32Movie.cpp) selects only the video stream:
  `SetStreamSelection(ALL, FALSE)` then video.
* While a Bink is up, the frontend enables the MovieMixerPreset `CINE_MUTE_FOR_BINK`, which mutes the GAME mix. That
  part is correct.
* Nothing played the movie's own sound.

**Evidence that the sound is the Bink audio, not a cue:**
* **The tracks** [CONF data]: every logo / intro / campaign FMV has 10 mono 48 kHz DCT Bink tracks, IDs 0–9
  (`tools/systems/bink_tracks.py` on Game Dump `Movies/*.bik`).
  * AssetTools' `.mkv` files carry them as 10 FLAC streams.
  * `TF_LoadingScreen_*`, `TF_InitialStartup_*` and `LoadingScreenAlpha` have none.
* **No cue for the boot movies** [CONF data]: no SoundCue exists for them. In `UI_FrontEnd_m`, the
  `SeqAct_MoviePlayer` ops link only to the next movie and to [FRONTEND START].
* **Config** [CONF config]: `Engine.MovieSettings MoviesToAlwaysPlaySound` names the three logos, and
  `[HM_Engine.FmodAudioDevice] MovieMixerPreset=CINE_MUTE_FOR_BINK` mutes the game mix while a movie plays.

**Systems side (agents/systems):**
* `audio::MovieAudioPlayer` decodes every audio track of the movie file on its own thread:
  * Media Foundation, loaded at run time, so no new link libraries.
  * Folds to stereo: tracks FL FR SL SR LFE + language centre.
  * Streams through new `IAudio` PCM streams, mixed beside the game mix (no category / Master scale), so
    CINE_MUTE does not mute the movie.
* `FrontendAudioRuntime::startMovieAudio(path)` / `stopMovieAudio()` / `movieAudioClock()` (also on `World`).
* `UnflushableMixerPresets=CINE_MUTE_FOR_BINK` [CONF config]: a level change no longer drops the movie mute.

**Frontend side (the patch):**
* `IFrontendAudio` gets `startMovieAudio(path)` / `stopMovieAudio()` with default no-ops.
* `FrontendRuntime::openVideo` starts the sound with the video.
* End, skip, loop restart and the underlay being released stop or restart it.
* The `SystemsFrontendAudio` adapter forwards both calls.

**Verified on the patched preview:**
* All four boot movies play their sound (`movie.audio playing:true`); the loading and startup Binks are silent by
  design.
* The title music starts only after FMV_intro (at [FRONTEND START]).
* The movie's audio clock stays within 70 ms of wall time over 13 s.
* `movie_audio_probe` (real device): a skip stops the sound at once, the game mix is restored after the chain, and
  repeated chains leave 0 streams, 0 voices and decoded PCM at its base.

**Open:**
* Which centre track (5–9) is INT is UNKNOWN; track 5 is used [PROVISIONAL]. The logos are exact: their centre
  tracks are identical.
* The native selection (`HmPlayerController.MovieAudioSetup`, a native) is an RE item.
* `WFC_MOVIE_LANGSLOT=0..4` overrides the centre track.

## 2. Loading screen "frozen" — audio requirements

* **The game thread blocks while the world loads** (`world_.load`, ~4.5 s). Audio does not depend on it:
  * the Systems mixer and the movie-audio decoders run on their own threads;
  * the loading Binks author no sound;
  * the game mix is muted by CINE_MUTE while the loading movie is up.
* **No audio needs pumping during a load.** Fades, the music player and Kismet timers pause with the game thread and
  resume afterwards; nothing audible depends on them while the loading movie is up.
* **The frozen picture** is the video / GFx loading presentation not being pumped during `world_.load`: Frontend /
  Integration. If the load is moved off the game thread, keep calling `IFrontendAudio::tick`. If a future loading
  movie has sound, it keeps playing on its own (the decoder thread keeps ~0.5 s queued).

## 3. Match audio (Gameplay events → Systems)

`World::matchAudio()` (`game::MatchAudio`) ports the original broadcasts [CONF script]:

| Gameplay `MatchEvent` | original | Systems call | sound |
|---|---|---|---|
| MatchStarted | TnGameRules.HandleStartGame → TnGameTypeMessage(0) | `onMatchStarted(modeTag, localTeam)` | announcer GameTypeDialog + GameDescriptionDialog (queued); music GameTypeMusic (TDM / DM `BL_LVL_MP_MX.DM_START`) |
| GameNearlyComplete | HandleGameNearlyComplete → switch 1 | `onGameNearlyComplete()` | GameNearlyCompleteMusic (`DM_FINALSTRETCH_LP`) |
| TimeAnnouncement / KillsLeftAnnouncement | TnGameProgressAnnouncementMessage(switch) | `onProgressAnnouncement(value)` | MP_30SecondsLeft / 1Minute / 2Minutes / 25 / 50 points / 1 / 3 / 5 kills |
| MatchEnded | HandleEndGame(Winner) → switch 2 (+ TnVersusGameOverMessage) | `onMatchEnded(winnerTeam, localPlayerWon)` | end music (priority 1): Autobots / Decepticons win / tie (DM: local winner → AutobotsWin), team games: MP_GameAutobotWin / MP_GameDecepticonWin [PARTIAL] |

**The announcer** (TnAnnouncer):
* **Voice:** the map's `TnWorldInfo.AnnouncerSoundEventSet` = `CHR_ANNOUNCER_DIALOG` (140 events →
  BL_DX_SWITCHBOARD cues) is in the Systems manifest of Streets and Gorge. The dialog component plays the
  OPRIME (team 0) or MGTRON (team 1) wave.
* **Queue:** one queued line, replaced only by a line of higher-or-equal priority.
* **Memory:** the dialogue and mode-music cues are streamed (decoded on first play, released after), so Streets'
  resident PCM is unchanged (97.2 MB).

**Not authored, not played:** kill feed (TnDeathMessage), kill-streak text messages, score text, kill awards. The HUD
movie's own `Sound.PlaySound` names (HUD_POSITIVE_HIT_INDICATOR, HUD_OBJECTIVE_ADDED, MP_LEVEL_UP_MX_STNG,
MP_REWARD_DIALOG_BOX, PICKUP_DMG_MULTIPLIER…) should go to `World::playUiSound(name)` when the in-match HUD movie runs.
Other announcer lines (flags, bombs, nodes, kill streaks, round time-up) are `announcerEvent(event)` with their
`SoundEvents_Dialog.Announcer.*` name, called where Gameplay broadcasts the matching message.

## 4. Lifecycle guarantees (unchanged contract, re-verified)

* `levelChange()` / `unloadMapAudio()` releases everything the level owns:
  * the music player and the announcer;
  * the Kismet sounds and the beds;
  * the presets, the cue bank and the decoded samples.
* Only the movie mute survives a level change (unflushable).
* The movie's own sound belongs to the movie, not the level.
* Second map: `MP_UND_Gorge` (AssetTools audio.json + Systems manifest) loads, plays and unloads through the same
  path: 15 emitters, 23 zones, 6 reverb presets, 13 bank cues and the announcer.
