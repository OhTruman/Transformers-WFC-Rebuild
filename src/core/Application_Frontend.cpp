// Clean-room reconstruction — application side of the frontend boot (docs/FRONTEND.md).
// The flow itself (levels, lobbies, URLs, UI controller) is src/frontend/GameFlow; this file only owns the
// process loop: frontend frames, loading the match world the flow launches, and releasing it on return.
#include "core/Application.h"
#include "core/FrontendSceneGL.h"
#include "core/LoadYield.h"
#include "core/Log.h"
#include "core/Time.h"
#include "frontend/FlowTrace.h"
#include "frontend/FrontendRuntime.h"
#include "game/MapState.h"
#include "game/MatchOpponent.h"
#include "platform/Movie.h"
#include "platform/Window.h"
#include "render/Renderer.h"
#include "ui/GfxPresenter.h"
#include "ui/UiGL.h"
#include "ui/gl/GlCensus.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#endif

// Systems audio seam (agents/systems game::FrontendAudioRuntime + the World level-audio contract). Compiled in once
// the Systems lifecycle is integrated; without it the frontend runs silent (calls are traced only).
#if __has_include("game/FrontendAudioRuntime.h")
#include "game/FrontendAudioRuntime.h"
#define WFC_SYSTEMS_FRONTEND_AUDIO 1
namespace {
struct SystemsFrontendAudio final : frontend::IFrontendAudio {
    game::FrontendAudioRuntime rt;
    audio::IAudio* device;
    audio::Sound movieSound = audio::kInvalidSound;
    explicit SystemsFrontendAudio(audio::IAudio* a) : rt(a), device(a) {}
    // Movie audio on the Systems device: one 2D, dry voice of the folded movie track (not a cue: the native movie
    // player owns Bink audio; CINE_MUTE_FOR_BINK ducks the game categories around it).
    int playMovieAudio(const std::string& wav) override {
        movieSound = device->load(wav);
        if (movieSound == audio::kInvalidSound) return -1;
        audio::VoiceParams p;
        p.volume = 1.0f; p.spatial = 1; p.wet = false; p.priority = 0;
        return device->playVoice(movieSound, p);
    }
    void stopMovieAudio(int v) override {
        device->stopVoice(v);
        if (movieSound != audio::kInvalidSound) { device->release(movieSound); movieSound = audio::kInvalidSound; }
    }
    int playUiSound(const std::string& n) override { return rt.playUiSound(n); }
    bool stopUiSound(const std::string& n, float f) override { return rt.stopUiSound(n, f); }
    void uiLevelStarted(const std::string& l) override { rt.uiLevelStarted(l); }
    void levelChange() override { rt.levelChange(); }
    void tick(float dt) override { rt.tick(dt); }
    void levelEvent(const std::string& t) override { rt.levelEvent(t); }
    void setMoviePlaying(bool p) override { rt.setMoviePlaying(p); }
    void prefetchLevel(const std::string& l) override { rt.prefetchLevel(l); }
};
} // namespace
#endif

namespace core {

namespace {
std::unique_ptr<frontend::IFrontendAudio> g_frontendAudio;
std::unique_ptr<FrontendSceneGL> g_scene;   // the live level under the menus (interim IRenderer presentation)

// Rendering's own bounded load-step callback (agents/rendering IRenderer::setLoadYield) when this tree's IRenderer has
// it: the loading frames then also come from inside loadMapRenderData's steps.
template <class R> void setRendererYield(R* r, bool on) {
    if constexpr (HasLoadYield<R>::value) {
        if (on) r->setLoadYield([] { core::loadYield("Render: load step"); });
        else r->setLoadYield(std::function<void()>());
    } else { (void)r; (void)on; }
}
// Process memory for the cycle soak (Experimental: leaks across frontend <-> match).
ui::GlCensus g_census;   // GL objects created by a match (released on travel away; stopgap, see GlCensus.h)

std::string processMemoryMB() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof pmc))
        return frontend::FlowTrace::num(pmc.PrivateUsage / (1024.0 * 1024.0));
#endif
    return "0";
}
} // namespace

Application::Application() = default;
Application::~Application() = default;

