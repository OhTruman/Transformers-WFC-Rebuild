# Netcode test harness (design, Experimental 2026-10-17; no code yet)

This harness tests online multiplayer against Systems' design, docs/handoff/CROSSPLAY_NETCODE_DESIGN.md (2026-10-16): lockstep on
inputs, D = 4 ticks of input delay, 60 Hz commands with R = 8 redundancy, a relay host, and SIMHASH as the desync detector.

The harness runs two (or more) wfc_rebuild instances on this PC, in lockstep over UDP loopback, through an impairment shim. It
exchanges and logs SIMHASH per tick, detects a desync at its first tick, and runs the NAVQ / BOTTEST rows through the network
path. **Section 2 lists what the exe must expose.** It is the part Systems' architecture doc and this harness have to agree on
before any code.

## 1. Shape

```
  exe A (host, slot 0)  <-UDP->  netshim  <-UDP->  exe B (client, slot 1)       (+ exe C ... for 3+ peers)
        |                         | per-direction latency / jitter / loss / burst / reorder / duplicate, seeded
        +-- net log (per tick) ---+-- packet log --- harness: compares the logs, finds the first divergent tick, writes the report
```

- **No card, no window.** Both exes run headless (null render), so a row needs no card slot. They are CPU only: one gap job, not
  a timed hold. Rows that measure frame cost (section 5, last row) are the exception.
- **Each exe's own participant** is driven by its PlayerBot brain (`-PlayerBot 1`, as in lockstep-ab). Its commands are the
  "human" input that crosses the wire. The remote peer's slot plays from the received command queue. Bots run on every peer,
  as the design says.
- **netshim** is `tools/fidelity/netshim.py`: asyncio UDP with one port pair per peer.
  - Impairment is deterministic per seed: the same seed gives the same drops at the same packet indices, so a failing row
    reruns identically.
  - Latency comes from a perf_counter timer wheel, with about 1 ms precision on Windows. That is enough for 16-200 ms cases.
  - It writes a packet log (jsonl: time, direction, size, tick range carried, dropped / delayed / duplicated).

## 2. What the exe must expose (the transport hook)

One interface in the sim's command layer, with two channels, matching section 5 of the design:

```
struct INetTransport {                         // one per peer process
  // unreliable, unordered (commands: one packet per tick, last R = 8 ticks each)
  virtual void sendUnreliable(int peer, const uint8_t* p, size_t n) = 0;
  // reliable, ordered (lobby / control / SIMHASH reports / state transfer)
  virtual void sendReliable(int peer, const uint8_t* p, size_t n) = 0;
  virtual bool poll(Packet* out) = 0;          // non-blocking; called once per sim tick before the tick runs
  virtual NetStats stats() const = 0;          // bytes / packets sent and received, resends, RTT estimate
};
```

Implementations:

- **(a) UdpTransport**, for this harness and LAN / direct IP: datagrams for the unreliable channel; seq / ack / resend over the
  same socket for the reliable one.
- **(b) WebRtcTransport**, for the product (design section 5).
- **(c) LoopbackTransport**, for an in-process unit test: two Worlds in one process, no sockets.

The harness only needs (a). It must sit behind the same interface as (b), so the lockstep layer above is tested unchanged.

Switches. These are environment variables, like the rest of the harness. They are proposals for Systems to adopt or rename:

| switch | meaning |
|---|---|
| WFC_NET_ROLE=host \| client | the relay role (design 5: the host merges tick bundles) |
| WFC_NET_BIND=127.0.0.1:port, WFC_NET_PEERS=127.0.0.1:port[,...] | sockets: the shim's ports, never each other's directly |
| WFC_NET_SLOT=n | this process's participant slot (the rest: bots, plus remote humans from the participant table) |
| WFC_NET_DELAY=D | input delay in ticks (default 4) |
| WFC_NET_HASH_EVERY=k | SIMHASH report period on the reliable channel (design: 60; the harness uses 1) |
| WFC_NET_LOG=path | per tick: tick, canonical SIMHASH, the hash of the merged command bundle, stall ms, bytes in / out (csv) |
| WFC_NET_DESYNC_AT=tick | test only: flip one bit of one sim value at that tick on this peer (the detector must catch it) |
| WFC_NET_STATEDUMP=dir | on a SIMHASH mismatch: write the last 120 ticks' command bundles plus a canonical state dump at the mismatch tick (design 9) |

Other requirements:

- **Headless match boot.** The null renderer (WFC_NULLRENDER) must accept a direct match boot. Today a null-render
  WFC_BOOT=match is refused (memory shot-frame-vs-sim-step), so each exe would otherwise need a window and the card.
- **Seed and participant table from the host.** WFC_SEED stays the local fallback. In net mode, the host's start message fixes the
  seed, the table and the start tick, as in design 6.
- **The same match as lockstep-ab.** The PlayerBot's commands must go through the command stream. Then a net run is replayable
  by a single-process WFC_LOCKSTEP run fed the recorded bundles. That replay equivalence is the strongest identity row
  (section 5, NET.identity).
