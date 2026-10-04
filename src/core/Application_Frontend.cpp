// Clean-room reconstruction — application side of the frontend boot (docs/FRONTEND.md).
// The flow itself (levels, lobbies, URLs, UI controller) is src/frontend/GameFlow; this file only owns the
// process loop: frontend frames, loading the match world the flow launches, and releasing it on return.
#include "core/Application.h"
#include "core/Log.h"
#include "core/Time.h"
#include "frontend/FlowTrace.h"
#include "frontend/FrontendRuntime.h"
#include "game/MapState.h"
#include "platform/Movie.h"
#include "platform/Window.h"
#include "render/Renderer.h"
#include "ui/GfxPresenter.h"
#include "ui/UiGL.h"
#include "ui/gl/GlCensus.h"

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
    explicit SystemsFrontendAudio(audio::IAudio* a) : rt(a) {}
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
                          "WFC_MAPTRAVERSE", "WFC_LOCKSTEP", "WFC_DEBUGCAM", "WFC_STARTVEHICLE"})
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
#ifdef WFC_SYSTEMS_FRONTEND_AUDIO
    if (audio_) { g_frontendAudio = std::make_unique<SystemsFrontendAudio>(audio_); frontend_->setAudio(g_frontendAudio.get()); }
#endif
    frontend_->script().keyHook = [this](int code, bool down) { if (presenter_) presenter_->injectKey(code, down); };
    frontend_->script().shotHook = [this](const std::string& f) { pendingShot_ = f; };
    frontend_->script().dumpHook = [this](const std::string& m) {
        for (const std::string& o : presenter_->openMovieObjects())
            if (o.find(m) != std::string::npos) LOG_INFO("GFX DUMP %s\n%s", o.c_str(), presenter_->dumpMovie(o).c_str());
    };
}

void Application::shutdownFrontend() {
    if (frontend_) frontend_->setAudio(nullptr);
    g_frontendAudio.reset();
    presenter_ = nullptr;
    frontend_.reset();
}

void Application::drawFrontendFrame() {
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
        // [integration] Frontend's PROVISIONAL adapter (immediate BeginGame) is replaced by Gameplay's match lifecycle:
        // World::launchMatch put the match in PendingMatch (10 s); routeMatchToFrontend() sends the character-selected
        // notification and UI event 3 when Gameplay's MatchStarted arrives (RE M05 blockers D4 / D5).
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
    g_census.begin();
    world_.load(*renderer_);
    if (!world_.usingSlice()) { LOG_WARN("FLOW match world failed to load (graybox fallback)"); return false; }
#ifdef WFC_SYSTEMS_FRONTEND_AUDIO
    // Systems level-audio contract: no hard-wired slice audio; the selected map's audio.
    world_.setAudio(audio_, false);
    world_.loadMapAudio(m.map->runtimeDir);
#else
    world_.setAudio(audio_);
#endif
    // [integration] Gameplay owns the match: the frontend's StartLevel URL is the launch contract (Gameplay PASS 20b,
    // RE M05 blockers D1-D3: GameModeTag / PointsToWin / TimeLimit passed explicitly). The map is the catalog
    // selection's runtime directory (World loaded it above); teams are Gameplay's PickTeam (RE E4, offline).
    game::MatchLaunch gl;
    game::MatchLaunch::fromURL(m.url.toString(), gl);
    gl.map = m.map->runtimeDir;
    if (!world_.launchMatch(gl)) { LOG_WARN("FLOW Gameplay refused the match (%s %s)", gl.map.c_str(), gl.modeTag.c_str()); return false; }
    frontend::FlowTrace::emit("match.gameplay", {{"map", gl.map}, {"mode", gl.modeTag}, {"goalScore", std::to_string(gl.settings.goalScore)},
                                                 {"timeLimit", std::to_string(gl.settings.timeLimit)}});
    gameMode_.begin(world_);
    frontend::FlowTrace::emit("match.loaded", {{"map", m.map->runtimeDir}, {"mode", m.modeTag},
                                               {"seconds", frontend::FlowTrace::num(nowSeconds() - t0)}, {"privateMB", processMemoryMB()}});
    return true;
}

void Application::routeMatchToFrontend(float dt) {
    frontend::GameFlow& flow = frontend_->flow();
    const int me = world_.localMatchPlayer();
    for (const game::MatchEvent& e : world_.matchEvents()) {
        switch (e.type) {
        case game::MatchEvent::Type::MatchStarted:
            // TnUIControllerMultiplayer: OnCharacterSelected (default character; no character select yet) then
            // InProgress.BeginState -> SendUIEventToControllers(3).
            flow.characterSelected();
            flow.onUIEvent((int)frontend::UIEvent::BeginGame);
            frontend::FlowTrace::emit("match.started", {});
            break;
        case game::MatchEvent::Type::PlayerKilled:
            frontend::FlowTrace::emit("match.kill", {{"victim", std::to_string(e.player)}, {"killer", std::to_string(e.other)}, {"how", e.text}});
            if (e.player == me) { localDeadForUi_ = true; spectatingUi_ = false; localDeadTime_ = 0.0f; }
            break;
        case game::MatchEvent::Type::PlayerSpawned:
            frontend::FlowTrace::emit("match.spawn", {{"player", std::to_string(e.player)}, {"start", e.text}});
            if (e.player == me && localDeadForUi_) {
                // RestartPlayer leaves spectating -> UI event 5 (RE E7.4).
                flow.onUIEvent((int)frontend::UIEvent::Respawn);
                localDeadForUi_ = spectatingUi_ = false;
            }
            break;
        case game::MatchEvent::Type::MatchEnded:
            frontend::FlowTrace::emit("match.ended", {{"winner", std::to_string(e.value)}, {"reason", e.text}});
            flow.onUIEvent((int)frontend::UIEvent::EndGame);   // MatchOver: HUD hidden, EndGameStats (RE F4)
            break;
        case game::MatchEvent::Type::ReturnToLobby:
            frontend::FlowTrace::emit("match.return", {});
            flow.returnToGameLobby();                           // MatchOver + 15 s -> ReturnToGameLobby (RE F4 / F6)
            break;
        default: break;
        }
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
    flow.setMatchValues(v);
}

void Application::unloadMatch() {
    // Travel replaces the world. The match world, its audio voices and the renderer's map data are released by
    // recreating them. PARTIAL: IRenderer has no map-unload entry point, so GL objects of the previous map are not
    // freed (HANDOFF Rendering: unloadMapRenderData / resource release for level travel).
#ifdef WFC_SYSTEMS_FRONTEND_AUDIO
    world_.unloadMapAudio();   // no voice / instance / map cue / sample remains (Systems guarantee)
#endif
    world_.~World();
    new (&world_) game::World();
    frontend_->flow().setMatchValues(frontend::MatchValues{});   // no stale match values in the lobby / frontend
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
    camera_ = render::Camera();
    clock_ = FixedStepClock(60.0);
    escWasDown_ = false;
    window_->setMouseCaptured(false);
    mouseCaptured_ = false;
    frontend::FlowTrace::emit("match.unloaded", {{"privateMB", processMemoryMB()}});
}

} // namespace core