bool Application::wantsFrontendBoot() {
    if (const char* b = std::getenv("WFC_BOOT")) return std::strcmp(b, "match") != 0;
    if (std::getenv("WFC_FRONTEND_SCRIPT") || std::getenv("WFC_FRONTEND_AUTOPLAY")) return true;
    // Existing automation and playtest variables keep the direct-to-match boot they were written for.
    for (const char* v : {"WFC_SMOKE_FRAMES", "WFC_SHOTLIST", "WFC_GAMEMODE", "WFC_MAP", "WFC_PICKUPTEST", "WFC_TRAVERSE",
                          "WFC_MAPTRAVERSE", "WFC_LOCKSTEP", "WFC_DEBUGCAM", "WFC_STARTVEHICLE",
                          // [integration M05] Gameplay / Rendering harnesses written against the direct boot
                          "WFC_XFORMTEST", "WFC_MATCHTEST", "WFC_CAMTEST", "WFC_CHAOS", "WFC_TDMTEST", "WFC_MATCH",
                          "WFC_MATCH_URL", "WFC_RELOADTEST"})
        if (std::getenv(v)) return false;
    return true;
}

void Application::attachPresenter() {
    // The shipped GFx movies (WFC_FRONTEND_NOGFX=1: flow only, no presentation).
    if (std::getenv("WFC_FRONTEND_NOGFX")) return;
    auto p = std::make_unique<ui::GfxPresenter>(*frontend_);
    if (!p->init()) { LOG_WARN("frontend: GFx movies unavailable (no frontend_gfx.json); flow only"); return; }
    presenter_ = p.get();
    frontend_->setPresenter(std::move(p));
    frontend_->setMoviePlayerFactory([] { return platform::createMoviePlayer(); });
    // PCSettings -> the platform window; the saved display settings apply at boot.
    frontend::FrontendRuntime::DisplayHooks dh;
    dh.modes = [this] {
        std::vector<std::pair<int, int>> v;
        for (const auto& m : window_->displayModes()) v.push_back({m.width, m.height});
        return v;
    };
    dh.apply = [this](int w, int h, bool fs) { window_->setDisplayMode(w, h, fs); };
    dh.vsync = [this](bool on) { window_->setVSync(on); };
    frontend_->setDisplayHooks(dh);
    {
        const auto& d = frontend_->flow().profile().display;
        if (d.fullscreen || d.width != window_->width() || d.height != window_->height()) window_->setDisplayMode(d.width, d.height, d.fullscreen);
        window_->setVSync(d.vsync);
    }
    // Profile settings -> their runtime owners. No owner API exists yet for the volumes (Systems), the camera
    // sensitivity / invert-Y (Gameplay), vibration, subtitles or gamma (Rendering): the values are stored, persisted
    // and reported here so the owners can consume LocalProfile when they add the entry points.
    frontend_->flow().profile().onApplied = [](const frontend::LocalProfile& p) {
        frontend::FlowTrace::emit("profile.apply", {{"FXVolume", p.get("FX Volume")}, {"DialogueVolume", p.get("Dialogue Volume")},
                                                    {"MusicVolume", p.get("Music Volume")}, {"CameraSensitivity", p.get("CameraSensitivity")},
                                                    {"InvertY_Robot", p.get("InvertY_Robot")}, {"Vibration", p.get("Controller Vibration")},
                                                    {"owners", "pending: Systems volumes, Gameplay camera, Rendering gamma"}});
    };
    if (!std::getenv("WFC_NO_FRONTEND_SCENE")) {
        g_scene = std::make_unique<FrontendSceneGL>(renderer_);
        frontend_->setSceneRenderer(g_scene.get());
    }
    // Logical UI bindings: defaults (the console presentation also binds Space to Start) + wfc_input.ini overrides.
    platform::UiBindings b = platform::UiBindings::defaults(!frontend_->isPC());
    bool ini = b.loadIni("wfc_input.ini");
    window_->setUiBindings(b);
    frontend::FlowTrace::emit("input.bindings", {{"sku", frontend_->platform()}, {"ini", frontend::FlowTrace::boolean(ini)}});
#ifdef WFC_SYSTEMS_FRONTEND_AUDIO
    if (audio_) { g_frontendAudio = std::make_unique<SystemsFrontendAudio>(audio_); frontend_->setAudio(g_frontendAudio.get()); }
#endif
    frontend_->script().keyHook = [this](int code, bool down) { if (presenter_) presenter_->injectKey(code, down); };
    frontend_->script().shotHook = [this](const std::string& f) { pendingShot_ = f; };
    frontend_->script().clipHook = [this](const std::string& path, int& x, int& y) {
        return presenter_ && presenter_->clipWindowCenter(path, x, y);
    };
    frontend_->script().dumpHook = [this](const std::string& m) {
        for (const std::string& o : presenter_->openMovieObjects())
            if (o.find(m) != std::string::npos) LOG_INFO("GFX DUMP %s\n%s", o.c_str(), presenter_->dumpMovie(o).c_str());
    };
}

