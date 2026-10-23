// WFC_LATESEATTEST (headless with WFC_NULLRENDER): join in progress through dormant reserved seats (World::queueLateSeat). Scenario A,
// this machine at seat 0: a 5 v 5 Streets TDM (the player + 4 + 5 bots) with seat 1 reserved; the seat is invisible until it wakes on
// the smaller (humans) team, a full team's last bot leaves for it, it spawns at once after its pick. Scenario B, this machine is the
// joiner (its launch seat dormant): no spawn, spectating, then the wake + pick spawn the local pawn. One SUMMARY line.
#include "core/Application.h"
#include "core/Log.h"
#include "game/World.h"

namespace core {

void Application::runLateSeatTest() {
    const float dt = (float)clock_.stepSeconds();
    int checks = 0, fails = 0;
    auto check = [&](bool ok, const char* what) { ++checks; if (!ok) ++fails; LOG_INFO("LATESEAT %s %s", ok ? "PASS" : "FAIL", what); };
    auto run = [&](float secs) { for (int k = 0; k < (int)(secs / dt); ++k) { platform::InputFrame none; world_.handleInput(none, dt); world_.tick(dt); } };
    game::Match& m = world_.match();
    auto launch = [&](int localSeat) {
        game::MatchLaunch L;
        game::MatchLaunch::fromURL("MP_IAC_Streets?GameModeTag=TDM?BotsFriendly=4?BotsEnemy=5", L);
        game::RemoteSeat host; host.name = "Host"; host.team = 0; host.member = "HOST";
        game::RemoteSeat reserved; reserved.dormant = true;
        L.humans = {host, reserved};
        L.localSeat = localSeat;
        return world_.launchMatch(L);
    };
    auto count = [&](int team, bool bots) { int n = 0; for (const auto& p : m.players()) n += !p.dormant && p.team == team && (!bots || p.kind == game::ParticipantKind::Bot); return n; };
    auto rows = [&] { return world_.hudState().scoreboard.size(); };

    // ---- A: this machine at seat 0, seat 1 reserved
    check(launch(0), "A: launch with a reserved seat (seat 1 dormant)");
    check(m.players().size() == 11 && m.players()[1].dormant && m.players()[1].team == 255, "A: 11 participants, seat 1 dormant with no team");
    check(rows() == 10 && count(0, false) == 5 && count(1, false) == 5, "A: the reserved seat is not listed or counted (5 v 5)");
    check(world_.seatPlayer(1) == 1 && world_.seatPlayer(2) == -1, "A: seatPlayer: launch seats only");
    run(11.0f);
    check(m.state() == game::Match::State::InProgress && !m.players()[1].alive && m.players()[1].timeToRespawn < 0.0f, "A: the match started; the dormant seat never spawns");
    check(world_.smallerTeam() == 1, "A: smallerTeam: both teams full -> the one with fewer humans (Decepticons)");
    game::RemoteSeat joiner; joiner.name = "Joiner"; joiner.team = world_.smallerTeam(); joiner.member = "JOIN";
    const uint32_t E = (uint32_t)world_.simTick() + 10;
    world_.queueLateSeat(1, joiner, E);
    run(0.5f);
    check(!m.players()[1].dormant && m.players()[1].team == 1 && m.players()[1].name == "Joiner" && !m.players()[1].alive,
          "A: at tick E the seat wakes on team 1 (no character yet: Choose Character)");
    check(count(1, false) == 5 && count(1, true) == 4 && rows() == 10, "A: the full team's last bot left for the joiner (still 5 v 5, 10 listed)");
    world_.queueSeatSelection(1, m.players()[0].selection, (uint32_t)world_.simTick() + 2);
    run(0.5f);
    bool spawned = false;
    for (const game::MatchOpponent* o : world_.matchOpponents()) if (o->matchPlayer() == 1) spawned = o->spawned();
    check(m.players()[1].alive && spawned, "A: the joiner spawns at once after its pick (no respawn wait)");
    run(5.0f);
    int retired = -1;
    for (size_t i = 0; i < m.players().size(); ++i) if (m.players()[i].dormant) retired = (int)i;
    check(retired >= 0 && !m.players()[(size_t)retired].alive, "A: the retired bot stays out (no respawn)");

    // ---- B: this machine is the joiner
    check(launch(1), "B: launch as the joiner (this machine's seat dormant)");
    const int me = world_.localMatchPlayer();
    check(me == 1 && m.players()[(size_t)me].dormant, "B: the local participant is the dormant seat 1");
    run(11.0f);
    check(world_.localPlayerDead() && !m.players()[(size_t)me].alive && m.spectating(me), "B: no local spawn; spectating before the wake");
    check(rows() == 10, "B: the scoreboard lists the 10 real participants");
    game::RemoteSeat self; self.name = "Me"; self.team = world_.smallerTeam(); self.member = "ME";
    world_.queueLateSeat(1, self, (uint32_t)world_.simTick() + 5);
    run(0.25f);
    world_.queueSeatSelection(1, m.players()[0].selection, (uint32_t)world_.simTick() + 2);
    run(0.5f);
    check(!world_.localPlayerDead() && m.players()[(size_t)me].alive && m.players()[(size_t)me].team == self.team, "B: woken and picked: the local pawn spawns on its team");
    // ---- C: a leaver empties a team (no bots): EndGame "Forfeit" after 1.5 s, the other team wins
    {
        game::MatchLaunch L;
        game::MatchLaunch::fromURL("MP_IAC_Streets?GameModeTag=TDM", L);
        game::RemoteSeat a; a.name = "Host"; a.team = 0; a.member = "HOST";
        game::RemoteSeat b; b.name = "Guest"; b.team = 1; b.member = "GUEST";
        L.humans = {a, b}; L.localSeat = 0;
        check(world_.launchMatch(L), "C: launch 1 v 1 humans, no bots");
        run(11.0f);
        world_.queueLeave(1, (uint32_t)world_.simTick() + 3);
        run(1.0f);
        check(m.players()[1].dormant && m.state() == game::Match::State::InProgress, "C: the guest left (seat dormant); no forfeit before 1.5 s");
        run(1.0f);
        check(m.state() == game::Match::State::MatchOver && m.endReason() == "Forfeit" && m.winnerTeam() == 0, "C: the empty team forfeits after 1.5 s; Autobots win");
    }
    // ---- D: a team empty from the start never forfeits
    {
        game::MatchLaunch L;
        game::MatchLaunch::fromURL("MP_IAC_Streets?GameModeTag=TDM", L);
        game::RemoteSeat a; a.name = "Host"; a.team = 0; a.member = "HOST";
        L.humans = {a}; L.localSeat = 0;
        check(world_.launchMatch(L), "D: launch a lone human, no bots");
        run(14.0f);
        check(m.state() == game::Match::State::InProgress, "D: a team empty from the start does not forfeit");
    }
    LOG_INFO("LATESEAT SUMMARY: %d/%d checks passed", checks - fails, checks);
    exitCode_ = fails ? 1 : 0;
}

} // namespace core
