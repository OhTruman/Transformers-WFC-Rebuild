#include <chrono>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif
#include "render/HudMarkers.h"
#include "core/Application.h"
#include "core/Config.h"
#include "core/Debug.h"
#include "core/Log.h"
#include "platform/Window.h"
#include "render/Renderer.h"
#include "game/VehicleTests.h"
#include "frontend/FrontendRuntime.h"
#include "ui/GfxPresenter.h"
#include "game/MapState.h"
#include "game/Match.h"
#include "game/Collision.h"
#include "game/PickupFactory.h"
#include "game/Destructible.h"
#include "assets/Gltf.h"
#include "assets/Json.h"

#include <algorithm>
#include <array>
#include <string>

#include <chrono>

#include <cmath>
#include <cstdio>
#include <utility>
#include <array>
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
    if (std::getenv("WFC_CHASSISTEST")) {
        game::runChassisTests(std::getenv("WFC_ASSETS") ? std::getenv("WFC_ASSETS") : core::config::kAssetRootDefault);
        return false;
    }
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
                ms.matchStarting();                                     // zones are Inactive until MatchStarting
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
    if (std::getenv("WFC_XFORMTEST")) { runTransformStress(); return false; }  // measurements only
    if (std::getenv("WFC_MATCHTEST")) { runMatchTest(); return false; }        // measurements only
    if (std::getenv("WFC_CAMTEST")) { runCameraTest(); return false; }         // measurements only
    if (std::getenv("WFC_CHAOS")) { runChaosTest(); return false; }            // measurements only
    if (std::getenv("WFC_TDMTEST")) { runTdmSessionTest(); return false; }     // measurements only
    if (std::getenv("WFC_CAMSYNC")) { runCameraSyncTest(); return false; }     // measurements only
    if (std::getenv("WFC_MODEPLAYTEST")) { runModePlayTest(); return false; }  // measurements only
    world_.setAudio(audio_);
    // WFC_CHASSIS=<UniqueId>: boot as that chassis (free play), or select it as the iconic character in a launched match.
    const char* bootChassis = std::getenv("WFC_CHASSIS");
    if (bootChassis && !world_.applyChassisToLocalPawn(bootChassis)) LOG_ERROR("WFC_CHASSIS=%s: chassis unavailable", bootChassis);
    // Local versus match (launch-independent runtime; a front end will call World::startLocalMatch the same way).
    // WFC_MATCH_URL=<StartLevel URL> (the Frontend contract) or WFC_MATCH=TDM|DM (authored defaults).
    {
        game::MatchLaunch launch;
        bool want = false;
        if (const char* u = std::getenv("WFC_MATCH_URL")) want = game::MatchLaunch::fromURL(u, launch);
        else if (const char* mm = std::getenv("WFC_MATCH")) { want = game::MatchLaunch::fromURL(world_.mapName() + "?GameModeTag=" + mm, launch); }   // the loaded map (WFC_MAP)
        if (want && world_.launchMatch(launch) && bootChassis) {
            game::CharacterSelection sel; sel.type = 1; sel.chassisId = bootChassis;
            world_.match().selectCharacter(world_.localMatchPlayer(), sel);
        }
        if (want && world_.matchActive())
            if (const char* n = std::getenv("WFC_MATCH_OPPONENTS"))   // diagnostic only: static synthetic participants (drawn boxes)
                for (int i = 0; i < std::atoi(n); ++i) world_.addMatchOpponent("Opponent" + std::to_string(i), true);
    }
    gameMode_.begin(world_);

    window_->setMouseCaptured(true);
    mouseCaptured_ = true;

    LOG_INFO("Init complete. Controls: WASD move, mouse look, Space jump, F transform, "
             "C toggle cursor, Esc quit.");
    return true;
}

