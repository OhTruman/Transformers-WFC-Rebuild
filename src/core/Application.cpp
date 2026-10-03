#include "core/Application.h"
#include "core/Config.h"
#include "core/Debug.h"
#include "core/Log.h"
#include "platform/Window.h"
#include "render/Renderer.h"
#include "game/VehicleTests.h"
#include "frontend/FrontendRuntime.h"
#include "game/MapState.h"
#include "game/Collision.h"
#include "game/PickupFactory.h"
#include "game/Destructible.h"
#include "assets/Gltf.h"
#include "assets/Json.h"

#include <algorithm>
#include <string>

#include <chrono>

#include <cmath>
#include <cstdio>
#include <vector>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sstream>

namespace core {

bool Application::init() {
    LOG_INFO("WFC Rebuild starting (clean-room skeleton)");
    if (std::getenv("WFC_VEHTEST")) { game::runVehicleTests(); return false; }   // measurements only
    if (std::getenv("WFC_MODETEST")) {   // Streets per-mode objective state (no rendering)
        const char* root = std::getenv("WFC_ASSET_ROOT");
        std::string gp = std::string(root ? root : "F:/Transformers Rebuild/ExtractedAssets/VerticalSlice") + "/Maps/MP_IAC_Streets/gameplay.json";
        const char* st[] = {"Active", "Inert", "Disabled", "Hidden", "KothInactive"};
        for (game::MatchMode m : {game::MatchMode::DM, game::MatchMode::TDM, game::MatchMode::CTF, game::MatchMode::EXT,
                                  game::MatchMode::DOM, game::MatchMode::KOTH}) {
            game::MapState ms;
            ms.load(gp, m);
            for (const auto& o : ms.objectives())
                LOG_INFO("MODETEST %-4s %-34s %-36s %-12s vis=%d col=%d touch=%d marker=%s added=%d show=%d", game::gameModeName(m),
                         o.cls.c_str(), o.actor.c_str(), st[(int)o.state], (int)o.visible, (int)o.collision, (int)o.touchable,
                         o.markerTypeString, (int)o.markerAdded, (int)o.markerShouldDisplay);
            for (const auto& v : ms.modeVisibleActors())
                LOG_INFO("MODETEST %-4s objective base %-20s visible=%d", game::gameModeName(m), v.actor.c_str(), (int)v.visible);
            for (const auto& r : ms.gameRules()) LOG_INFO("MODETEST %-4s rule %s", game::gameModeName(m), r.c_str());
            if (m == game::MatchMode::DM) {
                for (const auto& mv : ms.movers())
                    if (mv.kind == game::MapMover::Kind::Matinee)
                        LOG_INFO("MODETEST SkyBeam %s InitialRot residual %.5f (0 = authored rotation reproduces world.glb placement)", mv.actor.c_str(), ms.initialRotationResidual(mv));
                game::CollisionWorld cw;
                for (float t : {0.0f, 2.25f, 4.5f, 6.75f, 9.0f}) {
                    while (ms.clock() + 1e-4f < t) ms.tick(1.0f / 60.0f, cw, nullptr);
                    for (const auto& mv : ms.movers()) {
                        if (mv.kind != game::MapMover::Kind::Matinee || mv.actor != "StaticInterpActor_5249") continue;
                        float tr = mv.worldDelta.m[0] + mv.worldDelta.m[5] + mv.worldDelta.m[10];
                        LOG_INFO("MODETEST SkyBeam t=%.2f s: %s delta rotation %.2f deg", ms.clock(), mv.actor.c_str(), std::acos(std::max(-1.0f, std::min(1.0f, (tr - 1.0f) * 0.5f))) * 57.2958f);
                    }
                }
            }
            if (m == game::MatchMode::KOTH) {
                for (int k = 0; k < 4; ++k) {
                    LOG_INFO("MODETEST KOTH active zone: %s", ms.objectives()[(size_t)ms.activeKothZone()].actor.c_str());
                    ms.activateNewKothZone();
                }
            }
        }
        return false;
    }
    window_ = platform::createWindow(config::kWindowWidth, config::kWindowHeight, config::kWindowTitle);
    if (!window_) { LOG_ERROR("window creation failed"); return false; }

    renderer_ = render::createGLRenderer();
    if (!renderer_) { LOG_ERROR("renderer creation failed"); return false; }

    audio_ = audio::createAudio();
    if (std::getenv("WFC_DEBUGDRAW")) core::DebugFlags::get().enabled = true;

    // Frontend boot: the match world is loaded later, from the frontend's match launch (Application_Frontend.cpp).
    if (wantsFrontendBoot()) {
        frontend_ = std::make_unique<frontend::FrontendRuntime>();
        if (frontend_->init()) {
            attachPresenter();
            window_->setMouseCaptured(false);
            mouseCaptured_ = false;
            LOG_INFO("Init complete (frontend boot).");
            return true;
        }
        LOG_WARN("frontend data unavailable; booting straight into the match");
        frontend_.reset();
    }
    // Direct boot: WFC_MAP=<runtime map dir> selects the map (MP_IAC_Streets by default).
    if (const char* mp = std::getenv("WFC_MAP")) world_.setMapName(mp);

    // Match mode (authored rule set; Deathmatch by default). WFC_GAMEMODE=DM|TDM|CTF|KOTH|EXT|DOM selects it until a
    // front end exists; the World applies the matching authored world state at load.
    if (const char* gm = std::getenv("WFC_GAMEMODE"))
        for (game::MatchMode m : {game::MatchMode::DM, game::MatchMode::TDM, game::MatchMode::CTF, game::MatchMode::KOTH,
                                  game::MatchMode::EXT, game::MatchMode::DOM})
            if (std::string(gm) == game::gameModeName(m)) world_.setMatchMode(m);
    world_.load(*renderer_);
    // Gameplay's measurement mode is WFC_PICKUPTEST=1. [integration] Rendering's presentation diagnostic shares the
    // name with a comma form (<factory>,<take>,<respawn>; see run()), so that form must not exit here.
    if (const char* pt = std::getenv("WFC_PICKUPTEST"))
        if (!std::strchr(pt, ',')) { runPickupTest(); return false; }   // measurements only
    if (std::getenv("WFC_TRAVERSE")) { runTraverseTest(); return false; }   // measurements only
    if (std::getenv("WFC_MAPTRAVERSE")) { runMapTraverse(); return false; }   // measurements only
    world_.setAudio(audio_);
    gameMode_.begin(world_);

    window_->setMouseCaptured(true);
    mouseCaptured_ = true;

    LOG_INFO("Init complete. Controls: WASD move, mouse look, Space jump, F transform, "
             "C toggle cursor, Esc quit.");
    return true;
}

void Application::run() {
    if (frontend_) { runFrontend(); return; }
    runMatch();
}

Application::MatchExit Application::runMatch() {
    double last = nowSeconds();
    platform::InputFrame input;
    platform::InputFrame pumped;   // frontend boot: raw platform frame (input may be cleared while a movie has focus)

    // Headless smoke test: WFC_SMOKE_FRAMES=N runs N frames then exits (for automated checks).
    long smokeFrames = 0;
    if (const char* s = std::getenv("WFC_SMOKE_FRAMES")) smokeFrames = std::atol(s);
    bool autoWalk = std::getenv("WFC_AUTOWALK") != nullptr;
    long autoTransform = 0;
    if (const char* s = std::getenv("WFC_AUTOTRANSFORM")) autoTransform = std::atol(s);
    long frame = 0;

    for (;;) {
        if (smokeFrames > 0 && frame >= smokeFrames) {
            LOG_INFO("smoke test complete: ran %ld frames", frame);
            break;
        }
        ++frame;
        double now = nowSeconds();
        double realDt = now - last;
        last = now;
        if (realDt > 0.25) realDt = 0.25;
        // Deterministic captures (render A/B): one 60 Hz step per frame regardless of wall time.
        static const bool lockstep = std::getenv("WFC_LOCKSTEP") != nullptr;
        if (lockstep) realDt = 1.0 / 60.0;

        if (!window_->pump(frontend_ ? pumped : input)) break;
        if (frontend_) input = pumped;
        if (frontend_) {
            // Frontend boot: Escape / Start is "|onrelease showmenu" (Xe-TransInput.ini) -> pause UI, not quit.
            bool escDown = input.isDown(platform::Button::Quit);
            if (escWasDown_ && !escDown) frontend_->flow().showMenu();
            escWasDown_ = escDown;
            frontend_->updateInMatch(input, (float)realDt);
            if (frontend_->flow().quitRequested()) break;
            if (frontend_->flow().wantsWorldUnload()) return MatchExit::ReturnToFrontend;
            // A movie with focus (pause, end game) takes the input; the MP world keeps running (bPauseable false).
            if (frontend_->flow().ui().state() != frontend::UIState::InGame) {
                platform::InputFrame none;
                input = none;
                if (mouseCaptured_) { mouseCaptured_ = false; window_->setMouseCaptured(false); }
            }
        } else if (input.wasPressed(platform::Button::Quit)) break;

        if (autoWalk) input.down[(int)platform::Button::Forward] = true;  // scripted move for tests
        static const bool lockstepInput = std::getenv("WFC_LOCKSTEP") != nullptr;
        if (std::getenv("WFC_NOMOUSE") || lockstepInput) { input.mouseDX = 0; input.mouseDY = 0; }   // deterministic tests
        if (std::getenv("WFC_AUTOSTRAFE")) input.down[(int)platform::Button::Right] = true;
        if (std::getenv("WFC_AUTOBACK")) input.down[(int)platform::Button::Back] = true;
        if (std::getenv("WFC_AUTOFIRE")) input.down[(int)platform::Button::Fire] = true;
        if (const char* s = std::getenv("WFC_AUTOBOOST")) if (frame >= std::atol(s)) input.down[(int)platform::Button::FineAim] = true;   // vehicle Boost (RMB held) from frame N
        if (const char* s = std::getenv("WFC_AUTOBOOST_CYCLE"))         // repeated Boost: hold N frames, release N
            if (long n = std::atol(s); n > 0 && (frame / n) % 2 == 1) input.down[(int)platform::Button::FineAim] = true;
        if (const char* s = std::getenv("WFC_AUTOJUMP_EVERY"))          // repeated Jump press every N frames
            if (long n = std::atol(s); n > 0 && frame > 0 && frame % n == 0) input.pressed[(int)platform::Button::Jump] = true;
        if (const char* s = std::getenv("WFC_AUTODASH")) if (frame == std::atol(s)) input.pressed[(int)platform::Button::Dash] = true;
        if (const char* s = std::getenv("WFC_AUTODASH2")) if (frame == std::atol(s)) input.pressed[(int)platform::Button::Dash] = true;
        if (const char* s = std::getenv("WFC_AUTOWALK_UNTIL"))           // release scripted input
            if (frame > std::atol(s)) {
                input.down[(int)platform::Button::Forward] = false;
                input.down[(int)platform::Button::Right] = false;
                input.down[(int)platform::Button::FineAim] = false;
            }
        // Fine-aim test hooks: press the FineAim button (toggle) on the given frames; WFC_PADLT holds
        // the pad trigger instead.
        if (const char* s = std::getenv("WFC_FINEAIM_ON"))  if (frame == std::atol(s)) input.pressed[(int)platform::Button::FineAim] = true;
        if (const char* s = std::getenv("WFC_FINEAIM_OFF")) if (frame == std::atol(s)) input.pressed[(int)platform::Button::FineAim] = true;
        if (std::getenv("WFC_PADLT")) input.padLT = 1.0f;
        if (std::getenv("WFC_AUTOJUMP") && frame == 20) input.pressed[(int)platform::Button::Jump] = true;
        if (std::getenv("WFC_AUTORELOAD")) {                       // fire a few rounds, then reload
            if (frame <= 12) input.down[(int)platform::Button::Fire] = true;
            if (frame == 15) { input.pressed[(int)platform::Button::Reload] = true;   // one-frame tap:
                               input.down[(int)platform::Button::Reload] = true; }  // fires on release
        }
        if (autoTransform > 0 && frame == autoTransform) world_.player().pawn().beginTransform();
        if (const char* s = std::getenv("WFC_PRESSTRANSFORM")) if (frame == std::atol(s)) input.pressed[(int)platform::Button::Transform] = true;
        if (const char* s = std::getenv("WFC_RAMSELF")) if (frame == std::atol(s)) {   // diagnostic: robot ram reaction
            auto& pw = world_.player().pawn();
            pw.rammedAsRobot(core::forwardFromYawPitch(pw.yaw(), 0.0f) * -1.0f);
        }

        if (input.wasPressed(platform::Button::CameraToggle)) {
            mouseCaptured_ = !mouseCaptured_;
            window_->setMouseCaptured(mouseCaptured_);
        }
        // Scripted smoke runs ignore the interactive toggle (a stray 'B' typed while a capture runs
        // drew the debug capsule box + aim ray into screenshots); use WFC_DEBUGDRAW there.
        if (smokeFrames <= 0 && input.wasPressed(platform::Button::Debug))
            core::DebugFlags::get().enabled = !core::DebugFlags::get().enabled;

        if (const char* fy = std::getenv("WFC_FIXYAW"))   // diagnostic: pin the camera yaw
            world_.player().controller().setCameraYaw((float)std::atof(fy));
        if (const char* at = std::getenv("WFC_AUTOTURN")) {  // diagnostic: rotate the aim (rad/s)
            auto& pc = world_.player().controller();
            pc.setCameraYaw(pc.camYaw() + (float)std::atof(at) * (float)realDt);
        }
        if (const char* fp = std::getenv("WFC_FIXPITCH")) // diagnostic: pin the camera/aim pitch
            world_.player().controller().setCameraPitch((float)std::atof(fp));
        // Per-frame input (camera orientation, buffered movement intent).
        world_.handleInput(input, (float)realDt);

        // Fixed-step simulation.
        static const long perfEvery = std::getenv("WFC_PERFLOG") ? std::atol(std::getenv("WFC_PERFLOG")) : 0;
        auto simT0 = std::chrono::steady_clock::now();
        int steps = clock_.tick(realDt);
        float step = clock_.stepSeconds();
        for (int i = 0; i < steps; ++i) {
            world_.tick(step);
            gameMode_.tick(world_, step);
        }
        if (perfEvery > 0) {
            // Gameplay-side cost (WFC_PERFLOG=N): simulation time (movement, camera, aim, hitscan)
            // vs the whole frame.
            static double simMs = 0, frameMs = 0; static long n = 0;
            simMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - simT0).count();
            frameMs += realDt * 1000.0; ++n;
            if (n == perfEvery) {
                LOG_INFO("PERF f%ld frame=%.2fms sim=%.3fms ammo=%d", frame, frameMs / n, simMs / n,
                         world_.player().pawn().weapon().ammo);
                simMs = frameMs = 0; n = 0;
            }
        }

        if (smokeFrames > 0 && std::getenv("WFC_AUTOBOOST") && frame % 3 == 0) {
            const auto& pw = world_.player().pawn();
            const core::Vec3& vv = pw.velocity();
            LOG_INFO("boost frame %ld speed=%.2f y=%.2f form=%s", frame,
                     std::sqrt(vv.x * vv.x + vv.z * vv.z), pw.position().y, game::formName(pw.form()));
        }
        if (smokeFrames > 0 && std::getenv("WFC_AUTOJUMP") && frame % 5 == 0) {
            const auto& pw = world_.player().pawn();
            LOG_INFO("jump frame %ld y=%.2f vy=%.2f grounded=%d", frame,
                     pw.position().y, pw.velocity().y, (int)pw.onGround());
        }
        static const long logEvery = std::getenv("WFC_LOGEVERY") ? std::atol(std::getenv("WFC_LOGEVERY")) : 30;
        if (smokeFrames > 0 && logEvery > 0 && frame % logEvery == 0) {
            const auto& pawn = world_.player().pawn();
            core::Vec3 p = pawn.position();
            LOG_INFO("frame %ld pos %.2f %.2f %.2f grounded=%d form=%s anim=%s t=%.2f ammo=%d/%d reloading=%d "
                     "yaw=%.2f aimW=%.2f aimN=%.2f reloadW=%.2f legYaw=%.1f aimYawN=%.2f turn=%d recoil=%d "
                     "hspeed=%.2f moveForm=%s fineAim=%d fov=%.1f drv=%d ride=%.2f dash=%.2f nitro=%.2f wpn=%d "
                     "vy=%.2f pitch=%.1f roll=%.1f cont=%d vgnd=%d camS=%d vyaw=%.2f both=%d hasW=%d camD=%.2f camH=%.2f arm=%d hand=%d ram=%.2f hudAim=%d hudSpread=%.4f hudW=%s xhair=%d",
                     frame, p.x, p.y, p.z, (int)pawn.onGround(), game::formName(pawn.form()),
                     pawn.animName(), pawn.animTime(), pawn.weapon().ammo, pawn.weapon().reserve,
                     (int)pawn.weapon().reloading(), pawn.yaw(), pawn.aimWeight(), pawn.aimPitchNorm(),
                     pawn.reloadWeight(), pawn.legYaw() * 57.2958f, pawn.aimYawNorm(),
                     (int)pawn.turningInPlace(), (int)pawn.recoiling(),
                     std::sqrt(pawn.velocity().x * pawn.velocity().x + pawn.velocity().z * pawn.velocity().z),
                     game::formName(pawn.moveForm()), (int)world_.player().controller().fineAiming(),
                     world_.player().controller().fovXDeg(),
                     (int)pawn.vehicleState().driving, pawn.vehicleState().rideHeight,
                     pawn.vehicleState().dashRemain, pawn.vehicleState().nitroRemain, (int)pawn.weaponUsable(),
                     pawn.velocity().y, pawn.vehicleState().pitch * 57.2958f, pawn.vehicleState().roll * 57.2958f,
                     pawn.vehicleState().contacts, (int)pawn.vehicleState().onTheGround,
                     world_.player().controller().cameraStrategy(), world_.player().controller().viewYaw(),
                     (int)pawn.partnerShown(), (int)pawn.hasWeapon(),
                     core::length(world_.player().controller().cameraPos() - pawn.actorLocation()),
                     world_.player().controller().cameraPos().y - pawn.position().y, (int)pawn.armShown(), (int)pawn.handShrunk(), pawn.rammedRemain(),
                     world_.player().controller().hudAimState().aimType, world_.player().controller().hudAimState().spread,
                     world_.player().controller().hudAimState().weaponClass[0] ? world_.player().controller().hudAimState().weaponClass : "-",
                     (int)world_.player().controller().hudAimState().crosshairVisible);
        }

        if (std::getenv("WFC_HUDLOG"))   // diagnostic: the TnHUD Movie.Invoke calls produced this frame
            for (const auto& n : world_.player().controller().hudNotifies())
                LOG_INFO("HUD f%ld %s", frame,
                         n.type == game::HudNotify::Type::WeaponSpread ? ("NotifyWeaponSpreadChanged(" + std::to_string(n.spread) + ")").c_str()
                         : n.type == game::HudNotify::Type::FineAim ? ("NotifyFineAimChanged(" + std::to_string(n.aimType) + ")").c_str()
                         : ("NotifyCurrentWeaponChanged(" + std::string(n.weaponClass) + ")").c_str());

        // Camera + render.
        world_.player().controller().updateCamera(camera_);
        camera_.aspect = (float)window_->width() / (float)(window_->height() > 0 ? window_->height() : 1);

        // Debug camera overrides (for diagnosis / screenshots): WFC_DEBUGCAM=top|front
        if (const char* dc = std::getenv("WFC_DEBUGCAM")) {
            core::Vec3 pp = world_.player().pawn().position();
            if (dc[0] == 't') {            // top-down
                camera_.pos = pp + core::Vec3{0, 45, 0};
                camera_.yaw = 0.0f; camera_.pitch = -1.55f;
            } else if (dc[0] == 'f') {     // in front of the player at +Z, looking toward -Z
                camera_.pos = pp + core::Vec3{0, 3, 12};
                camera_.yaw = 0.0f; camera_.pitch = -0.1f;
            } else if (std::strncmp(dc, "at:", 3) == 0) {   // at:x,y,z,tx,ty,tz (glTF metres): fixed look-at
                float v[6] = {0, 0, 0, 0, 0, -1};
                if (std::sscanf(dc + 3, "%f,%f,%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6) {
                    camera_.pos = {v[0], v[1], v[2]};
                    core::Vec3 f = core::normalize(core::Vec3{v[3] - v[0], v[4] - v[1], v[5] - v[2]});
                    camera_.pitch = std::asin(std::max(-1.0f, std::min(1.0f, f.y)));
                    camera_.yaw = std::atan2(-f.x, -f.z);
                }
            }
        }

        // Diagnostic multi-shot: WFC_SHOTLIST=<file> with lines "<name> x,y,z,tx,ty,tz" (glTF metres); each camera
        // is held for 8 frames and captured to WFC_SHOTDIR/<name>.bmp, then the run ends (one map load for many views).
        static std::vector<std::pair<std::string, std::vector<float>>> shotList;
        static bool shotListLoaded = false;
        if (!shotListLoaded) {
            shotListLoaded = true;
            if (const char* sl = std::getenv("WFC_SHOTLIST")) {
                std::ifstream in(sl);
                std::string name, cam;
                while (in >> name >> cam) {
                    std::vector<float> v(6, 0.0f);
                    if (std::sscanf(cam.c_str(), "%f,%f,%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6)
                        shotList.push_back({name, v});
                    std::string rest; std::getline(in, rest);
                }
            }
        }
        if (!shotList.empty()) {
            size_t idx = (size_t)((frame - 1) / 8);
            if (idx >= shotList.size()) break;
            const std::vector<float>& v = shotList[idx].second;
            camera_.pos = {v[0], v[1], v[2]};
            core::Vec3 f = core::normalize(core::Vec3{v[3] - v[0], v[4] - v[1], v[5] - v[2]});
            camera_.pitch = std::asin(std::max(-1.0f, std::min(1.0f, f.y)));
            camera_.yaw = std::atan2(-f.x, -f.z);
        }

        if (std::getenv("WFC_FACELOG") && smokeFrames > 0 && frame % 10 == 0) {
            const auto& pw = world_.player().pawn();
            core::Vec3 pp = pw.position();
            // The mesh's authored forward is model +X, so measure it through the actual draw rotation.
            core::Vec3 face = core::transformDir(core::Mat4::rotateY(pw.yaw() + core::config::kMeshYawOffset),
                                                 core::Vec3{1, 0, 0});
            core::Vec3 toCam = core::normalize(camera_.pos - pp);
            // dot<0 => character faces AWAY from camera (back shown, correct for chase cam).
            LOG_INFO("FACE f%ld pawnYaw=%.2f camYaw=%.2f face.toCam=%.2f", frame, pw.yaw(), camera_.yaw,
                     core::dot(face, toCam));
        }
        renderer_->beginFrame(camera_, window_->width(), window_->height());
        world_.draw(*renderer_);
        renderer_->endFrame();
        if (frontend_) frontend_->draw(window_->width(), window_->height());   // open movies (pause, end game)
        if (frontend_ && !pendingShot_.empty()) { renderer_->captureScreenshot(pendingShot_.c_str()); pendingShot_.clear(); }

        if (const char* sa = std::getenv("WFC_SHOWACTOR"))       // diagnostic: Gameplay-style unhide (e.g. a KOTH zone)
            renderer_->setActorHidden(sa, false);
        if (const char* ds = std::getenv("WFC_DESTRUCTSTATE"))   // diagnostic: destructible presentation state
            if (frame == 1) renderer_->setDestructibleState("TnStaticDestructibleActor_14465", std::atoi(ds));
        // Diagnostic: WFC_PICKUPTEST=<factory actor>,<take frame>,<respawn frame> drives the pickup presentation
        // (SetPickupHidden / SetPickupVisible) the way Gameplay's PickupEvents will. [integration] Gameplay's
        // syncMapPresentation now pushes the real factory state every frame, so this forced state only lasts one
        // frame; take a pickup in play instead.
        if (const char* pt = std::getenv("WFC_PICKUPTEST")) {
            char actor[128] = {0}; long take = -1, back = -1;
            if (std::sscanf(pt, "%127[^,],%ld,%ld", actor, &take, &back) == 3) {
                if (frame == take) {
                    renderer_->setMapEffectState(std::string(actor) + "|custom", false, true);
                    renderer_->setMapEffectState(std::string(actor) + "|highlight", false, false);
                } else if (frame == back) {
                    renderer_->setMapEffectState(std::string(actor) + "|custom", true, false);
                    renderer_->setMapEffectState(std::string(actor) + "|highlight", true, false);
                }
            }
        }
        if (smokeFrames > 0 && frame == smokeFrames)
            if (const char* shot = std::getenv("WFC_SHOT")) renderer_->captureScreenshot(shot);
        if (!shotList.empty() && frame % 8 == 0 && (size_t)(frame / 8 - 1) < shotList.size()) {
            const char* dir = std::getenv("WFC_SHOTDIR");
            std::string out = std::string(dir ? dir : ".") + "/" + shotList[(size_t)(frame / 8 - 1)].first + ".bmp";
            renderer_->captureScreenshot(out.c_str());
        }

        window_->present();
        if (audio_) {
            core::Vec3 fwd = core::forwardFromYawPitch(camera_.yaw, camera_.pitch);
            core::Vec3 right = core::normalize(core::cross(fwd, core::Vec3{0, 1, 0}));
            audio_->setListener(camera_.pos, fwd, right);
            audio_->update();
        }

        updateTitleHud(realDt);
    }
    return MatchExit::Quit;
}

void Application::updateTitleHud(double realDt) {
    double fps = realDt > 1e-6 ? 1.0 / realDt : 0.0;
    fpsSmoothed_ = fpsSmoothed_ <= 0.0 ? fps : (fpsSmoothed_ * 0.9 + fps * 0.1);
    titleTimer_ += realDt;
    if (titleTimer_ < 0.25) return;
    titleTimer_ = 0.0;

    const auto& pawn = world_.player().pawn();
    const core::Vec3 p = pawn.position();
    const core::Vec3& v = pawn.velocity();
    float speed = std::sqrt(v.x * v.x + v.z * v.z);
    const auto& w = pawn.weapon();
    char title[320];
    std::snprintf(title, sizeof(title),
                  "WFC Rebuild | %.0f FPS | pos %.1f,%.1f,%.1f | spd %.1f | %s | %s | "
                  "anim %s %.2fs | hp %.0f | ammo %d/%d%s%s",
                  fpsSmoothed_, p.x, p.y, p.z, speed,
                  game::formName(pawn.form()), pawn.onGround() ? "grounded" : "airborne",
                  pawn.animName(), pawn.animTime(), pawn.health().current,
                  w.ammo, w.reserve, w.reloading() ? " RELOAD" : "",
                  core::DebugFlags::get().enabled ? " | DBG" : "");
    window_->setTitle(title);
}

// WFC_PICKUPTEST: deterministic pickup / destructible validation on the loaded slice world at the fixed
// 60 Hz step. Places the pawn on one factory of each kind, logs every PickupEvent / DestructibleEvent and
// the availability transitions through the authored RespawnTime.
void Application::runPickupTest() {
    const float dt = (float)clock_.stepSeconds();
    auto& pawn = world_.player().pawn();
    auto logEvents = [&](float t) {
        for (const auto& e : world_.pickupEvents())
            LOG_INFO("PICKUPTEST t=%7.2f event factory=%d %s %s available=%d sound=%s", t, e.factory,
                     game::PickupFactory::className(e.kind), e.type == game::PickupEvent::Type::Taken ? "TAKEN" : "RESPAWNED",
                     (int)e.available, e.pickupSound ? e.pickupSound : "-");
        for (const auto& e : world_.destructibleEvents())
            LOG_INFO("PICKUPTEST t=%7.2f destructible %d state %d -> %d", t, e.actor, e.fromState, e.toState);
    };
    const auto& fac = world_.pickupFactories();
    float t = 0.0f;
    auto idxOf = [&](const game::PickupFactory* f) { return (int)(std::find(fac.begin(), fac.end(), f) - fac.begin()); };
    for (int kind = 0; kind < 3; ++kind) {
        const game::PickupFactory* f = nullptr;
        for (auto* x : fac) if ((int)x->kind() == kind) { f = x; break; }
        if (!f) continue;
        const char* cls = game::PickupFactory::className(f->kind());
        LOG_INFO("PICKUPTEST %s initial: available=%d mesh=%d customFx=%d beam=%d", cls, (int)f->available(),
                 (int)f->meshVisible(), (int)f->customEffectActive(), (int)f->beamActive());
        pawn.health().current = 30.0f; pawn.weapon().reserve = 10;
        pawn.setPosition(f->position()); pawn.velocity() = {0, 0, 0};
        int taken = 0, respawned = 0; float tTaken = -1.0f, tBack = -1.0f, tRetake = -1.0f;
        bool stillOn = true;
        for (int i = 0; i < (int)((f->respawnTime() + 3.0f) / dt); ++i) {
            // While sleeping, keep the pawn standing on it and eligible again (overlap must be ignored).
            if (taken == 1 && respawned == 0) { pawn.health().current = 30.0f; pawn.weapon().reserve = 10; }
            pawn.setPosition(f->position()); pawn.velocity() = {0, 0, 0};
            world_.tick(dt); t += dt;
            for (const auto& e : world_.pickupEvents()) {
                if (e.factory != idxOf(f)) continue;
                LOG_INFO("PICKUPTEST t=%7.2f %s %s available=%d mesh=%d customFx=%d beam=%d sound=%s", t, cls,
                         e.type == game::PickupEvent::Type::Taken ? "TAKEN" : "RESPAWNED", (int)e.available, (int)e.meshVisible,
                         (int)e.customEffectActive, (int)e.beamActive, e.pickupSound ? e.pickupSound : "-");
                if (e.type == game::PickupEvent::Type::Taken) { ++taken; if (tTaken < 0) tTaken = t; else if (tRetake < 0) tRetake = t; }
                else { ++respawned; tBack = t; }
            }
            if (respawned && t > tBack + 1.0f) break;
        }
        (void)stillOn;
        LOG_INFO("PICKUPTEST %s: taken at %.2f, sleeping overlap ignored, respawn after %.2f s (authored %.0f); still standing on it at respawn -> %s",
                 cls, tTaken, tBack - tTaken, f->respawnTime(), tRetake > 0 ? "RE-TAKEN (CheckTouching)" : "not re-taken (Touch = overlap begin)");
        // Step off and back on: a fresh Touch.
        if (f->available()) {
            pawn.health().current = 30.0f; pawn.weapon().reserve = 10;
            pawn.setPosition(f->position() + core::Vec3{0, 50.0f, 0}); world_.tick(dt); t += dt;
            pawn.setPosition(f->position()); world_.tick(dt); t += dt;
            LOG_INFO("PICKUPTEST %s: step off and back on -> available=%d", cls, (int)f->available());
        }
        pawn.setPosition(f->position() + core::Vec3{0, 50.0f, 0}); world_.tick(dt); t += dt;
    }
    for (auto* dc : world_.destructibles()) {
        auto* d = const_cast<game::Destructible*>(dc);
        LOG_INFO("PICKUPTEST destructible %s at %.2f %.2f %.2f state %d health %.0f", d->name().c_str(),
                 d->position().x, d->position().y, d->position().z, d->state(), d->health());
        // State machine in place (the panel stays at its authored location): 15 damage (stays intact),
        // 15 more (health 20 reached -> destroyed), then the 10 s AutomaticTransitionTime -> settled.
        d->applyDamage(world_, 15.0f); logEvents(t);
        LOG_INFO("PICKUPTEST destructible after 15 dmg: state %d health %.0f", d->state(), d->health());
        world_.tick(dt); t += dt;
        d->applyDamage(world_, 15.0f);
        float t1 = t;
        for (int i = 0; i < (int)(11.0f / dt) && d->state() != 2; ++i) { world_.tick(dt); t += dt; logEvents(t); }
        LOG_INFO("PICKUPTEST destructible: destroyed at t=%.2f, settled %.2f s later (authored 10); position still %.2f %.2f %.2f",
                 t1, t - t1, d->position().x, d->position().y, d->position().z);
    }
    LOG_INFO("PICKUPTEST factories loaded: %zu", fac.size());
}

// WFC_TRAVERSE: deterministic traversal of the authored map at the fixed 60 Hz step. For one player start per
// spawn cluster: robot and vehicle runs in four headings (4 s each, forward input), then robot->vehicle and
// vehicle->robot at the end point. Logs distance, height range, KillZ falls and stuck runs. Then a moving-
// collision check through rotating dome StaticInterpActor_15810 and the mode-visibility state.
void Application::runTraverseTest() {
    const float dt = (float)clock_.stepSeconds();
    auto& pc = world_.player().pawn();
    auto& ctl = world_.player().controller();
    const auto& starts = world_.startPoints();
    std::vector<int> picks;
    std::vector<std::string> seen;
    for (size_t i = 0; i < starts.size(); ++i)
        if (std::find(seen.begin(), seen.end(), starts[i].cluster) == seen.end()) { seen.push_back(starts[i].cluster); picks.push_back((int)i); }
    for (size_t i = 0; i < starts.size(); i += 12) picks.push_back((int)i);   // FFA/team spread
    int falls = 0, stuck = 0, runs = 0;
    for (int form = 0; form < 2; ++form) {
        for (int si : picks) {
            for (int dir = 0; dir < 4; ++dir) {
                if (form == 1 && pc.moveForm() != game::Form::Vehicle) { pc.setForm(game::Form::Vehicle); }
                if (form == 0 && pc.moveForm() != game::Form::Robot) { pc.setForm(game::Form::Robot); }
                world_.teleportToStart(si);
                float yaw = starts[(size_t)si].yaw + dir * 1.5707963f;
                core::Vec3 p0 = pc.position();
                float ymin = p0.y, ymax = p0.y;
                // Nearest pawn-blocking surface along the heading at body height (wall check).
                float wallT = 1.0f; core::Vec3 wn;
                core::Vec3 fwd = core::forwardFromYawPitch(yaw, 0.0f);
                core::Vec3 ta = pc.actorLocation(), tb = ta + fwd * 20.0f;
                bool wallHit = world_.collision()->segmentHit(ta, tb, wallT, wn);
                float wallDist = wallHit ? wallT * 20.0f : 99.0f;
                bool fell = false;
                platform::InputFrame in;
                in.down[(int)platform::Button::Forward] = true;
                for (int k = 0; k < (int)(4.0f / dt); ++k) {
                    ctl.setCameraYaw(yaw);
                    world_.handleInput(in, dt);
                    world_.tick(dt);
                    ymin = std::min(ymin, pc.position().y); ymax = std::max(ymax, pc.position().y);
                    if (pc.position().y < -749.0f || !std::isfinite(pc.position().y)) { fell = true; break; }
                }
                core::Vec3 p1 = pc.position();
                float dist = std::sqrt((p1.x - p0.x) * (p1.x - p0.x) + (p1.z - p0.z) * (p1.z - p0.z));
                ++runs; falls += fell;
                bool snag = dist < 2.0f && wallDist > 4.5f;     // blocked although no wall ahead within reach
                LOG_INFO("TRAVERSE %s start %2d %-26s dir %d: dist %6.1f m  y %.1f..%.1f wallAhead %.1f m%s%s", form ? "vehicle" : "robot  ",
                         si, starts[(size_t)si].actor.c_str(), dir, dist, ymin, ymax, wallDist, fell ? "  FELL" : "",
                         snag ? "  SNAG" : (dist < 2.0f ? "  (wall)" : ""));
                stuck += snag;
            }
            // Transform at the end point (both directions), no fall-through.
            float y0 = pc.position().y;
            platform::InputFrame none;
            pc.beginTransform();
            for (int k = 0; k < (int)(3.0f / dt); ++k) { world_.handleInput(none, dt); world_.tick(dt); }
            pc.beginTransform();
            for (int k = 0; k < (int)(3.0f / dt); ++k) { world_.handleInput(none, dt); world_.tick(dt); }
            LOG_INFO("TRAVERSE transform x2 at start %d: y %.2f -> %.2f form %s", si, y0, pc.position().y, game::formName(pc.form()));
            if (pc.position().y < -749.0f) ++falls;
        }
    }
    LOG_INFO("TRAVERSE summary: %d runs, %d falls below KillZ, %d snags (<2 m in 4 s with no wall within 4.5 m)", runs, falls, stuck);

    // Moving collision: a horizontal segment through dome StaticInterpActor_15810 at several clock times.
    const auto& ms = world_.mapState();
    for (const auto& m : ms.movers()) {
        if (m.actor != "StaticInterpActor_15810") continue;
        for (int k = 0; k < 4; ++k) {
            core::Vec3 a = m.pivot + core::Vec3{-8.0f, 0.5f, 0.3f}, b = m.pivot + core::Vec3{8.0f, 0.5f, 0.3f};
            float t = -1.0f; core::Vec3 n;
            bool hit = world_.collision()->segmentHit(a, b, t, n);
            LOG_INFO("TRAVERSE dome %s clock %.2f s: segment hit %d at x=%.2f normal (%.2f,%.2f,%.2f)", m.actor.c_str(), ms.clock(),
                     (int)hit, hit ? a.x + 16.0f * t - m.pivot.x : 0.0f, n.x, n.y, n.z);
            for (int s = 0; s < (int)(2.0f / dt); ++s) world_.tick(dt);   // 30 deg of rotation
        }
    }
    for (const auto& m : ms.movers())
        LOG_INFO("TRAVERSE mover %s %s delta.x=(%.3f,%.3f,%.3f)", m.actor.c_str(), m.kind == game::MapMover::Kind::Rotating ? "rotating" : "matinee",
                 m.worldDelta.m[0], m.worldDelta.m[1], m.worldDelta.m[2]);
    for (const auto& v : ms.modeVisibleActors())
        LOG_INFO("TRAVERSE mode %s: %s visible=%d", game::gameModeName(ms.mode()), v.actor.c_str(), (int)v.visible);
    int active = 0; for (const auto& o : ms.objectives()) active += o.activeInMode;
    LOG_INFO("TRAVERSE objectives: %zu (active in %s: %d)", ms.objectives().size(), game::gameModeName(ms.mode()), active);
}

// WFC_MAPTRAVERSE: traversal of MP_IAC_Streets against authored data, at the fixed 60 Hz step.
//  1. ORACLE: every authored TnReachSpec (navigation.json, 426 directed edges, all R_WALK, path sizes 250..1210 UU)
//     is a straight path the editor's path builder found clear and floored for a cylinder >= the truck. Each edge is
//     walked as the robot and driven as the hover vehicle, steering at the end node; it must arrive. Floor
//     continuity is sampled every 0.5 m along the segment. Failures log position + the authored collision actors.
//  2. TOUR (coverage, not an oracle): a nearest-neighbour tour through all 123 nav points (both forms); each leg
//     logs arrival, the y range and, when blocked, the blocking authored actors - so blocked areas can be traced.
//  3. Bounds: every nav point above KillZ and inside the collision bounds; no run may fall below KillZ.
void Application::runMapTraverse() {
    const float dt = (float)clock_.stepSeconds();
    auto& pc = world_.player().pawn();
    auto& ctl = world_.player().controller();
    const game::CollisionWorld* col = world_.collision();
    if (!col) { LOG_WARN("MAPTRAVERSE: no collision"); return; }
    std::string root = std::getenv("WFC_ASSETS") ? std::getenv("WFC_ASSETS") : core::config::kAssetRootDefault;
    std::ifstream f(root + "/Maps/MP_IAC_Streets/navigation.json", std::ios::binary);
    std::stringstream ss; ss << f.rdbuf();
    assets::Json nav;
    if (!assets::Json::parse(ss.str(), nav)) { LOG_WARN("MAPTRAVERSE: cannot read navigation.json"); return; }
    struct Node { std::string name, cls; core::Vec3 p; };
    std::vector<Node> nodes;
    std::map<std::string, int> byName;
    for (size_t i = 0; i < nav["nodes"].size(); ++i) {
        const assets::Json& n = nav["nodes"][i];
        const assets::Json& L = n["location_gltf"];
        nodes.push_back({n["node"].asString(), n["class"].asString(), {L[0].asFloat(), L[1].asFloat(), L[2].asFloat()}});
        byName[nodes.back().name] = (int)nodes.size() - 1;
    }
    auto shortName = [](const std::string& s) { size_t d = s.rfind('.'); return d == std::string::npos ? s : s.substr(d + 1); };
    struct Edge { int a, b; std::string spec; float radius; };
    std::vector<Edge> edges;
    for (size_t i = 0; i < nav["edges"].size(); ++i) {
        const assets::Json& e = nav["edges"][i];
        auto ia = byName.find(shortName(e["start"].asString())), ib = byName.find(shortName(e["end"].asString()));
        if (ia == byName.end() || ib == byName.end()) continue;
        edges.push_back({ia->second, ib->second, e["spec"].asString(), e["collision_radius"].asFloat()});
    }
    // Bounds.
    core::Vec3 bmn = col->boundsMin(), bmx = col->boundsMax();
    int outside = 0;
    for (const Node& n : nodes) {
        float gy; core::Vec3 gn;
        bool g = col->groundHeight(n.p.x, n.p.z, n.p.y + 1.5f, 3.0f, gy, gn);
        bool in = n.p.x >= bmn.x && n.p.x <= bmx.x && n.p.z >= bmn.z && n.p.z <= bmx.z && n.p.y > -750.0f;
        if (!in || !g) { ++outside; LOG_INFO("MAPTRAVERSE bounds %s %s at (%.1f %.1f %.1f): inside=%d floor=%d", n.cls.c_str(), n.name.c_str(), n.p.x, n.p.y, n.p.z, (int)in, (int)g); }
    }
    LOG_INFO("MAPTRAVERSE bounds: %zu nav points, %d outside the collision bounds / without floor (KillZ -750 m)", nodes.size(), outside);

    // Visual / collision coherence: the rendered map (world.glb, minus movers and the mode-hidden objective bases)
    // as a trace world, each render component joined to its authored pawn collision representation
    // (physics.json props: simple / per_poly / none, from the component's Block* flags and BodySetup).
    std::map<std::string, std::pair<std::string, std::string>> compRep;   // component -> (pawn rep, mesh)
    std::map<std::string, bool> compBlockCam;                             // component -> authored BlockCameras
    {
        std::ifstream pf(root + "/Maps/MP_IAC_Streets/physics.json", std::ios::binary);
        std::stringstream ps; ps << pf.rdbuf();
        assets::Json ph;
        if (assets::Json::parse(ps.str(), ph))
            for (size_t i = 0; i < ph["props"].size(); ++i)
            {
                compRep[ph["props"][i]["component"].asString()] = {ph["props"][i]["pawn"].asString(), ph["props"][i]["mesh"].asString()};
                compBlockCam[ph["props"][i]["component"].asString()] = ph["props"][i]["flags"]["BlockCameras"].asBool(false);
            }
    }
    struct RAct { std::string name, comp, rep, mesh; core::Vec3 lo, hi; };
    std::vector<RAct> ract;
    game::CollisionWorld renderCol;
    {
        render::MeshData rm, rs;
        assets::loadGlb(root + "/Maps/MP_IAC_Streets/world.glb", rm);
        std::vector<std::string> skip = game::MapState::moverActorNames();
        for (const auto& v : world_.mapState().modeVisibleActors()) if (!v.visible) skip.push_back(v.actor);
        rs.positions = rm.positions;
        for (const render::SubMesh& sm : rm.subs) {
            if (std::find(skip.begin(), skip.end(), sm.nodeName) != skip.end() || sm.indexCount == 0) continue;
            RAct a;
            a.name = sm.nodeName; a.comp = sm.component;
            auto it = compRep.find(sm.component);
            a.rep = sm.component.rfind("bsp:", 0) == 0 ? "bsp" : (it != compRep.end() ? it->second.first : "?");
            a.mesh = it != compRep.end() ? it->second.second : sm.sourceMesh;
            uint32_t v0 = rm.indices[sm.indexOffset];
            a.lo = a.hi = core::Vec3{rm.positions[v0 * 3], rm.positions[v0 * 3 + 1], rm.positions[v0 * 3 + 2]};
            for (uint32_t i = sm.indexOffset; i < sm.indexOffset + sm.indexCount; ++i) {
                uint32_t v = rm.indices[i];
                core::Vec3 q{rm.positions[v * 3], rm.positions[v * 3 + 1], rm.positions[v * 3 + 2]};
                a.lo = {std::min(a.lo.x, q.x), std::min(a.lo.y, q.y), std::min(a.lo.z, q.z)};
                a.hi = {std::max(a.hi.x, q.x), std::max(a.hi.y, q.y), std::max(a.hi.z, q.z)};
                rs.indices.push_back(v);
            }
            ract.push_back(a);
        }
        renderCol.build(rs);
        LOG_INFO("MAPTRAVERSE render trace world: %zu components (%zu skipped: movers + mode-hidden), %zu tris", ract.size(), skip.size(), rs.indices.size() / 3);
    }
    // Smallest render component whose bounds contain q (pad metres).
    auto renderActorAt = [&](const core::Vec3& q, float pad) -> const RAct* {
        const RAct* best = nullptr; float bestVol = 1e30f;
        for (const RAct& a : ract) {
            if (q.x < a.lo.x - pad || q.x > a.hi.x + pad || q.y < a.lo.y - pad || q.y > a.hi.y + pad || q.z < a.lo.z - pad || q.z > a.hi.z + pad) continue;
            float vol = (a.hi.x - a.lo.x + 0.01f) * (a.hi.y - a.lo.y + 0.01f) * (a.hi.z - a.lo.z + 0.01f);
            if (vol < bestVol) { bestVol = vol; best = &a; }
        }
        return best;
    };
    struct Pass { std::string rep, mesh, actor; int n = 0; core::Vec3 at, rlo, rhi; };
    std::map<std::string, Pass> passThrough;            // render component crossed by the pawn's chest segment
    std::map<std::string, int> blockVisible, blockInvisible;

    auto place = [&](const core::Vec3& at, game::Form form) {
        if (pc.moveForm() != form) pc.setForm(form);
        core::Vec3 p = at;
        float gy; core::Vec3 gn;
        if (col->groundHeight(p.x, p.z, p.y + 1.5f, 3.0f, gy, gn)) p.y = gy;
        if (form == game::Form::Vehicle) p.y += pc.meshToActor(game::Form::Robot) - pc.meshToActor(game::Form::Vehicle);
        pc.setPosition(p); pc.velocity() = {0, 0, 0}; pc.groundY = p.y;
    };
    struct Result { bool arrived = false, fell = false; float t = 0, ymin = 0, ymax = 0; core::Vec3 end; std::string blockers; };
    // Steer at the target (camera yaw = bearing; robot faces the view, the hover truck follows the view yaw).
    auto run = [&](const core::Vec3& to, float arriveR, float timeout) {
        Result r;
        core::Vec3 p0 = pc.position();
        r.ymin = r.ymax = p0.y;
        platform::InputFrame in;
        in.down[(int)platform::Button::Forward] = true;
        float lastProgT = 0.0f, best = 1e9f;
        for (float t = 0.0f; t < timeout; t += dt) {
            core::Vec3 p = pc.position();
            float dx = to.x - p.x, dz = to.z - p.z, d = std::sqrt(dx * dx + dz * dz);
            if (d < arriveR) { r.arrived = true; r.t = t; break; }
            if (d < best - 0.25f) { best = d; lastProgT = t; }
            if (t - lastProgT > 1.5f) break;                       // no progress for 1.5 s: blocked
            ctl.setCameraYaw(std::atan2(-dx, -dz));
            core::Vec3 chest0 = pc.position() + core::Vec3{0, 1.0f, 0};
            world_.handleInput(in, dt);
            world_.tick(dt);
            core::Vec3 chest1 = pc.position() + core::Vec3{0, 1.0f, 0};
            float th;
            if (core::length(chest1 - chest0) < 3.0f && renderCol.segmentHit(chest0, chest1, th)) {
                core::Vec3 q = chest0 + (chest1 - chest0) * th;
                if (const RAct* a = renderActorAt(q, 0.05f)) {
                    Pass& ps = passThrough[a->comp.empty() ? a->name : a->comp];
                    ps.rep = a->rep; ps.mesh = a->mesh; ps.at = q; ++ps.n;
                    ps.actor = a->name.substr(0, a->name.find('.')); ps.rlo = a->lo; ps.rhi = a->hi;
                }
            }
            r.ymin = std::min(r.ymin, pc.position().y); r.ymax = std::max(r.ymax, pc.position().y);
            if (pc.position().y < -749.0f || !std::isfinite(pc.position().y)) { r.fell = true; break; }
            r.t = t;
        }
        r.end = pc.position();
        if (!r.arrived && !r.fell) {
            core::Vec3 fwd = core::normalize(core::Vec3{to.x - r.end.x, 0.0f, to.z - r.end.z});
            r.blockers = world_.collisionActorsAt(r.end + fwd * 1.0f + core::Vec3{0, 1.0f, 0}, 0.4f);
            // Is there visible geometry where the pawn is stopped (chest ray 4 m toward the target)?
            core::Vec3 c0 = r.end + core::Vec3{0, 1.0f, 0}, c1 = c0 + fwd * 4.0f;
            float th;
            if (renderCol.segmentHit(c0, c1, th)) {
                const RAct* a = renderActorAt(c0 + (c1 - c0) * th, 0.05f);
                r.blockers += std::string(" visible=") + (a ? a->name + "(" + a->rep + ")" : "?");
                ++blockVisible[a ? a->rep : "?"];
            } else {
                r.blockers += " visible=NONE";
                ++blockInvisible[r.blockers.substr(0, r.blockers.find(' '))];
            }
        }
        return r;
    };
    // Floor continuity along a + (b-a)s at the interpolated height (robot step-up 0.45 m, drop <= 3 m).
    auto floorGaps = [&](const core::Vec3& a, const core::Vec3& b) {
        int gaps = 0;
        float ga = a.y, gb = b.y, gy; core::Vec3 gn;
        if (col->groundHeight(a.x, a.z, a.y + 1.5f, 3.0f, gy, gn)) ga = gy;
        if (col->groundHeight(b.x, b.z, b.y + 1.5f, 3.0f, gy, gn)) gb = gy;
        float len = std::sqrt((b.x - a.x) * (b.x - a.x) + (b.z - a.z) * (b.z - a.z));
        int n = std::max(1, (int)(len / 0.5f));
        for (int i = 1; i < n; ++i) {
            float s = (float)i / n;
            core::Vec3 q{a.x + (b.x - a.x) * s, ga + (gb - ga) * s, a.z + (b.z - a.z) * s};
            if (!col->groundHeight(q.x, q.z, q.y + 1.5f, 4.5f, gy, gn)) ++gaps;
        }
        return gaps;
    };

    const char* formName[2] = {"robot", "vehicle"};
    int edgeRuns = 0, edgePass = 0, edgeFall = 0, edgeGapEdges = 0;
    std::map<std::string, int> blockerCount;
    for (int form = 0; form < 2; ++form) {
        game::Form fm = form ? game::Form::Vehicle : game::Form::Robot;
        float speed = form ? 10.0f : core::config::kRobotMoveSpeed;
        for (const Edge& e : edges) {
            const Node& A = nodes[(size_t)e.a]; const Node& B = nodes[(size_t)e.b];
            place(A.p, fm);
            float dx = B.p.x - A.p.x, dz = B.p.z - A.p.z, D = std::sqrt(dx * dx + dz * dz);
            Result r = run(B.p, form ? 2.0f : 1.0f, D / speed * 2.5f + 3.0f);
            int gaps = form == 0 ? floorGaps(A.p, B.p) : 0;
            ++edgeRuns; edgePass += r.arrived; edgeFall += r.fell; edgeGapEdges += gaps > 0;
            if (!r.arrived) ++blockerCount[r.blockers];
            LOG_INFO("MAPTRAVERSE edge %-7s %-18s %s -> %s (%.1f m, r%.0f): %s t=%.2f y %.2f..%.2f%s%s%s", formName[form], e.spec.c_str(),
                     A.name.c_str(), B.name.c_str(), D, e.radius, r.arrived ? "ARRIVED" : (r.fell ? "FELL" : "BLOCKED"), r.t,
                     r.ymin, r.ymax, gaps ? (" floor-gaps=" + std::to_string(gaps)).c_str() : "",
                     r.arrived ? "" : (" at (" + std::to_string(r.end.x) + " " + std::to_string(r.end.y) + " " + std::to_string(r.end.z) + ")").c_str(),
                     r.arrived ? "" : (" blockers=" + r.blockers).c_str());
        }
    }
    LOG_INFO("MAPTRAVERSE ORACLE: %d/%d authored ReachSpec runs arrived (robot + vehicle), %d fell, %d robot edges with floor gaps",
             edgePass, edgeRuns, edgeFall, edgeGapEdges);
    for (const auto& [b, n] : blockerCount) LOG_INFO("MAPTRAVERSE ORACLE blocked x%d by %s", n, b.c_str());

    // Tour: nearest-neighbour order through every nav point (coverage of the whole authored playable area).
    std::vector<int> order{0};
    std::vector<bool> used(nodes.size(), false); used[0] = true;
    for (size_t k = 1; k < nodes.size(); ++k) {
        const core::Vec3& c = nodes[(size_t)order.back()].p;
        int bi = -1; float bd = 1e18f;
        for (size_t j = 0; j < nodes.size(); ++j) {
            if (used[j]) continue;
            float d = (nodes[j].p.x - c.x) * (nodes[j].p.x - c.x) + (nodes[j].p.z - c.z) * (nodes[j].p.z - c.z) + 4.0f * (nodes[j].p.y - c.y) * (nodes[j].p.y - c.y);
            if (d < bd) { bd = d; bi = (int)j; }
        }
        used[(size_t)bi] = true; order.push_back(bi);
    }
    for (int form = 0; form < 2; ++form) {
        game::Form fm = form ? game::Form::Vehicle : game::Form::Robot;
        float speed = form ? 10.0f : core::config::kRobotMoveSpeed;
        int legs = 0, arrived = 0, fell = 0;
        float travelled = 0.0f, ylo = 1e9f, yhi = -1e9f;
        for (size_t k = 0; k + 1 < order.size(); ++k) {
            const Node& A = nodes[(size_t)order[k]]; const Node& B = nodes[(size_t)order[k + 1]];
            place(A.p, fm);
            float dx = B.p.x - A.p.x, dz = B.p.z - A.p.z, D = std::sqrt(dx * dx + dz * dz);
            Result r = run(B.p, form ? 2.0f : 1.0f, D / speed * 2.5f + 3.0f);
            ++legs; arrived += r.arrived; fell += r.fell;
            core::Vec3 e = r.end;
            travelled += std::sqrt((e.x - A.p.x) * (e.x - A.p.x) + (e.z - A.p.z) * (e.z - A.p.z));
            ylo = std::min(ylo, r.ymin); yhi = std::max(yhi, r.ymax);
            LOG_INFO("MAPTRAVERSE tour %-7s %3zu %s -> %s (%.1f m): %s t=%.2f y %.2f..%.2f%s", formName[form], k, A.name.c_str(), B.name.c_str(), D,
                     r.arrived ? "ARRIVED" : (r.fell ? "FELL" : "BLOCKED"), r.t, r.ymin, r.ymax,
                     r.arrived ? "" : (" at (" + std::to_string(e.x) + " " + std::to_string(e.y) + " " + std::to_string(e.z) + ") blockers=" + r.blockers).c_str());
        }
        LOG_INFO("MAPTRAVERSE TOUR %s: %d/%d legs arrived, %d fell, %.0f m travelled, y %.1f..%.1f", formName[form], arrived, legs, fell, travelled, ylo, yhi);
    }
    // Boost / jump / transform sweeps from every nav point (4 headings each), with the same pass-through check and a
    // camera check: the camera segment (anchor -> camera) crossing visible geometry.
    std::map<std::string, std::pair<int, bool>> camThrough;   // component -> (frames, authored BlockCameras)
    int camFrames = 0;
    auto stepChecked = [&](const platform::InputFrame& in) {
        core::Vec3 chest0 = pc.position() + core::Vec3{0, 1.0f, 0};
        world_.handleInput(in, dt);
        world_.tick(dt);
        core::Vec3 chest1 = pc.position() + core::Vec3{0, 1.0f, 0};
        float th;
        if (core::length(chest1 - chest0) < 3.0f && renderCol.segmentHit(chest0, chest1, th))
            if (const RAct* a = renderActorAt(chest0 + (chest1 - chest0) * th, 0.05f)) {
                Pass& ps = passThrough[a->comp.empty() ? a->name : a->comp];
                ps.rep = a->rep; ps.mesh = a->mesh; ps.at = chest0 + (chest1 - chest0) * th; ++ps.n;
                ps.actor = a->name.substr(0, a->name.find('.')); ps.rlo = a->lo; ps.rhi = a->hi;
            }
        core::Vec3 anchor = pc.actorLocation() + core::Vec3{0, 1.0f, 0}, cam = ctl.cameraPos();
        ++camFrames;
        if (renderCol.segmentHit(anchor, cam, th))
            if (const RAct* a = renderActorAt(anchor + (cam - anchor) * th, 0.05f)) {
                auto it = compBlockCam.find(a->comp);
                auto& e = camThrough[a->comp.empty() ? a->name : a->comp];
                ++e.first; e.second = a->rep == "bsp" || (it != compBlockCam.end() && it->second);
            }
    };
    int sweeps = 0, sweepFalls = 0, sweepOut = 0, transforms = 0, transformBad = 0;
    float sweepTop = -1e9f;
    for (size_t ni = 0; ni < nodes.size(); ++ni) {
        for (int dir = 0; dir < 4; ++dir) {
            float yaw = dir * 1.5707963f;
            // (a) vehicle: hover, boost 2.5 s, jump at 1.0 s.
            place(nodes[ni].p, game::Form::Vehicle);
            ctl.setCameraYaw(yaw);
            for (int k = 0; k < (int)(2.5f / dt); ++k) {
                platform::InputFrame in;
                in.down[(int)platform::Button::Forward] = true;
                in.down[(int)platform::Button::FineAim] = k > 6;
                in.pressed[(int)platform::Button::FineAim] = k == 7;
                in.down[(int)platform::Button::Jump] = in.pressed[(int)platform::Button::Jump] = k == 60;
                stepChecked(in);
                sweepTop = std::max(sweepTop, pc.position().y);
                if (pc.position().y < -749.0f) { ++sweepFalls; break; }
            }
            core::Vec3 e = pc.position();
            if (e.x < bmn.x || e.x > bmx.x || e.z < bmn.z || e.z > bmx.z) ++sweepOut;
            ++sweeps;
            // (b) robot: run + jump.
            place(nodes[ni].p, game::Form::Robot);
            ctl.setCameraYaw(yaw);
            for (int k = 0; k < (int)(2.0f / dt); ++k) {
                platform::InputFrame in;
                in.down[(int)platform::Button::Forward] = true;
                in.down[(int)platform::Button::Jump] = in.pressed[(int)platform::Button::Jump] = k == 20;
                stepChecked(in);
                if (pc.position().y < -749.0f) { ++sweepFalls; break; }
            }
            ++sweeps;
            // (c) transform robot -> vehicle -> robot where the run ended (next to geometry when blocked), after
            //     settling on the floor (a run may end mid-fall off a ledge).
            { platform::InputFrame none; for (int k = 0; k < (int)(2.0f / dt); ++k) stepChecked(none); }
            float y0 = pc.position().y;
            for (int pass = 0; pass < 2; ++pass) {
                platform::InputFrame tin; tin.down[(int)platform::Button::Transform] = tin.pressed[(int)platform::Button::Transform] = true;
                stepChecked(tin);
                platform::InputFrame none;
                for (int k = 0; k < (int)(2.0f / dt); ++k) stepChecked(none);
                ++transforms;
                float gy; core::Vec3 gn;
                bool floor = col->groundHeight(pc.position().x, pc.position().z, pc.position().y + 1.0f, 3.0f, gy, gn);
                if (!floor || std::fabs(pc.position().y - y0) > 1.0f || pc.position().y < -749.0f) {
                    ++transformBad;
                    LOG_INFO("MAPTRAVERSE transform at %s dir %d: y %.2f -> %.2f floor=%d form %s", nodes[ni].name.c_str(), dir, y0, pc.position().y,
                             (int)floor, game::formName(pc.form()));
                }
            }
        }
    }
    LOG_INFO("MAPTRAVERSE SWEEP: %d boost/jump runs from %zu nav points x 4 headings, %d fell below KillZ, %d left the collision bounds, max y %.1f; %d transforms, %d without floor / height change > 1 m",
             sweeps, nodes.size(), sweepFalls, sweepOut, sweepTop, transforms, transformBad);
    int camAuth = 0, camPass = 0;
    for (const auto& [comp, e] : camThrough) {
        (e.second ? camAuth : camPass) += 1;
        LOG_INFO("MAPTRAVERSE CAMERA view through visible %s x%d frames (authored BlockCameras=%d)", comp.c_str(), e.first, (int)e.second);
    }
    LOG_INFO("MAPTRAVERSE CAMERA: %d frames checked; camera behind visible geometry: %d components authored BlockCameras (camera should have been kept in front), %d not camera-blocking (authored)",
             camFrames, camAuth, camPass);

    // Coherence summary.
    for (const auto& [rep, n] : blockVisible) LOG_INFO("MAPTRAVERSE COHERENCE blocked at visible geometry (pawn rep %s): %d", rep.c_str(), n);
    for (const auto& [who, n] : blockInvisible) LOG_INFO("MAPTRAVERSE COHERENCE blocked with NO visible geometry within 4 m: %d by %s", n, who.c_str());
    // A component authored to block pawns (simple / per_poly) can still be crossed where its visual mesh extends
    // beyond its authored simple collision hull (UE3 pawn traces use the BodySetup hull, not the render mesh).
    // Classify by the component's own collision node (same actor + mesh, overlapping the render bounds):
    //   HULL-SMALLER-THAN-VISUAL = hull present and placed with the render component, point outside it (authored);
    //   MISSING-OR-DISPLACED     = no collision node of that actor+mesh overlaps the render component (defect).
    int authoredPass = 0, hullPass = 0, defectPass = 0;
    for (const auto& [comp, ps] : passThrough) {
        std::string cls;
        if (ps.rep == "none") { cls = "authored: no pawn collision"; ++authoredPass; }
        else {
            const game::World::ColActor* hull = nullptr; float bestOv = 0.0f;
            for (const auto& c : world_.collisionActors()) {
                if (c.name != ps.actor || c.mesh != ps.mesh) continue;
                float ox = std::min(c.hi.x, ps.rhi.x) - std::max(c.lo.x, ps.rlo.x);
                float oy = std::min(c.hi.y, ps.rhi.y) - std::max(c.lo.y, ps.rlo.y);
                float oz = std::min(c.hi.z, ps.rhi.z) - std::max(c.lo.z, ps.rlo.z);
                if (ox <= 0 || oy <= 0 || oz <= 0) continue;
                float ov = ox * oy * oz / std::max(1e-6f, (c.hi.x - c.lo.x) * (c.hi.y - c.lo.y) * (c.hi.z - c.lo.z));
                if (ov > bestOv) { bestOv = ov; hull = &c; }
            }
            char buf[256];
            if (hull) {
                std::snprintf(buf, sizeof buf, "HULL-SMALLER-THAN-VISUAL: authored hull (%.1f..%.1f, %.1f..%.1f, %.1f..%.1f) inside render bounds (%.1f..%.1f, %.1f..%.1f, %.1f..%.1f)",
                              hull->lo.x, hull->hi.x, hull->lo.y, hull->hi.y, hull->lo.z, hull->hi.z, ps.rlo.x, ps.rhi.x, ps.rlo.y, ps.rhi.y, ps.rlo.z, ps.rhi.z);
                ++hullPass;
            } else { std::snprintf(buf, sizeof buf, "MISSING-OR-DISPLACED collision for this component [DEFECT]"); ++defectPass; }
            cls = buf;
        }
        LOG_INFO("MAPTRAVERSE COHERENCE passed through visible %s x%d (%s) mesh %s at (%.1f %.1f %.1f): %s", comp.c_str(), ps.n, ps.rep.c_str(),
                 ps.mesh.c_str(), ps.at.x, ps.at.y, ps.at.z, cls.c_str());
    }
    LOG_INFO("MAPTRAVERSE COHERENCE: visible components crossed: %d authored no-pawn-collision, %d where the authored simple hull is smaller than the mesh, %d with missing/displaced collision",
             authoredPass, hullPass, defectPass);
}

void Application::shutdown() {
    shutdownFrontend();   // the frontend (movies, Systems audio seam) goes before the devices it uses
    delete audio_; audio_ = nullptr;
    delete renderer_; renderer_ = nullptr;
    delete window_; window_ = nullptr;
    LOG_INFO("Shutdown complete");
}

} // namespace core
