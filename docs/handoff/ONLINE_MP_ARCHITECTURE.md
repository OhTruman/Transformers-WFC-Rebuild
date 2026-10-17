# Online multiplayer architecture (Systems, 2026-10-17; design, no code yet)

Scope: the transport and session layer under the frontend's `ISession` (src/frontend/Session.h, integration 3a19b47a) and
over the deterministic lockstep sim. Inputs: CROSSPLAY_NETCODE_DESIGN.md (the lockstep design, 2026-10-16),
CROSSPLAY_PLATFORM_FEASIBILITY.md, ONLINE_MP_FLOWS.md section 1 (the original's rules), ONLINE_MP_FRONTEND_TESTS.md, and
Experimental's tools/fidelity/NETCODE_HARNESS.md (2405aa83), whose section 7 questions are answered in section 9.

**The bar (user, 2026-10-17):** make it work with what we have - our deterministic lockstep and our frontend. The original's
rules are the starting point and change wherever they do not fit; there is no fidelity gate on the online layer itself.
So every choice below is the simplest robust one for its phase, and each simplification is stated (section 1).

## 1. Decisions and simplifications

| topic | decision | simplification vs the full design / the original |
|---|---|---|
| sync model | deterministic lockstep on commands (60 Hz sim, one ~10-byte command per human per tick, input delay D) | no rollback; no client prediction beyond the local camera turning |
| topology | the original's player-hosted listen host: one peer hosts the lobby and relays the match (star). Every peer runs the full sim | no full mesh, no dedicated server |
| transport, phases 1-3 | **plain UDP** (one socket per peer), behind `INetTransport` | WebRTC only for the browser (phase 4); PC builds need no WebRTC library before then |
| reliable channel | on the **same UDP socket** (seq / ack / resend), not TCP | one socket, one NAT mapping, one firewall rule |
| finding a game | **direct IP:port and join codes first** (a code = the host's address + port + a session nonce, base32) | no matchmaking, no server browser, no presence; LAN discovery optional (phase 2) |
| NAT | host port-forwards or uses UPnP-IGD (one optional attempt); otherwise the **relay fallback**: a tiny self-hostable UDP relay | no STUN / hole punching in phases 1-3 (added later only if needed) |
| desync | detect by SIMHASH exchange; **drop everyone to the lobby** with a "Desync at tick T" message and both peers' dumps | no state-transfer resync (the late-join / resync machinery is a later item) |
| join in progress | **not in phase 1-3**: a joiner waits in the lobby while a match runs (frontend decision D6, `MatchInProgress`) | the original allowed join-in-progress |
| leave mid-match | the host swaps the slot to a bot at an agreed tick (`SessionSlotSwap`, `PeerDropped`) | as the design |
| host loss | **the session ends** for everyone ("The game has ended because the host has quit.", `HostLost`) - the original's own rule | no host migration |
| voice | in scope (user): an unreliable voice lane in the same transport, Opus, push-to-talk default | no third-party voice service |
| stats / XP | private-match rules (no XP, decision D8) | public XP / leaderboards later |

## 2. Layering

```
 GameFlow (frontend)  --ISession (10 calls, 17 events)-->  SessionLayer (Systems)        lobby, join codes, member table,
                                                              |                         countdown, load gate, kick, voice ctl
 World / Match (sim)  --ILockstepNet-------------------->  LockstepLayer (Systems)       commands in / tick bundles out,
                                                              |                         input delay, SIMHASH exchange, swaps
                                                         INetTransport                    two channels + voice lane, poll()
                                                       UdpTransport | WebRtcTransport | LoopbackTransport
```

- **SessionLayer implements `ISession`** exactly as declared (no renames). It owns the member table, the lobby state the host
  publishes, the countdown, the load gate (`reportLoaded` -> `PeerLoaded` / `AllLoaded` with the start tick), kicks, and the
  end-of-session events.
- **LockstepLayer** sits beside it for the match: `ILockstepNet { submit(tick, command); bool bundleFor(tick, out); void
  reportHash(tick, hash); }`. World calls it once per sim tick; the PlayerBot / local input produces the command. It is not
  part of `ISession` on purpose: the frontend never sees commands.
- **Pumping:** the transport is polled from two places, never twice per frame: by `ISession::poll(now)` in the frontend
  (lobby, loading, menus) and by the LockstepLayer once per sim tick before the tick runs (match). Both drain the same
  socket and dispatch by channel.

## 3. Transport (`INetTransport`) - the harness's shape, adopted

```
struct INetTransport {
  virtual void sendUnreliable(int peer, const uint8_t* p, size_t n) = 0;   // commands, tick bundles
  virtual void sendReliable(int peer, const uint8_t* p, size_t n) = 0;     // lobby / control / SIMHASH / dumps
  virtual void sendVoice(int peer, const uint8_t* p, size_t n) = 0;        // NEW: voice frames (unreliable, own budget)
  virtual bool poll(Packet* out) = 0;                                      // non-blocking
  virtual NetStats stats() const = 0;                                      // bytes, packets, resends, RTT, loss
};
```

- **UdpTransport** (phases 1-3): one non-blocking UDP socket; reliable = per-peer seq numbers, acks piggybacked on every
  outgoing packet, resend after 2 x RTT (min 50 ms), ordered delivery; messages up to 64 KB are fragmented at 1,200 bytes.
- **WebRtcTransport** (phase 4): the browser's data channels - one unreliable / unordered (commands, voice), one reliable /
  ordered - with the same packet format; the PC side then needs a WebRTC library (libdatachannel-class; licence check first).
- **LoopbackTransport**: two Worlds in one process, for unit tests (harness section 2 c).

**Packet header (8 bytes):** magic 'WF' (2) | protocol version (1) | channel (1: 0 unreliable, 1 reliable, 2 voice, 3 control)
| sender slot (1) | flags (1) | ack-bits / seq (2). The session nonce from the join code is checked on the first packet of a
peer and dropped afterwards (the peer's address is then the key).

## 4. Lockstep on the wire (from the netcode design; unchanged unless stated)

- **Command:** ~15 bytes per human per tick: buttons held + pressed (2 x 32 bits), quantised ABSOLUTE view yaw (u16) / pitch (i16),
  two int8 move axes (section 10.2 contract; absolute aim replaces the earlier deltas so a missed bundle cannot drift).
- **Up (client -> host):** one packet per tick with that client's last R commands (R = 8; R = 3 for > 10 humans).
- **Down (host -> all):** one packet per tick with the merged bundle for the ticks each receiver has not acked.
- **Input delay D** = 4 at start (67 ms), from the measured RTT: D = ceil(RTT_max / 2 / 16.7 ms) + 1, clamped 2..8,
  changed only at host-announced ticks. A peer stalls only when tick T's bundle is missing.
- **Swaps:** a leave becomes `SessionSlotSwap{matchPlayer, To::Bot, atTick = T + D}` on every peer.

## 5. Desync detection and policy

- **SIMHASH**: the existing FNV-1a over every pawn's position / velocity / health / yaw and every bot's steering state
  (World::finishRemainder), made **canonical** (ordered by match player id, not "local pawn first"; no per-process state) -
  netcode design work item 1.
- **Exchange:** each peer reports (tick, hash) on the reliable channel every `WFC_NET_HASH_EVERY` ticks (default 60 in play,
  1 in the harness); the host compares.
- **Policy (simplest robust):** on the first mismatch the host sends `Desync(T)` to all; every peer writes its dump
  (`WFC_NET_STATEDUMP`: the last 120 tick bundles + a canonical state dump at T) and the session returns everyone to the
  lobby with the `Desync` event. No resync in phases 1-3.

## 6. Session, join codes, NAT and the relay

- **Host:** binds UDP (default port 7777, configurable), optionally tries UPnP-IGD once, then `HostOpened{joinCode, address}`.
  `HostFailed{NoPath}` only when the socket cannot bind.
- **Join code:** base32 of {IPv4 or IPv6, port, 32-bit session nonce} with a check symbol, e.g. "WFC-7QK2-M9TD-4H". A typed
  `ip:port` is accepted too. `JoinFailed{Timeout}` after the original's InitialConnectTimeout (50 s) without a handshake;
  `{Full}` / `{Version}` / `{MapMissing}` from the host's answer.
- **Relay fallback (phase 3):** a ~200-line UDP relay (`wfc_relay`, self-hostable by the user or a friend; no accounts): both
  sides register with a room id, the relay forwards packets between them. A join code can name the relay instead of the
  host ("WFC-R-..."). Cost: the relay sees encrypted-nothing (commands are not secret) and adds one hop of latency.
- **Keep-alive / timeouts:** the original's ConnectionTimeout 8 s (`PeerDropped{Timeout}` / `ConnectionLost{timedOut}`) and
  KeepAliveTime 0.1 s.
- **Kick:** the host's idle kick (180 s, decision D5) via `kick()` -> `Kicked{Idle}` on the member.

## 7. Voice (user decisions: in scope; proximity voice with room reverb)

- **Lane:** `sendVoice` on the same socket, unreliable, its own byte budget; frames are dropped, never resent.
- **Codec:** Opus (BSD), mono 16 kHz, 20 ms frames at a fixed 16 kbit/s (~40 bytes per frame + header): ~2.6 KB/s per
  talking member up; the host fans each frame out to that frame's audience.
- **Capture:** in the Systems audio layer (Win32Audio; the browser's WebAudio later), only while push-to-talk is held
  (default) or, in OpenMic, behind a voice-activity gate (`SessionVoiceSettings`).
- **Modes** (`SessionVoiceSettings.audience`, extended): **Team radio** (team members, anywhere on the map), **All** (all
  members, radio), **Proximity** (everyone in earshot, positional), and **Radio + Proximity** (the usual choice and the
  proposed default: the squad hears you on the radio anywhere; everyone near you hears you in the world). Each voice frame
  carries a 1-bit flag "radio" / "proximity" per audience; the host fans out radio frames to the team and proximity frames
  to everyone within the cut-off distance (below).
- **Radio playback:** 2D, dry (never through the reverb), on a voice bus with its own volume; the original's chat was
  non-positional, so this is the faithful baseline.
- **Proximity playback:** the speaker's decoded stream is a **3D emitter at the speaker's pawn head**, positioned every
  mixer block from the lockstep sim (every peer already has every pawn's exact position: no position data on the wire).
  It uses the sound system we already have for every cue:
  - **attenuation:** the original's dialog-class cue values (BL_HUD_INTERFACE.DX_SLIDER_VOLUME, category DX_DRY - the only
    DX cue: DistanceMin 400 UU, DistanceMax 6,400 UU, rolloff 1, the FMOD log curve our SoundCues applies): full level
    within 4 m, -6 dB per doubling, silent beyond 64 m (the host's proximity cut-off for fan-out: 64 m);
  - **occlusion:** the existing sound-occlusion traces (the same low-pass / attenuation every 3D cue gets);
  - **panning:** the existing 3D pan.
- **Reverb (the room):** the rebuild's maps do not use ReverbVolume actors (one in all of authored.db, none in MP). The
  original sets the room reverb with **Kismet SeqAct_Reverb actions** fired by Touch volumes (MP maps: 7-49 per map) - the
  data we already export (tools/systems/gen_level_audio.py -> LevelAudio.inc: the Touch volumes' brush polygons, the
  preset per action) and play (LevelAudioScript: the listener's pawn enters a volume -> the I3DL2 preset cross-fades on the
  MASTER_WET bus).
  - **Listener's room:** a proximity voice is a wet 3D emitter like every world sound, so it goes through the listener's
    current preset - the engine's own rule, at no extra cost.
  - **Speaker's room:** a static map volume -> preset (the Touch volume whose Touched output leads to a SeqAct_Reverb,
    resolved by the generator) gives the speaker's room by point-in-volume on the speaker's position (LevelAudioScript's
    insideVolume, ray parity over the brush triangles; ~10-50 us per test, once per talker per 100 ms). When it differs from
    the listener's, that voice sends 50 % of its wet signal to a **second reverb instance** set to the speaker's preset
    (cross-fading like the listener's), so a voice from the next hall sounds like that hall. One extra instance covers the
    common case; at most 2 (the two loudest remote-room talkers), others fall back to the listener's room.
- **What the mixer needs:** a VOICE bus (radio, dry) and the proximity voices as ordinary 3D emitters on the existing
  wet / dry paths; the second (and third) reverb instance = another copy of the existing I3DL2 echo + reflections + late tank
  (Win32Audio.cpp) on its own send. No convolution reverb is needed: the presets are algorithmic I3DL2 parameter sets.
- **CPU per talker:** Opus decode ~20-50 us per 20 ms frame (~0.2 % of a core); 3D pan + occlusion filter as any cue
  (negligible); point-in-volume 10-50 us per 100 ms. Each extra reverb instance ~1-2 % of a core at 48 kHz stereo (the
  existing tank's cost). A 16-talker worst case stays under ~5 % of one core.
- **Data needed:** none from AssetTools beyond what LevelAudio.inc has; gen_level_audio.py gains the static volume -> preset
  map (Systems). ReverbVolume support is not needed for MP (the campaign may author some; checked when campaign audio
  starts).
- **Audience / limits:** team or all per member setting; the host enforces MaxRemoteTalkers 30 (the original's value) per
  receiver; proximity is cut at 64 m.
- **UI:** per-player mute (local only) and the original's HeadsetState (0 none / 1 headset / 2 speaking) in PlayerList_GFX
  and Hud_GFX - `setVoiceSettings` / `setPushToTalk` / `setMuted` / `MemberVoice` (section 10.1); the Proximity and
  Radio + Proximity modes extend `SessionVoiceSettings.audience` (Frontend to add the two values).

## 8. Bandwidth per player (60 Hz, ~60 bytes UDP framing per packet; IPv4 + UDP = 28, our header 8)

| match | humans | up per client | down per client | host up (to all) |
|---|---|---|---|---|
| 5 v 5, 10 humans, R = 8 | 10 | (36 + 80) x 60 = 7.0 KB/s | (36 + 8 x 10 x 10) x 60 = 50 KB/s | 9 x 50 = 450 KB/s |
| 5 v 5, 10 humans, acked deltas (R_eff ~3) | 10 | 7.0 KB/s | (36 + 3 x 100) x 60 = 20 KB/s | 180 KB/s |
| 16 v 16, 32 humans, acked deltas | 32 | 7.0 KB/s | (36 + 3 x 320) x 60 = 60 KB/s | 31 x 60 = 1.9 MB/s |
| voice, per talking member | - | +2.6 KB/s | +2.6 KB/s per talker heard (radio: team; proximity: within 64 m) | + fan-out; proximity adds no position data (every peer has the speaker's sim position) |

- Bots cost nothing (every peer simulates them). 5 v 5 fits any home connection; a 32-human host needs ~2 MB/s up (a
  good fibre line, or 30 Hz bundles: 2 ticks per packet halves the framing, or the relay as the hub).
- The bundle carries each receiver only the ticks it has not acked, so R is an upper bound, not a cost.

## 9. Answers to the harness's questions (NETCODE_HARNESS.md section 7)

1. **Interface:** yes - `INetTransport` with the two channels and `poll()` per sim tick, plus `sendVoice` (a third, voice
   lane; the harness can ignore it). The reliable channel is **on the same UDP socket** (seq / ack / resend), not TCP.
2. **Switches:** adopted as named: `WFC_NET_ROLE`, `WFC_NET_BIND`, `WFC_NET_PEERS`, `WFC_NET_SLOT`, `WFC_NET_DELAY`,
   `WFC_NET_HASH_EVERY`, `WFC_NET_LOG`, `WFC_NET_DESYNC_AT`, `WFC_NET_STATEDUMP`. Added: `WFC_NET_RELAY=host:port[,room]`
   (phase 3) and `WFC_NET_VOICE=0` (off in harness runs). Exit codes adopted: 0 match ended, 10 desync, 11 peer timeout,
   12 host lost, 13 version / load-hash mismatch.
3. **SIMHASH per tick:** the hash itself is cheap - FNV-1a over ~32 bytes per pawn and ~40 per bot, ~3 KB at 64 players,
   a few microseconds per tick. The cost in today's code is the per-step `LOG_INFO` line; in net mode the hash goes to the
   `WFC_NET_LOG` csv (buffered, one line per tick) and the reliable channel (8 bytes + tick). So `WFC_NET_HASH_EVERY=1` is
   fine to leave on in harness runs; the product default stays 60.
4. **Headless match boot (Systems, as Integration decided; World side Gameplay):** today `WFC_NULLRENDER` + `WFC_BOOT=match`
   passes the boot check but stops at the interactive loop, which needs a window (Application.cpp: "the interactive loop
   needs a window"). Plan: `Application::runHeadlessMatch()` - when null render and `WFC_BOOT=match`, load the map exactly as
   the windowed match boot does (no GL upload; the null renderer already loads sims for BOTTEST / NAVQ), then run fixed 60 Hz
   sim ticks without pacing (or paced with `WFC_FRAMELIMIT`), the PlayerBot driving the local participant, until
   `WFC_MATCH_SECONDS` / EndGame / a net exit code. The World side (Gameplay): confirm nothing in the match tick reads the
   renderer (corpses, FX hooks already guard `renderer_`); the audio device is absent there by design (cue counts need a
   paced windowed run, as today). Estimate: Systems ~3-4 h + a Gameplay review.

## 10. `ISession` alignment (Session.h, 3a19b47a): implemented as declared; mismatches listed, not renamed

| item | status |
|---|---|
| host / join / cancelJoin / leave / publishLobby / setMemberInfo / requestStart / reportLoaded / kick / poll | implemented by SessionLayer as declared |
| the 17 events | all produced; `Desync(value = tick)` ends in the lobby (section 5); `MatchInProgress` is what a joiner gets while a match runs (no join-in-progress, section 1) |
| `SessionSlotSwap` | produced by the host for leaves (To::Bot); To::Remote unused until join-in-progress |
| **mismatch 1: voice** | `ISession` has no voice calls or events. Needed: `setVoice(bool pushToTalkHeld)` (or a mode + key state), `setMuted(member, bool)`, `setVoiceAudience(team / all)`, and an event `MemberVoice{member, headsetState 0 / 1 / 2}` for PlayerList_GFX HeadsetState and the Hud_GFX talker list. Frontend to add (their header); the session layer feeds them |
| **mismatch 2: relay** | `join(target)` takes a code or address - a relay code fits as a code, no change; hosting through a relay needs `SessionHostSettings.relay` (string, empty = direct) |
| **mismatch 3: NAT failure** | `SessionHostFailure::StrictNat` is never produced with direct UDP (we cannot detect NAT type without STUN); "Unable to Host" maps to `NoPath` only |
| **mismatch 4: load hash** | `reportLoaded(simId, loadHash)`: the host compares both; a mismatch becomes `JoinFailed{Version}` (before the match) - no new event needed |
| **mismatch 5: platform** | `SessionPlatform::Web` is phase 4; phases 1-3 report PC only |

### 10.1 Resolved with the Frontend (2026-10-17; landed in Session.h, agents/frontend e0421fc3 = integration 202cef15; shared record: docs/handoff/ONLINE_MP_MISMATCHES.md)

| point | resolution |
|---|---|
| voice | `setVoiceSettings(SessionVoiceSettings{mode Off / PushToTalk / OpenMic, audience Team / All, device, volume})`, `setPushToTalk(bool held)` per change (sampled per 20 ms voice frame), `setMuted(member, bool)` (local only); event `MemberVoice{member, headsetState 0 / 1 / 2}` |
| relay | `SessionHostSettings.relay` ("" = direct), filled from a [PCSettings] value in phase 3 |
| StrictNat | kept in the enum, never produced in phases 1-3; the frontend shows "Unable to Host" + the PC text for NoPath |
| load-hash / version | `JoinFailed{Version}` accepted in any state (lobby or load gate): that peer's session ends with the original ClientOutdated text |
| join codes | "WFC-" + base32 groups, or ip:port / [ipv6]:port; decoding is Systems'; new `SessionJoinFailure::Invalid` for a failed decode / check symbol (emitted on the next poll, no network wait) |
| join timeout | Systems emits `JoinFailed{Timeout}` at 50 s; the frontend's own timer is a 55 s backstop |
| pumping | one socket; whichever pump runs first (`ISession::poll`, every frontend update incl. the match, or the LockstepLayer's per-tick poll) drains it and dispatches by channel into two queues (lockstep / session+voice); each consumer reads only its queue |
| slots / teams | the host's GameFlow decides (PickTeam: smaller team; humans take bot slots) and publishes `SessionLobby.slots`; SessionLayer owns membership / connectivity; in-match slot changes go through `SessionSlotSwap` at a tick the host's LockstepLayer picks |
| idle kick | the host's LockstepLayer detects 180 s without a non-idle command; SessionLayer kicks (`Kicked{Idle}` to that member, `PeerDropped{Kicked}` to the others); `kick()` stays for manual kicks |
| desync text | "The game has gone out of sync and cannot continue." - the tick goes to the log / dump only |

- **Addition (agents/frontend 1c2dbd11):** `SessionMemberInfo::iconicCharacter` (std::string) - in iconic mode the member's claimed
  iconic CharacterName, else "". It rides setMemberInfo / MemberJoined like characterSlot; the lobby message serializes it next to
  characterSlot (one length-prefixed string; an old peer without it reads "").
- **Gameplay session hooks (agents/gameplay 178a7bef):**
  - `World::noteRemoteCommand(matchPlayer)` resets a Remote participant's 180 s idle clock. LockstepLayer calls it inside the lockstep
    step, for non-idle commands only, so every peer reaches the idle swap on the same tick.
  - `World::simTick()` counts sim steps since launchMatch. It is the unit of PeerDropped's SessionEvent.value and of
    SessionSlotSwap.atTick.
- **Addition (Frontend, pending):** `virtual float micLevel() const` returns the capture level 0..1 for the Settings > Audio Mic Test
  meter (default 0). VoiceCapture fills it from the pre-gate RMS of the last 20 ms frame.

### 10.2 Remote command contract (with Gameplay, 2026-10-17; their docs/handoff/REMOTE_COMMAND_CONTRACT.md, agents/gameplay b46a2565)
- `bool World::applyRemoteCommand(int matchPlayer, const RemoteCommand& c)`, with
  `RemoteCommand { uint32_t tick; int8_t moveForward, moveRight; uint32_t buttons, pressed; float aimYaw, aimPitch; }`. The floats are
  the DECODED wire values (yaw u16 over 0..2pi, pitch i16 over +-pi/2), so every peer applies identical numbers.
- **When:** inside the lockstep step for tick N, once all of N's commands have arrived, before World::tick runs step N, in ascending
  match-player order on every peer.
- **The local human too:** in an online match the local human goes through the same call, decoded from the bytes sent. The camera
  turns locally every frame; the sim side waits for N.
- **World builds the MoveIntent** from the axes, aim and buttons, and judges idle itself. Any held / pressed bit, a non-zero axis, or
  an aim change > 0.5 deg against that player's previously APPLIED command counts as non-idle; World then calls noteRemoteCommand.
  The transport never calls it.
- **No holes:** a missing command for N stalls the step (section 5). A dropped peer becomes a SlotSwap to Bot at an agreed simTick.
- **Button count:** the Button enum is static_assert-ed to <= 32 bits.

## 11. Phases and exit criteria

| phase | scope | exit criteria |
|---|---|---|
| 1 loopback | two instances on one PC over UDP loopback (and LoopbackTransport in-process); headless match boot; canonical SIMHASH; command stream with D; harness rows NET.identity / NET.delay / NET.desync (Experimental) | NET.identity: SIMHASH equal every tick over 9,000 ticks and equal to a single-process lockstep replay; NET.desync caught at the injected tick; NET.leave swaps to a bot at T + D on both |
| 2 LAN | two PCs on a LAN, windowed, through the frontend (Create Game online -> join by code / ip:port -> lobby -> match -> back to lobby); voice on | a full 5 v 5 match (2 humans + bots) from lobby to scoreboard and back, twice, no desync; voice heard both ways with push-to-talk; leave / host-quit texts as the original |
| 3 internet + relay | over the internet with port-forward / UPnP, and through `wfc_relay` when neither side can forward | the phase-2 match over typical / bad profiles (netshim) and one real internet pair; relay path works with both sides behind NAT; bandwidth within section 8 |
| 4 browser | WebRtcTransport; the browser build joins a PC host | a PC + browser match with no desync (cross-build SIMHASH gate in CI) |

## 12. Work items and owners

| item | owner | notes |
|---|---|---|
| canonical SIMHASH + state dump | Systems + Gameplay | design item 1 |
| N human participants (`ParticipantKind::Remote`) fed by the command stream | Gameplay | design item 2 |
| command stream + input delay (LockstepLayer) | Systems | design item 3 |
| headless match boot | Systems (shell) + Gameplay (World review) | section 9.4 |
| UdpTransport, packet format, reliable channel | Systems | section 3 |
| SessionLayer implementing `ISession`; join codes | Systems | sections 6, 10 |
| voice lane, Opus capture / playback, the voice UI calls | Systems (audio, transport) + Frontend (UI, `ISession` additions) | section 7 |
| netshim + rows | Experimental | harness doc |
| `wfc_relay` | Systems | phase 3 |
| WebRtcTransport | Systems | phase 4 |

## 13. Open points (decisions already given are not repeated)

- Default port 7777 vs the original's system-link 14000 (cosmetic; firewalls).
- Whether a non-host can relay voice when the host's upstream is the limit (32 humans): later, with the relay as the hub.
