# Cross-play netcode design (Systems, 2026-10-16; design only, no code)

User decision: cross-play design now, code after package 6; order PC-to-PC, then PC-to-browser, then PS3 / Xbox 360 towards
the end. Input: Gameplay's CROSSPLAY_SIM_DETERMINISM_AUDIT.md (integration 038bfee9). Platform feasibility is AssetTools'
study. This document picks the network model, the wire, the session flow and the numbers; open questions are at the end.

## 1. Decision in one paragraph

**Deterministic lockstep on inputs** for PC-to-PC and PC-to-browser (phase 1 / 2), on top of Gameplay's **option A**
(strict-float flags, one deterministic libm for the sim, stable sorts with integer tiebreaks, canonical little-endian
serialisation, a cross-build SIMHASH CI gate). Only player commands travel; every peer runs the full simulation, bots
included; SIMHASH (canonical) is the desync detector. **Option C (server-authoritative snapshots)** is the planned fallback
for the PowerPC consoles: a PC (or any phase-1 peer) runs the authoritative lockstep sim and streams state to a console
client, so the consoles never need bit-identical floats. Fixed-point (option B) is not planned.

Why lockstep: the rebuild already runs a fixed 1/60 s step with seeded RNG, serial participant order and a byte-exact
SIMHASH; bots cost no bandwidth (they are simulated everywhere); bandwidth stays tiny at 32 v 32; the same machinery gives
replays and spectating for free. Its costs - input delay, full-state visibility to every peer, desync risk across builds - are
handled in sections 4, 9 and 3.

## 2. What the original did (reference, not wire compatibility)

The original is UE3 replication: server-authoritative (listen server on the 360), actors replicated by relevance and
priority, clients predicting their own pawn. From Xe-TransEngine.ini (ExtractedAssets, read only):

| setting | value | meaning |
|---|---|---|
| [IpDrv.TcpNetDriver] NetServerMaxTickRate / LanServerMaxTickRate | 30 / 35 | server network tick (Hz) on Live / system link |
| MaxInternetClientRate / MaxClientRate | 10,000 / 15,000 | bytes / s per client (internet / LAN) |
| [Engine.Player] ConfiguredInternetSpeed / ConfiguredLanSpeed | 10,000 / 20,000 | client rate request |
| ConnectionTimeout / InitialConnectTimeout / KeepAliveTime | 8 s / 50 s / 0.1 s | disconnect rules |
| RelevantTimeout / SpawnPrioritySeconds | 5 s / 1 s | replication relevance |
| NetConnectionClassName | OnlineSubsystemLive.IpConnectionLive | Xbox Live secure transport |
| [OnlineSubsystemLive] LiveSystemLinkPort / LanAnnouncePort | 14000 / 14001 | system link |
| MaxRemoteTalkers / MaxLocalTalkers | 30 / 4 | voice chat |
| [Engine.NetDriver] PacketSimulationSettings (commented) | PktLoss=2, PktLag=87, variance 10 | the developers' "real world" test profile |
| LanGameUniqueId | 0x000B100D | LAN discovery id |

Matches were at most 10 players (5 v 5); the 16 v 16 / 32 v 32 ExtendedPlayers rows are our extension. There is no wire
compatibility to keep: the original's Live services are gone and its protocol is not ours. Fidelity applies to the match
flow and rules (lobby, countdown, scoring, end), which the rebuild already reconstructs offline. RE gap (open question 1):
the original's client-side prediction and hit registration rules (lag compensation) are not reverse-engineered.

## 3. Determinism contract (from the audit, adopted as-is)

- Phase 1 (PC-to-PC, same exe): already deterministic. Phase 1 still ships the option-A work so phase 2 needs no rule change.
- Phase 2 (PC-to-browser, x64 clang vs wasm clang): option A is required. Gate: the same seeds run headless on both builds
  with canonical SIMHASH byte-identical for the whole BOTTEST / SIMHASH run (CI row, per merge touching the sim).
- Consoles: option A tested on the toolchain first; if contraction / float-in-double / denormal behaviour cannot be pinned,
  option C (section 10).