void Application::shutdownFrontend() {
    if (frontend_) { frontend_->setAudio(nullptr); frontend_->setSceneRenderer(nullptr); }
    g_scene.reset();
    g_frontendAudio.reset();
    presenter_ = nullptr;
    frontend_.reset();
}

void Application::drawFrontendFrame() {
    window_->setOsCursorHidden(presenter_ && presenter_->drawsCursor());
    ui::beginScreenFrame(window_->width(), window_->height());
    frontend_->draw(window_->width(), window_->height());
    if (!pendingShot_.empty()) { renderer_->captureScreenshot(pendingShot_.c_str()); pendingShot_.clear(); }
    window_->present();
}

void Application::runFrontend() {
    frontend::GameFlow& flow = frontend_->flow();
    double last = nowSeconds();
    double started = last;
    double titleTimer = 0.0;
    const double timeout = std::getenv("WFC_FLOW_TIMEOUT") ? std::atof(std::getenv("WFC_FLOW_TIMEOUT")) : 0.0;
    const bool lockstep = std::getenv("WFC_LOCKSTEP") != nullptr;
    platform::InputFrame input;
    for (;;) {
        // ---- frontend levels (and the loading screen up to the match load) ----
        bool quit = false;
        for (;;) {
            if (!window_->pump(input)) { quit = true; break; }
            double now = nowSeconds();
            double dt = now - last;
            last = now;
            if (dt > 0.25) dt = 0.25;
            if (lockstep) dt = 1.0 / 60.0;
            frontend_->update(input, (float)dt);
            if (flow.quitRequested()) { quit = true; break; }
            drawFrontendFrame();
            titleTimer += dt;
            if (titleTimer > 0.25) { titleTimer = 0.0; window_->setTitle(frontend_->titleText().c_str()); }
            // The loading movie plays its intro (LoadScreen_GFX "spinIn" -> "loopStart": 34 frames at 30 fps) before the
            // blocking world load starts (the load is not threaded yet: PARTIAL).
            if (flow.hasPendingMatch() && (!presenter_ || !presenter_->hasLoadingMovie() || presenter_->loadingSeconds() >= 34.0f / 30.0f)) break;
            if (timeout > 0.0 && now - started > timeout) {
                frontend::FlowTrace::emit("timeout", {{"seconds", frontend::FlowTrace::num(now - started)}});
                flow.traceSnapshot("timeout");
                quit = true;
                break;
            }
        }
        if (quit) return;

        // ---- match: ServerTravelToMap(match URL). The loading movie stays up (last frame) during the blocking load.
        if (!loadMatch(flow.pendingMatch())) {
            flow.matchLoadFailed("world load failed");
            unloadMatch();
            flow.worldUnloaded();
            continue;
        }
        flow.matchLoaded();
        // [integration] Frontend's PROVISIONAL adapter (immediate BeginGame) is replaced by Gameplay's match lifecycle.
        // World::launchMatch put the match in PendingMatch (10 s). Client order (RE M05 blockers D5): WaitingOnGameStart ->
        // character select -> PreGameCountdown during PendingMatch -> UI event 3 at InProgress. No character select screen
        // exists yet, so the default character is selected now (OnCharacterSelected, bMatchHasBegun false -> GameStartUI);
        // routeMatchToFrontend() sends UI event 3 when Gameplay's MatchStarted arrives.
        // WaitingOnGameStart opens CustomTransformers_GFX ("Choose Character"); the player's Customize.SelectCharacter
        // continues to the pre-game screen [RE MILESTONE05_PLAYTEST_RE section 7, CONFIRMED]. Automation (scripted
        // frontend runs, the lifecycle driver) selects the first default character instead unless WFC_CHARSELECT=1.
        bool automated = std::getenv("WFC_FRONTEND_SCRIPT") || std::getenv("WFC_FRONTEND_AUTOPLAY") || std::getenv("WFC_LIFECYCLE");
        if (automated && !std::getenv("WFC_CHARSELECT")) {
            frontend::GameFlow::SelectedCharacter sc;
            if (!frontend_->roster().customCharacters().empty()) {
                const auto& p = frontend_->roster().customCharacters().front();
                sc.name = p.name; sc.specialty = p.specialty; sc.chassis[0] = p.chassis[0]; sc.chassis[1] = p.chassis[1];
            }
            flow.selectCharacter(sc);
        }
        localDeadForUi_ = spectatingUi_ = false;
        localDeadTime_ = 0.0f;
        window_->setMouseCaptured(true);
        mouseCaptured_ = true;

        MatchExit e = runMatch();
        if (e == MatchExit::Quit) return;
        unloadMatch();
        flow.worldUnloaded();
        last = nowSeconds();
    }
}

