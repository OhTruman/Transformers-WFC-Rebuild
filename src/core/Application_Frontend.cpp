// Clean-room reconstruction — application side of the frontend boot (docs/FRONTEND.md).
// The flow itself (levels, lobbies, URLs, UI controller) is src/frontend/GameFlow; this file only owns the
// process loop: frontend frames, loading the match world the flow launches, and releasing it on return.
#include "core/Application.h"
#include "core/Log.h"
#include "core/Time.h"
#include "frontend/FlowTrace.h"
#include "frontend/FrontendRuntime.h"
#include "game/MapState.h"
#include "platform/Window.h"
#include "render/Renderer.h"
#include "ui/UiGL.h"

#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

namespace core {

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

void Application::drawFrontendFrame() {
    ui::beginScreenFrame(window_->width(), window_->height());
    frontend_->draw(window_->width(), window_->height());
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
            if (flow.hasPendingMatch()) break;
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
        // PROVISIONAL adapter (handoff to Gameplay): the rebuild has no TnMultiplayerGame PendingMatch / InProgress yet,
        // no character select (CustomTransformers) and no PreGameCountdown data. The original order of UI events is
        // reproduced without their delays: OnCharacterSelected (default character) -> GameStartUI, then
        // InProgress.BeginState -> SendUIEventToControllers(3). Gameplay replaces this with its own lifecycle.
        frontend::FlowTrace::emit("adapter", {{"what", "character selected + match begun (no PendingMatch in Gameplay)"},
                                              {"provenance", "PROVISIONAL"}});
        flow.characterSelected();
        flow.onUIEvent((int)frontend::UIEvent::BeginGame);
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
    world_.load(*renderer_);
    if (!world_.usingSlice()) { LOG_WARN("FLOW match world failed to load (graybox fallback)"); return false; }
    world_.setAudio(audio_);
    gameMode_.begin(world_);
    frontend::FlowTrace::emit("match.loaded", {{"map", m.map->runtimeDir}, {"mode", m.modeTag},
                                               {"seconds", frontend::FlowTrace::num(nowSeconds() - t0)}});
    return true;
}

void Application::unloadMatch() {
    // Travel replaces the world. The match world, its audio voices and the renderer's map data are released by
    // recreating them. PARTIAL: IRenderer has no map-unload entry point, so GL objects of the previous map are not
    // freed (HANDOFF Rendering: unloadMapRenderData / resource release for level travel).
    world_.~World();
    new (&world_) game::World();
    gameMode_ = game::GameMode();
    delete audio_;
    audio_ = audio::createAudio();
    delete renderer_;
    renderer_ = render::createGLRenderer();
    camera_ = render::Camera();
    clock_ = FixedStepClock(60.0);
    escWasDown_ = false;
    window_->setMouseCaptured(false);
    mouseCaptured_ = false;
    frontend::FlowTrace::emit("match.unloaded", {});
}

} // namespace core
