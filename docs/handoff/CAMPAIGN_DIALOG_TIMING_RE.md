# Conversation timing (SeqAct_Conversation native) and subtitles (RE, 2026-10-23)

Closes CAMPAIGN_DIALOG_SCHEMA's "native halves of Play / Stop and the PostDelayTime timing".
Labels: C = WFC binary / bytecode; H = inference.

**Natives** (exec table 0x837074F8):
- Play 0x827AAD58 → 0x8276EE00;
- Stop 0x827A7090;
- DialogEventCallback 0x827AACB8 → 0x8276ED30;
- OnDialogEnded 0x827A7100 → 0x82769F18;
- **UpdateOp 0x82787588** (per tick);
- line start 0x827845B8 / 0x82784688.

**State on the op:** CurrentLineIndex (+0xA4, default −1); flags (+0xA8: bit 31 = line started, bit 30 = line playing);
**PostDelayTimer** (+0xAC, default −1); Lines (+0x90, 0xA0 bytes each, PostDelayTime at +4).
**Links:** outputs 0 Played, 1 Stopped, 2 Finished, then **one output per line (3 + i)**; inputs Play / Stop. Variable links
come in pairs per line: [2i] the speaker, [2i + 1] a second actor (H: the listener / look-at target).

## 1. Play (C)
1. The current line is stopped (if one is playing) and the state reset: index −1, timer −1.
2. **CurrentLineIndex = 0**, the "started" flag is cleared.
3. The **DuckingMixerPreset is pushed** on the audio device (device +0x130).
4. Output **Played** fires.

## 2. UpdateOp, every tick (C, 0x82787588)
```
if (PostDelayTimer >= 0) {
    PostDelayTimer -= dt
    if (PostDelayTimer >= 0) return still-running        // waiting out the post-delay
    started = false; CurrentLineIndex += 1               // delay over: next line
}
if (CurrentLineIndex >= Lines.Num) Stop(finished)        // pops the mixer, fires Finished
if (0 <= idx < Num && !StartLine(idx)) reset (idx = -1, timer = -1)
if (CurrentLineIndex < 0) { Stop(aborted); done }        // pops the mixer, fires Stopped
```
**StartLine(i):**
- only if not already started;
- calls the speaker's `HmDialogInterface.PlayDialog(SoundCue, true, Type, false, DialogEventCallback)`;
- succeeds only if the **Played callback arrived synchronously**: the dialog really started. It then reads the line's second
  variable (H: a look-at target).
- **A line whose speaker can't play** (no HmDialogInterface, no cue, or refused) **aborts the whole conversation → Stopped**. The
  later lines don't play.

## 3. DialogEventCallback (C, 0x8276ED30)
| event | effect |
|---|---|
| 0 Played | the "playing" flag is set |
| **2 Finished** (the cue ended normally) | playing cleared; **PostDelayTimer = Lines[i].PostDelayTime**; **output 3 + i fires** (the per-line output, if the op has it) |
| 1 / 3 / 4 (stopped / interrupted / failed) | the conversation is reset → the next tick fires **Stopped** |

**Timing:**
- The next line starts on the **first tick after the timer goes negative.** So the gap = PostDelayTime rounded up to a frame, and
  PostDelayTime 0 means the next tick.
- Authored values: 1,261 lines at 0; the others mostly 0.1–2 s.
- The wait starts when the **cue's Finished event** arrives. That's the cue duration; FaceFX / animation don't hold it.

## 4. Stop (C, 0x82764048)
- Pops the ducking preset (device +0x134).
- Fires **Stopped**, or **Finished** when the conversation ran to the end.
- The Stop input (0x827A7090) first stops the current line via its dialog interface, then resets.

## 5. Subtitles (C for the data path; H for the canvas placement)
- Subtitles are queued per wave into `Engine.SubtitleManager` (QueueSubtitles: priority, speaker, per-line times over the wave
  Duration).
- **They also drive the HUD speaker portrait:**
  - `TnHudDataObserverPipCharacter` polls `SubtitleManager.FindHighestPrioritySubtitle(now)` every check;
  - if the active subtitle's Speaker is a TnPawn, it sends `_CurrentCharacter.CharacterName` to the HUD movie; if it's an
    HmDialogEmitter, `DialogCharacter.LongName`; else "" (no portrait).
  - So **the PIP portrait follows the queued subtitle even when subtitle display is off.** The option only gates drawing
    (`SetShowSubtitles`).
- **Drawing:**
  - The subtitle text isn't drawn by the Hud_GFX movie (CAMPAIGN_HUD_OBJECTIVES_RE).
  - It's the engine canvas path (stock UE3 `USubtitleManager::DisplaySubtitles`).
  - WFC's SubtitleManager script uses opcodes our disassembler doesn't decode (tokens 0x4E / 0x50 / 0x59), and the font / region
    aren't referenced by name in the binary, so **placement stays H, stock:** the highest-priority subtitle, centred at the
    bottom of the title-safe region, word-wrapped to the safe zone (SplitLinesToSafeZone), white with shadow.
- **Toggles:** the profile Subtitles option → SetShowSubtitles. The UI controller disables subtitles while menus or movies are up
  and restores the profile value afterwards (TnUIController).
