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
    void runChargeTest();
    void runDropTest();
    void runRiserTest();
    void runPreloadTest();
    void runClassChangeTest();
    void runEventTest();
    void runPacingTest();
    void runBotTest();
    void runBotNavTest();
    void runXpTest();
    void runBotObjectiveTest();
    void runExtraBodyTest();
    void runDoubleJumpTest();
    void runSpawnFillTest();
    void runQaBotTest();
    void runDeterminismTest();
    void runWeaponAudit();
    void runVehicleAudit();
    void runVehicleFrameTest();
    void runStuckSpot(const char* spec);
    void runMarkersTest();
    void runEngageTest();
    void runAsyncStepTest();
    void runScaleTest();
    void runRayBench();
    void runBarrierWalkTest();

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
