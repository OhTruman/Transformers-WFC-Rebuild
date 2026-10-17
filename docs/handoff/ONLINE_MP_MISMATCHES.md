# Online multiplayer: frontend flows vs the session model - mismatches and resolutions (Frontend + Systems, 2026-10-17)

Inputs: ONLINE_MP_FLOWS.md / ONLINE_MP_FRONTEND_TESTS.md (Frontend) and ONLINE_MP_ARCHITECTURE.md section 10 (Systems).
All 11 points were agreed with Systems on 2026-10-17. The interface changes are in src/frontend/Session.h; the session
layer implements them.

| # | from | mismatch | resolution | where |
|---|---|---|---|---|
| 1 | Systems | ISession had no voice calls / events | `setVoiceSettings(SessionVoiceSettings{mode Off / PushToTalk / OpenMic, audience Team / All, device, volume})`, `setPushToTalk(held)` per change (sampled per 20 ms voice frame), `setMuted(member, muted)` (local only); event `MemberVoice{member, headsetState 0 / 1 / 2}` | Session.h |
| 2 | Systems | hosting through the relay needs a field | `SessionHostSettings::relay` ("host:port[,room]", "" = direct UDP); filled from a [PCSettings] value in phase 3 | Session.h |
| 3 | Systems | StrictNat cannot be detected with direct UDP | kept in the enum for later; never produced in phases 1-3. "Unable to Host" shows the PC text for NoPath | Session.h comment, GameFlow_Online.cpp |
| 4 | Systems | a sim id / load hash mismatch at reportLoaded | becomes `JoinFailed{Version}`, which may now arrive after the join; it ends the session for that peer with the original ClientOutdated text (flow group E) | Session.h comment |
| 5 | Systems | Web peers | phase 4; phases 1-3 report PC only | Session.h comment |
| 6 | Frontend | join code shape ("WFC-7QK2-M9TD-4H", relay "WFC-R-...") vs the frontend's WFC-XXXX-XXXX | the frontend checks the shape only ("WFC-" + base32 groups, or host:port / [ipv6]:port); decoding and the check symbol are the session layer's: `SessionJoinFailure::Invalid` (emitted synchronously on the next poll) -> the entry text "That is not a join code or an address." | Session.h, GameFlow_Online.cpp, test online.join_code_shapes |
| 7 | Frontend | two join timeouts | the session layer's JoinFailed{Timeout} at the original 50 s; the frontend's timer is a backstop at 55 s | GameFlow.h kJoinBackstop, tests online.S5_join_timeout / online.join_backstop_55s |
| 8 | Frontend | polling during a match | the frontend polls ISession once per frontend update in every level. One socket: whichever pump runs first drains it and dispatches by channel (lockstep packets to the LockstepLayer queue; lobby / control / voice to the session event queue); each consumer reads its own queue once per its own update | Session.h comment |
| 9 | Frontend | who owns teams / slots | the host's GameFlow (the original PickTeam; humans take bot slots) publishes them in `SessionLobby::slots`; SessionLayer owns membership and connectivity. Mid-match slot changes: GameFlow requests, the host's LockstepLayer picks the tick (`SessionSlotSwap`) | Session.h comment; flow group C / D |
| 10 | Frontend | remote idleness is only visible in commands | the host's LockstepLayer flags 180 s (D5) without a non-idle command; SessionLayer kicks: `Kicked{Idle}` to that member, `PeerDropped{Kicked}` to the others. `kick()` stays for manual kicks; no MemberIdle event | Session.h comment |
| 11 | Frontend | the desync message | the popup says "The game has gone out of sync and cannot continue." with no tick; the tick goes to the log and the dump | flow group E |
