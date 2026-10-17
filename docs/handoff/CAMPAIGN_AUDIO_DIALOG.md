# Campaign audio and dialog pipeline: D5_OME first, then A5_TRY (Systems, 2026-10-16; design only, no code)

Sources (read-only): AssetTools authored.db (class / property counts for the D5_OME packages), the D5_OME slice
(AssetTools work/campaign_pilot/VerticalSlice/Maps/D5_OME: audio.json, streaming.json), ExtractedAssets content (FaceFX
.fxa files), and the rebuild's audio runtime (src/game: SoundCues, LevelAudioScript, LevelAudioHost, MusicPlayer,
MatchAudio, AmbientAudio). Open questions are at the end.

## 1. What D5_OME contains (audio / dialog classes in its 11 packages)

Packages: D5_OME_BASE_m (+ _LOC_int / _LOC_FRA), _ART_m, _AUDIO_m, _DSN_m, _Music_m, _GLB_ART_m, _GLB_AUD_m,
_CINE_LevelIntro_m, _CINE_Outro_m. The 9 streamed sublevels each carry a TnWorldInfo with its own HmMusicPlayer and
HmDialogEventManager.

| class | count | what it is | rebuild status |
|---|---|---|---|
| SoundCue / SoundNodeRoot / SoundNodeWaveEvent | 109 / 109 / 251 | cues (the rebuild's cue tree format) | **covered** (SoundCues) |
| SoundNodeWaveEx | 51 (+ 33 in _LOC_int) | waves; the localized ones carry the subtitles | **waves covered**; subtitles new (section 3) |
| SoundMixerPresetName / CategoryName | 158 / 218 | mixer presets and categories | **covered** (SoundMixer) |
| AmbientSound / HmAmbientSoundVolumeEmitter | 1 / 1 | level emitters | **covered** (AmbientAudio; audio.json) |
| SeqAct_AmbientAudioZone | 2 | ambient zones | **covered** (LevelAudioScript "zone") |
| SeqAct_Reverb | 3 | reverb switches | **covered** ("reverb") |
| SeqAct_PlaySound / SeqAct_PlayPlayerPositionalSound / SeqAct_PlayFlybySound | 2 / 2 / 2 | Kismet sounds | **covered** (play_sound / positional_pool / flyby, flyby PARTIAL) |
| SeqAct_PlayMusic / SeqAct_StopMusic / SeqAct_PlayStinger | 7 / 4 / 6 | music (D5_OME_Music_m) | **covered** (MusicPlayer incl. PlayStinger) |
| HmMusicPlayer / TnSoundEventSetInfo / HmSoundEventSet | 9 / 9 / 4 | per-sublevel music player and event sets | **covered** for one level; **new**: one per streamed sublevel (section 5) |
| SeqAct_Interp + InterpTrackEvent | 40 + 27 | matinees with event keys | **covered** ("timeline": event keys) |
| **InterpTrackSound** | **66** | sounds keyed on a matinee timeline (cinematics, set pieces) | **new** (section 4) |
| **SeqAct_Conversation** | **42** (898 campaign-wide) | scripted dialog: Lines [{SoundCue, PostDelayTime}], DuckingMixerPreset, Play / Stop in, Played / Finished out | **new** (section 2) |
| **HmDialogEventManager** | **9** | per-WorldInfo dialog event queue (combat chatter / event lines) | **new** (section 2) |
| **InterpTrackFaceFX** | **13** (123 campaign-wide) | FaceFX sequences on a matinee: FaceFXGroupName "Dialogue", FaceFXSeqName (e.g. DX_MGTRON_LK008868), StartTime | **new** (section 6) |
| HmAnimNotify_Sound | 10 | sounds fired by animation notifies | **new** for campaign anims (section 4) |
| HmSoundDestructionEffectFactory | 22 | destructible break sounds | check: MP destructibles exist; confirm the sound path covers them |
| InterpTrackDirector / Fade / AnimControl / Move | 24 / 7 / 77 / 102 | cinematic camera cuts, fades, animation, movement | Gameplay / Rendering (cinematics); audio hooks via InterpTrackSound / FaceFX |

## 2. Dialog: SeqAct_Conversation and HmDialogEventManager

- **Data:** each Conversation is a short list of lines `{SoundCue, PostDelayTime}`, cues in the shared dialog banks
  (e.g. BL_DX_SWITCHBOARD.SC001521, the same bank the MP announcer uses), plus a per-conversation DuckingMixerPreset
  (music / SFX duck while it plays). Inputs Play / Stop; outputs Played / Finished (Kismet continues on Finished).
- **The runtime already has** HmDialogComponent-style playback (SoundCues::playDialog with a DialogCharacter, the
  announcer's queue / priority rules in MatchAudio, per-event DialogCharacter filtering).
- **New:**
  1. A LevelAudioScript op "conversation": Play -> play line 0's cue through the dialog path, enable
     DuckingMixerPreset; when the cue ends, wait PostDelayTime, then the next line; after the last line, release the
     preset and fire Finished. Stop -> stop the current line, release the preset, fire the Stop output.
  2. Priority / interruption between conversations, and against HmDialogEventManager lines - to be ported from the
     decompiled script (HM_Engine SeqAct_Conversation / HmDialogEventManager); not guessed (open question 1).
  3. HmDialogEventManager per streamed sublevel's WorldInfo: the queue that gameplay events feed (combat chatter,
     objective lines); the MatchAudio announcer queue is the template.
- **gen_level_audio.py** (tools/systems) extends its Kismet export with the Conversation lines, ducking preset and
  links, as it does for the existing ops.

## 3. Subtitles

- **Data:** subtitles live on the localized waves (SoundNodeWaveEx in the _LOC packages): `Subtitles [{Text, Time,
  UseSubtitles}]`, plus `bAlwaysLocalise`, `Duration`. Example: WL_DX_BRKDWN.DX_BRKDWN_LK000585 "Argh -- he's regenerating
  again! We've got to stop him!" at 0.0 s. French text is in the FRA twins (with UseSubtitles false on some lines).
- **Profile:** the original's "Subtitles" option already exists (LocalProfile, default off).
- **Runtime already resolves localized waves** (SoundCues: content/_LOC/<twin>/... else the merged file; the 282-line
  check in verify_slim).
- **New:**
  1. Export: a per-wave subtitle table (wave path -> [{time, text}], per language) from the _LOC packages into the level /
     bank manifests (AssetTools or gen_level_audio).
  2. Playback: when a dialog wave starts and Subtitles is on, the HUD shows each text at its Time for the wave's
     Duration; the next line replaces it. The HUD widget is Frontend / HUD's (the original's subtitle movie clip -
     open question 2).

