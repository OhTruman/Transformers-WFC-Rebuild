// WFC_CAMPAIGNTEST (with WFC_MAP=D5_OME, headless with WFC_NULLRENDER): the campaign chapter boot through World - the chapter's units
// merged (collision), the chapter start, TnCampaignGameSolo's PendingMatch -> InProgress with the AI squad (the trio minus the human's
// pick), the level Kismet beginning, the squad following, the game over (InProgressOver, UI event 14 after 1 s, no respawn) and a
// launch at a checkpoint. One SUMMARY line.
#include "core/Application.h"
#include "core/Log.h"
#include "game/World.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace core {

void Application::runCampaignTest() {
    const float dt = (float)clock_.stepSeconds();
    int checks = 0, fails = 0;
    auto check = [&](bool ok, const char* what) { ++checks; if (!ok) ++fails; LOG_INFO("CAMPAIGNTEST %s %s", ok ? "PASS" : "FAIL", what); };
    bool guard = true;
    auto step = [&] {
        platform::InputFrame in;
        world_.handleInput(in, dt); world_.tick(dt);
        if (guard && !world_.localPlayerDead()) world_.player().pawn().health().reset();
    };
    auto run = [&](float secs) { for (int k = 0; k < (int)(secs / dt); ++k) step(); };
    auto hdist = [](const core::Vec3& a, const core::Vec3& b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z)); };
    const game::CampaignChapter& C = world_.campaignChapter();
    if (!world_.campaignMap() || C.chapter != "D5_OME") {   // the harness runs on the loaded map: refuse anything else cleanly
        LOG_ERROR("CAMPAIGNTEST needs WFC_MAP=D5_OME (loaded: %s)", world_.mapName().c_str());
        LOG_INFO("CAMPAIGNTEST SUMMARY: 0/1 checks passed");
        exitCode_ = 1;
        return;
    }
    check(world_.campaignMap() && C.ok && C.chapter == "D5_OME" && C.mapId == 105 && C.faction == 1 && C.units.size() == 6,
          "D5_OME: a campaign chapter (MapId 105, Decepticon), 6 units loaded at the start (initial state Arena + the persistent level)");
    check(world_.usingSlice() && world_.collision() != nullptr, "the chapter World: the units' collision_pawn.glb merged (no graybox fallback)");
    check(C.playerStart.actor == "TnCampaignPlayerStart_16313" && C.checkpoints.size() == 2 && C.checkpoints[0].actor == "Checkpoint_4578" &&
          C.checkpoints[0].tag == "Stage 1", "the chapter start TnCampaignPlayerStart_16313; checkpoints Stage 1 (4578), Stage 2 (10917) in DisplayOrder");
    game::MatchLaunch L;
    check(game::MatchLaunch::fromURL("D5_OME_BASE_m?GameModeTag=SOLO?MapId=105?WfcDifficulty=1", L) && L.settings.campaign && L.map == "D5_OME" &&
          L.settings.goalScore == 0 && L.settings.timeLimit == 0 && L.difficulty == 1, "URL: SOLO = campaign, the chapter by its persistent level, no goal / time limit");
    check(world_.launchMatch(L), "launchMatch(SOLO) on D5_OME");
    const game::Match& m = world_.match();
    const game::CampaignState& S = world_.campaign();
    const int me = world_.localMatchPlayer();
    check(S.active && S.squad.size() == 2 && S.squadChassis.size() == 2 && S.squadChassis[0] == "Truck4" && S.squadChassis[1] == "Car3",
          "SetupAiSquad: Soundwave (Truck4) and Breakdown (Car3) - the trio minus the human's Megatron");
    check(world_.botNav().valid(), "the chapter's bot nav loaded");
    check(S.kismetLoaded && !S.kismetBegun, "the level Kismet loaded; it waits for InProgress");
    run(1.5f);
    check(m.state() == game::Match::State::InProgress && !world_.localPlayerDead(), "PendingMatch -> InProgress; the human spawned");
    check(world_.player().pawn().chassis().id == "Tank" && m.players()[(size_t)me].team == 1, "the human plays Megatron (Tank) on the Decepticon team");
    check(hdist(world_.player().pawn().position(), C.playerStart.pos) < 3.0f, "the human starts at TnCampaignPlayerStart_16313");
    bool squadIn = true;
    for (int p : S.squad) squadIn = squadIn && m.players()[(size_t)p].alive && m.players()[(size_t)p].team == 1;
    check(squadIn, "both squad members spawned on the human's team");
    check(S.kismetBegun && S.vm.activations.count("SeqEvent_GameplayStarted") > 0, "the level Kismet began with InProgress (GameplayStarted fired)");
    // The squad follows: move the human 25 m along the nav and wait.
    {
        const core::Vec3 from = world_.player().pawn().position();
        core::Vec3 to = from;
        for (const auto& c : world_.botNav().cells())
            if (hdist(c.centroid, from) > 20.0f && hdist(c.centroid, from) < 30.0f && std::fabs(c.centroid.y - from.y) < 2.0f) { to = c.centroid; break; }
        world_.player().pawn().setPosition(to + core::Vec3{0, 0.5f, 0});
        run(20.0f);
        float worst = 0.0f;
        for (int p : S.squad) if (const game::Character* c = world_.participantPawn(p)) worst = std::max(worst, hdist(c->position(), world_.player().pawn().position()));
        LOG_INFO("CAMPAIGNTEST squad after the move: farthest %.1f m (moved %.1f m)", worst, hdist(to, from));
        check(hdist(to, from) > 15.0f && worst < 12.0f, "Move-To-Buddy: the squad follows the human (both within 12 m 20 s after a 20+ m move)");
    }
    // A squad member dies: back at once near the human (AIRespawnDelay 0).
    {
        const int p = S.squad[0];
        if (game::Character* c = world_.participantPawnMutable(p)) c->health().applyDamage(1.0e6f);
        run(2.0f);
        const game::Character* c = world_.participantPawn(p);
        check(c && m.players()[(size_t)p].alive && hdist(c->position(), world_.player().pawn().position()) < 15.0f && m.state() == game::Match::State::InProgress,
              "a squad member's death: no game over; respawned at once near the human");
    }
    // The human dies (solo: never downed) -> CampaignGameOver -> InProgressOver; UI event 14 after 1 s; nobody respawns.
    guard = false;
    world_.player().pawn().health().applyDamage(1.0e6f);
    run(0.5f);
    check(S.gameOver && m.state() == game::Match::State::InProgressOver && !S.gameOverSent, "the human's death: game over -> InProgressOver");
    run(1.0f);
    bool ev14 = false;
    for (const game::MatchEvent& e : world_.matchEvents()) ev14 = ev14 || (e.type == game::MatchEvent::Type::CampaignGameOver && e.value == 14);
    check(S.gameOverSent && world_.hudState().cpGameOver, "1 s later: SendGameOverEvent (UI event 14; HUD cpGameOver)");
    run(10.0f);
    check(world_.localPlayerDead() && m.state() == game::Match::State::InProgressOver, "InProgressOver: no respawn after 10 s");
    LOG_INFO("CAMPAIGNTEST game over event seen in the last batch: %d", (int)ev14);
    // A launch at Stage 2 (?Checkpoint=): the start is the checkpoint.
    guard = true;
    game::MatchLaunch L2;
    check(game::MatchLaunch::fromURL("D5_OME_BASE_m?GameModeTag=SOLO?MapId=105?Checkpoint=D5_OME_BASE_m.TheWorld.PersistentLevel.Checkpoint_10917", L2) &&
          world_.launchMatch(L2), "a launch at Checkpoint_10917 (Stage 2)");
    run(1.5f);
    const game::CampaignChapter::Checkpoint* c2 = C.checkpoint("Checkpoint_10917");
    check(c2 && S.checkpoint == "Checkpoint_10917" && !world_.localPlayerDead() && hdist(world_.player().pawn().position(), c2->pos) < 3.0f && S.squad.size() == 2,
          "the human and the squad start at Stage 2's checkpoint");
    run(30.0f);
    int total = 0; for (const auto& a : S.vm.activations) total += a.second;
    std::string held;
    for (const auto& d : S.vm.defaulted) held += " " + d.first + "x" + std::to_string(d.second);
    LOG_INFO("CAMPAIGNTEST Kismet: %d activations, cap hits %d, checkpoints reached %zu, mission complete %d; generic Out:%s", total, S.vm.queueCapHits,
             S.checkpointsReached.size(), (int)S.missionComplete, held.c_str());
    check(total > 0 && !S.missionComplete && m.state() == game::Match::State::InProgress, "30 s at Stage 2: the Kismet runs, no false MissionComplete");
    {   // step 3: the pawn factories and the in-map pick
        int ai = 0, pooled = 0, firstAi = -1;
        for (size_t k = 0; k < S.factories.size(); ++k) if (S.factories[k].ai) { ++ai; pooled += (int)S.factories[k].pool.size(); if (firstAi < 0 && !S.factories[k].pool.empty()) firstAi = (int)k; }
        check(S.factories.size() == 30 && ai == 26 && pooled >= 24, "D5 pawn factories: 30 (26 AI), the Jet factories pooled");
        const int n = world_.cpTestFireFactory(firstAi);
        run(2.0f);
        bool alive = false; int team = -1;
        if (firstAi >= 0) for (int p : S.factories[(size_t)firstAi].pool) if (m.players()[(size_t)p].alive) { alive = true; team = m.players()[(size_t)p].team; }
        check(n > 0 && alive && team == 0 && S.enemiesSpawned >= 1, "Spawn Actor: the factory spawns its Autobot AI at its spawn point");
        game::CharacterSelection pick; pick.type = 1; pick.chassisId = "Car3";   // the in-map pick: Breakdown
        const bool ok = world_.match().selectCharacter(world_.localMatchPlayer(), pick);
        run(0.2f);
        const bool swapped = std::find(S.squadChassis.begin(), S.squadChassis.end(), "Car3") == S.squadChassis.end() &&
                             std::find(S.squadChassis.begin(), S.squadChassis.end(), "Tank") != S.squadChassis.end();
        check(ok && swapped, "the in-map pick of Breakdown: accepted; the squad becomes Megatron + Soundwave");
    }
    LOG_INFO("CAMPAIGNTEST SUMMARY: %d/%d checks passed", checks - fails, checks);
    exitCode_ = fails ? 1 : 0;
}

} // namespace core
