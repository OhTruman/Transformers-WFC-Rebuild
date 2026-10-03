// Clean-room reconstruction — application lifecycle + main loop.
#pragma once
#include "core/Time.h"
#include "render/Camera.h"
#include "game/World.h"
#include "game/GameMode.h"
#include "audio/Audio.h"
#include <memory>

namespace platform { class IWindow; }
namespace render { class IRenderer; }
namespace frontend { class FrontendRuntime; struct MatchLaunch; }
namespace ui { class GfxPresenter; }

namespace core {

class Application {
public:
    Application();
    ~Application();
    bool init();
    void run();
    void shutdown();

private:
    // Frontend boot (docs/FRONTEND.md): boot -> UI_FrontEnd_m -> lobbies -> match -> return. Legacy direct-to-match
    // boot is kept for automation (WFC_BOOT=match, or any of the existing test variables).
    static bool wantsFrontendBoot();
    void runFrontend();
    enum class MatchExit { Quit, ReturnToFrontend };
    MatchExit runMatch();
    bool loadMatch(const frontend::MatchLaunch& m);
    void unloadMatch();
    void drawFrontendFrame();
    void attachPresenter();
    std::unique_ptr<frontend::FrontendRuntime> frontend_;
    bool escWasDown_ = false;
    ui::GfxPresenter* presenter_ = nullptr;   // owned by frontend_
    std::string pendingShot_;

    void updateTitleHud(double realDt);
    void runPickupTest();
    void runTraverseTest();
    void runMapTraverse();

    platform::IWindow* window_ = nullptr;
    render::IRenderer* renderer_ = nullptr;
    audio::IAudio* audio_ = nullptr;
    game::World world_;
    game::GameMode gameMode_;
    render::Camera camera_;
    FixedStepClock clock_{60.0};

    bool mouseCaptured_ = true;
    double titleTimer_ = 0.0;
    double fpsSmoothed_ = 0.0;
};

} // namespace core
