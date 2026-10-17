# FMV playback: movies, start paths, skip, audio, loading movies, movie end

WFC RE lane, 2026-10-18. This closes the "native FMV playback" item in CAMPAIGN_RE_INDEX.md.

Sources:
- script: HM SeqAct_MoviePlayer, HmMoviePlayer, HmPlayerController, TnPlayerController, Engine, TnMoviePlayer,
  TransContent TnSeqAct_MissionComplete / TnCampaignGame;
- natives (headless Ghidra; queries `work/q/mv*.txt`): SeqAct PlayMovie 0x8278A270 / StopMovie 0x8278A448, ClientPlayMovie
  0x8274C0C8, the movie path builder 0x82CD3348, volume 0x82CDDC08, the audio-setup path 0x827902D0 / 0x82CE0F00;
- data: Bink headers, ini, authored.db.

## 1. The shipped movies (CONFIRMED, `Game Dump/TransGame/Movies`, 66 files)

**Container and codec:**
- **Bink 1** (`BIKi`), **1280×720**, 29.97 fps (30.0 for the text and loading movies).
- Audio: **16-bit, 48 kHz, mono tracks** (Bink audio flags 0x5000):
  - story FMVs and logos carry **10 tracks**, ids 0–9;
  - text cards carry 6;
  - loading and startup movies have none.
- UNKNOWN: which track is which speaker or language. The track selection is in the Bink middleware, which was not traced.

| Group | Files | Started by |
|---|---|---|
| Boot logos | Logo_Activision, Logo_Hasbro, Logo_HighMoon | `UI_FrontEnd_m` Kismet SeqAct_MoviePlayer |
| Game intro | FMV_intro (128.7 s) | `UI_FrontEnd_m` SeqAct_MoviePlayer |
| Chapter intros | FMV_a1intro, a4intro, d1intro | the chapter's base map SeqAct_MoviePlayer |
| Chapter title cards | FMV_{a1,a2,a3,a4,a5i,a5o,d1,d1i,d2,d3,d4,d5,d5i,d5o}text_{INT,FRA,ESN} | base map SeqAct_MoviePlayer (`MovieName` e.g. `FMV_a1text.bik`; the language suffix is added at load, §2) |
| Chapter outros | FMV_{a1,a2,a3,a5,d1,d2,d3,d5}outro, FMV_credits (A5) | `TnSeqAct_MissionComplete.MovieName` |
| Black | Blank.bik (2 s, silent) | MissionComplete in A4_ORB_FallingTryp_CINE and D3B, which end on an in-engine cine |
| Loading | TF_LoadingScreen_{INT,FRA} (4 s loop), LoadingScreenAlpha (0.5 s) | the loading-movie system (§5) |
| Startup | TF_InitialStartup(_NA/_EU)_{INT,FRA} | boot |
| (unused?) | Starscream_D1.bik | no authored reference found |