- **Exit codes.** 0 for an ended match; distinct codes for a desync, a peer timeout, and a host loss. Then the harness never has
  to parse logs to know the outcome.

Dependencies on the design's work items (section 11):

- Item 1, canonical SIMHASH: without it, the per-tick hash differs across processes for non-sim reasons.
- Item 2, N human participants: a remote human in a slot.
- Item 3, the command stream with input delay.

The harness can start on (c) LoopbackTransport as soon as items 1-3 exist, and move to (a) when the UDP transport lands.

## 3. Desync detector

- **In band.** Every peer reports SIMHASH each tick at WFC_NET_HASH_EVERY=1. The host compares and, on a mismatch, makes every peer
  write its WFC_NET_STATEDUMP bundle.
- **Out of band, independent of the exe's own detector.** The harness joins the peers' WFC_NET_LOG files on tick and reports:
  - the first tick where SIMHASH differs;
  - whether the command-bundle hashes already differed before it. That would be a transport / merge bug, not a sim one.
- **Bisect aid.** It replays the host's recorded bundles in a single-process lockstep run and states which peer matches the
  replay. The odd one out is the desynced peer.
- **Diff.** With state dumps on both sides, it diffs the canonical dumps field by field: the first differing object and field.

## 4. Impairment profiles (netshim)

| profile | one-way latency | jitter | loss | other |
|---|---|---|---|---|
| lan | 0.5 ms | 0.2 ms | 0 | - |
| good | 20 ms | 3 ms | 0.1 % | - |
| typical | 50 ms | 10 ms | 1 % | - |
| bad | 100 ms | 30 ms | 5 % | 2 % reorder, 1 % duplicate |
| burst | 50 ms | 10 ms | 1 % | 8 packets dropped in a row every 10 s (beyond R = 8: a real stall) |
| asym | 20 ms A->B, 120 ms B->A | 10 ms | 1 % | - |
| outage | typical | - | - | 2 s of 100 % loss, then recovery |

## 5. Rows

**Smoothness rule (Systems / Integration 2026-10-23):** judge smoothness by step gaps, not stall_ms. net.csv has wall_ms per step since 7ee650c7: a visible hitch = two consecutive steps more than 50 ms apart; report "hitches > 50 ms: N (max gap M ms)". stall_ms also counts the sub-frame waits of a peer that is merely gated by arrival (rttjump: thousands of ~9 ms waits, 12.7 s "stalled" in one run while the steps stayed smooth - 3 gaps > 50 ms in 100 s, max 163 ms). stall_ms stays a diagnostic column.

| row | what | pass |
|---|---|---|
| NET.identity | 2 peers, lan, 9,000 ticks Berth 31 v 32 | SIMHASH equal every tick on both; equal to a single-process lockstep replay of the host's bundles; 0 stalls |
| NET.delay | D = 4 and 6, profiles good / typical / bad | no desync; stall ticks reported (design: stall only when T + D's commands are missing); bytes/s per peer vs design 8 (about 10 B per human per tick + framing) |
| NET.burst / NET.outage | burst and outage profiles | stalls during the gap, recovery after, no desync, no unbounded queue growth |
| NET.asym | asym profile | no desync; the stall pattern follows the slow direction |
| NET.leave | kill the client mid-match | the host swaps the slot to a bot at T + D; the remaining peer's SIMHASH equals a single-process replay with that swap |
| NET.desync | WFC_NET_DESYNC_AT on one peer | detected at that exact tick (period 1), or within 60 ticks at the design period; state dumps written; the harness names the peer and the field |
| NET.3peer | 3 peers (host + 2), typical | as NET.identity / NET.delay |
| NET.navq / NET.bottest | the NAVQ (4 user maps) and BOTTEST rows with each peer running them through the net path | the rows pass on every peer, with identical SIMHASH, and the same verdict as the single-process gates |
| NET.soak | 60 min, typical, map rotation | no desync; memory flat on both; no socket / handle growth |
| NET.cost | the transport + lockstep layer's CPU per tick (headless) and, windowed, frame-time cost vs lockstep-ab | report-only; windowed runs take a card slot like any timed row |

## 6. Effort and order

1. **netshim.py + the log join / report** (Experimental): about half a day, testable with a dummy UDP echo before the exe has the
   hook.
2. **The exe hook** (Systems / Gameplay, design items 1-3 + the UdpTransport): theirs to estimate.
3. **First rows**, NET.identity on LoopbackTransport and then on UDP: about 2 h once item 2 lands.
4. **The full matrix** (section 5): about 4 h of runs in gaps. All headless, no card.

## 7. Questions for Systems (before code)

1. **The interface.** Is `INetTransport` (two channels, poll per tick) the shape your architecture doc will use? Is the reliable
   channel the same socket for UDP (my assumption) or a TCP side channel?
2. **The switch names** in section 2: adopt them or rename them. The harness reads whatever the doc fixes.
3. **SIMHASH per tick.** Is canonical SIMHASH at WFC_NET_HASH_EVERY=1 cheap enough to leave on in harness runs? WFC_SIMHASH_PERIOD
   exists today.
4. **Headless match boot** under WFC_NULLRENDER: Systems or Gameplay?
