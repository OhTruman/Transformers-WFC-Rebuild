# Voice chat (Systems, 2026-10-23)

The user put voice chat in the online scope (the live mic test is approved). This is the design: what exists on the head, what is
missing for the game to actually use it, and the order of the patches.

## 1. What exists (merged in phase 1 / 2)

- **Codec** (`src/net/VoiceCodec`): Opus 1.5.2 (BSD-3-Clause, `tools/net/fetch_opus.py` into gitignored `third_party/opus`), mono
  16 kHz, 20 ms frames, VOIP mode, 16 kbit/s (~40 bytes a frame), packet-loss concealment in the decoder. Built only when the
  source is present (`WFC_OPUS=1`).
- **Capture** (`src/platform/win32/Win32VoiceCapture.cpp`): WASAPI, the communications microphone (fallback: the console one), event
  driven on its own thread, the device's mix format converted to 16 kHz mono 20 ms frames (`VoiceFrames`).
- **Voice lane** (`src/net/VoiceSession`): unreliable VOICE packets `{u8 7, u8 seat, u16 seq, u8 flags, opus}` over the lockstep's
  transport; a star relay (clients send to the host, the host fans out by audience: radio = the talker's team, proximity = within
  64 m by the sim's positions); per talker a jitter buffer (3 frames = 60 ms), decode with concealment, and playback:
  - radio: a 2D stream at the voice volume (outside the cue mix, as the original's chat);
  - proximity: a 3D stream inside the cue mix at the talker's head (the dialog-cue distance curve, the listener's room reverb,
    occlusion as the cue mix does it).
- **Settings** (`frontend::SessionVoiceSettings`): mode Off / Push to talk (default) / Open mic, audience (radio + proximity
  default), device, volume; `setPushToTalk(held)`, `setMuted(member)`.
- **Tests**: `WFC_VOICEFRAMETEST` (the framer + codec on a synthetic tone: frame count, level, pitch); `WFC_NET_VOICE=tone` on the
  headless lockstep harness (two peers exchange a synthetic tone).

## 2. Gaps (found 2026-10-23)

1. **The game never opens the microphone.** `NetMatchDriver` creates the VoiceSession with default settings and no capture source;
   only the headless harness calls `setSource`. In a real match nobody is heard.
2. **The player's settings are not applied**: volume, audience (radio / proximity) and the device row never reach the VoiceSession.
3. **Mute does nothing**: `UdpSession::setMuted` stores the member, nobody reads it.
4. **No lobby voice**: voice rides the lockstep, so it exists only inside a match.
5. **No talking indicators**: `SessionEvent::MemberVoice` (headset state 0 / 1 / 2, voiceVia) is never emitted, so the lobby
   player list and the scoreboard cannot show who talks.
6. **Lane runs**: `WFC_MUTE` silences playback at the device already; `WFC_NOINTERACT` must also keep the microphone closed.
7. **Packages**: refresh.sh did not copy `third_party/opus`, so a package would build without the codec (voice silently off).
   Fixed in refresh.sh (copy + a `WFC_OPUS=1` build check).
8. **No end-to-end test with a quality figure**.

## 3. Design of the fixes

- **Voice settings into the lane** (gaps 1, 2, 6): the driver gets the session's voice settings. Mode Off: no capture, playback
  still on (you hear others; the original let you hear without a headset). Push to talk / Open mic: `createVoiceCapture()` opened
  on the first talk (no device held while silent) unless `WFC_NOINTERACT`. Open mic gates on the capture level (a simple
  -45 dBFS gate with 300 ms hang, so breathing does not stream). Volume and audience applied live (`VoiceSession::setConfig`).
- **Mute** (gap 3): the VoiceSession asks `isMuted(seat)` (seat -> member through the session) before queueing a talker's frame;
  a muted talker is never decoded or played. Local only, as the settings say.
- **Talking indicators** (gap 5): the VoiceSession reports per seat "voice on" (frames seen in the last 10 s) and "speaking"
  (a frame in the last 250 ms, with the via flag); the session turns changes into `MemberVoice` events for the frontend.
- **Lobby voice** (gap 4): the VoiceSession talks to a small link interface instead of the Lockstep directly: `send(packet)`,
  `fanOut(talker, packet)`, `onReceive`. The match link is the Lockstep (as today); the lobby link is the UdpSession (the same
  unreliable VOICE packet as a session datagram, the host relaying to every lobby member: everyone hears everyone, radio-style
  2D, as the original's lobby chat). The lane switches link at the match start / return, keeping its decoders.
- **Test** (gap 8): `WFC_VOICETEST` (headless): two VoiceSessions over a loopback transport pair, a synthetic 440 Hz tone at
  -6 dBFS through capture framing -> Opus -> 5 % random loss -> jitter buffer -> decode; the received PCM is compared with the
  reference tone (the best-aligned delay): SNR >= 15 dB with loss, >= 25 dB without, no underruns after the start, and a muted
  talker produces nothing. Then the live microphone test on the two-PC day (section 4 of PACKAGE_7_TWO_PC_TEST.md gains a voice
  step).

## 4. Order

1. Settings + capture + mute + NOINTERACT gate + MemberVoice + WFC_VOICETEST (the match lane): the game has working in-match voice.
2. The lobby link.
3. The two-PC voice step in the test doc.