**Subtitle sidecars:**
- Format: `<Movie>.txt` beside the movie. Line 1 is **`100`**; then `start,end,KEY` lines.
- **Times are in 1/100 s** (H: line 1 = ticks per second; FMV_a1outro's last cue ends at 7800 of an 81.5 s movie).
- Keys resolve in `Localization/<LANG>/Subtitles.<lang>`, section `[Subtitles]` (e.g. `A1OUT1`).
- **Placement:** `[SubtitleSettings] RegionMovie=0.2,0.6,0.8,0.9` (Xe-TransEngine.ini), the same box as in-game, loading and
  cinematic subtitles. This is a normalized screen rect; H: X1, Y1, X2, Y2.

## 2. File resolution (CONFIRMED native 0x82CD3348)

- The path is `Movies\` + name + `.bik`.
- The resolver runs base-name and language passes using the current language (`INT` is the default) and tries the localized name
  `<name>_<LANG>` before the plain name.
- So `FMV_a1text.bik` → `FMV_a1text_INT.bik` or `FMV_a1text_FRA.bik`.
- Several authored names have a **trailing space** (`"FMV_a1text.bik "`, `"FMV_credits.bik "`), and one shipped file is
  `FMV_a5otext_ESN .bik`. Trim names before resolving.

## 3. How a movie starts

### 3.1 Kismet `SeqAct_MoviePlayer` (HM; "(High Moon) Movie Player")

- **Links:** inputs **Play, Stop**; outputs **0 Stopped, 1 Begin Playing**.
- **CDO:** bIsSkippable **true**, bRenderLastFrameAtEnd **true**; the rest false. Only `MovieName` is authored in the maps.
- **Bool order** at +0x9C (CONFIRMED by the masks in the native code): bIsSkippable 0x80000000, bLoop 0x40000000,
  bBlendWithGameRendering 0x20000000, bRenderLastFrameAtEnd 0x10000000, bPreload 0x08000000, bIsRequestingPlaying 0x04000000,
  bIsPlaying 0x02000000, bPreviouslySkippable 0x01000000.

**Play:**
1. `Activated`: Play while not playing → bIsRequestingPlaying.
2. The native PlayMovie (0x8278A270), once:
   1. sets bIsPlaying; bPreviouslySkippable = bIsSkippable;
   2. for each HM player controller that qualifies (H: remote): `ClientPlayMovie(name, bRenderLastFrameAtEnd,
      bBlendWithGameRendering, 0)`;
   3. starts the local full-screen movie with these parameters:
      - preload = bPreload;
      - mode = **loop if bLoop**, else bRenderLastFrameAtEnd (H: 0 = play once, 1 = hold last frame, 2 = loop);
      - **not-skippable = !bIsSkippable**;
      - flags = bBlend ? 2 : 0;
      - volume (§4);
   4. calls `OnPlayMovie` on the game's `HmMovieInterface`. `TnCampaignGame` stores `_MovieParameters` so a **co-op player joining
      mid-movie** gets `ClientPlayMovie(…, StartTimeInMilliseconds = HmMoviePlayer.GetElapsedTimeInMilliseconds())`
      (`PostLogin`);
   5. fires **Begin Playing**.

**Stop** (input, or the movie finishing; native 0x8278A448):
1. Clear bIsPlaying and bIsRequestingPlaying.
2. Each remote PC gets `ClientStopMovie`; the game gets `OnStopMovie` (clears `MovieFileName`).
3. Stop the full-screen movie, then fire **Stopped**.

### 3.2 `TnSeqAct_MissionComplete` (TransContent, a SeqAct_MoviePlayer subclass)

- Already in CAMPAIGN_KISMET_HANDLERS_RE.md: OnPlayMovie → `StartCompleteMission` (profile + `EndGame("Complete")`).
- **Skip is locked:** `Update` sets `bIsSkippable = IsEndGameHandlingComplete()` every tick, so the authored bIsSkippable (true on
  A3, A5, D2, D5) is overwritten.
- `StopMovie` is refused until end-game handling finishes, then calls `ReturnToGameLobby`.

### 3.3 Other paths

- `TnGameActionScriptBinding.PlayMovie(MovieName)` (front-end ActionScript → native 0x829EE898).
- `HasWatchedIntroMovie` / `SetHasWatchedIntroMovie` (H: the profile flag that lets the front end skip FMV_intro).

## 4. Skip and audio

**Skip:**
- `TnPlayerController.PostControllerIdChange` → **`HmMoviePlayer.SetSkippableByOneController(ControllerId)`**: only the active
  player's pad skips. `SetSkippableByAllControllers` exists (native → movie player vtable +0x3C).
- A movie started not-skippable (`!bIsSkippable`, or any `ClientPlayMovie` on a client: H, the field is forced to 1) ignores
  input.
- A skip stops the movie, which follows the Stop path (§3.1): **Stopped** fires. There is no separate "skipped" output.

**Movie mode** (`Engine.BeginMovieMode` / `EndMovieMode` → the first local PC):
- **Begin:**
  - `OnFullScreenMovieStart`: UI event 12, `bFullScreenMoviePlaying`;
  - **`MovieAudioSetup`** (native 0x827902D0): if the audio device is the FMOD device, it applies the movie mixer preset,
    **`[HM_Engine.FmodAudioDevice] MovieMixerPreset=CINE_MUTE_FOR_BINK`**, which mutes the game mix under Bink;
  - `SetMovieCinematicMode(true)`;
  - `SetCinematicPauseable(false)`;
  - force feedback stopped.
- **End:**
  - `MovieAudioShutdown` (preset off);
  - `SetMovieCinematicMode(false)`;
  - `OnFullScreenMovieStop` (§6).
- **Pause:** when the system UI opens, `PauseMovie` / `UnpauseMovie` run if the game is pauseable.

**Volume** (native 0x82CDDC08, CONFIRMED structure):
- `[MoviePlayer] VolumeScalar` (default 1.0; not set in the shipped ini) × the **SFX** sound-class volume.
- **Unless** the movie is in `[MoviePlayer] FullVolumeMovies` (none shipped).
- Clamped to 0..1.
- `[Engine.MovieSettings] MoviesToAlwaysPlaySound = Logo_Activision, Logo_Hasbro, Logo_HighMoon`: these get a fixed output level
  (0xCCCC, about 0.8; H) regardless of the user's settings.

## 5. Loading movies (CONFIRMED script + ini)

**Files** (`[LoadingMovie]`, Xe-TransGame.ini):
- `InitialStartupFileName=TF_InitialStartup` (boot; the regional and language variant by H region code);
- `DefaultFileName=TF_LoadingScreen` (all travel);
- `AlphaFileName=UI_GFxLoading_p.LoadScreenAlpha_GFX`: a GFx overlay drawn on the loading movie.

**There is one loading movie.** The "variants" are only overlay text, set via `HmMoviePlayer.AddDeferredMethod` →
`_global.SetLevelText(Title, Message)` and `_global.AddEngageText(tip)` × 3 random tips.

`TnMoviePlayer.StartLoadingMovie(PendingURL)` chooses the text:

| URL | Title / message | Tips from |
|---|---|---|
| `Game=…TnPartyLobbyGame` or `…TnGameLobbyGame` | empty | `TnOnlineGameSettingsBase.EngageText` |
| `MapId=n` | game-mode FriendlyName (from `GameModeTag`) / `LoadScreen.LoadingMap` with `` `m`` = the map's friendly name | that mode's settings class EngageText |
| other | empty | base EngageText |
| checkpoint restart (`ClientRestartFromCheckpoint`) | `$UIText.LoadScreen.ResumeAutobot` (team 0) / `ResumeDecepticon` | `TnOnlineGameSettingsCP.EngageText` |

## 6. What the player sees at movie end

- `bBlendWithGameRendering` is never authored, so movies cover the screen fully.
- **`bRenderLastFrameAtEnd` is true by default:** the last frame stays up until the Stop path runs. H: the movie player holds the
  final frame instead of dropping to the game view.
- **In-level movies (intros and title cards):**
  1. **Stopped** fires;
  2. `EndMovieMode`;
  3. `OnFullScreenMovieStop`:
     - **UI event 13 (the HUD comes back) unless the HUD is off for cinematic mode**;
     - `_global.MovieEnded` is invoked on the front-end UI;
     - `bFullScreenMoviePlaying` is cleared.
  4. Kismet continues from **Stopped**. In the maps that is typically a matinee or cinematic mode, then gameplay. The first gameplay
     frame comes straight after the held last frame; there is no engine fade.
- **Outros (MissionComplete):** after end-game handling and the movie end or skip → `ReturnToGameLobby` → travel → the loading
  movie (§5). The two Blank.bik chapters show 2 s of black after their in-engine ending.

**Residual (UNKNOWN):** the Bink audio-track to speaker / language mapping, and the internal timing of the held last frame. Both
are middleware, not game code.