void Application::run() {
    // Diagnostics: a frontend 3D scene through the renderer contract, without the frontend runtime.
    // WFC_FRONTENDSCENE=<level>[,<level>...]; WFC_SCENECAM=x,y,z,pitch,yaw,roll,fov (UE units / degrees; default the
    // UI_FrontEnd_m title camera CameraActor_6585); WFC_SMOKE_FRAMES / WFC_SHOT as usual.
    if (const char* ft = std::getenv("WFC_FOVTEST")) {   // diagnostics: <SeqAct_Interp> FOVAngle track in the lobby scene
        if (renderer_->loadFrontendScene({"UI_PartyLobby_m", "UI_CharacterCustomization_m"})) {
            std::vector<render::IRenderer::InterpKeyF> keys;
            for (const auto& t : renderer_->frontendFloatTracks()) {
                LOG_INFO("FOVTEST track %s \"%s\" %s.%s: %zu keys", t.seqActInterp.c_str(), t.matineeComment.c_str(),
                         t.group.c_str(), t.property.c_str(), t.keys.size());
                if (t.seqActInterp == ft && t.property == "FOVAngle") keys = t.keys;
            }
            for (float tt : {0.0f, 0.125f, 0.25f, 0.375f, 0.5f, 1.0f})
                LOG_INFO("FOVTEST %s t=%.3f FOVAngle=%.3f", ft, tt, render::IRenderer::evalInterpCurveFloat(keys, tt, 70.0f));
            platform::InputFrame in;
            for (float tt : {0.0f, 0.5f}) {
                float fov = render::IRenderer::evalInterpCurveFloat(keys, tt, 70.0f);
                for (int f = 0; f < 30 && window_->pump(in); ++f) {
                    renderer_->drawFrontendScene(core::Vec3{784.123f, 5252.110f, 234.995f}, core::Vec3{-2.988f, -175.605f, 0}, fov,
                                                 window_->width(), window_->height(), f / 60.0);
                    if (f == 29) if (const char* sh = std::getenv("WFC_SHOT"))
                        renderer_->captureScreenshot((std::string(sh) + "_t" + std::to_string((int)(tt * 1000)) + ".bmp").c_str());
                    window_->present();
                }
            }
            renderer_->unloadFrontendScene();
        }
        return;
    }
    if (const char* mc = std::getenv("WFC_MEMCYCLE")) {   // diagnostics: renderer-only map load / unload memory cycle
        auto privMB = [] {
#ifdef _WIN32
            PROCESS_MEMORY_COUNTERS_EX pmc{};
            if (K32GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof pmc))
                return pmc.PrivateUsage / (1024.0 * 1024.0);
#endif
            return 0.0;
        };
        std::string list = mc;
        platform::InputFrame in;
        LOG_INFO("MEMCYCLE start private %.1f MB", privMB());
        for (size_t a = 0; a <= list.size();) {
            size_t b = list.find(',', a);
            std::string m = list.substr(a, b == std::string::npos ? std::string::npos : b - a);
            if (renderer_->loadFrontendScene({m})) {
                for (int f = 0; f < 30 && window_->pump(in); ++f) {
                    renderer_->drawFrontendScene(core::Vec3{0, 0, 300}, core::Vec3{0, 0, 0}, 90, window_->width(), window_->height(), f / 60.0);
                    window_->present();
                }
                double loaded = privMB();
                renderer_->unloadFrontendScene();
                LOG_INFO("MEMCYCLE %s loaded %.1f MB unloaded %.1f MB | GL %s", m.c_str(), loaded, privMB(),
                         renderer_->glObjectCensus().c_str());
            } else LOG_WARN("MEMCYCLE %s: no render data", m.c_str());
            if (b == std::string::npos) break;
            a = b + 1;
        }
        return;
    }
    if (const char* fs = std::getenv("WFC_FRONTENDSCENE")) {
        std::vector<std::string> levels;
        std::string s = fs;
        for (size_t a = 0; a <= s.size();) {
            size_t b = s.find(',', a);
            levels.push_back(s.substr(a, b == std::string::npos ? std::string::npos : b - a));
            if (b == std::string::npos) break;
            a = b + 1;
        }
        float c[7] = {-6701.84f, -15212.47f, 237.44f, -111.0f * 360.0f / 65536.0f, 13184.0f * 360.0f / 65536.0f,
                      62.0f * 360.0f / 65536.0f, 45.0f};
        if (const char* sc = std::getenv("WFC_SCENECAM"))
            std::sscanf(sc, "%f,%f,%f,%f,%f,%f,%f", &c[0], &c[1], &c[2], &c[3], &c[4], &c[5], &c[6]);
        static int yields = 0;
        static auto lastYield = std::chrono::steady_clock::now();
        static double maxGapMs = 0.0;
        renderer_->setLoadYield([&] {
            auto now = std::chrono::steady_clock::now();
            double gap = std::chrono::duration<double, std::milli>(now - lastYield).count();
            if (gap > 40.0) LOG_INFO("load yield %d after a %.0f ms step", yields, gap);
            maxGapMs = std::max(maxGapMs, gap);
            lastYield = now;
            ++yields;
        });
        auto loadT0 = std::chrono::steady_clock::now();
        lastYield = loadT0;
        bool ok = renderer_->loadFrontendScene(levels);
        renderer_->setLoadYield({});
        LOG_INFO("frontend scene load: %.0f ms, %d yields, longest gap %.1f ms",
                 std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - loadT0).count(), yields, maxGapMs);
        LOG_INFO("frontend scene test: load %s", ok ? "ok" : "FAILED");
        const long frames = std::getenv("WFC_SMOKE_FRAMES") ? std::atol(std::getenv("WFC_SMOKE_FRAMES")) : 120;
        platform::InputFrame sceneInput;
        if (const char* uh = std::getenv("WFC_SCENEUNHIDE")) {   // diagnostics: actor names to unhide (a,b,...)
            std::string u = uh;
            for (size_t x = 0; x <= u.size();) {
                size_t y = u.find(',', x);
                renderer_->setActorHidden(u.substr(x, y == std::string::npos ? std::string::npos : y - x), false);
                if (y == std::string::npos) break;
                x = y + 1;
            }
        }
        if (const char* gs = std::getenv("WFC_GAMMASETTING"))   // diagnostics: profile Brightness 0..100
            renderer_->setDisplayGamma(2.2f + (-0.95f + 1.9f * std::min(std::max((float)std::atof(gs) / 100.0f, 0.0f), 1.0f)));
        // diagnostics: a roster body drawn through setFrontendSceneDraw:
        //   WFC_SCENEPREVIEW=<content glTF>|x,y,z,yawDeg|r,g,b;r,g,b[#<next body>...]   (bind pose; linear colours, optional)
        // optional 4th / 5th fields: AnimSets (a;b;...) and the sequence: loadPreviewBody + posePreviewBody per frame
        struct PreviewBody { render::MeshData mesh; core::Mat4 model; render::CharacterColors colors; int body = -1; float fixedT = -1.0f; };
        static std::vector<PreviewBody> previews;
        if (const char* pv = std::getenv("WFC_SCENEPREVIEW")) {
            std::string all = pv;
            for (size_t p0 = 0; p0 <= all.size();) {
                size_t p1 = all.find('#', p0);
                std::string s = all.substr(p0, p1 == std::string::npos ? std::string::npos : p1 - p0);
                size_t a = s.find('|'), b = a == std::string::npos ? a : s.find('|', a + 1);
                PreviewBody body;
                float x = 0, y = 0, z = 0, yaw = 0;
                if (a != std::string::npos) std::sscanf(s.c_str() + a + 1, "%f,%f,%f,%f", &x, &y, &z, &yaw);
                if (b != std::string::npos)
                    std::sscanf(s.c_str() + b + 1, "%f,%f,%f;%f,%f,%f", &body.colors.primary[0], &body.colors.primary[1],
                                &body.colors.primary[2], &body.colors.secondary[0], &body.colors.secondary[1],
                                &body.colors.secondary[2]);
                if (std::getenv("WFC_SCENEPREVIEW_SNAP")) {   // FindGround: the floor under the spawn point
                    float g = 0.0f;
                    bool ok = renderer_->sceneGroundHeight(x, y, z, g);
                    LOG_INFO("WFC_SCENEPREVIEW ground under (%.1f, %.1f) from z %.1f: %s %.1f", x, y, z, ok ? "hit" : "none", g);
                    if (ok) z = g;
                }
                size_t c3 = b == std::string::npos ? b : s.find('|', b + 1), c4 = c3 == std::string::npos ? c3 : s.find('|', c3 + 1);
                if (c3 != std::string::npos && c4 != std::string::npos) {
                    std::vector<std::string> sets;
                    std::string sl = s.substr(c3 + 1, c4 - c3 - 1);
                    for (size_t q0 = 0; q0 <= sl.size();) {
                        size_t q1 = sl.find(';', q0);
                        sets.push_back(sl.substr(q0, q1 == std::string::npos ? std::string::npos : q1 - q0));
                        if (q1 == std::string::npos) break;
                        q0 = q1 + 1;
                    }
                    std::string an = s.substr(c4 + 1);
                    size_t at = an.find('@');                     // "anim@seconds": sample at a fixed time
                    if (at != std::string::npos) { body.fixedT = (float)std::atof(an.c_str() + at + 1); an = an.substr(0, at); }
                    body.body = renderer_->loadPreviewBody(s.substr(0, a), sets, an);
                }
                if (renderer_->loadContentMesh(s.substr(0, a), body.mesh)) {
                    body.model = renderer_->actorMatrix(core::Vec3{x, y, z}, core::Vec3{0, yaw, 0});
                    previews.push_back(std::move(body));
                } else LOG_WARN("WFC_SCENEPREVIEW: cannot load %s", s.substr(0, a).c_str());
                if (p1 == std::string::npos) break;
                p0 = p1 + 1;
            }
            renderer_->setFrontendSceneDraw([](render::IRenderer& r) {
                static int frame = 0;
                ++frame;
                for (size_t i = 0; i < previews.size(); ++i) {
                    r.setDrawOwner(1 + (int)i);
                    r.setCharacterColors(previews[i].colors);
                    if (previews[i].body >= 0)
                        r.posePreviewBody(previews[i].body, previews[i].fixedT >= 0 ? previews[i].fixedT : frame / 60.0f, previews[i].mesh);
                    r.drawDynamicMesh(previews[i].mesh, previews[i].model, core::Vec3{1, 1, 1});
                }
            });
        }
        if (const char* fx = std::getenv("WFC_SCENEFX")) {   // diagnostics: activate scene emitters (a,b,...)
            std::string u = fx;
            for (size_t x = 0; x <= u.size();) {
                size_t y = u.find(',', x);
                renderer_->setMapEffectActive(u.substr(x, y == std::string::npos ? std::string::npos : y - x), true);
                if (y == std::string::npos) break;
                x = y + 1;
            }
        }
        if (const char* ss = std::getenv("WFC_SCENESCALE")) {   // diagnostics: actor,drawScale[;actor,drawScale...]
            std::string all = ss;
            for (size_t p0 = 0; p0 <= all.size();) {
                size_t p1 = all.find(';', p0);
                std::string t = all.substr(p0, p1 == std::string::npos ? std::string::npos : p1 - p0);
                size_t c = t.find(',');
                if (c != std::string::npos) renderer_->setFrontendActorScale(t.substr(0, c), (float)std::atof(t.c_str() + c + 1));
                if (p1 == std::string::npos) break;
                p0 = p1 + 1;
            }
        }
        if (const char* sp = std::getenv("WFC_SCENEPOSE")) {     // diagnostics: actor,x,y,z,pitch,yaw,roll (UE, deg)
            char name[128] = {0}; float v[6] = {0};
            if (std::sscanf(sp, "%127[^,],%f,%f,%f,%f,%f,%f", name, &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 7)
                renderer_->setFrontendActorTransform(name, core::Vec3{v[0], v[1], v[2]}, core::Vec3{v[3], v[4], v[5]});
        }
        for (long f = 1; f <= frames && window_->pump(sceneInput); ++f) {
            renderer_->drawFrontendScene(core::Vec3{c[0], c[1], c[2]}, core::Vec3{c[3], c[4], c[5]}, c[6],
                                         window_->width(), window_->height(), f / 60.0);
            if (f == frames) if (const char* shot = std::getenv("WFC_SHOT")) renderer_->captureScreenshot(shot);
            window_->present();
        }
        renderer_->unloadFrontendScene();
        return;
    }
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
        // Diagnostics: deterministic display rate other than the 60 Hz simulation (WFC_RENDERHZ=144 -> 0 or 1 steps
        // per frame), to reproduce frame-pacing artefacts in captures.
        if (const char* hz = std::getenv("WFC_RENDERHZ")) realDt = 1.0 / std::max(1.0, std::atof(hz));

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
            if (frontend_->flow().ui().state() != frontend::UIState::InGame || frontend_->scoreboardOpen()) {
                platform::InputFrame none;
                input = none;
                if (mouseCaptured_) { mouseCaptured_ = false; window_->setMouseCaptured(false); }
                uiReleasedMouse_ = true;
            } else if (uiReleasedMouse_) {
                // The menu closed (Resume / respawn): mouse-look again.
                uiReleasedMouse_ = false;
                if (!mouseCaptured_) { mouseCaptured_ = true; window_->setMouseCaptured(true); }
            }
            window_->setOsCursorHidden(presenter_ && presenter_->drawsCursor());
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
        if (const char* s = std::getenv("WFC_PRESSTRANSFORM_EVERY"))    // soak: transform every N frames
            if (long n = std::atol(s); n > 0 && frame > 0 && frame % n == 0) input.pressed[(int)platform::Button::Transform] = true;

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
            if (frontend_) { driveLifecycleTest(step); routeMatchToFrontend(step); }   // Gameplay match events -> frontend flow (per tick)
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
        if (std::getenv("WFC_CAMLOG")) {   // diagnostics: pawn screen position per rendered frame (frame pacing)
            core::Vec3 pp = world_.player().pawn().position() + core::Vec3{0, 2.0f, 0};
            core::Vec3 f = core::forwardFromYawPitch(camera_.yaw, camera_.pitch);
            core::Vec3 r = core::normalize(core::cross(f, core::Vec3{0, 1, 0}));
            core::Vec3 u = core::cross(r, f);
            core::Vec3 d = pp - camera_.pos;
            float z = core::dot(d, f);
            LOG_INFO("CAMLOG %ld %.5f %.5f %.4f", (long)frame, core::dot(d, r) / std::max(z, 0.01f),
                     core::dot(d, u) / std::max(z, 0.01f), z);
        }
        camera_.aspect = (float)window_->width() / (float)(window_->height() > 0 ? window_->height() : 1);
        world_.player().controller().setViewAspect(camera_.aspect);

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

        // Diagnostic: level-travel render-data cycle (WFC_RELOADTEST=<frame>): release everything, reload the map,
        // re-load the world's meshes (as a travel back into the match does).
        if (const char* rt = std::getenv("WFC_RELOADTEST")) {
            if (frame == std::atoi(rt)) {
                renderer_->unloadMapRenderData();
                world_.load(*renderer_);
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
        if (std::getenv("WFC_PICK")) {                 // diagnostics: authored source of the surface under the crosshair
            static render::IRenderer::PickHit last;
            static bool lastOk = false;
            static long pickFrame = -100;
            static float pickCol = -1.0f;
            if (frame - pickFrame >= 10) {             // CPU ray over the world mesh: every 10 frames
                pickFrame = frame;
                lastOk = renderer_->pickWorld(camera_.pos, core::forwardFromYawPitch(camera_.yaw, camera_.pitch), 500.0f, last);
                // the movement collision world along the same ray: "rendered but no pawn collision" reports
                pickCol = -1.0f;
                if (const game::CollisionWorld* cw = world_.collision()) {
                    core::Vec3 dir = core::forwardFromYawPitch(camera_.yaw, camera_.pitch);
                    float t;
                    if (cw->segmentHit(camera_.pos, camera_.pos + dir * 500.0f, t)) pickCol = t * 500.0f;
                }
                if (lastOk) LOG_INFO("PICK %s | %s | %s | %.2f m | n=(%.2f %.2f %.2f) at (%.2f %.2f %.2f)", last.component.c_str(),
                                     last.mesh.c_str(), last.material.c_str(), last.distance, last.normal.x, last.normal.y,
                                     last.normal.z, last.point.x, last.point.y, last.point.z);
            }
            const uint8_t col[4] = {255, 230, 120, 255};
            std::string line1 = lastOk ? last.component.substr(last.component.find("PersistentLevel.") == std::string::npos
                                                                    ? 0 : last.component.find("PersistentLevel.") + 16)
                                       : std::string("(no static mesh: BSP / sky / none)");
            std::string line2 = lastOk ? last.mesh + "  " + last.material : std::string();
            char d[128];
            if (pickCol < 0.0f) std::snprintf(d, sizeof d, "  %.1f m  | collision: NONE on this ray", lastOk ? last.distance : 0.0f);
            else if (lastOk && std::fabs(pickCol - last.distance) > 0.25f)
                std::snprintf(d, sizeof d, "  %.1f m  | collision at %.1f m (differs)", last.distance, pickCol);
            else std::snprintf(d, sizeof d, "  %.1f m  | collision ok", lastOk ? last.distance : pickCol);
            const float y = (float)window_->height() * 0.86f;
            renderer_->drawCanvasText("MarkerFont", line1 + d, 20.0f, y, col);
            if (!line2.empty()) renderer_->drawCanvasText("MarkerFont", line2, 20.0f, y + 22.0f, col);
        }
        if (std::getenv("WFC_MARKERTEST")) {           // diagnostics: TDM player tags (ally + enemy) ahead of the player
            static render::HudMarkers hm;
            static bool hmLoaded = hm.load(render::wfcRenderDataRoot());
            if (hmLoaded) {
                core::Vec3 p = world_.player().pawn().position();
                core::Vec3 f = core::forwardFromYawPitch(camera_.yaw, 0.0f);
                core::Vec3 rt = core::normalize(core::cross(f, core::Vec3{0, 1, 0}));
                std::vector<render::MarkerRequest> ms;
                render::MarkerRequest a;
                a.key = "ally"; a.type = "TnObjectiveMarkerTypeTransformerVersus"; a.setup = "AllyMarkerSetup";
                a.base = p + f * 14.0f + rt * 3.0f + core::Vec3{0, 4.2f, 0}; a.labelZ = 0.0f; a.label = "Bumblebee";
                a.drawHealthBar = true; a.health = 0.6f;
                render::MarkerRequest e = a;
                e.key = "enemy"; e.setup = "EnemyMarkerSetup"; e.base = p + f * 25.0f - rt * 4.0f + core::Vec3{0, 4.2f, 0};
                e.label = "Megatron"; e.drawHealthBar = false;
                ms.push_back(a); ms.push_back(e);
                hm.draw(*renderer_, camera_, window_->width(), window_->height(), ms, 1.0f / 60.0f);
            }
        }
        if (std::getenv("WFC_SCREENTEST")) {            // diagnostics: 2D composition path (fade + panel)
            using RB = render::IRenderer;
            const float W = (float)window_->width(), H = (float)window_->height();
            RB::ScreenBatch fade; fade.blend = RB::ScreenBlend::Alpha;
            auto quad = [](RB::ScreenBatch& b, float x0, float y0, float x1, float y1, uint8_t r, uint8_t g, uint8_t bb, uint8_t a) {
                RB::ScreenVertex v[4] = {{x0, y0, 0, 0, r, g, bb, a}, {x1, y0, 1, 0, r, g, bb, a},
                                         {x1, y1, 1, 1, r, g, bb, a}, {x0, y1, 0, 1, r, g, bb, a}};
                for (int i : {0, 1, 2, 0, 2, 3}) b.verts.push_back(v[i]);
            };
            quad(fade, 0, 0, W, H, 0, 0, 0, 128);                    // 50 % black fade
            renderer_->drawScreenTriangles(fade);
            RB::ScreenBatch panel; panel.blend = RB::ScreenBlend::Additive;
            quad(panel, W * 0.1f, H * 0.8f, W * 0.5f, H * 0.9f, 80, 181, 213, 255);   // friendly label colour
            renderer_->drawScreenTriangles(panel);
        }
        if (std::getenv("WFC_TILETEST")) {             // diagnostics: Canvas material tiles (HUD marker materials)
            const float W = (float)window_->width(), H = (float)window_->height(), S = std::min(W, H);
            auto tile = [&](const char* m, float cx, float cy, float size,
                            std::vector<std::pair<std::string, std::array<float, 4>>> params, float rot = 0.0f) {
                render::IRenderer::MaterialTile t;
                t.material = m; t.w = t.h = size * S; t.x = cx * W - t.w * 0.5f; t.y = cy * H - t.h * 0.5f;
                t.rotation = rot; t.params = std::move(params);
                renderer_->drawMaterialTile(t);
            };
            tile("UI_HudMarkers_p.MPMarkerBase_MAT", 0.30f, 0.30f, 0.05f, {{"OnScreen", {1, 0, 0, 0}}, {"Neutral", {1, 0, 0, 0}}});
            tile("UI_HudMarkers_p.MPMarkerBase_MAT", 0.45f, 0.30f, 0.05f, {{"OnScreen", {1, 0, 0, 0}}, {"Neutral", {0, 0, 0, 0}}});
            tile("UI_HudMarkers_p.MPMarkerBase_MAT", 0.60f, 0.30f, 0.0625f, {{"OnScreen", {0, 0, 0, 0}}, {"ArrowAngle", {90, 0, 0, 0}}});
            tile("UI_HudMarkers_p.MarkerAlly_MAT", 0.30f, 0.55f, 0.05f, {});
            tile("UI_HudMarkers_p.MarkerEnemy_MAT", 0.45f, 0.55f, 0.03125f, {});
            tile("UI_HudMarkers_p.DeathIndicator_MAT", 0.60f, 0.55f, 0.03125f, {{"Alpha", {1, 0, 0, 0}}});
            render::IRenderer::MaterialTile hb;
            hb.material = "UI_HudMarkers_p.TargetHealthBar_MAT"; hb.w = 0.12f * S; hb.h = 0.02f * S;
            hb.x = 0.30f * W; hb.y = 0.72f * H; hb.params = {{"Health", {0.6f, 0, 0, 0}}, {"Neutral", {1, 0, 0, 0}}};
            renderer_->drawMaterialTile(hb);
        }
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
        if (const char* se = std::getenv("WFC_SHOTEVERY")) {   // diagnostics: <dir>,<from>,<to> every frame
            char dir[260] = {0}; long f0 = 0, f1 = 0;
            if (std::sscanf(se, "%259[^,],%ld,%ld", dir, &f0, &f1) == 3 && frame >= f0 && frame <= f1) {
                char path[300]; std::snprintf(path, sizeof path, "%s/f%04ld.bmp", dir, frame);
                renderer_->captureScreenshot(path);
            }
        }
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
    std::ifstream f(root + "/Maps/" + world_.mapName() + "/navigation.json", std::ios::binary);
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
        std::ifstream pf(root + "/Maps/" + world_.mapName() + "/physics.json", std::ios::binary);
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
        assets::loadGlb(root + "/Maps/" + world_.mapName() + "/world.glb", rm);
        std::vector<std::string> skip = world_.mapState().moverActorNames();
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

// WFC_XFORMTEST: vehicle -> robot transform stress on MP_IAC_Streets (corrected collision), fixed 60 Hz.
// From every authored nav point x 4 headings, each case drives the truck through the real input path
// (PlayerController -> MoveIntent), presses Transform, keeps holding forward for 3 s and checks every tick:
//   UNDER-FLOOR: a walkable surface (n.y > 0.7) between 0.3 m and 3.0 m above the robot's feet at its xz (the robot
//                is 4 m tall: no legit place has a floor slab through its body); KILLZ: below -749 m.
// Cases: stationary, hover forward 1.5 s, hover max speed 4 s, boost 2.5 s, boost + nitro, boosted turn,
// airborne (boost jump, transform near the apex), and a transform-time sweep during boost (0.6..2.8 s).
// Optional WFC_XFORMTEST=<n>: only the first n nav points.
void Application::runTransformStress() {
    const float dt = (float)clock_.stepSeconds();
    auto& pc = world_.player().pawn();
    auto& ctl = world_.player().controller();
    const game::CollisionWorld* col = world_.collision();
    if (!col) return;
    std::string root = std::getenv("WFC_ASSETS") ? std::getenv("WFC_ASSETS") : core::config::kAssetRootDefault;
    std::ifstream f(root + "/Maps/" + world_.mapName() + "/navigation.json", std::ios::binary);
    std::stringstream ss; ss << f.rdbuf();
    assets::Json nav;
    if (!assets::Json::parse(ss.str(), nav)) return;
    struct Node { std::string name; core::Vec3 p; };
    std::vector<Node> nodes;
    for (size_t i = 0; i < nav["nodes"].size(); ++i) {
        const assets::Json& L = nav["nodes"][i]["location_gltf"];
        nodes.push_back({nav["nodes"][i]["node"].asString(), {L[0].asFloat(), L[1].asFloat(), L[2].asFloat()}});
    }
    // Level shell: the BSP part of the authored pawn collision. A walkable BSP surface above the robot = under the map.
    game::CollisionWorld bspCol;
    {
        render::MeshData cm, bm;
        assets::loadGlb(root + "/Maps/" + world_.mapName() + "/collision_pawn.glb", cm);
        bm.positions = cm.positions;
        for (const render::SubMesh& sm : cm.subs)
            if (sm.nodeName.rfind("BSPCollision", 0) == 0)
                for (uint32_t i = sm.indexOffset; i < sm.indexOffset + sm.indexCount; ++i) bm.indices.push_back(cm.indices[i]);
        bspCol.build(bm);
    }
    int limit = std::atoi(std::getenv("WFC_XFORMTEST"));
    if (limit > 0 && (size_t)limit < nodes.size()) nodes.resize((size_t)limit);

    auto placeVehicle = [&](const core::Vec3& at, float yaw) {
        if (pc.moveForm() != game::Form::Vehicle || pc.isTransforming()) {
            while (pc.isTransforming()) { platform::InputFrame none; world_.handleInput(none, dt); world_.tick(dt); }
            if (pc.form() != game::Form::Vehicle) pc.setForm(game::Form::Vehicle);
        }
        core::Vec3 p = at;
        float gy; core::Vec3 gn;
        if (col->groundHeight(p.x, p.z, p.y + 1.5f, 3.0f, gy, gn)) p.y = gy;
        p.y += pc.meshToActor(game::Form::Robot) - pc.meshToActor(game::Form::Vehicle);
        pc.setPosition(p); pc.velocity() = {0, 0, 0}; pc.groundY = p.y; pc.setYaw(yaw);
        ctl.setCameraYaw(yaw);
        // settle on the springs
        for (int k = 0; k < 30; ++k) { platform::InputFrame none; world_.handleInput(none, dt); world_.tick(dt); }
    };
    struct Case { const char* name; float drive; bool boost, nitro, turn, jump; float hold; };
    std::vector<Case> cases = {
        {"stationary", 0.0f, false, false, false, false, 1.0f},
        {"hover", 1.5f, false, false, false, false, 0.0f},
        {"hover-max", 4.0f, false, false, false, false, 0.0f},
        {"boost", 2.5f, true, false, false, false, 0.0f},
        {"boost+nitro", 2.0f, true, true, false, false, 0.0f},
        {"boost-turn", 2.0f, true, false, true, false, 0.0f},
        {"boost-air", 1.6f, true, false, false, true, 0.0f},
    };
    for (float tt = 0.6f; tt <= 2.81f; tt += 0.2f) cases.push_back({"boost-sweep", tt, true, false, false, false, 0.0f});
    std::map<std::string, std::array<int, 4>> stats;   // runs, under-floor runs, killz, max speed at transform x10
    float worstUnder = 0.0f; int overhangRuns = 0, refusedRuns = 0, forcedRuns = 0;
    int total = 0, bad = 0;
    for (const Node& n : nodes) {
        for (int dir = 0; dir < 4; ++dir) {
            float yaw = dir * 1.5707963f;
            for (const Case& cs : cases) {
                placeVehicle(n.p, yaw);
                // Drive.
                int steps = (int)(std::max(cs.drive, cs.hold) / dt);
                bool jumped = false;
                for (int k = 0; k < steps; ++k) {
                    platform::InputFrame in;
                    in.down[(int)platform::Button::Forward] = cs.drive > 0.0f;
                    if (cs.boost) { in.down[(int)platform::Button::FineAim] = true; in.pressed[(int)platform::Button::FineAim] = k == 0; }
                    if (cs.nitro && k == steps - 30) { in.down[(int)platform::Button::Dash] = in.pressed[(int)platform::Button::Dash] = true; }
                    if (cs.turn) { in.padConnected = true; in.padRX = 1.0f; }
                    if (cs.jump && k == steps - 18) { in.down[(int)platform::Button::Jump] = in.pressed[(int)platform::Button::Jump] = true; jumped = true; }
                    ctl.setCameraYaw(cs.turn ? ctl.viewYaw() : yaw);
                    world_.handleInput(in, dt);
                    world_.tick(dt);
                }
                (void)jumped;
                core::Vec3 p0 = pc.position(), v0 = pc.velocity();
                bool driving0 = pc.vehicleState().driving;
                float speed0 = core::length(core::Vec3{v0.x, 0, v0.z});
                // Transform (keep the inputs a player would still hold: forward + boost).
                platform::InputFrame tin;
                tin.down[(int)platform::Button::Transform] = tin.pressed[(int)platform::Button::Transform] = true;
                tin.down[(int)platform::Button::Forward] = cs.drive > 0.0f;
                tin.down[(int)platform::Button::FineAim] = cs.boost;
                int refusedBefore = ctl.cantTransformCount(), forcedBefore = ctl.forcedVehicleCount();
                world_.handleInput(tin, dt); world_.tick(dt);
                bool refused = ctl.cantTransformCount() > refusedBefore;
                refusedRuns += refused;
                float under = 0.0f, ymin = pc.position().y; bool killz = false; int underFrames = 0, overhangFrames = 0;
                core::Vec3 underAt;
                for (int k = 0; k < (int)(3.0f / dt); ++k) {
                    platform::InputFrame in;
                    in.down[(int)platform::Button::Forward] = cs.drive > 0.0f;
                    world_.handleInput(in, dt); world_.tick(dt);
                    core::Vec3 p = pc.position();
                    ymin = std::min(ymin, p.y);
                    if (p.y < -749.0f) { killz = true; break; }
                    if (pc.moveForm() == game::Form::Robot && !pc.isTransforming()) {   // full robot cylinder (after the fold)
                        float gy; core::Vec3 gn;
                        // Under the map = a walkable LEVEL (BSP) floor above the feet; a prop / volume above = low overhang.
                        bool slabAbove = col->groundHeight(p.x, p.z, p.y + 3.0f, 0.0f, gy, gn) && gn.y > 0.7f && gy > p.y + 0.3f;
                        float by; core::Vec3 bn;
                        bool bspAbove = bspCol.groundHeight(p.x, p.z, p.y + 3.0f, 0.0f, by, bn) && bn.y > 0.7f && by > p.y + 0.3f;
                        if (slabAbove && !bspAbove) ++overhangFrames;
                        else if (bspAbove) { gy = by;
                            ++underFrames;
                            if (gy - p.y > under) { under = gy - p.y; underAt = p; }
                        }
                    }
                }
                forcedRuns += ctl.forcedVehicleCount() > forcedBefore;
                bool fail = killz || underFrames > 3;
                if (overhangFrames > 3) ++overhangRuns;
                auto& st = stats[cs.name];
                st[0]++; st[1] += underFrames > 3; st[2] += killz; st[3] = std::max(st[3], (int)(speed0 * 10.0f));
                ++total; bad += fail;
                if (fail) {
                    worstUnder = std::max(worstUnder, under);
                    LOG_INFO("XFORM FAIL %-11s %.1fs %s dir %d: at transform pos (%.2f %.2f %.2f) v (%.1f %.1f %.1f) driving=%d -> %s%d frames under a floor (max %.2f m at %.1f %.1f %.1f), ymin %.2f",
                             cs.name, cs.drive, n.name.c_str(), dir, p0.x, p0.y, p0.z, v0.x, v0.y, v0.z, (int)driving0, killz ? "KILLZ " : "",
                             underFrames, under, underAt.x, underAt.y, underAt.z, ymin);
                }
            }
        }
    }
    for (const auto& [name, st] : stats)
        LOG_INFO("XFORM %-11s runs %4d  under-floor %4d  killz %3d  max speed at transform %.1f m/s", name.c_str(), st[0], st[1], st[2], st[3] / 10.0f);
    LOG_INFO("XFORM SUMMARY: %d/%d transforms ended UNDER THE MAP (no floor under the robot, a walkable surface above) or KillZ (worst %.2f m); %d ended on a real floor under a low overhang; %d refused (NotifyCantTransform); %d forced back to vehicle", bad, total, worstUnder, overhangRuns, refusedRuns, forcedRuns);
}

// WFC_MATCHTEST: the local TDM / DM match state machine (Match + World host glue) with controlled events, fixed 60 Hz.
//  A. TDM to the score limit: local player + 3 harness players (teams by PickTeam); scripted kills incl. a suicide and an
//     environmental death; checks pre-match (no spawn for 10 s), score, deaths, 5 s respawn, EndGame("Score"),
//     MatchOver 15 s, ReturnToLobby; the local pawn's spawn start / team / cluster.
//  B. TDM to the time limit with equal scores (tie, no overtime) using a short TimeLimit override (harness only).
//  C. DM (FFA) score limit 20 with FFA starts.
void Application::runMatchTest() {
    const float dt = (float)clock_.stepSeconds();
    auto logEvents = [&](const game::Match& m, const std::vector<game::MatchEvent>& evs, float t) {
        for (const auto& e : evs) {
            const char* n[] = {"CountdownTick", "MatchStarted", "PlayerSpawned", "PlayerKilled", "GameNearlyComplete",
                               "TimeAnnouncement", "KillsLeftAnnouncement", "PointsLeftAnnouncement", "MatchEnded", "ReturnToLobby"};
            if (e.type == game::MatchEvent::Type::CountdownTick && e.value != 10 && e.value != 0) continue;
            LOG_INFO("MATCHTEST t=%7.2f %-22s player %d other %d value %d %s | state %s status %d score %d-%d remaining %d",
                     t, n[(int)e.type], e.player, e.other, e.value, e.text.c_str(), game::matchStateName(m.state()), m.gameStatus(),
                     m.teamScore(0), m.teamScore(1), m.remainingTime());
        }
    };
    // A: through World (local pawn) + harness players.
    {
        world_.startLocalMatch(game::MatchSettings::forMode("TDM"));
        game::Match& m = world_.match();
        int me = world_.localMatchPlayer();
        int a = m.addPlayer("HarnessA"), b = m.addPlayer("HarnessB"), c = m.addPlayer("HarnessC");
        for (const auto& p : m.players()) LOG_INFO("MATCHTEST A team %s -> %d", p.name.c_str(), p.team);
        float t = 0.0f;
        auto run = [&](float secs) { for (int k = 0; k < (int)(secs / dt); ++k) { platform::InputFrame none; world_.handleInput(none, dt); world_.tick(dt); t += dt; logEvents(m, world_.matchEvents(), t); } };
        run(5.0f);
        LOG_INFO("MATCHTEST A t=5 pending: state %s local dead/unspawned %d countdown %d", game::matchStateName(m.state()), (int)world_.localPlayerDead(), m.countdown());
        run(5.5f);
        LOG_INFO("MATCHTEST A t=10.5: state %s local spawned %d at start %d pawn (%.1f %.1f %.1f)", game::matchStateName(m.state()),
                 (int)!world_.localPlayerDead(), m.lastSpawnStart(me), world_.player().pawn().position().x, world_.player().pawn().position().y,
                 world_.player().pawn().position().z);
        // Find an enemy and a teammate of the local player.
        int myTeam = m.players()[(size_t)me].team, enemy = -1, mate = -1;
        for (int i : {a, b, c}) { if (m.players()[(size_t)i].team != myTeam) { if (enemy < 0) enemy = i; } else if (mate < 0) mate = i; }
        // Suicide by the local player (no score), environmental death of the enemy (no score), then a kill exchange.
        world_.killLocalPlayer(me, true); run(0.1f);
        LOG_INFO("MATCHTEST A after local suicide: score me %d deaths %d team %d-%d, respawn in %.2f", m.players()[(size_t)me].score,
                 m.players()[(size_t)me].deaths, m.teamScore(0), m.teamScore(1), m.players()[(size_t)me].timeToRespawn);
        float tDeath = t; run(5.2f);
        LOG_INFO("MATCHTEST A local respawned %d after %.2f s (WaveRespawnTime 5)", (int)!world_.localPlayerDead(), t - tDeath);
        m.killed(-1, enemy, false); run(0.1f);
        LOG_INFO("MATCHTEST A environmental death of %d: deaths %d, team %d-%d", enemy, m.players()[(size_t)enemy].deaths, m.teamScore(0), m.teamScore(1));
        // Kills by the local player until the goal (40): enemy respawns 5 s after each death.
        int kills = 0;
        while (m.state() == game::Match::State::InProgress && kills < 60) {
            if (!m.players()[(size_t)enemy].alive) { run(0.5f); continue; }
            m.killed(me, enemy, false); ++kills; run(0.05f);
        }
        LOG_INFO("MATCHTEST A after %d kills: state %s winner team %d (my team %d) me score %d kills %d", kills, game::matchStateName(m.state()),
                 m.winnerTeam(), myTeam, m.players()[(size_t)me].score, m.players()[(size_t)me].kills);
        m.killed(me, enemy, false);   // MatchOver: ScoreKill no-op
        LOG_INFO("MATCHTEST A kill in MatchOver ignored: me score %d", m.players()[(size_t)me].score);
        float tOver = t; run(15.5f);
        LOG_INFO("MATCHTEST A MatchOver -> %s after %.1f s; World match active %d", game::matchStateName(m.state()), t - tOver, (int)world_.matchActive());
        (void)mate;
    }
    // B: time limit tie (harness override of TimeLimit to 125 s; authored 900).
    {
        game::Match m;
        std::string root = std::getenv("WFC_ASSETS") ? std::getenv("WFC_ASSETS") : core::config::kAssetRootDefault;
        m.loadSpawnData(root + "/Maps/" + world_.mapName() + "/gameplay.json");
        game::MatchSettings s = game::MatchSettings::forMode("TDM");
        s.timeLimit = 125;
        m.begin(s);
        int p0 = m.addPlayer("P0"), p1 = m.addPlayer("P1");
        float t = 0.0f;
        for (int k = 0; k < (int)(140.0f / dt); ++k) {
            m.tick(dt); t += dt; logEvents(m, m.events(), t); m.clearEvents();
            if (k == (int)(15.0f / dt)) { int a = m.players()[(size_t)p0].team == 0 ? p0 : p1, b = a == p0 ? p1 : p0; m.killed(a, b); m.killed(b, a); }
        }
        LOG_INFO("MATCHTEST B time limit: state %s, score %d-%d, winner team %d (tie = -1), elapsed %d", game::matchStateName(m.state()),
                 m.teamScore(0), m.teamScore(1), m.winnerTeam(), m.elapsedTime());
    }
    // C: DM.
    {
        game::Match m;
        std::string root = std::getenv("WFC_ASSETS") ? std::getenv("WFC_ASSETS") : core::config::kAssetRootDefault;
        m.loadSpawnData(root + "/Maps/" + world_.mapName() + "/gameplay.json");
        m.begin(game::MatchSettings::forMode("DM"));
        int p0 = m.addPlayer("P0"), p1 = m.addPlayer("P1");
        float t = 0.0f; (void)t;
        for (int k = 0; k < (int)(11.0f / dt); ++k) { m.tick(dt); t += dt; }
        LOG_INFO("MATCHTEST C DM spawn starts %s / %s", m.starts()[(size_t)m.lastSpawnStart(p0)].actor.c_str(), m.starts()[(size_t)m.lastSpawnStart(p1)].actor.c_str());
        int kills = 0;
        while (m.state() == game::Match::State::InProgress && kills < 40) {
            if (m.players()[(size_t)p1].alive) { m.killed(p0, p1); ++kills; }
            m.tick(dt); t += dt; m.clearEvents();
        }
        LOG_INFO("MATCHTEST C DM: %d kills -> state %s, p0 score %d (goal %d), winner team %d", kills, game::matchStateName(m.state()),
                 m.players()[(size_t)p0].score, m.settings().goalScore, m.winnerTeam());
    }
}

// WFC_CAMTEST: third-person camera obstruction on Streets (the camera actually used for the view, after the
// obstruction behaviour). From every nav point x 8 headings: (a) robot backing up 2 s with the camera behind,
// (b) robot standing while the camera yaw sweeps 360 deg in 2 s, (c) hover truck backing up, (d) boost forward 2 s,
// (e) robot -> vehicle -> robot transform. Per frame, against the RENDERED map (world.glb, what the player sees):
//   HIDDEN  - visible geometry between the pawn's anchor and the camera (the view is blocked by a wall)
//   UNDER   - a walkable surface between 0.05 and 2 m above the camera at its xz (camera sunk under a floor)
//   NEAR    - rendered geometry within 0.1 m of the camera in any axis direction (near-plane clipping)
//   JUMP    - camera moved > 1.5 m in one step more than the pawn did (snap / pop)
// Default = provisional pull-in; WFC_CAMRE=1 = the RE obstruction behaviours.
void Application::runCameraTest() {
    const float dt = (float)clock_.stepSeconds();
    auto& pc = world_.player().pawn();
    auto& ctl = world_.player().controller();
    const game::CollisionWorld* col = world_.collision();
    if (!col) return;
    std::string root = std::getenv("WFC_ASSETS") ? std::getenv("WFC_ASSETS") : core::config::kAssetRootDefault;
    game::CollisionWorld renderCol;
    {
        render::MeshData rm, rs;
        assets::loadGlb(root + "/Maps/" + world_.mapName() + "/world.glb", rm);
        std::vector<std::string> skip = world_.mapState().moverActorNames();
        for (const auto& v : world_.mapState().modeVisibleActors()) if (!v.visible) skip.push_back(v.actor);
        rs.positions = rm.positions;
        for (const render::SubMesh& sm : rm.subs) {
            if (std::find(skip.begin(), skip.end(), sm.nodeName) != skip.end()) continue;
            for (uint32_t i = sm.indexOffset; i < sm.indexOffset + sm.indexCount; ++i) rs.indices.push_back(rm.indices[i]);
        }
        renderCol.build(rs);
    }
    std::ifstream f(root + "/Maps/" + world_.mapName() + "/navigation.json", std::ios::binary);
    std::stringstream ss; ss << f.rdbuf();
    assets::Json nav;
    if (!assets::Json::parse(ss.str(), nav)) return;
    std::vector<core::Vec3> nodes;
    for (size_t i = 0; i < nav["nodes"].size(); ++i) {
        const assets::Json& L = nav["nodes"][i]["location_gltf"];
        nodes.push_back({L[0].asFloat(), L[1].asFloat(), L[2].asFloat()});
    }
    int limit = std::atoi(std::getenv("WFC_CAMTEST"));
    if (limit > 1 && (size_t)limit < nodes.size()) nodes.resize((size_t)limit);
    const char* sc[5] = {"robot-back", "robot-spin", "hover-back", "boost", "transform"};
    long frames[5] = {0}, hidden[5] = {0}, under[5] = {0}, nearc[5] = {0}, jump[5] = {0}, obstructed[5] = {0};
    auto settle = [&](const core::Vec3& at, game::Form form, float yaw) {
        while (pc.isTransforming()) { platform::InputFrame none; world_.handleInput(none, dt); world_.tick(dt); }
        if (pc.form() != form) pc.setForm(form);
        core::Vec3 p = at; float gy; core::Vec3 gn;
        if (col->groundHeight(p.x, p.z, p.y + 1.5f, 3.0f, gy, gn)) p.y = gy;
        if (form == game::Form::Vehicle) p.y += pc.meshToActor(game::Form::Robot) - pc.meshToActor(game::Form::Vehicle);
        pc.setPosition(p); pc.velocity() = {0, 0, 0}; pc.groundY = p.y; pc.setYaw(yaw); ctl.setCameraYaw(yaw);
        for (int k = 0; k < 90; ++k) { platform::InputFrame none; world_.handleInput(none, dt); world_.tick(dt); }   // camera settles
    };
    for (size_t ni = 0; ni < nodes.size(); ++ni) {
        for (int dir = 0; dir < 8; ++dir) {
            float yaw = dir * 0.7853982f;
            for (int s = 0; s < 5; ++s) {
                settle(nodes[ni], (s == 2 || s == 3) ? game::Form::Vehicle : game::Form::Robot, yaw);
                core::Vec3 prevCam = ctl.cameraPos(), prevPawn = pc.actorLocation();
                int n = (int)((s == 4 ? 3.0f : 2.0f) / dt);
                for (int k = 0; k < n; ++k) {
                    platform::InputFrame in;
                    if (s == 0 || s == 2) in.down[(int)platform::Button::Back] = true;
                    if (s == 3) { in.down[(int)platform::Button::Forward] = true; in.down[(int)platform::Button::FineAim] = true; in.pressed[(int)platform::Button::FineAim] = k == 0; }
                    if (s == 4 && (k == 0 || k == 90)) in.down[(int)platform::Button::Transform] = in.pressed[(int)platform::Button::Transform] = true;
                    if (s == 1) ctl.setCameraYaw(yaw + 6.2831853f * (float)k / n); else if (s != 3) ctl.setCameraYaw(yaw);
                    world_.handleInput(in, dt);
                    world_.tick(dt);
                    core::Vec3 cam = ctl.cameraPos(), anchor = pc.actorLocation();
                    ++frames[s];
                    obstructed[s] += ctl.cameraObstructed();
                    float t;
                    if (renderCol.segmentHit(anchor, cam, t)) ++hidden[s];
                    float gy; core::Vec3 gn;
                    if (col->groundHeight(cam.x, cam.z, cam.y + 2.0f, 0.0f, gy, gn) && gn.y > 0.7f && gy > cam.y + 0.05f) ++under[s];
                    const core::Vec3 ax[6] = {{0.1f, 0, 0}, {-0.1f, 0, 0}, {0, 0.1f, 0}, {0, -0.1f, 0}, {0, 0, 0.1f}, {0, 0, -0.1f}};
                    for (const auto& a : ax) if (renderCol.segmentHit(cam, cam + a, t)) { ++nearc[s]; break; }
                    if (core::length(cam - prevCam) - core::length(anchor - prevPawn) > 1.5f) ++jump[s];
                    prevCam = cam; prevPawn = anchor;
                }
            }
        }
    }
    const char* model = std::getenv("WFC_CAMRE") ? "RE TnThirdPersoncollision / TnAvoidClipping" : "default provisional pull-in";
    for (int s = 0; s < 5; ++s)
        LOG_INFO("CAMTEST %-11s [%s] frames %6ld obstructed %5.1f%%  HIDDEN %5.2f%%  UNDER %5.2f%%  NEAR %5.2f%%  JUMP %ld", sc[s], model, frames[s],
                 100.0 * obstructed[s] / std::max(1L, frames[s]), 100.0 * hidden[s] / std::max(1L, frames[s]),
                 100.0 * under[s] / std::max(1L, frames[s]), 100.0 * nearc[s] / std::max(1L, frames[s]), jump[s]);
}

// WFC_CHAOS: adversarial movement on Streets. From every nav point, 20 s of seeded random play through the real
// input path: move direction changes, jumps, transforms (both ways, at any speed), boost, nitro, camera spins.
// Checks every tick: UNDER-FLOOR (walkable surface 0.3..3 m above the robot's feet after a fold / 0.3..1.5 m above
// the vehicle root), KILLZ, STUCK (< 0.5 m net motion over 5 s while moving input is held).
// Failures log position + authored collision actors. WFC_CHAOS=<n>: first n nav points; seed fixed.
void Application::runChaosTest() {
    const float dt = (float)clock_.stepSeconds();
    auto& pc = world_.player().pawn();
    auto& ctl = world_.player().controller();
    const game::CollisionWorld* col = world_.collision();
    if (!col) return;
    std::string root = std::getenv("WFC_ASSETS") ? std::getenv("WFC_ASSETS") : core::config::kAssetRootDefault;
    std::ifstream f(root + "/Maps/" + world_.mapName() + "/navigation.json", std::ios::binary);
    std::stringstream ss; ss << f.rdbuf();
    assets::Json nav;
    if (!assets::Json::parse(ss.str(), nav)) return;
    std::vector<std::pair<std::string, core::Vec3>> nodes;
    for (size_t i = 0; i < nav["nodes"].size(); ++i) {
        const assets::Json& L = nav["nodes"][i]["location_gltf"];
        nodes.push_back({nav["nodes"][i]["node"].asString(), {L[0].asFloat(), L[1].asFloat(), L[2].asFloat()}});
    }
    int limit = std::atoi(std::getenv("WFC_CHAOS"));
    if (limit > 1 && (size_t)limit < nodes.size()) nodes.resize((size_t)limit);
    game::CollisionWorld bspCol;   // level shell (BSP) of the pawn collision: under it = under the map
    {
        render::MeshData cm, bm;
        assets::loadGlb(root + "/Maps/" + world_.mapName() + "/collision_pawn.glb", cm);
        bm.positions = cm.positions;
        for (const render::SubMesh& sm : cm.subs)
            if (sm.nodeName.rfind("BSPCollision", 0) == 0)
                for (uint32_t i = sm.indexOffset; i < sm.indexOffset + sm.indexCount; ++i) bm.indices.push_back(cm.indices[i]);
        bspCol.build(bm);
    }
    int propRuns = 0;
    unsigned rng = 0xC0FFEEu;
    auto rnd = [&]() { rng = rng * 1664525u + 1013904223u; return (float)((rng >> 8) & 0xFFFF) / 65535.0f; };
    long ticks = 0, transforms = 0, jumps = 0, boosts = 0;
    int underRuns = 0, killz = 0, stuck = 0;
    for (size_t ni = 0; ni < nodes.size(); ++ni) {
        while (pc.isTransforming()) { platform::InputFrame none; world_.handleInput(none, dt); world_.tick(dt); }
        if (pc.form() != game::Form::Robot) pc.setForm(game::Form::Robot);
        core::Vec3 p0 = nodes[ni].second; float gy; core::Vec3 gn;
        if (col->groundHeight(p0.x, p0.z, p0.y + 1.5f, 3.0f, gy, gn)) p0.y = gy;
        pc.setPosition(p0); pc.velocity() = {0, 0, 0}; pc.groundY = p0.y;
        float yaw = rnd() * 6.2831853f, nextChange = 0.0f;
        bool fwd = true, back = false, left = false, right = false, boost = false;
        int underFrames = 0, propFrames = 0; float worst = 0.0f; core::Vec3 worstAt;
        core::Vec3 stuckRef = pc.position(); float stuckT = 0.0f; bool reportedStuck = false;
        for (int k = 0; k < (int)(20.0f / dt); ++k) {
            float t = k * dt;
            platform::InputFrame in;
            if (t >= nextChange) {
                nextChange = t + 0.4f + rnd() * 1.6f;
                yaw += (rnd() - 0.5f) * 3.0f;
                fwd = rnd() < 0.75f; back = !fwd && rnd() < 0.5f; left = rnd() < 0.2f; right = !left && rnd() < 0.2f;
                boost = pc.moveForm() == game::Form::Vehicle && rnd() < 0.6f;
                if (rnd() < 0.35f) { in.down[(int)platform::Button::Transform] = in.pressed[(int)platform::Button::Transform] = true; ++transforms; }
                if (rnd() < 0.3f) { in.down[(int)platform::Button::Jump] = in.pressed[(int)platform::Button::Jump] = true; ++jumps; }
                if (boost && rnd() < 0.3f) { in.down[(int)platform::Button::Dash] = in.pressed[(int)platform::Button::Dash] = true; }
                if (boost) { in.pressed[(int)platform::Button::FineAim] = true; ++boosts; }
            }
            in.down[(int)platform::Button::Forward] = fwd; in.down[(int)platform::Button::Back] = back;
            in.down[(int)platform::Button::Left] = left; in.down[(int)platform::Button::Right] = right;
            in.down[(int)platform::Button::FineAim] = boost;
            if (boost) { in.padConnected = true; in.padRX = std::sin(t * 1.7f); }
            ctl.setCameraYaw(boost ? ctl.viewYaw() : yaw);
            world_.handleInput(in, dt);
            world_.tick(dt);
            ++ticks;
            core::Vec3 p = pc.position();
            if (p.y < -749.0f) { ++killz; LOG_INFO("CHAOS KILLZ from %s at t=%.2f", nodes[ni].first.c_str(), t); break; }
            if (!pc.isTransforming()) {
                bool robot = pc.moveForm() == game::Form::Robot;
                float lo = 0.3f, hi = robot ? 3.0f : 1.5f;
                float by; core::Vec3 bn;
                bool slab = col->groundHeight(p.x, p.z, p.y + hi, 0.0f, gy, gn) && gn.y > 0.7f && gy > p.y + lo;
                bool bsp = slab && bspCol.groundHeight(p.x, p.z, p.y + hi, 0.0f, by, bn) && bn.y > 0.7f && by > p.y + lo;
                if (slab && !bsp) ++propFrames;
                if (bsp) {
                    ++underFrames;
                    if (gy - p.y > worst) { worst = gy - p.y; worstAt = p; }
                }
            }
            stuckT += dt;
            if (stuckT >= 5.0f) {
                bool moving = fwd || back || left || right;
                if (moving && core::length(core::Vec3{p.x - stuckRef.x, 0, p.z - stuckRef.z}) < 0.5f && !reportedStuck) {
                    ++stuck; reportedStuck = true;
                    LOG_INFO("CHAOS STUCK from %s at (%.1f %.1f %.1f) form %s blockers %s", nodes[ni].first.c_str(), p.x, p.y, p.z,
                             game::formName(pc.moveForm()), world_.collisionActorsAt(p + core::Vec3{0, 1.0f, 0}, 1.5f).c_str());
                }
                stuckRef = p; stuckT = 0.0f;
            }
        }
        if (propFrames > 3) ++propRuns;
        if (underFrames > 3) {
            ++underRuns;
            LOG_INFO("CHAOS UNDER-FLOOR from %s: %d frames, worst %.2f m at (%.1f %.1f %.1f) blockers %s", nodes[ni].first.c_str(), underFrames, worst,
                     worstAt.x, worstAt.y, worstAt.z, world_.collisionActorsAt(worstAt + core::Vec3{0, worst, 0}, 0.5f).c_str());
        }
    }
    LOG_INFO("CHAOS SUMMARY: %zu starts x 20 s (%ld ticks, %ld transform presses, %ld jumps, %ld boosts): %d runs UNDER THE MAP (BSP floor above), %d KillZ, %d stuck; %d runs with the pawn inside / under a prop",
             nodes.size(), ticks, transforms, jumps, boosts, underRuns, killz, stuck, propRuns);
}

// WFC_TDMTEST: a whole MP_IAC_Streets TDM session through World (the real pawn, hitscan, pickups, map state) with three
// synthetic participants (test-only MatchOpponent). Checks: launch contract (URL), TDM map state, spawn / teams / starts,
// friendly-fire filter, damage + overshield, a real Ion Blaster hitscan kill, assists, death mid-transform in vehicle form,
// respawn state, pickup timers across death, score-limit end + HUD result, return, a second match without restarting
// (scores reset, map reset), and the HUD state contract.
void Application::runTdmSessionTest() {
    const float dt = (float)clock_.stepSeconds();
    auto& pc = world_.player().pawn();
    int checks = 0, fails = 0;
    auto check = [&](bool ok, const char* what) { ++checks; if (!ok) ++fails; LOG_INFO("TDMTEST %s %s", ok ? "PASS" : "FAIL", what); };
    auto run = [&](float secs) { for (int k = 0; k < (int)(secs / dt); ++k) { platform::InputFrame none; world_.handleInput(none, dt); world_.tick(dt); } };
    // 1. Launch from the original URL form (PointsToWin override for test speed).
    game::MatchLaunch L;
    check(game::MatchLaunch::fromURL("MP_IAC_Streets_Base_m?PlaylistId=-1?PointsToWin=5?Game=TransContent.TnVersusGame?GameModeTag=TDM?TimeLimit=900.00?listen?MapId=508", L)
          && L.map == "MP_IAC_Streets" && L.modeTag == "TDM" && L.settings.goalScore == 5 && L.settings.timeLimit == 900, "URL parsed: MP_IAC_Streets TDM goal 5 time 900");
    game::MatchLaunch bad;
    game::MatchLaunch::fromURL("MP_ESC_BrokenHope_Base_m?GameModeTag=TDM", bad);
    check(!world_.launchMatch(bad), "unloaded map rejected");
    check(world_.launchMatch(L), "launchMatch(TDM)");
    // 2. TDM map state.
    {
        int visibleObjective = 0, visibleBases = 0;
        for (const auto& o : world_.mapState().objectives()) visibleObjective += o.visible;
        for (const auto& b : world_.mapState().modeVisibleActors()) visibleBases += b.visible;
        int rules = 0; for (const auto& r : world_.mapState().gameRules()) rules += r == "TransGame.TnGameRules_ScoreKillsTDM";
        check(visibleObjective == 0 && visibleBases == 0 && rules == 1, "TDM map state: totems / KOTH / flag / bomb / bases hidden, ScoreKillsTDM rule");
        check(world_.pickupFactories().size() == 24, "24 ordinary pickup factories present");
    }
    // 3. Participants and spawn.
    game::Match& m = world_.match();
    int me = world_.localMatchPlayer();
    auto* oA = world_.addMatchOpponent("Ally", false);
    auto* oB = world_.addMatchOpponent("EnemyB", false);
    auto* oC = world_.addMatchOpponent("EnemyC", false);
    game::MatchOpponent* ally = nullptr; std::vector<game::MatchOpponent*> enemies;
    int myTeam = m.players()[(size_t)me].team;
    for (auto* o : {oA, oB, oC}) { if (m.players()[(size_t)o->matchPlayer()].team == myTeam && !ally) ally = o; else enemies.push_back(o); }
    check(ally && enemies.size() == 2, "teams: 2 v 2 by PickTeam");
    run(5.0f);
    check(m.remainingTime() == 900 && m.state() == game::Match::State::PendingMatch, "clock does not run during PendingMatch (900 s)");
    run(5.5f);
    check(m.state() == game::Match::State::InProgress && !world_.localPlayerDead() && oB->spawned() && oC->spawned(), "match started after the 10 s countdown; all spawned");
    {
        int st = m.lastSpawnStart(me);
        bool teamStart = st >= 0 && !m.starts()[(size_t)st].ffa && m.starts()[(size_t)st].team == myTeam;
        check(teamStart, "local pawn at a TnTeamPlayerStart of its own team (initial cluster)");
        float gy; core::Vec3 gn;
        check(world_.collision()->groundHeight(pc.position().x, pc.position().z, pc.position().y + 0.5f, 1.0f, gy, gn) && std::fabs(gy - pc.position().y) < 0.05f, "spawned on the floor");
        check(std::fabs(std::remainder(pc.yaw() - m.starts()[(size_t)st].yaw, 6.2831853f)) < 0.01f, "spawn yaw = authored start rotation");
    }
    // 4. Friendly fire filtered; enemy damage applies; overshield first.
    float h0 = pc.health().current;
    world_.applyMatchDamage(me, ally->matchPlayer(), 100.0f, false);
    check(pc.health().current == h0, "teammate instant-hit damage discarded (TnPlayerPawn.TakeDamage)");
    world_.applyMatchDamage(me, ally->matchPlayer(), 50.0f, true);
    check(pc.health().current == h0 - 50.0f, "teammate AOE damage applies");
    // The local pawn is the iconic Optimus ("Truck", DefaultSpecialty Leader): versus applies TnSpecialtyLeader ->
    // TR_Health_p.Health_Leader 5 x 60 = 300, overshield 200, speed x 0.95 [CONF script, RE TARGETED_PASS3].
    check(pc.specialty() == "Leader" && pc.health().max == 300.0f && pc.health().segmentCount == 5 &&
          std::fabs(pc.speedMultiplier() - 0.95f) < 1e-4f, "ApplySpecialty: Leader 5 x 60 health, speed x 0.95");
    {   // Regeneration: 20 HP/s after 2.0 s, up to the current segment top (damage 300 -> 200: segment 3 = 180..240).
        pc.health().reset();
        int dmgBefore = world_.hudState().damageTakenCount;
        world_.applyMatchDamage(me, enemies[0]->matchPlayer(), 100.0f, false);   // 300 -> 200
        check(world_.hudState().damageTakenCount == dmgBefore + 1 && !world_.hudState().weaponName.empty(), "HUD damage event + weapon identity exposed");
        run(1.9f);
        float before = pc.health().current;
        run(1.0f);
        float after = pc.health().current;
        run(2.0f);
        check(before == 200.0f && after > 200.0f && after < 240.0f && pc.health().current == 240.0f,
              "regen: none for 2.0 s, then 20 HP/s up to the current segment top (240), not to HealthMax");
    }
    pc.health().heal(game::Health::HealType::AddOverShield, 1.0f);
    check(pc.health().current == 500.0f && pc.health().activeSegment() == 5, "overshield: HealthMax + Health_Leader.Overshield 200, top segment");
    world_.applyMatchDamage(me, enemies[0]->matchPlayer(), 150.0f, false);
    check(pc.health().current == 350.0f && pc.health().overshield() == 50.0f, "enemy damage comes off the overshield first");
    // 5. Real hitscan kill: enemy 12 m in front of the local pawn, fire the Ion Blaster through World::fireHitscan.
    {
        game::MatchOpponent* e = enemies[0];
        core::Vec3 fwd = core::forwardFromYawPitch(pc.yaw(), 0.0f);
        e->setPosition(pc.position() + fwd * 12.0f);
        world_.applyMatchDamage(e->matchPlayer(), ally->matchPlayer(), 100.0f, true);   // ally AOE first (assist)
        int shots = 0; int scoreBefore = m.players()[(size_t)me].score;
        while (e->spawned() && shots < 200) {
            core::Vec3 eye = pc.actorLocation() + core::Vec3{0, 0.5f, 0};
            core::Vec3 tgt = e->position() + core::Vec3{0, 2.0f, 0};
            world_.fireHitscan(eye, core::normalize(tgt - eye));
            ++shots;
        }
        LOG_INFO("TDMTEST hitscan kill after %d Ion Blaster hits (InstantHitDamage 15, HealthMax 550)", shots);
        check(!e->spawned() && m.players()[(size_t)me].score == scoreBefore + 1 && m.teamScore(myTeam) == 1 && m.players()[(size_t)me].kills == 1,
              "Ion Blaster kill credited: +1 score, +1 team, +1 kill");
        check(std::fabs(m.players()[(size_t)ally->matchPlayer()].assists - 100.0f / 550.0f) < 1e-3f, "assist = first other damager, 100 / HealthMax");
        game::HudGameState hk = world_.hudState();
        bool feedOk = !hk.killFeed.empty() && hk.killFeed.back().messageSwitch == 0 && hk.killFeed.back().killer == me &&
                      hk.killFeed.back().victim == e->matchPlayer() && hk.killFeed.back().damageType == "TransGame.TnDamageTypeIonBlaster" &&
                      hk.killFeed.back().killerTeam == myTeam && hk.killFeed.back().victimTeam != myTeam;
        check(feedOk, "kill feed: TnDeathMessage switch 0, killer / victim / teams, TnDamageTypeIonBlaster");
        run(4.0f);
        check(!world_.hudState().killFeed.empty(), "kill feed row still present at 4 s (rows live 5 s)");
        run(2.1f);
        check(world_.hudState().killFeed.empty(), "kill feed row gone after 5 s + 1 s fade");
        check(!m.killHistory().empty(), "kill history retained for the match");
    }
    // 6. Pickup across death, then death mid-transform in vehicle form.
    {
        game::PickupFactory* hf = nullptr;
        for (auto* f : world_.pickupFactories()) if (f->kind() == game::PickupFactory::Kind::Health) { hf = f; break; }
        pc.health().current = 100.0f;
        pc.setPosition(hf->position()); pc.velocity() = {0, 0, 0};
        run(0.1f);
        bool taken = !hf->available() && pc.health().current == pc.health().max;
        check(taken, "health pickup: full heal to HealthMax (SHT_AddAllSegments)");
        pc.setForm(game::Form::Vehicle);
        pc.beginTransform();                       // vehicle -> robot fold in progress
        run(0.2f);
        bool folding = pc.isTransforming();
        int enemyScore = m.players()[(size_t)enemies[1]->matchPlayer()].score;
        world_.applyMatchDamage(me, enemies[1]->matchPlayer(), 2000.0f, false);
        check(folding && world_.localPlayerDead() && m.players()[(size_t)me].deaths == 1 &&
              m.players()[(size_t)enemies[1]->matchPlayer()].score == enemyScore + 1, "death mid-transform: killer credited, local dead");
        game::HudGameState hd = world_.hudState();
        check(!hd.alive && hd.timeToRespawn > 4.0f && hd.timeToRespawn <= 5.0f && !hd.spectating, "HUD: dead, TimeToRespawn ~5 s, not yet spectating");
        { const auto& kf = world_.hudState().killFeed; check(!kf.empty() && kf.back().victim == me && kf.back().killer == enemies[1]->matchPlayer() && kf.back().messageSwitch == 0, "kill feed: local death by the enemy"); }
        run(3.05f);
        check(world_.hudState().spectating, "spectating after MinRespawnDelay 3.0 s");
        run(2.25f);
        check(!world_.localPlayerDead() && pc.form() == game::Form::Robot && !pc.isTransforming() && pc.health().current == 300.0f &&
              pc.weapon().ammo == pc.weapon().magSize && pc.weapon().reserve == 150, "respawn: fresh robot pawn, Health_Leader 300, 50 / 150 ammo, no fold");
        check(!hf->available(), "pickup factory still sleeping after the respawn (timers are not reset by death)");
    }
    // 7. Score limit (goal 5): local kills the remaining enemy repeatedly.
    {
        game::MatchOpponent* e = enemies[1];
        int guard = 0;
        while (m.state() == game::Match::State::InProgress && guard++ < 200) {
            if (e->spawned()) world_.applyMatchDamage(e->matchPlayer(), me, 600.0f, false);
            run(0.25f);
        }
        game::HudGameState hd = world_.hudState();
        LOG_INFO("TDMTEST end: team %d-%d, me score %d kills %d deaths %d assists %.3f, result \"%s\"", hd.teamScore[0], hd.teamScore[1], hd.score,
                 hd.kills, hd.deaths, hd.assists, hd.result.c_str());
        check(m.state() == game::Match::State::MatchOver && m.teamScore(myTeam) == 5 && hd.result == "Your team won" && hd.gameStatus == 5,
              "score limit 5 -> EndGame(Score), MatchOver, 'Your team won'");
        check(hd.endReason == "Score" && hd.matchOverTimeLeft > 14.0f && hd.scoreboard.size() == 4, "end reason Score, MatchOver countdown, 4 scoreboard rows");
        int remainingAtEnd = m.remainingTime(), scoreAtEnd = m.teamScore(myTeam);
        world_.applyMatchDamage(e->matchPlayer(), me, 600.0f, false);
        run(5.0f);
        check(m.remainingTime() == remainingAtEnd && m.teamScore(myTeam) == scoreAtEnd, "clock and score frozen in MatchOver");
        run(10.3f);
        check(!world_.matchActive(), "MatchOver 15 s -> ReturnToGameLobby handoff");
    }
    // 8. Second match without restarting: default TDM settings (40 / 900); scores reset, map reset.
    {
        world_.mapState();   // (map clock advanced during match 1)
        float clockBefore = world_.mapState().clock();
        game::MatchLaunch L2;
        game::MatchLaunch::fromURL("MP_IAC_Streets_Base_m?GameModeTag=TDM", L2);
        check(world_.launchMatch(L2), "second launch");
        bool reset = m.teamScore(0) == 0 && m.teamScore(1) == 0 && m.players()[(size_t)me].score == 0 && m.players()[(size_t)me].deaths == 0;
        check(reset && m.settings().goalScore == 40 && m.remainingTime() == 900, "second match: scores reset, goal 40, time 900");
        bool pickupsBack = true; for (auto* f : world_.pickupFactories()) pickupsBack &= f->available();
        check(pickupsBack && world_.mapState().clock() < 0.1f && clockBefore > 10.0f, "fresh level state: pickups available, map clock restarted");
        bool desOk = true; for (auto* d : world_.destructibles()) desOk &= d->state() == 0;
        check(desOk, "destructible back to state 0");
        run(10.5f);
        check(m.state() == game::Match::State::InProgress && !world_.localPlayerDead(), "second match starts and spawns");
        game::HudGameState hd = world_.hudState();
        check(hd.matchActive && hd.goalScore == 40 && hd.myTeam == myTeam && hd.healthMax == 300.0f && hd.segmentCount == 5 && hd.clipAmmo == 50, "HUD state contract populated");
        int allyTags = 0; for (const auto& t : hd.tags) allyTags += t.ally && t.drawn;
        check(allyTags == 1, "player tags: one ally tag drawn, enemy markers disabled");
    }
    {   // Frontend selection flow: no spawn before selectCharacter (CheckReadySpawn); the selected chassis is reported,
        // and the Optimus stand-in is flagged as a fallback rather than hidden.
        world_.startLocalMatch(game::MatchSettings::forMode("TDM"));
        game::Match& m3 = world_.match();
        int me3 = world_.localMatchPlayer();
        m3.requireCharacterSelection(me3);
        run(12.0f);
        bool waited = m3.state() == game::Match::State::InProgress && !m3.players()[(size_t)me3].alive;
        game::CharacterSelection sel; sel.type = 0; sel.specialty = game::Specialty::Scout;
        m3.selectCharacter(me3, sel);
        run(1.0f);
        game::HudGameState h3 = world_.hudState();
        const char* want = game::defaultChassis(game::Specialty::Scout, m3.faction(me3));
        check(waited && m3.players()[(size_t)me3].alive && h3.selectedChassis == want && h3.drawnChassis == want &&
              world_.player().pawn().chassis().id == want && h3.specialty == "Scout" && h3.healthMax == 200.0f && h3.segmentCount == 4,
              "selection gate + selected body spawns: custom Scout -> faction body, Health_Scout 4x50");
    }
    LOG_INFO("TDMTEST SUMMARY: %d/%d checks passed", checks - fails, checks);
}

// WFC_CAMSYNC: render-rate vs fixed-step coherence of the presented character. Renders at WFC_CAMSYNC Hz (default 144)
// against the 60 Hz simulation while turning and moving (robot run, hover truck, boost). Per render frame: the pawn's
// angular offset from the view axis (what the player sees of the character's placement on screen); the jitter is the
// mean |second difference| of that offset. "per-frame" = the shipped camera (cameraPos() per render frame);
// "per-tick cache" = the Pass 20 behaviour (camera position stored at the last simulation step, rotation per frame).
void Application::runCameraSyncTest() {
    float hz = (float)std::atof(std::getenv("WFC_CAMSYNC"));
    if (hz < 30.0f) hz = 144.0f;
    const float rdt = 1.0f / hz;
    auto& pc = world_.player().pawn();
    auto& ctl = world_.player().controller();
    const char* names[3] = {"robot run+turn", "hover drive+turn", "boost"};
    for (int sc = 0; sc < 3; ++sc) {
        world_.teleportToStart(20);
        if (sc > 0 && pc.form() != game::Form::Vehicle) { pc.setForm(game::Form::Vehicle); pc.setPosition(pc.position() + core::Vec3{0, pc.meshToActor(game::Form::Robot) - pc.meshToActor(game::Form::Vehicle), 0}); }
        if (sc == 0 && pc.form() != game::Form::Robot) pc.setForm(game::Form::Robot);
        float yaw = pc.yaw();
        std::vector<float> ax, axStale;
        core::Vec3 stale = ctl.cameraPos();
        clock_ = core::FixedStepClock(60.0);
        for (int f = 0; f < (int)(hz * 4.0f); ++f) {
            platform::InputFrame in;
            in.down[(int)platform::Button::Forward] = true;
            if (sc == 2) { in.down[(int)platform::Button::FineAim] = true; in.pressed[(int)platform::Button::FineAim] = f == 0; in.padConnected = true; in.padRX = 0.6f; }
            else { yaw += 1.6f * rdt; ctl.setCameraYaw(yaw); }        // mouse-like turn, applied per render frame
            world_.handleInput(in, rdt);
            int steps = clock_.tick(rdt);
            for (int i = 0; i < steps; ++i) world_.tick(clock_.stepSeconds());
            if (steps > 0) stale = ctl.cameraPos();                    // Pass 20: position cached at the step
            render::Camera cam;
            ctl.updateCamera(cam);
            core::Vec3 fwd = core::forwardFromYawPitch(cam.yaw, 0.0f);
            core::Vec3 right = core::normalize(core::cross(fwd, core::Vec3{0, 1, 0}));
            core::Vec3 target = pc.actorLocation();
            auto offset = [&](const core::Vec3& eye) { core::Vec3 d = target - eye; return std::atan2(core::dot(d, right), core::dot(d, fwd)) * 57.2958f; };
            ax.push_back(offset(cam.pos));
            axStale.push_back(offset(stale));
        }
        auto jitter = [](const std::vector<float>& a) {
            double s = 0; float mx = 0; int n = 0;
            for (size_t i = 2; i < a.size(); ++i) { float d2 = std::fabs(a[i] - 2 * a[i - 1] + a[i - 2]); s += d2; mx = std::max(mx, d2); ++n; }
            return std::make_pair(n ? (float)(s / n) : 0.0f, mx);
        };
        auto j = jitter(ax), js = jitter(axStale);
        LOG_INFO("CAMSYNC %-16s @%3.0f Hz render / 60 Hz sim: character screen-offset jitter per-frame camera %.4f deg (max %.3f) | per-tick cached camera %.4f deg (max %.3f)",
                 names[sc], hz, j.first, j.second, js.first, js.second);
    }
}

// WFC_MODEPLAYTEST: Conquest (DOM) and Power Struggle (KOTH) through World::launchMatch with test participants placed in the
// authored objective volumes. Checks the recovered rules (TnDominationPointBase / TnKingOfTheHillZoneBase bytecode, authored
// defaults): capture 20 s per attacker, defender holds, +1 team / 3 s per owned node, capture +2 personal, kills personal only;
// KOTH zone only after MatchStarting, +1 personal & team per living pawn per second when uncontested, contested = no score,
// rotation after 60 s to an unvisited zone, zones deactivate at the end; score-limit end.
void Application::runModePlayTest() {
    const float dt = (float)clock_.stepSeconds();
    auto& pc = world_.player().pawn();
    int checks = 0, fails = 0;
    auto check = [&](bool ok, const char* what) { ++checks; if (!ok) ++fails; LOG_INFO("MODEPLAY %s %s", ok ? "PASS" : "FAIL", what); };
    auto run = [&](float secs) { for (int k = 0; k < (int)(secs / dt); ++k) { platform::InputFrame none; world_.handleInput(none, dt); world_.tick(dt); } };
    auto floorAt = [&](core::Vec3 p) { float gy; core::Vec3 gn; if (world_.collision()->groundHeight(p.x, p.z, p.y + 1.0f, 3.0f, gy, gn)) p.y = gy; return p; };
    auto putLocal = [&](const core::Vec3& p) { pc.setPosition(floorAt(p)); pc.velocity() = {0, 0, 0}; };
    const game::ObjectiveObject* objOf = nullptr;
    auto findObj = [&](const char* cls, int nth) -> const game::ObjectiveObject* {
        int k = 0; for (const auto& o : world_.mapState().objectives()) if (o.cls == cls && k++ == nth) return &o; return nullptr; };
    (void)objOf;
    // ---------------- DOM ----------------
    {
        game::MatchLaunch L; game::MatchLaunch::fromURL("MP_IAC_Streets_Base_m?GameModeTag=DOM?PointsToWin=12?TimeLimit=900", L);
        check(world_.launchMatch(L), "launch Conquest (DOM)");
        game::Match& m = world_.match();
        int me = world_.localMatchPlayer();
        game::MatchOpponent* other[3] = {world_.addMatchOpponent("P1", false), world_.addMatchOpponent("P2", false), world_.addMatchOpponent("P3", false)};
        run(10.5f);
        int myTeam = m.players()[(size_t)me].team;
        game::MatchOpponent* ally = nullptr; game::MatchOpponent* enemy = nullptr;
        for (auto* o : other) { if (m.players()[(size_t)o->matchPlayer()].team == myTeam) { if (!ally) ally = o; } else if (!enemy) enemy = o; }
        int vis = 0; for (const auto& o : world_.mapState().objectives()) if (o.cls == "TnDominationPoint") vis += o.visible;
        check(m.state() == game::Match::State::InProgress && vis == 3 && m.settings().goalScore == 12, "DOM in progress, 3 totems visible, goal from URL");
        // Park everyone far from the nodes.
        core::Vec3 park = floorAt(m.starts()[0].pos);
        for (auto* o : other) o->setPosition(park);
        const game::ObjectiveObject* n0 = findObj("TnDominationPoint", 0);
        const game::ObjectiveObject* n1 = findObj("TnDominationPoint", 1);
        check(n0 && !n0->volume.empty() && n0->contains(floorAt(n0->pos) + core::Vec3{0, 2.0f, 0}), "node volume loaded and contains the node");
        // Kill: personal +1, no team score (ScoreKillsMP TeamScoreAmount 0).
        world_.applyMatchDamage(enemy->matchPlayer(), me, 2000.0f, false, "TransGame.TnDamageTypeIonBlaster");
        check(m.players()[(size_t)me].score == 1 && m.teamScore(myTeam) == 0, "DOM kill: +1 personal, 0 team");
        run(5.2f); enemy->setPosition(park);
        // Solo capture of node 0: 20 s.
        putLocal(n0->pos);
        run(19.5f);
        check(n0->defenderTeam == 255 && n0->captureTime > 19.0f, "solo capture still pending at 19.5 s");
        run(0.6f);
        check(n0->defenderTeam == myTeam && m.players()[(size_t)me].score == 3, "captured at 20 s, +2 personal (PersonalScoreAmount)");
        int t0 = m.teamScore(myTeam);
        run(3.05f);
        check(m.teamScore(myTeam) == t0 + 1, "owned node: +1 team after ScoreInterval 3 s");
        // Enemy contests node 0 while I stand there: progress holds at 0 (defender present).
        enemy->setPosition(floorAt(n0->pos));
        run(5.0f);
        check(n0->defenderTeam == myTeam && n0->captureTime == 0.0f, "attacker with a defender present: no capture progress");
        // I leave: the enemy captures in 20 s.
        putLocal(park);
        run(20.2f);
        check(n0->defenderTeam != myTeam && n0->defenderTeam != 255, "enemy alone captures the node in 20 s");
        enemy->setPosition(park);
        // Two attackers (me + ally) on neutral node 1: 10 s.
        putLocal(n1->pos); ally->setPosition(floorAt(n1->pos));
        run(10.1f);
        check(n1->defenderTeam == myTeam, "two attackers capture in 10 s (dt x attackers)");
        // Score-limit end (goal 12): wait for the owned node to tick the team to the goal.
        int guard = 0;
        while (m.state() == game::Match::State::InProgress && guard++ < 200) run(1.0f);
        game::HudGameState h = world_.hudState();
        check(m.state() == game::Match::State::MatchOver && h.endReason == "Score" && (m.teamScore(0) >= 12 || m.teamScore(1) >= 12), "DOM ends at the score limit");
        check(h.objectives.size() == 3, "HUD: 3 Domination objectives");
        run(15.3f);
    }
    // ---------------- KOTH ----------------
    {
        game::MatchLaunch L; game::MatchLaunch::fromURL("MP_IAC_Streets_Base_m?GameModeTag=KOTH?PointsToWin=70?TimeLimit=900", L);
        check(world_.launchMatch(L), "launch Power Struggle (KOTH)");
        game::Match& m = world_.match();
        int me = world_.localMatchPlayer();
        int active = 0; for (const auto& o : world_.mapState().objectives()) active += o.cls == "TnKingOfTheHillZone" && o.visible;
        check(active == 0, "no KOTH zone before MatchStarting");
        run(10.5f);
        active = 0; for (const auto& o : world_.mapState().objectives()) active += o.cls == "TnKingOfTheHillZone" && o.visible;
        check(m.state() == game::Match::State::InProgress && active == 1, "one Active zone after MatchStarting");
        int myTeam = m.players()[(size_t)me].team;
        game::MatchOpponent* enemy = nullptr;
        for (auto* o : world_.matchOpponents()) if (m.players()[(size_t)o->matchPlayer()].team != myTeam) { enemy = o; break; }
        core::Vec3 park = floorAt(m.starts()[0].pos);
        for (auto* o : world_.matchOpponents()) o->setPosition(park);
        const game::ObjectiveObject& z = world_.mapState().objectives()[(size_t)world_.mapState().activeKothZone()];
        std::string firstZone = z.actor;
        putLocal(z.pos);
        int s0 = m.players()[(size_t)me].score, ts0 = m.teamScore(myTeam);
        run(5.02f);
        int ds = m.players()[(size_t)me].score - s0, dts = m.teamScore(myTeam) - ts0;
        LOG_INFO("MODEPLAY KOTH 5 s alone in the zone: personal +%d team +%d", ds, dts);
        check(ds >= 4 && ds <= 5 && dts == ds, "uncontested zone: +1 personal and +1 team per second");
        enemy->setPosition(floorAt(z.pos));
        int s1 = m.players()[(size_t)me].score;
        run(3.0f);
        check(z.defenderTeam == 254 && m.players()[(size_t)me].score == s1, "contested zone (both teams): no score");
        enemy->setPosition(park);
        run(60.0f);
        int nowZone = world_.mapState().activeKothZone();
        check(nowZone >= 0 && world_.mapState().objectives()[(size_t)nowZone].actor != firstZone, "zone rotated after 60 s to another zone");
        // Go to the new zone until the score limit.
        putLocal(world_.mapState().objectives()[(size_t)nowZone].pos);
        int guard = 0;
        while (m.state() == game::Match::State::InProgress && guard++ < 200) run(1.0f);
        int stillActive = 0; for (const auto& o : world_.mapState().objectives()) stillActive += o.cls == "TnKingOfTheHillZone" && o.visible;
        check(m.state() == game::Match::State::MatchOver && m.teamScore(myTeam) >= 70 && stillActive == 0, "KOTH ends at the score limit; zones deactivate (CheckEndGame)");
        run(15.3f);
    }
    // TDM still runs after the other modes (no state leaks).
    {
        game::MatchLaunch L; game::MatchLaunch::fromURL("MP_IAC_Streets_Base_m?GameModeTag=TDM", L);
        check(world_.launchMatch(L), "TDM after DOM / KOTH");
        int vis = 0; for (const auto& o : world_.mapState().objectives()) vis += o.visible;
        check(vis == 0 && world_.match().settings().teamScoreAmount == 1 && world_.match().settings().goalScore == 40, "TDM map state and rules restored");
    }
    LOG_INFO("MODEPLAY SUMMARY: %d/%d checks passed", checks - fails, checks);
}

void Application::shutdown() {
    shutdownFrontend();   // the frontend (movies, Systems audio seam) goes before the devices it uses
    delete audio_; audio_ = nullptr;
    delete renderer_; renderer_ = nullptr;
    delete window_; window_ = nullptr;
    LOG_INFO("Shutdown complete");
}

} // namespace core
