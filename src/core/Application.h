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
namespace frontend { class FrontendRuntime; struct MatchLaunch; class LocalProfile; }
namespace net { class UdpSession; class NetMatchDriver; class VoiceSession; }
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
    void runHeadlessMatch();   // [Systems] WFC_NULLRENDER + direct match boot: the sim loop without a window
    void runNetMatch();        // [Systems] WFC_NET_SLOT: the headless match over the lockstep layer (Application_Net.cpp)
    static bool netMatchRequested();
    void runVoiceFrameTest();   // [Systems] WFC_VOICEFRAMETEST: the voice framer on synthetic input (no device)
    void runUdpSessionTest();   // [Systems] WFC_UDPSESSIONTEST: two UdpSessions in this process over loopback UDP (host / join / lobby / start / load gate / leave)
    void runCampaignAudioTest();   // [Systems] WFC_CAMPAIGNAUDIOTEST=<chapter audio JSON>: the campaign Kismet audio ops, headless
    void runUdpSessionMatch();  // [Systems] WFC_UDPHOST / WFC_UDPJOIN: a headless lockstep match through UdpSession + NetMatchDriver (identity rows)
    void runOnlineFlowTest();   // [Frontend] WFC_ONLINEFLOWTEST: two frontend flows, each on a real UdpSession, over loopback UDP
    void runFlowSimTest();     // WFC_FLOWSIMTEST=<MP|ESC|CP>: the playtest section 43 flows' sim half (solo launch -> play -> QA)
    void runSoloLaunchTest();   // [Systems] WFC_SOLOLAUNCHTEST: a private match hosted alone takes the local loop (no driver); same seed = same hash
    void runVoiceTest();
    void runImageCompareTest();  // [Systems] WFC_IMAGECMPTEST: every PNG under WFC_IMAGECMP_ROOTS decoded by GDI+ and WIC, compared per class        // [Systems] WFC_VOICETEST: the voice lane end to end over loopback (SNR, loss, mute, indicator)
    int exitCode_ = -1;        // [Systems] set by the headless / net runs (main returns it when init stops); -1 = the default
public:
    int exitCode() const { return exitCode_; }
private:
    bool loadMatch(const frontend::MatchLaunch& m);
    void unloadMatch();
    void drawFrontendFrame();
    void attachPresenter();
    void applyRendererSettings(const frontend::LocalProfile& p, const char* when, bool force);   // boot / Apply / a recreated renderer
    static void applyStartupGraphics();   // before the renderer exists: settings it can only take at start-up (frame generation)
    void applyBootProfile();              // WFC_BOOT_PROFILE=1: the direct boot applies the profile's display / graphics rows (harness)
    void shutdownFrontend();
    std::unique_ptr<frontend::FrontendRuntime> frontend_;
    // [Systems, online phase 2] The real session (created at boot for the frontend, listening only after host() / join()) and the
    // online match driver (from the online launch until the match ends; it steps the World instead of the fixed-step loop).
    net::UdpSession* udpSession_ = nullptr;   // owned by the frontend flow (setSession)
    std::unique_ptr<net::NetMatchDriver> netDriver_;
    std::unique_ptr<net::VoiceSession> lobbyVoice_;   // [Systems] the lobby voice lane (the match lane is the driver's)
    void tickLobbyVoice();
    void bindMicMeter();                       // [Systems] the Mic Test meter <- the running voice lane's microphone level
    const void* micMeterFor_ = nullptr;        // the session the meter source was bound to
    void startNetDriver(const std::string& launchUrl, uint32_t seed, int seats, int localSeat);
    void tickNetDriver(double realDt);
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
    uint32_t selectionSentSerial_ = 0;   // GameFlow::selectionSerial() last forwarded to Gameplay (0 = none this match)
    // DEBUG-ONLY QA panel (WFC_QA=1, F10; NOT ORIGINAL): see qaTick in Application_Frontend.cpp.
    int appliedFrameLimit_ = -1;   // [PCSettings] FrameLimit last handed to the limiter
    // [Systems, M5 item 10] the displayed-frame target (FrameLimit / WFC_FPS_LIMIT) and the frame-generation factor at the
    // last check (frames on screen per rendered frame): the limiter gets target / factor (the rendered rate)
    int pacingHz_ = 0;
    int pacingFg_ = 1;
    void pacingCheckFrameGeneration();
    int appliedUpscaling_ = -1;    // PC EXTENSION [PCSettings] Upscaling / HDTextures last handed to the renderer
    int appliedHdTextures_ = 0;
    int appliedAnisotropy_ = -1;
    int appliedRayTracing_ = -1;   // PC EXTENSION [PCSettings] RayTracing last handed to the renderer
    int appliedVolumetric_ = -1;   // PC ADAPTATION [PCSettings] VolumetricLighting last handed to the renderer
    int appliedLensFlare_ = -1;     // the Lens Flares row last applied to the renderer
    int appliedTextureQuality_ = -1;   // [PCSettings] TextureQuality last applied (Rendering setTextureQuality)
    void qaTick(const platform::InputFrame& in);
    void playtestOverlayTick(const platform::InputFrame& in);
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
    void runLateSeatTest();     // WFC_LATESEATTEST (Application_LateSeatTest.cpp)
    void runKismetTest();       // WFC_KISMETTEST (Application_KismetTest.cpp)
    void runEscalationTest();   // WFC_ESCTEST (Application_EscTest.cpp)
    void runCampaignTest();     // WFC_CAMPAIGNTEST (Application_CampaignTest.cpp)
    void runCameraSyncTest();
    void runModePlayTest();
    void runWeaponTest();
    void runMapSuite();
    void runCtfExtTest();
    void runParticipantTest();
    void runSwitchTest();
    void runScoreTest();
    void runHeightTest();
    void runTipTest();
    void runVehPhysTest();
    void runHeadingJitterTest();
    void runVehicleSocketProbe();
    void runTransformVisibilityTest();
    void runFineAimTest();
    void runQaToolTest();
    void runProjectileFxTest();
    void runMuzzleTest();
    void runRobotMuzzleTest();
    void runFxOwnerTest();
    void runNavQualityTest();
    void runChargeTest();
    void runDropTest();
    void runRiserTest();
    void runPreloadTest();
    void runClassChangeTest();
    void runEventTest();
    void runTransformingActorsTest();
    void runDecoyTest();
    void runSessionTest();
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
    void runEvictTest();
    void runAiDuel();

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
