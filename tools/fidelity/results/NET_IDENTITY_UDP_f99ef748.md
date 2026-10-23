# NET.identity, UdpSession path + late join + member leaves - f99ef748 (2026-10-23)

Run times (for Systems' card-side correlation): build 03:26:14-03:31:57 (-j 2); five profiles 03:32:05-03:38:17; late join
03:38:17-03:40:01; leave 'end' 03:40:01-03:41:14; leave 'kill' 03:41:14-03:42:28. Headless, CPU only. Runs: work/udp/*_f99/.

## Five profiles: PASS
Every hash equal (4,198-4,201 ticks), exits 0, no dump. Hitches > 50 ms host / member: lan 0 / 0, typical 0 / 0, bad 1 / 2 (max 232 ms),
burst 0 / 1 (69 ms), rttjump 0 / 1 (117 ms).

## Late join (tools/fidelity/udp-late-join.ps1): PASS
Three peers identical on all 6,001 ticks, exits 0. (CatchUpProgress still floods after READY on this head: 4,530 events - Systems
fixes it in 97a22bb6.)

## Member leaves, 1v1 (tools/fidelity/udp-leave.ps1 -Seats 2, host 120 s, leaver at +60 s)

| Arm | Host exit | Host steps | Leaver | SIMHASH up to the leave | What the host did |
|---|---|---|---|---|---|
| end (leaver's WFC_MATCH_SECONDS=60) | 0 | 4,201 | exit 0 at 4,201 | identical | 'PeerDropped (in match)', 'NET seat 1 left (its last tick 4201)', then stopped stepping at that tick and ended - **no Forfeit / MATCH leave / EndGame line, no play-on** |
| kill (leaver's PID killed 60 s after AllLoaded) | **4** (timeout / peers missing) | 3,607 | killed | identical | 'PeerDropped (in match)', then 'NET peer timeout: no commands for 10 s (seat@tick: 1@3607)' and exit 4 - **no drop -> Forfeit path** |

Read: on f99ef748 a mid-match leave or drop still ends the lockstep (the end arm cleanly, the kill arm through the 10 s timeout and
exit 4); Gameplay's Forfeit (65c7ccc8) is never reached because the session's mid-match leave protocol is not on this head - it comes
with Systems' SYSTEMS_LEAVE_MIDMATCH.patch (97a22bb6: WFC_UDPLEAVE_AT, 'NET seat N left mid-match', the seat back to Reserved, the
hard-drop path through the same protocol). Expected after that merge (Integration): Forfeit 1.5 s after the leave tick on every
surviving peer, hashes equal up to it. Rerun 'end', 'kill' and 'switch' on that merge.