bool Application::loadMatch(const frontend::MatchLaunch& m) {
    if (!m.map) return false;
    world_.setMapName(m.map->runtimeDir);
    bool modeOk = false;
    for (game::MatchMode mm : {game::MatchMode::DM, game::MatchMode::TDM, game::MatchMode::CTF, game::MatchMode::KOTH,
                               game::MatchMode::EXT, game::MatchMode::DOM})
        if (m.modeTag == game::gameModeName(mm)) { world_.setMatchMode(mm); modeOk = true; }
    if (!modeOk) { LOG_WARN("FLOW match mode %s is not supported by Gameplay", m.modeTag.c_str()); return false; }
    LOG_INFO("FLOW loading match world: map dir %s, mode %s", m.map->runtimeDir.c_str(), m.modeTag.c_str());
    double t0 = nowSeconds();
    if (g_scene) {   // the frontend scene's render data and GL objects go before the match map loads
        ui::GlCensus::Owned keep;
        if (presenter_) presenter_->ownedGl(keep);
        frontend::FlowTrace::emit("scene.release", {{"released", g_scene->release(keep)}});
    }
    g_census.begin();
    // The loading screen keeps presenting during the synchronous load (core::loadYield): window messages, the
    // LoadScreen_GFX animation and the TF_LoadingScreen underlay, one frame per yield.
    core::resetLoadYieldStats();
    core::setLoadYield([this](double dt) {
        platform::InputFrame in;
        window_->pump(in);   // a close request stays pending and ends the frontend loop after the load
        frontend_->updateLoading((float)std::min(dt, 0.1));
        // Validation: WFC_LOADSHOTS=<prefix> captures loading frames 10 / 60 / 120 from inside the load.
        static const char* shots = std::getenv("WFC_LOADSHOTS");
        static int n = 0;
        if (shots && (++n == 10 || n == 60 || n == 120)) pendingShot_ = std::string(shots) + std::to_string(n) + ".bmp";
        drawFrontendFrame();
    });
    struct YieldGuard {
        render::IRenderer* r;
        ~YieldGuard() { core::setLoadYield(nullptr); setRendererYield(r, false); }
    } yieldGuard{renderer_};   // also on the failure returns
    setRendererYield(renderer_, true);
    world_.load(*renderer_);
    core::loadYield("Application.loadMatch: world loaded");
    if (!world_.usingSlice()) { LOG_WARN("FLOW match world failed to load (graybox fallback)"); return false; }
#ifdef WFC_SYSTEMS_FRONTEND_AUDIO
    // Systems level-audio contract: no hard-wired slice audio; the selected map's audio.
    world_.setAudio(audio_, false);
    {   // [integration] soak evidence: the frontend audio baseline before the map audio loads
        const auto as = world_.audioState();
        frontend::FlowTrace::emit("audio.baseline", {{"instances", std::to_string(as.instances)}, {"voices", std::to_string(as.voices)},
                                                      {"levelCues", std::to_string(as.levelCues)}, {"pcmMB", frontend::FlowTrace::num(as.pcmMB)}});
    }
    world_.loadMapAudio(m.map->runtimeDir);
    core::loadYield("Application.loadMatch: map audio loaded");
    {   // [integration] soak evidence: Systems audio state with the selected map loaded
        const auto as = world_.audioState();
        frontend::FlowTrace::emit("audio.loaded", {{"level", as.level}, {"instances", std::to_string(as.instances)}, {"voices", std::to_string(as.voices)},
                                                    {"levelCues", std::to_string(as.levelCues)}, {"pcmMB", frontend::FlowTrace::num(as.pcmMB)}});
    }
#else
    world_.setAudio(audio_);
#endif
    // [integration] Gameplay owns the match: the frontend's StartLevel URL is the launch contract (Gameplay PASS 20b,
    // RE M05 blockers D1-D3: GameModeTag / PointsToWin / TimeLimit passed explicitly). The map is the catalog
    // selection's runtime directory (World loaded it above); teams are Gameplay's PickTeam (RE E4, offline).
    game::MatchLaunch gl;
    game::MatchLaunch::fromURL(m.url.toString(), gl);
    gl.map = m.map->runtimeDir;
    // TEST / VALIDATION ONLY (explicit opt-in): WFC_LIFECYCLE=<goal score> shortens the match to that score and adds
    // one Gameplay diagnostic opponent (MatchOpponent); driveLifecycleTest() then produces kills and deaths through
    // World::applyMatchDamage, so the real rules run scoring, death, respawn wave, score-limit end and the return.
    lifecycleGoal_ = 0;
    if (const char* lc = std::getenv("WFC_LIFECYCLE")) {
        lifecycleGoal_ = std::max(1, std::atoi(lc));
        gl.settings.goalScore = lifecycleGoal_;
        frontend::FlowTrace::emit("test.lifecycle", {{"goalScore", std::to_string(lifecycleGoal_)}, {"provenance", "TEST ONLY"}});
    }
    if (!world_.launchMatch(gl)) { LOG_WARN("FLOW Gameplay refused the match (%s %s)", gl.map.c_str(), gl.modeTag.c_str()); return false; }
    if (lifecycleGoal_ > 0) world_.addMatchOpponent("LifecycleOpponent", true);
    lifecycleT_ = 0.0f; lifecycleStep_ = 0;
    frontend::FlowTrace::emit("match.gameplay", {{"map", gl.map}, {"mode", gl.modeTag}, {"goalScore", std::to_string(gl.settings.goalScore)},
                                                 {"timeLimit", std::to_string(gl.settings.timeLimit)}});
    {   // Experimental RUNTIME-EVENTS protocol (MATCH lines), from Gameplay's launch settings and the authored rule list
        std::string rules;
        if (m.settings) for (const std::string& r : m.settings->rules) rules += (rules.empty() ? "" : ",") + r;
        if (matchesLaunched_++ > 0) LOG_INFO("MATCH restart");
        LOG_INFO("MATCH init mode=%s rules=%s teams=%d time_limit_s=%d score_limit=%d", gl.modeTag.c_str(), rules.c_str(),
                 gl.settings.teamGame ? 2 : 0, gl.settings.timeLimit, gl.settings.goalScore);
        matchClock_ = 0.0f; lastLoggedRemaining_ = -1; deathAt_.clear();
    }
    gameMode_.begin(world_);
    core::setLoadYield(nullptr);
    const core::LoadYieldStats ys = core::loadYieldStats();
    frontend::FlowTrace::emit("match.loaded", {{"map", m.map->runtimeDir}, {"mode", m.modeTag},
                                               {"seconds", frontend::FlowTrace::num(nowSeconds() - t0)}, {"privateMB", processMemoryMB()},
                                               {"loadingFrames", std::to_string(ys.frames)},
                                               {"maxFrameGapMs", frontend::FlowTrace::num(ys.maxGapMs)}, {"maxGapAt", ys.maxGapAt}});
    return true;
}

