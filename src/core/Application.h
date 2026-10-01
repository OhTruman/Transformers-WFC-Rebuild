// Clean-room reconstruction — application lifecycle + main loop.
#pragma once
#include "core/Time.h"
#include "render/Camera.h"
#include "game/World.h"
#include "game/GameMode.h"
#include "audio/Audio.h"

namespace platform { class IWindow; }
namespace render { class IRenderer; }

namespace core {

class Application {
public:
    bool init();
    void run();
    void shutdown();

private:
    void updateTitleHud(double realDt);

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