## 4. Matinee-driven sounds: InterpTrackSound and HmAnimNotify_Sound

- **InterpTrackSound** (66 in D5): keys {Time, SoundCue, Volume, Pitch} on a matinee group; the sound plays from the
  group's actor when the matinee position passes the key (UE3: forward playback only, stops on matinee stop when
  bContinueSoundOnMatineeEnd is false). The existing "timeline" op already fires matinee event keys forward [old, new),
  so a sound key is the same walk plus a cue play at the group actor's position.
- **HmAnimNotify_Sound** (10): a cue fired by an animation notify at a time in an anim sequence. That needs the animation
  system's notify callbacks (Gameplay); Systems provides the play-at-socket entry.

## 5. Streaming: audio per sublevel

- D5_OME streams 9 sublevels (streaming.json: initial state, states, flush actions). Each sublevel's WorldInfo has its
  own HmMusicPlayer, sound event set and dialog event manager; its Kismet audio ops belong to that sublevel.
- **New:** LevelAudioHost loads / unloads per sublevel: when a sublevel streams in, its emitters, zones, Kismet ops and
  cues load; when it streams out, its components stop (bStopWhenOwnerDestroyed) and its cues release. One music player
  is active (the persistent level's, or as the original selects it - open question 3).

## 6. FaceFX (lip-sync)

- **Data:** 22 FaceFX actor files (.fxa) in ExtractedAssets content, one per character skeleton (e.g.
  TR_Barricade_ROBO_p/RB_Barricade_SKEL_FaceFX.fxa, TPRO_KON_MegatronWallFace_p); 23 FaceFXAsset + 1 FaceFXAnimSet
  objects in authored.db; 123 InterpTrackFaceFX tracks campaign-wide naming sequences in group "Dialogue"
  (DX_<CHAR>_LK<nnnnnn>, matching the dialog wave names).
- **What FaceFX does:** each sequence holds animation curves (FaceFX's proprietary binary) that drive the face / jaw bones
  or morph targets of the character's skeleton in sync with the dialog wave.
- **New, and the largest item:**
  1. RE of the .fxa format (actor: bone / node setup; animations: curves per node over time). This is OC3's proprietary
     format, so no public spec - RE from the files.
  2. Runtime: evaluate the curves at the dialog wave's time, map them to bone transforms on the skinned character
     (Gameplay's skinning).
- **A cheaper first step** (open question 4): jaw-only amplitude lip-flap from the dialog wave's RMS until the FaceFX
  data is decoded. That is NOT faithful; a fallback to name and keep out of Faithful.

## 7. Order of work for D5_OME (then A5_TRY)

1. Per-sublevel audio load / unload (section 5) - the level's ambience and music must survive streaming.
2. Conversation op + ducking (section 2), with gen_level_audio export.
3. InterpTrackSound on the timeline op (section 4).
4. Subtitle export + HUD hook (section 3).
5. HmDialogEventManager (section 2) and HmAnimNotify_Sound (section 4) as gameplay events arrive.
6. FaceFX RE and runtime (section 6) - parallel research, the last to land.
A5_TRY: the same pipeline; its counts to be pulled the same way once D5 works.

## 8. Open questions

1. Conversation / dialog-event priority and interruption rules: port from the decompiled HM_Engine script. Is
   HmDialogEventManager decompiled in RE-Workspace?
2. Subtitle display: the original's HUD subtitle element (GFx movie clip) - Frontend / HUD to locate it.
3. Music across streamed sublevels: which sublevel's HmMusicPlayer is authoritative when several are loaded?
4. FaceFX: wait for the real decode (faithful), or ship a jaw-flap fallback meanwhile (not faithful, campaign only)?
5. HmSoundDestructionEffectFactory (22 in D5): confirm the MP destructible sound path covers the campaign's factories.