void Application::routeMatchToFrontend(float dt) {
    frontend::GameFlow& flow = frontend_->flow();
    const int me = world_.localMatchPlayer();
    const game::Match& match = world_.match();
    matchClock_ += dt;
    auto teamOf = [&](int p) { return (p >= 0 && (size_t)p < match.players().size()) ? (match.players()[(size_t)p].team == 255 ? -1 : match.players()[(size_t)p].team) : -1; };
    auto posOf = [&](int p) {
        if (p == me) return world_.player().pawn().position();
        for (const game::MatchOpponent* o : world_.matchOpponents()) if (o->matchPlayer() == p) return o->position();
        return core::Vec3{0, 0, 0};
    };
    for (const game::MatchEvent& e : world_.matchEvents()) {
        switch (e.type) {
        case game::MatchEvent::Type::MatchStarted:
            // InProgress.BeginState -> SendUIEventToControllers(3) (the character was selected at load, in PendingMatch).
            flow.onUIEvent((int)frontend::UIEvent::BeginGame);
            // TnGameTypeMessage switch 0: HUD GameAnnouncement with the mode name [RE A5, CONFIRMED].
            frontend_->hud().announce(frontend_->catalog().modeFriendlyName(match.settings().modeTag));
            frontend::FlowTrace::emit("match.started", {});
            break;
        case game::MatchEvent::Type::PlayerKilled:
            frontend::FlowTrace::emit("match.kill", {{"victim", std::to_string(e.player)}, {"killer", std::to_string(e.other)}, {"how", e.text}});
            {   // RUNTIME-EVENTS: kill (score already applied by Gameplay), team / player score, death
                const core::Vec3 dp = posOf(e.player);
                LOG_INFO("MATCH kill killer=%d victim=%d killer_team=%d victim_team=%d weapon=%s", e.other, e.player, teamOf(e.other),
                         teamOf(e.player), e.text.empty() ? "unknown" : e.text.c_str());
                if (e.other >= 0 && e.other != e.player) {
                    if (teamOf(e.other) >= 0) LOG_INFO("MATCH score team=%d score=%d", teamOf(e.other), match.teamScore(teamOf(e.other)));
                    else LOG_INFO("MATCH score team=-1 player=%d score=%d", e.other, match.players()[(size_t)e.other].score);
                }
                LOG_INFO("MATCH death player=%d pos=%.1f,%.1f,%.1f", e.player, dp.x, dp.y, dp.z);
                deathAt_[e.player] = matchClock_;
            }
            {   // HUD kill feed (TnDeathMessage -> _global.GameMessage). The damage type is not in Gameplay's event yet:
                // the base [TnDamageType] template is used [PARTIAL, Gameplay handoff].
                frontend::HudKill k;
                auto nameOf = [&](int p) { return p >= 0 && (size_t)p < match.players().size() ? match.players()[(size_t)p].name : std::string(); };
                k.victim = nameOf(e.player); k.killer = nameOf(e.other);
                k.victimTeam = teamOf(e.player); k.killerTeam = teamOf(e.other);
                k.victimLocal = e.player == me; k.killerLocal = e.other == me;
                k.suicide = e.text == "suicide" || e.other == e.player || e.other < 0;
                k.environment = e.text == "environment";
                frontend_->hud().addKill(k, teamOf(me));
            }
            if (e.player == me) { localDeadForUi_ = true; spectatingUi_ = false; localDeadTime_ = 0.0f; }
            break;
        case game::MatchEvent::Type::PlayerSpawned:
            frontend::FlowTrace::emit("match.spawn", {{"player", std::to_string(e.player)}, {"start", e.text}});
            {   // RUNTIME-EVENTS: spawn (start = the PlayerStart Gameplay chose), respawn with the delay since death
                const core::Vec3 sp = (e.value >= 0 && (size_t)e.value < match.starts().size()) ? match.starts()[(size_t)e.value].pos : posOf(e.player);
                LOG_INFO("MATCH spawn player=%d team=%d start=%s pos=%.1f,%.1f,%.1f", e.player, teamOf(e.player), e.text.c_str(), sp.x, sp.y, sp.z);
                auto d = deathAt_.find(e.player);
                if (d != deathAt_.end()) { LOG_INFO("MATCH respawn player=%d start=%s delay_s=%.2f", e.player, e.text.c_str(), matchClock_ - d->second); deathAt_.erase(d); }
            }
            if (e.player == me && localDeadForUi_) {
                // RestartPlayer leaves spectating -> UI event 5 (RE E7.4).
                flow.onUIEvent((int)frontend::UIEvent::Respawn);
                localDeadForUi_ = spectatingUi_ = false;
            }
            break;
        case game::MatchEvent::Type::MatchEnded:
            frontend::FlowTrace::emit("match.ended", {{"winner", std::to_string(e.value)}, {"reason", e.text}});
            {   // RUNTIME-EVENTS: end (Gameplay's EndGame reason / winner; -1 = tie)
                const char* reason = e.text == "Score" ? "score_limit" : (e.text.find("ime") != std::string::npos ? "time_limit" : "other");
                const std::string winner = e.value >= 0 ? std::to_string(e.value) : std::string("draw");
                LOG_INFO("MATCH end reason=%s winner=%s t=%d (gameplay reason %s)", reason, winner.c_str(), match.elapsedTime(), e.text.c_str());
            }
            flow.onUIEvent((int)frontend::UIEvent::EndGame);   // MatchOver: HUD hidden, EndGameStats (RE F4)
            break;
        case game::MatchEvent::Type::ReturnToLobby:
            frontend::FlowTrace::emit("match.return", {});
            flow.returnToGameLobby();                           // MatchOver + 15 s -> ReturnToGameLobby (RE F4 / F6)
            break;
        default: break;
        }
    }
    // RUNTIME-EVENTS: timer, once per change of GRI.RemainingTime while the match runs
    if (match.state() == game::Match::State::InProgress && match.remainingTime() != lastLoggedRemaining_) {
        lastLoggedRemaining_ = match.remainingTime();
        LOG_INFO("MATCH timer remaining_s=%d", lastLoggedRemaining_);
    }
    // Dead: after MinRespawnDelay 3.0 s the controller enters PlayerSpectating -> UI event 4 (RE E7.3, CONFIRMED).
    if (localDeadForUi_ && !spectatingUi_) {
        localDeadTime_ += dt;
        if (localDeadTime_ >= 3.0f) { flow.onUIEvent((int)frontend::UIEvent::Spectating); spectatingUi_ = true; }
    }
    // <CurrentGame:*> / <PlayerOwner:*> match values for the in-match movies (Gameplay authoritative).
    const game::HudGameState h = world_.hudState();
    frontend::MatchValues v;
    v.valid = h.matchActive;
    v.pending = h.matchState == (int)game::Match::State::PendingMatch;
    v.countingDown = v.pending || (h.matchState == (int)game::Match::State::InProgress && h.remainingTime > 0);
    v.countdown = v.pending ? h.countdown : h.remainingTime;
    v.goalScore = h.goalScore;
    v.teamScore[0] = h.teamScore[0]; v.teamScore[1] = h.teamScore[1];
    v.myTeam = h.myTeam;
    v.score = h.score; v.kills = h.kills; v.deaths = h.deaths;
    v.dead = !h.alive;
    v.timeToRespawn = h.timeToRespawn;
    v.gameOverMessage = h.result;
    flow.setMatchValues(v);
    // HUD movie values (TnHUD data observers), Gameplay authoritative.
    frontend::HudFrame hf;
    hf.valid = h.matchActive;
    hf.alive = h.alive;
    hf.totalSegments = h.segmentCount;
    if (h.activeSegment >= h.segmentCount) { hf.fullSegments = h.segmentCount; hf.currentSegment = 1.0; }
    else {
        const auto& hp = world_.player().pawn().health();
        float bottom = h.activeSegment > 0 ? hp.segmentTop(h.activeSegment - 1) : 0.0f, top = hp.segmentTop(h.activeSegment);
        hf.fullSegments = h.activeSegment;
        hf.currentSegment = top > bottom ? std::max(0.0f, std::min(1.0f, (h.health - bottom) / (top - bottom))) : 0.0;
    }
    hf.overshield = h.normalizedOverShield;
    const auto& wpn = world_.player().pawn().weapon();
    hf.clip = h.clipAmmo; hf.clipCapacity = wpn.magSize; hf.reserve = h.reserveAmmo; hf.reserveCapacity = wpn.reserveMax;
    hf.weapon = "IonBlaster";   // the rebuild's only player weapon (Gameplay) - TnWeaponIonBlaster icon / crosshair
    hf.vehicleForm = h.vehicleForm;
    hf.spectating = spectatingUi_;
    frontend_->hud().setFrame(hf);
}