- **Sim build identity:** every peer computes a `simId` = hash of (sim source version, the sim-affecting data: collision,
  nav, weapons / characters / match tables, the [CONF] constants) at boot; a session only admits peers with the same simId.
  Presentation data (render, audio, HD) is free to differ.
- **Rule for every sim change** (Gameplay's, verbatim in substance): code that affects simulation state uses only float /
  double arithmetic with no compiler-dependent transcendentals outside core/Math.h; never iterates an unordered container;
  never std::sort's float keys without a total-order integer tiebreak; draws randomness only from core::SimRandom or the
  bot's own rng in a fixed serial order, never from threads; keeps worker-pool work per participant and order-independent;
  never branches on a frame-rate-dependent value (fixed dt); never serialises raw host-endian bytes across machines; and is
  proven by WFC_SIMHASH serial vs threaded plus the NAVQ / BOTTEST gates.

## 4. Tick, commands and input delay

- **Sim tick 60 Hz** (unchanged). The network carries one **command** per human per tick, not raw devices:

  | field | bits | note |
  |---|---|---|
  | buttons held | 32 | the Button enum's sim-relevant members (move, jump, transform, fire, fine aim / boost, dash, ascend / descend, weapon next / prev, melee, grenade, interact, killstreak, ability); "pressed" is derived from the previous tick's bits |
  | view yaw / pitch delta | 2 x 16 | quantised (1/4096 rad), so the sim applies identical deltas everywhere; the local camera uses the same quantised value |
  | move axes (pad) | 2 x 8 | -127..127; keyboard maps to the extremes |
  | triggers (pad LT) | 8 | 0..255 |
  | total | ~10 bytes | per human per tick, before framing |

  UI-only input (menus, text, absolute pointer, device activity) never enters the sim stream.
- **Input delay D ticks:** a command issued at tick T executes at T + D on every peer. Starting value D = 4 (67 ms) on
  LAN-class links; adaptive per session from the measured RTT (D = ceil(RTT_max / 2 / 16.7 ms) + 1, clamped 2..8, changed
  only at agreed ticks). The original's own developers tested at 87 ms lag; 4-6 ticks is in that range.
- **Hiding the delay:** the local camera turns immediately (look is presentation until the shot); the sim uses the delayed,
  identical command. Firing, movement and abilities are delayed by D. A rollback (GGPO-style) mode for the local pawn is a
  later option (open question 4): it needs a save / restore of the full sim state and re-simulating up to D ticks per frame
  (64 bots x 8 ticks at ~1-2 ms AI per step is affordable on PC, not in a browser).
- **Send rate:** one packet per tick per peer (60 Hz), each carrying the last R = 8 commands (redundancy covers 2-7 lost
  packets without retransmission). A peer stalls only if tick T + D's commands of some peer are still missing; stalls are
  logged with the slow peer's id.

## 5. Topology and transport

- **Star through a relay peer:** one peer (the host) collects every human's commands for tick T and broadcasts the merged
  tick bundle. It decides nothing about the sim; it orders joins / leaves / bot swaps on tick boundaries and owns the lobby.
  Every peer still runs the full sim. A full mesh (each peer to each) is a later option for latency (no relay hop).
- **Transport: WebRTC data channels** (unreliable, unordered mode for commands; one reliable channel for lobby / control /
  state transfer), on every PC and in the browser. One transport for all phase-1 / 2 pairs: browsers cannot open raw UDP,
  and PC builds can use a native WebRTC library (libdatachannel-class, BSD / MPL licences - check before choosing).
- **NAT traversal:** ICE with STUN (public STUN servers are free to use; open question 6) for direct paths; TURN relay only
  when both sides are behind symmetric NATs (needs a server someone runs). **Signalling** (exchanging the WebRTC offer /
  answer) needs a rendezvous: (a) a tiny self-hostable signalling server (the user or a friend runs it), or (b) a
  zero-server mode: copy-paste / QR join codes carrying the offer / answer, or (c) direct IP + port for LAN and
  port-forwarded hosts. Non-commercial and no-account: (b) and (c) ship first, (a) is optional.
- **Consoles (option C):** WebRTC is not available on PS3 / 360 homebrew; a PC host bridges plain UDP to them (section 10).

## 6. Session and match flow (the original's, reused)

- **Lobby:** the existing frontend flow (PartyLobby -> InLobby -> game mode / map / bot settings) becomes the host's lobby;
  peers join it, pick characters / loadouts there; the host broadcasts the lobby state (reliable channel). simId and
  protocol version are checked on join.
- **Start:** the host fixes the match seed, the participant table (humans, then bots in slot order), D, and the start tick;
  every peer loads the map, reports "ready" with its simId and a load hash, then the countdown (AutostartCountdown, as the
  original's GRI) runs in sim ticks so it ends on the same tick everywhere.
- **During the match:** commands, plus SIMHASH every 60 ticks (8 bytes, canonical) on the reliable channel.
- **End:** match rules run in the sim, so every peer reaches EndGame on the same tick; results (scoreboard, XP, stats) are
  computed locally and identically (CROSSPLAY_PROFILE_STATS.md).

## 7. Join, leave, late join, host migration

- **Leave / disconnect** (no packets for the original's 8 s ConnectionTimeout, or a clean leave): the host schedules, at tick
  T + D, the swap of that participant to a bot (the Smart / Classic brain the lobby uses) - every peer swaps at the same tick,
  the match continues. The bot keeps the score row (open question 8: or vacate the slot).
- **Late join** (into a running match): the host picks a tick, serialises the full sim state canonically (pawns, bots incl.
  their rng / brain state, projectiles, match state, RNG state), sends it on the reliable channel; the joiner loads the map,
  applies the state, fast-forwards the buffered commands to the present, then takes over a bot slot at an agreed tick. The
  same state transfer is the **desync recovery** path. Size estimate: a few hundred KB at 64 participants (open question 2:
  measure); one-off, so seconds on any link.
- **Host migration:** every peer has the full state and the command history, so the host role is only relaying and
  ordering. On host loss, the remaining peers elect the lowest participant id still connected; it resumes relaying from the
  last fully-acknowledged tick; peers resend their unacknowledged commands. No state transfer is needed unless a peer had
  diverged.

## 8. Bandwidth (lockstep, commands only)

Per tick per human ~10 bytes of command; packets carry R = 8 ticks of redundancy; UDP / WebRTC framing ~60 bytes per packet
(IP + UDP + DTLS / SCTP). At 60 packets / s:

| match | humans | upstream per peer (to host) | downstream per peer (from host) | host upstream (to all) |
|---|---|---|---|---|
| 5 v 5, 10 humans | 10 | (60 + 8 x 10) x 60 = 8.4 KB/s | (60 + 8 x 10 x 10) x 60 = 51 KB/s | 9 x 51 = 460 KB/s |
| 5 v 5, 2 humans + 8 bots | 2 | 8.4 KB/s | (60 + 8 x 10 x 2) x 60 = 13.2 KB/s | 13.2 KB/s |
| 16 v 16, 32 humans | 32 | 8.4 KB/s | (60 + 8 x 10 x 32) x 60 = 157 KB/s | 31 x 157 = 4.9 MB/s |

Redundancy dominates. With R = 3 (enough for 1-2 losses) and the bundle carrying each human's commands only since the
receiver's last ack, a 5 v 5 downstream drops to ~(60 + 3 x 100) x 60 = 22 KB/s and the 32-human host upstream to ~2 MB/s -
fine on a home connection for 5 v 5 (the original's own cap was 10 KB/s per client at 30 Hz with snapshots), heavy for a
32-human host. Bots cost zero. Options for large human counts: 30 Hz bundles (2 ticks per packet; halves framing), full
mesh (no host fan-out), or host on a server-class link (open question 7).

## 9. Desync, cheating, privacy

- **Desync:** canonical SIMHASH every 60 ticks; on mismatch every peer dumps the last 120 ticks of commands + its state hash
  per tick (a replay bundle) for offline bisection, and the host offers recovery by state transfer (section 7). A
  phase-2 CI gate (cross-build SIMHASH) is what keeps this rare.
- **Cheating:** lockstep gives every peer the full state (wall-hack-able) and trusts each peer's commands (no speed
  hacks possible beyond what a command can express - commands are bounded by the sim's own rules). Acceptable for a
  non-commercial, friends-first game; open question 9.
- **Privacy:** peers see each other's IP addresses (WebRTC); join codes should say so. No accounts, no telemetry; the
  portable identity is in CROSSPLAY_PROFILE_STATS.md.

## 10. Console fallback (option C)

- A PC peer runs the authoritative sim (the same lockstep sim, with the console's commands fed in as one human's commands)
  and streams **snapshots** to the console at 20-30 Hz: the console's pawn prediction plus interpolated remote pawns,
  corrected by the snapshot. That is the original's own UE3 model.
- Snapshot size: ~40-60 bytes per participant quantised (position, velocity, yaw / pitch, form, health, weapon, state
  flags), delta-compressed against the last acked snapshot: ~2-4 KB per snapshot at 64 participants, ~60-120 KB/s at 30
  Hz - inside the original's 15 KB/s only at 10 participants with relevance culling (the original's RelevantTimeout model).
- The console runs presentation from snapshots (no bit-exact sim needed); it still needs the same data and rules to predict
  its own pawn. Transport: plain UDP to the PC host, which bridges to the WebRTC session for PC / browser peers.
- Work: snapshot serialisation (shared with late join), a client-side prediction / reconciliation path for one pawn,
  interpolation for the rest, and the console network layer (AssetTools' feasibility study decides whether it exists).

## 11. Work items and order (after package 6)

1. Gameplay: option A (libm swap, sorts, canonical SIMHASH serialisation) + the cross-build SIMHASH CI gate.
2. Multiple human participants in one sim: today World has one local `Player` (~200 uses in World.cpp) plus `bots_`.
   Proposal: a remote human occupies a participant slot exactly like a bot, its `BotBrain` replaced by a command queue, so
   the per-participant serial order and the existing bot pipeline stay; the local player is "participant 0 with a local
   queue". This is the largest gameplay refactor here (estimate open: question 3).
3. Command stream + input delay + the tick bundle (headless two-process test over loopback first: two exes, one host, same
   SIMHASH every tick).
4. WebRTC transport, join codes, STUN; lobby over the network; start / countdown / end.
5. Leave -> bot swap, state transfer (late join, desync recovery), host migration.
6. Browser build (wasm) running the same sim; the CI gate across x64 / wasm.
7. Console fallback (option C) per AssetTools' feasibility study.

## 12. Open questions

1. RE: the original's lag compensation / hit registration and prediction rules - wanted for option C (consoles), not for
   lockstep. Who reverse-engineers TnPlayerController / TnWeapon netcode, and is it worth it?
2. Full sim state size at 64 participants (for late join / desync recovery) - measure once canonical serialisation exists.
3. Effort for "N human participants" in World (section 11.2) - Gameplay to estimate.
4. Rollback for the local pawn (hides input delay) vs fixed delay only - decide after a delay-only playtest at 4-6 ticks.
5. WebRTC library for the PC build and its licence (libdatachannel MPL-2.0, others) under the non-commercial rule.
6. STUN: rely on public STUN servers, or ship without STUN (direct IP / LAN / port-forward only) to avoid any third-party
   service? TURN relay: who would run one, if anyone?
7. 16 v 16 / 32 v 32 with many humans: host on a stronger link, full mesh, or cap humans per match (bots fill the rest)?
8. A leaving human: bot takes over the slot and keeps the score row, or the slot empties (the original dropped the player)?
9. Cheating posture: friends-only trust (lockstep reveals all state) acceptable for public sessions too?
10. Voice chat (the original had MaxRemoteTalkers 30): in scope for cross-play or not?
11. Replays: the command log + seed is a full replay for free - expose it to the player?
