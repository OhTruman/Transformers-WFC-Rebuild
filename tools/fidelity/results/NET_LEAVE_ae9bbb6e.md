# NET.leave - member leaves mid-match, ae9bbb6e (D16: leavers become bots; WFC_UDPLEAVE_EMPTY=1 = empty + Forfeit) (2026-10-23)

tools/fidelity/udp-leave.ps1, headless, direct loopback, Streets TDM, seed 7, hash every tick, 120 s of match, the leaver goes at +60 s.
Times: 03:56:19-04:05:22 (four arms) and the no-bot Forfeit arm right after. Runs work/udp/leave_*_ae9/.

| Arm | Seats | How the leaver goes | Leave tick (every peer) | What the slot became | Host / stay after the leave | SIMHASH |
|---|---|---|---|---|---|---|
| switch | 3 (host, stay, leaver) | WFC_UDPLEAVE_AT=60 (the pause-menu leave) | 4205 ('1 member reports merged') | a bot ('a bot takes the slot') | both play on to 7,801 steps, exit 0 | host = stay on all 7,801 ticks; = leaver up to its end |
| end | 3 | the leaver's WFC_MATCH_SECONDS=60 runs out | 4205 | a bot | 7,801, exit 0 | same |
| kill | 3 | PID killed 60 s after AllLoaded (hard drop) | 3606 (via the session's drop) | a bot | 7,801, exit 0 (was: 10 s timeout + exit 4 on f99ef748) | same |
| empty, with bots (2 v 2 Smart) | 2 | WFC_UDPLEAVE_AT + WFC_UDPLEAVE_EMPTY=1 | 4205 ('0 member reports merged') | empty ('the slot empties'); 'MATCH leave player=1 ... (left the match)' | host plays on to 7,801 - **no Forfeit, correctly**: the leaver's team still has its bots, and Match::tickForfeit counts every non-dormant participant | = leaver |
| empty, no bots | 2 | same, BotsFriendly=0 / BotsEnemy=0 | 4205 | empty | **EndGame "Forfeit" at t 71.58, 1.51 s after the leave at t 70.07**, winner team 0, host exit 0 at 4,295 steps | = leaver |

**PASS**: the leave lands at one agreed tick on every peer and is logged the same way on each; under D16 the slot becomes a bot and
the match plays on with every surviving peer's hashes identical; the hard drop now goes through the same protocol (no more 10 s
timeout / exit 4); the empty path forfeits 1.5 s after the leave exactly when a team has nobody left. Every exit 0 (the killed leaver
-1), no new minidump.