void Application::driveLifecycleTest(float dt) {
    // TEST ONLY (WFC_LIFECYCLE): every 2.5 s of InProgress, alternately the local player kills the opponent (+1 player
    // and team score) and the opponent kills the local player (death -> spectating -> 5 s wave respawn). Waits while
    // either side is dead. Uses only Gameplay's match API; no rule is reimplemented here.
    if (lifecycleGoal_ <= 0 || world_.match().state() != game::Match::State::InProgress || world_.matchOpponents().empty()) return;
    if ((lifecycleT_ += dt) < 2.5f) return;
    game::MatchOpponent* opp = world_.matchOpponents()[0];
    const int me = world_.localMatchPlayer();
    if (world_.localPlayerDead() || !opp->spawned()) return;
    lifecycleT_ = 0.0f;
    const bool killOpponent = (lifecycleStep_++ % 2) == 0;
    if (killOpponent) world_.applyMatchDamage(opp->matchPlayer(), me, 100000.0f, false);
    else world_.applyMatchDamage(me, opp->matchPlayer(), 100000.0f, false);
    frontend::FlowTrace::emit("test.lifecycle.damage", {{"victim", killOpponent ? "opponent" : "local"}});
}

void Application::unloadMatch() {
    // Travel replaces the world. The match world and its audio voices are released by recreating them; the map's
    // render resources by Rendering's IRenderer::unloadMapRenderData (docs/RENDERER_CONTRACT.md) [integration].
    // GlCensus then releases what remains outside the map data (World uploadMesh meshes, effect textures); names the
    // renderer already freed are ignored by glDelete* and nothing is created in between.
#ifdef WFC_SYSTEMS_FRONTEND_AUDIO
    world_.unloadMapAudio();   // no voice / instance / map cue / sample remains (Systems guarantee)
    {   // [integration] soak evidence: Systems audio state after the map audio is released (expect the frontend baseline)
        const auto as = world_.audioState();
        frontend::FlowTrace::emit("audio.unloaded", {{"level", as.level}, {"instances", std::to_string(as.instances)}, {"voices", std::to_string(as.voices)},
                                                      {"levelCues", std::to_string(as.levelCues)}, {"pcmMB", frontend::FlowTrace::num(as.pcmMB)}});
    }
#endif
    world_.~World();
    new (&world_) game::World();
    frontend_->flow().setMatchValues(frontend::MatchValues{});   // no stale match values in the lobby / frontend
    LOG_INFO("MATCH cleanup");                                   // RUNTIME-EVENTS: the match world is gone
    renderer_->unloadMapRenderData();
    ui::GlCensus::Owned keep;
    if (presenter_) presenter_->ownedGl(keep);
    if (!std::getenv("WFC_NO_GL_RELEASE")) frontend::FlowTrace::emit("match.glRelease", {{"released", g_census.release(keep)}});
    gameMode_ = game::GameMode();
#ifndef WFC_SYSTEMS_FRONTEND_AUDIO
    // Without the Systems lifecycle the device is recreated to drop the match's voices.
    delete audio_;
    audio_ = audio::createAudio();
#endif
    delete renderer_;
    renderer_ = render::createGLRenderer();
    if (g_scene) g_scene->setRenderer(renderer_);
    camera_ = render::Camera();
    clock_ = FixedStepClock(60.0);
    escWasDown_ = false;
    window_->setMouseCaptured(false);
    mouseCaptured_ = false;
    frontend::FlowTrace::emit("match.unloaded", {{"privateMB", processMemoryMB()}});
}

} // namespace core
