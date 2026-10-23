# Join in progress (Systems, 2026-10-23)

Public games follow the original (RE `ONLINE_JOIN_IN_PROGRESS_RE.md`): a joiner comes into a running match at once, on the smaller
team. Patch: `docs/handoff/SYSTEMS_JOIN_IN_PROGRESS.patch` (against integration 61e13ef0, on Gameplay's dormant seats ea73ac61).

## How it works

1. **Reserved seats.** The host's launch roster ends with `SessionSlot::Kind::Reserved` slots (up to the player limit). Every peer
   launches them as dormant humans (`game::RemoteSeat::dormant`: no pawn, no team, no score). The Lockstep treats them as seats with
   `activeFrom = 0`: implicit empty commands, no HELLO, nobody waits on them, nothing is sent to them.
2. **Join request.** A member whose lobby says the match runs (`MatchInProgress`) calls `ISession::joinRunningMatch()`. The host
   answers with a LateLaunch record (URL, seed, roster, the live seat table, the joiner's match seat), and tells the other members
   which session seat now holds that match seat. A refusal is `JoinFailed{Full}` (no free seat, or late join off) or
   `JoinFailed{TooFarAlong}` (past the catch-up limit, `setLateJoinLimit`, default 300 s of match time in the harness).
3. **Load gate.** The joiner loads, `reportLoaded`; the host releases that one peer (`AllLoaded`), the others keep playing.
4. **Catch-up.** The joiner's Lockstep asks the host for the history (JOINREQ). The host keeps every tick's commands, every event and
   its state hashes (`keepHistory`) and streams them, paced at 2 MB/s in datagrams under 1100 bytes, events before the ticks they
   are stamped for, then forwards every tick it steps. The joiner replays from tick 1 unpaced (12 ms of sim per frame), checking
   the host's hashes. `NetMatchDriver::catchingUp() / catchUpTick() / catchUpTarget()` drive `SessionEvent::CatchUpProgress`.
5. **Activation.** Within 10 ticks of the host's live tick the joiner says READY. The host picks the team (`World::smallerTeam()`)
   and stamps the event `SeatJoin {seat, activeTick = E + 30, team}` at its next tick E. Every peer applies it at E
   (`World::queueLateSeat`: the dormant seat wakes, a bot retires if the team is full); the joiner's commands start at E + 30
   and it goes through Choose Character as usual. Events from two paths (host forward + direct) are de-duplicated by a per-seat
   sequence number.

## Frontend glue still to do (Frontend)

- Public lobbies: append `Reserved` slots to the launch roster after the humans (max players minus humans) and call
  `setLateJoinLimit(seconds)` before `requestStart`.
- `fillOnlineLaunch`: a `Reserved` slot becomes a `RemoteSeat{dormant = true}` in `gl.humans`, in slot order. A late joiner's
  `CountdownStarted{lateJoin}` launches with `gl.localSeat = e.lateSeat` (its own Reserved slot), no countdown.
- The loading screen shows `CatchUpProgress` (catchUpTick of catchUpTarget) until the joiner is live.
- The solo rule (`SYSTEMS_SOLO_NO_DRIVER`) counts dormant seats: a public match hosted alone with Reserved seats keeps the driver
  (joiners may come); a private one alone has none.

## Tests (headless, loopback, no card)

- `work/jip/jip_run.ps1` (raw Lockstep, 3 peers, a pre-placed idle seat): joins at 15-110 s, profiles lan / typical / bad / burst
  through netshim: every tick's simStateHash equal on all peers (hash every tick), live 1.1-1.7 s after starting, no hitch > 50 ms
  on the others at the join.
- `work/jip/jip_session.ps1` (UdpSession + NetMatchDriver + Gameplay's dormant seats, ports 24100+): 2 peers + 2 late joiners at
  20 s and 40 s: both wake (teams 0 then 1), pick, and all 4 peers equal at all 5101 ticks; a limit below the join time ->
  `JoinFailed{TooFarAlong}`, the match runs on.
- Regressions on 61e13ef0 + patch: UDPSESSIONTEST PASS, ONLINEFLOWTEST 0 failures, SOLOLAUNCHTEST 5 / 5, LATESEATTEST 15 / 15, a
  2-peer typical row hash-every-tick: equal, no hitch.

## Note (campaign audio)

`WFC_CAMPAIGNAUDIOTEST=<chapter JSON>` needs `WFC_NULLRENDER=1 WFC_NULLRENDER_AUDIO=1 WFC_MUTE=1` (the cue mix runs on the audio
device, silent); without NULLRENDER_AUDIO there is no device and the test exits 2.
