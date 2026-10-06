// Clean-room reconstruction — application lifecycle + main loop.
#pragma once
#include "core/Time.h"
#include "platform/QaPanel.h"
#include "render/Camera.h"
#include "game/World.h"
#include "game/GameMode.h"
#include "audio/Audio.h"
#include <map>
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
    void shutdownFrontend();
    std::unique_ptr<frontend::FrontendRuntime> frontend_;
    bool escWasDown_ = false;
    bool uiReleasedMouse_ = false;   // a focused movie (pause, end game) released mouse-look
    ui::GfxPresenter* presenter_ = nullptr;   // owned by frontend_
    std::string pendingShot_;
    // [integration] Gameplay's match lifecycle -> the frontend flow (replaces Frontend's PROVISIONAL adapter):
    // load (PendingMatch) -> default character selected; MatchStarted -> UI event 3; local death + MinRespawnDelay 3.0 s -> 4; respawn -> 5;
    // MatchEnded -> 9; ReturnToLobby -> GameFlow::returnToGameLobby. Also pushes the <CurrentGame:*> values.
    void routeMatchToFrontend(float dt);
    bool localDeadForUi_ = false, spectatingUi_ = false;
    float localDeadTime_ = 0.0f;
    void driveLifecycleTest(float dt);   // TEST ONLY: WFC_LIFECYCLE=<goal score> (see Application_Frontend.cpp)
    int lifecycleGoal_ = 0, lifecycleStep_ = 0;
    float lifecycleT_ = 0.0f;
    // Experimental RUNTIME-EVENTS MATCH protocol state (Application_Frontend.cpp)
    int matchesLaunched_ = 0, lastLoggedRemaining_ = -1;
    float matchClock_ = 0.0f;
    std::map<int, float> deathAt_;
    bool selectionSent_ = false;   // the frontend's character selection reached Gameplay this match
    // DEBUG-ONLY QA panel (WFC_QA=1, F10; NOT ORIGINAL): see qaTick in Application_Frontend.cpp.
    void qaTick(const platform::InputFrame& in);
    std::unique_ptr<platform::QaPanel> qa_;
    platform::QaRequest qaLast_;
    std::string qaCharacter_;

    std::vector<std::pair<int, core::Vec3>> orbit_;   // WFC_FXTEST "~" sources (handle, centre)
    void updateTitleHud(double realDt);
    void runPickupTest();
    void runTraverseTest();
    void runMapTraverse();
    void runTransformStress();
    void runMatchTest();
    void runCameraTest();
    void runChaosTest();
    void runTdmSessionTest();
    void runCameraSyncTest();
    void runModePlayTest();
    void runWeaponTest();
    void runMapSuite();
    void runCtfExtTest();
    void runParticipantTest();
    void runSwitchTest();
    void runScoreTest();
    void runHeightTest();
    void runVehPhysTest();
    void runHeadingJitterTest();
    void runVehicleSocketProbe();
    void runTransformVisibilityTest();
    void runFineAimTest();
    void runQaToolTest();
    void runProjectileFxTest();
    void runMuzzleTest();
    void runRobotMuzzleTest();

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
