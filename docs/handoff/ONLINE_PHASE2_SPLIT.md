# Online phase 2: the playable online match - split, dependencies, estimates (Systems, 2026-10-17)

Goal: from the menus, PC A **Host**s (shows its LAN IPv4:port and a join code), PC B **Join**s by IP (relay codes later), both
pick characters in the lobby, the host starts, and both play the same rendered match with their own controls, HUD and voice; the
match ends back in the lobby. Phase 1 (package 7) already gives the deterministic lockstep, record / replay, the identity harness,
the lead-based auto delay and the voice lane; the Frontend's Session.h contract is complete and consumed by every flow.

Estimates are agent working hours (code + headless tests), excluding card slots; the critical path is 1 -> 2 -> 4.

## 1. UdpSession: ISession over UdpTransport (Systems, ~11 h)

| part | what | est. |
|---|---|---|
| 1a dynamic peers | UdpTransport today knows its peers at start (WFC_NET_PEERS) and ignores other senders: add `addPeer(seat, addr)` / `removePeer`, accept a JOIN datagram from an unknown address on the host only, and seat it | 2 h |
| 1b lobby protocol | on the reliable channel: JOIN (version, map set hash) -> JOIN_OK{member id, lobby} / JOIN_FAIL{Full, Version, MapMissing, Invalid}; host fan-out of LobbyState / MemberJoined (repeat = update) / MemberLeft; setMemberInfo; the member list carries each member's address so the match can run full-mesh as phase 1 does | 4 h |
| 1c host / join API | `host(settings)` -> HostOpened{joinCode, LAN IPv4:port} or HostFailed{NoPath}; `join(code or ip:port)` -> JoinProgress / Joined / JoinFailed{Timeout 50 s}; cancelJoin; join code = the LAN address + port base32-coded (the relay code later; StrictNat reported when a relay is configured but unreachable) | 1 h |
| 1d launch + load gate | setLaunch(url, seed, roster) + requestStart -> CountdownStarted{launchUrl, seed, lobby} on every member; reportLoaded -> PeerLoaded / AllLoaded{start tick}; simHeld() true from reportLoaded to the start tick; load timeout -> PeerDropped{LoadTimeout}. Uses phase 1's HELLO launch hash (a mismatch = JoinFailed{Version}) | 2 h |
| 1e in-match + return | Lockstep status -> PeerDropped{Left / Timeout, agreed tick} (BYE's last tick), HostLost, ConnectionLost{timedOut}, Desync{tick}; Kicked{Idle} only with the host's Kick Idle row on; noteRemoteCommand; after the match the host's return carries members back (LobbyState, matchRunning false); a late joiner gets MatchInProgress; leave(); a host leaving ends the session | 1 h |
| 1f voice bridge | setVoiceSettings / setPushToTalk / setMuted -> VoiceSession; MemberVoice{headsetState, via}; micLevel() | 1 h |

Tests (in the same hours): two UdpSessions in one process over loopback UDP (host + join + lobby + launch + a dropped member);
netshim profiles for the lobby (join under 'bad'); the Frontend's two-runtime loopback test once UdpSession lands.

## 2. The online match in the real window (Systems + Gameplay, ~10 h)

| part | owner | what | est. |
|---|---|---|---|
| 2a net match driver | Systems | runNetMatch's loop becomes a `NetMatchDriver` the normal frame loop calls each frame: pump, submit the local seat's commands up to N + D, step the sim when `haveAll(N)` (else hold: render the last state, no step), hashes, record, end / BYE; started by CountdownStarted / AllLoaded instead of WFC_NET_* env (the env path stays for the harness) | 4 h |
| 2b local human seat | Gameplay | the local player's real controller input -> `encodeLocalCommand` through the seat path (decision A: the pawn moves only through commands, as the player bot online today); camera, aim and HUD on the local seat's pawn | 3 h |
| 2c HUD + presentation for remote seats | Gameplay (+ Frontend for names) | kill feed / scoreboard names from the lobby members; "currently spectating" while dead; the remote seats' pawns animate and sound as participants already do | 2 h |
| 2d session events in the match | Systems | PeerDropped -> the disconnect default (the original's: the pawn dies, slot empty; a bot only when the host has bots on) through Gameplay's seat API; ConnectionLost / HostLost -> the Frontend's error flow | 1 h |

## 3. Choose Character in the match (Gameplay + Systems, ~3 h)

The wire command has no room for a character pick, so: **Systems** adds tick-stamped reliable **events** to Lockstep (a second
per-seat ordered stream, applied at their tick on every peer before the step, recorded and replayed like commands; 1 h);
**Gameplay** sends the in-match Choose Character as an event and applies it with `Match::selectCharacter` on every peer (2 h).

## 4. Frontend + boot (Frontend, ~2 h; Integration's policy)

The Application creates the UdpSession at boot in the playable build (listening only after host()), so the online rows appear in
the package; the Frontend wires Host / Join / the lobby to it (the flows exist) and adds the two-runtime loopback test.

## What each piece needs from the others

- **Systems needs from Gameplay:** 2b's seat path for real input (the encodeLocalCommand contract from 10.2 is unchanged), a seat
  API to drop / bot a seat at an agreed tick (2d), and 3's event consumer.
- **Gameplay needs from Systems:** 2a's driver (when to step, the local seat index, held / stalled state for the HUD), 3's event
  stream, the lobby roster (names, teams) from UdpSession.
- **Frontend needs from Systems:** UdpSession emitting the Session.h events exactly as the FakeSession does today (the same flows
  are driven by both), and the join code format for the join panel.
- **Integration:** a card slot per windowed check (one exe per PC; two-PC runs on the user's second PC or two exes on the dev PC
  with different ports), and the decision on the default port (7777 or the original's 14000; the package 7 test uses 14000).

## Order and checks

1 (UdpSession, headless tests) and 3's event stream first, in parallel with Gameplay's 2b on the existing harness; then 2a + 2b
together under a window (one card slot: two windowed exes on the dev PC, loopback); then 4; then the user's two-PC playtest with
the menus. Each step keeps phase 1's identity rows green (lan / typical / bad / burst / rttjump, hash every tick) and the offline
SIMHASH unchanged.
