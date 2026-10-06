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
    // Map: WFC_MAP=<MapName>, else the launch URL's map, else Streets (one map per session).
    if (const char* mp = std::getenv("WFC_MAP")) world_.setMap(game::World::canonicalMapName(mp));
    else if (const char* u = std::getenv("WFC_MATCH_URL")) {
        std::string url = u; world_.setMap(game::World::canonicalMapName(url.substr(0, url.find('?'))));
    }
    LOG_INFO("map: %s", world_.mapName().c_str());
    world_.load(*renderer_);
    // Gameplay's measurement mode is WFC_PICKUPTEST=1. [integration] Rendering's presentation diagnostic shares the
    // name with a comma form (<factory>,<take>,<respawn>; see run()), so that form must not exit here.
    if (const char* pt = std::getenv("WFC_PICKUPTEST"))
        if (!std::strchr(pt, ',')) { runPickupTest(); return false; }   // measurements only
    if (std::getenv("WFC_TRAVERSE")) { runTraverseTest(); return false; }   // measurements only
    // WFC_CHASSIS=<UniqueId>: boot as that chassis (free play and every harness), or select it as the iconic character in a launched match.
    const char* bootChassis = std::getenv("WFC_CHASSIS");
    if (bootChassis && !world_.applyChassisToLocalPawn(bootChassis)) LOG_ERROR("WFC_CHASSIS=%s: chassis unavailable", bootChassis);
    if (const char* fp = std::getenv("WFC_FITPROBE")) {   // diagnostic: robot clearance columns at x,y,z (feet)
        core::Vec3 p{0, 0, 0}; std::sscanf(fp, "%f,%f,%f", &p.x, &p.y, &p.z);
        const game::CollisionWorld* col = world_.collision();
        const float r = world_.player().pawn().robotParams().radius * 0.7f, top = 2.0f * world_.player().pawn().robotParams().halfHeight;
        const core::Vec3 off[5] = {{0, 0, 0}, {r, 0, 0}, {-r, 0, 0}, {0, 0, r}, {0, 0, -r}};
        for (int i = 0; i < 5; ++i) {
            float from = i == 0 ? 0.4f : 0.4f + r, t; core::Vec3 n;
            core::Vec3 a = p + off[i] + core::Vec3{0, from, 0}, b = p + off[i] + core::Vec3{0, top, 0};
            bool hit = col && col->segmentHit(a, b, t, n);
            float gy = 0; core::Vec3 gn; bool g = col && col->groundHeight(a.x, a.z, p.y + 0.5f, 1.0f, gy, gn);
            LOG_INFO("FITPROBE col %d: %s at %.2f m (n %.2f %.2f %.2f); floor %s %.2f; actors %s", i, hit ? "BLOCKED" : "clear", hit ? from + (top - from) * t : 0.0f,
                     n.x, n.y, n.z, g ? "at" : "none", gy - p.y, world_.collisionActorsAt(a + core::Vec3{0, 1.0f, 0}, 1.0f).c_str());
        }
        return false;
    }
    if (std::getenv("WFC_MAPTRAVERSE")) { runMapTraverse(); return false; }   // measurements only
    if (std::getenv("WFC_XFORMTEST")) { runTransformStress(); return false; }  // measurements only
    if (std::getenv("WFC_MATCHTEST")) { runMatchTest(); return false; }        // measurements only
    if (std::getenv("WFC_CAMTEST")) { runCameraTest(); return false; }         // measurements only
    if (const char* pp = std::getenv("WFC_POINTPROBE")) {   // diagnostic: rays from a point in 6 directions (pawn / weapon collision)
        core::Vec3 p{}; std::sscanf(pp, "%f,%f,%f", &p.x, &p.y, &p.z);
        const core::Vec3 dirs[6] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
        for (const core::Vec3& d : dirs) {
            float t1 = -1, t2 = -1, t; core::Vec3 n;
            if (world_.collision() && world_.collision()->segmentHit(p, p + d * 20.0f, t, n)) t1 = t * 20.0f;
            if (world_.weaponCollision() && world_.weaponCollision()->segmentHit(p, p + d * 20.0f, t, n)) t2 = t * 20.0f;
            LOG_INFO("POINTPROBE dir (%.0f %.0f %.0f): pawn %.2f m weapon %.2f m", d.x, d.y, d.z, t1, t2);
        }
        return false;
    }
    if (std::getenv("WFC_CHAOS")) { runChaosTest(); return false; }            // measurements only
    if (std::getenv("WFC_TDMTEST")) { runTdmSessionTest(); return false; }     // measurements only
    if (std::getenv("WFC_CAMSYNC")) { runCameraSyncTest(); return false; }     // measurements only
    if (std::getenv("WFC_MODEPLAYTEST")) { runModePlayTest(); return false; }  // measurements only
    if (std::getenv("WFC_WEAPONTEST")) { runWeaponTest(); return false; }      // measurements only
    if (std::getenv("WFC_MAPSUITE")) { runMapSuite(); return false; }          // measurements only
    if (std::getenv("WFC_CTFTEST")) { runCtfExtTest(); return false; }         // measurements only
    if (std::getenv("WFC_PARTICIPANTTEST")) { runParticipantTest(); return false; }
    if (std::getenv("WFC_SWITCHTEST")) { runSwitchTest(); return false; }   // weapon switching, human playtest M09
    if (std::getenv("WFC_SCORETEST")) { runScoreTest(); return false; }     // fresh match state, human playtest M09
    if (std::getenv("WFC_HEIGHTTEST")) { runHeightTest(); return false; }   // robot body height idle vs locomotion, human playtest M09
    if (std::getenv("WFC_VEHPHYS")) { runVehPhysTest(); return false; }     // vehicle jump / attitude / wall response, human playtest M09
    if (std::getenv("WFC_HEADJIT")) { runHeadingJitterTest(); return false; } // drawn heading vs camera per render frame, playtest M10
    if (std::getenv("WFC_VSOCKET")) { runVehicleSocketProbe(); return false; } // vehicle weapon socket vs hull, playtest M10
    if (std::getenv("WFC_XFORMVIS")) { runTransformVisibilityTest(); return false; } // per-chassis transform mesh handoff, playtest M10
    if (std::getenv("WFC_FINEAIMTEST")) { runFineAimTest(); return false; }   // per-weapon fine aim camera, playtest M10
    if (std::getenv("WFC_QATEST")) { runQaToolTest(); return false; }        // DEV / QA TOOLING self-test (needs WFC_QA=1)
    if (std::getenv("WFC_PROJFXTEST")) { runProjectileFxTest(); return false; }   // projectile FlightEffect / ExplosionEffect binding
    if (std::getenv("WFC_MUZZLETEST")) { runMuzzleTest(); return false; }          // vehicle weapon Primary / Primary2 alternation
    if (std::getenv("WFC_RMUZZLETEST")) { runRobotMuzzleTest(); return false; }    // robot projectiles spawn at the weapon MuzzleFlash socket
    if (std::getenv("WFC_CHARGETEST")) { runChargeTest(); return false; }          // Plasma Cannon charge levels + grenade spin
    if (std::getenv("WFC_DROPTEST")) { runDropTest(); return false; }              // hover vehicle 10 m drop: per-step vertical trace
    if (std::getenv("WFC_RISERTEST")) { runRiserTest(); return false; }            // hover pitch crossing a real 0.2-0.3 m step
    if (std::getenv("WFC_PRELOADTEST")) { runPreloadTest(); return false; }        // World::preloadSelections (saved custom characters)
    if (std::getenv("WFC_ANIMSHARECHECK")) {   // robot.glb vs bodies assembled from shared AnimSets, every MP chassis
        int pass = 0, n = 0;
        for (const char* id : {"Truck", "Truck3", "Truck4", "Jet4", "Jet", "Car2", "Car4", "Tank3", "Tank2"}) {
            bool ok = false; const std::string r = world_.compareRobotShared(id, ok); ++n; if (ok) ++pass;
            LOG_INFO("ANIMSHARE %s %s", ok ? "PASS" : "FAIL", r.c_str());
        }
        LOG_INFO("ANIMSHARE SUMMARY: %d/%d checks passed", pass, n);
        return false;
    }
    if (std::getenv("WFC_WEAPONLOADPROF")) {   // first-use weapon model load cost per robot weapon (diagnostics)
        double total = 0; for (int i = 0; i < game::weaponDefCount(); ++i) { const game::WeaponDef& d = game::weaponDefAt(i); if (d.typeCode < 0 || d.typeCode == 3 || !d.meshGltf || !*d.meshGltf) continue;
            const double ms = world_.profileWeaponModelLoad(d); total += ms; LOG_INFO("WEAPONLOAD %-22s %6.1f ms", d.id, ms); }
        LOG_INFO("WEAPONLOAD total %.1f ms", total); return false; }   // measurements only
    world_.setAudio(audio_);
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
        if (const char* mp = std::getenv("WFC_MATPARAM")) {      // diagnostics: actor,param,value[;actor,param,value...]
            std::string all = mp;
            for (size_t p0 = 0; p0 <= all.size();) {
                size_t p1 = all.find(';', p0);
                std::string t = all.substr(p0, p1 == std::string::npos ? std::string::npos : p1 - p0);
                size_t c1 = t.find(','), c2 = c1 == std::string::npos ? c1 : t.find(',', c1 + 1);
                if (c2 != std::string::npos) {
                    bool ok = renderer_->setFrontendMaterialParam(t.substr(0, c1), t.substr(c1 + 1, c2 - c1 - 1),
                                                                  (float)std::atof(t.c_str() + c2 + 1));
                    LOG_INFO("MATPARAM %s -> %d", t.c_str(), ok ? 1 : 0);
                }
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
        if (frontend_) qaTick(pumped);   // DEBUG QA panel (WFC_QA only)
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
        if (const char* s = std::getenv("WFC_AUTOSWITCH_EVERY"))        // repeated NextWeapon press every N frames (soak)
            if (long n = std::atol(s); n > 0 && frame > 0 && frame % n == 0) input.pressed[(int)platform::Button::NextWeapon] = true;
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
        // Diagnostic: WFC_FXTEST="<template>[;<template>...]" spawns each runtime particle template (M32 template
        // library) 4 m in front of the camera, emitting to the camera's right, every 30 frames, side by side.
        if (const char* ft = std::getenv("WFC_FXTEST")) {
            static std::vector<std::string> tpls;
            static bool parsed = false;
            if (!parsed) {
                parsed = true;
                std::string s = ft, cur;
                for (char ch : s) { if (ch == ';') { if (!cur.empty()) tpls.push_back(cur); cur.clear(); } else cur += ch; }
                if (!cur.empty()) tpls.push_back(cur);
            }
            if (frame % 30 == 1) {
                core::Vec3 f = core::forwardFromYawPitch(camera_.yaw, 0.0f);
                core::Vec3 right = core::normalize(core::cross(f, core::Vec3{0, 1, 0}));
                for (size_t i = 0; i < tpls.size(); ++i) {
                    core::Vec3 p = camera_.pos + f * 4.0f + right * (((float)i - 0.5f * (float)(tpls.size() - 1)) * 1.5f);
                    // prefixes: '>' = segment (tracer / beam) 10 m to the right; '~' = source circling (trail), one spawn
                    std::string t = tpls[i];
                    int h = -1;
                    if (!t.empty() && t[0] == '>') h = renderer_->spawnParticleEffectSegment(t.substr(1), p - right * 5.0f + f * 2.0f, p + right * 5.0f + f * 2.0f);
                    else if (!t.empty() && t[0] == '~') { if (frame == 1) { h = renderer_->spawnParticleEffect(t.substr(1), p, right, core::Vec3{0, 1, 0}); orbit_.push_back({h, p}); } }
                    else h = renderer_->spawnParticleEffect(t, p, right, core::Vec3{0, 1, 0});   // authored colours
                    if (const char* zs = std::getenv("WFC_FXTEST_SIZE")) {   // PSC 'Size' parameter on every spawn
                        float z = (float)std::atof(zs); const float v[4] = {z, z, z, 1.0f};
                        if (h >= 0) renderer_->setParticleEffectParam(h, "Size", v);
                    }
                    if (frame == 1) LOG_INFO("FXTEST %s -> handle %d", tpls[i].c_str(), h);
                }
                LOG_INFO("FXTEST frame %ld live effects %d", frame, renderer_->liveParticleEffects());
            }
            for (const auto& o : orbit_) {                 // '~' sources: a 2 m circle, 1 turn / 2 s
                float a = (float)frame / 60.0f * 3.14159f, r = 2.0f;
                core::Vec3 c{o.second.x + std::cos(a) * r, o.second.y, o.second.z + std::sin(a) * r};
                core::Vec3 tang{-std::sin(a), 0, std::cos(a)};
                renderer_->setParticleEffectTransform(o.first, c, tang, core::Vec3{0, 1, 0});
            }
        }
        // "<frame>[,<period>]": with a period the cycle repeats (M28 texture-lifetime soak: live textures must plateau).
        if (const char* rt = std::getenv("WFC_RELOADTEST")) {
            long first = std::atol(rt), period = 0;
            if (const char* c = std::strchr(rt, ',')) period = std::atol(c + 1);
            if (frame == first || (period > 0 && frame > first && (frame - first) % period == 0)) {
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
                    if (pc.position().y < world_.killZ() + 1.0f || !std::isfinite(pc.position().y)) { fell = true; break; }
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
            if (pc.position().y < world_.killZ() + 1.0f) ++falls;
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
    std::ifstream f(world_.mapDir() + "navigation.json", std::ios::binary);
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
        bool in = n.p.x >= bmn.x && n.p.x <= bmx.x && n.p.z >= bmn.z && n.p.z <= bmx.z && n.p.y > world_.killZ();
        if (!in || !g) { ++outside; LOG_INFO("MAPTRAVERSE bounds %s %s at (%.1f %.1f %.1f): inside=%d floor=%d", n.cls.c_str(), n.name.c_str(), n.p.x, n.p.y, n.p.z, (int)in, (int)g); }
    }
    LOG_INFO("MAPTRAVERSE bounds: %zu nav points, %d outside the collision bounds / without floor (KillZ of the map)", nodes.size(), outside);

    // Visual / collision coherence: the rendered map (world.glb, minus movers and the mode-hidden objective bases)
    // as a trace world, each render component joined to its authored pawn collision representation
    // (physics.json props: simple / per_poly / none, from the component's Block* flags and BodySetup).
    std::map<std::string, std::pair<std::string, std::string>> compRep;   // component -> (pawn rep, mesh)
    std::map<std::string, bool> compBlockCam;                             // component -> authored BlockCameras
    {
        std::ifstream pf(world_.mapDir() + "physics.json", std::ios::binary);
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
        assets::loadGlb(world_.mapDir() + "world.glb", rm);
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
            if (pc.position().y < world_.killZ() + 1.0f || !std::isfinite(pc.position().y)) { r.fell = true; break; }
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
                if (pc.position().y < world_.killZ() + 1.0f) { ++sweepFalls; break; }
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
                if (pc.position().y < world_.killZ() + 1.0f) { ++sweepFalls; break; }
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
                if (!floor || std::fabs(pc.position().y - y0) > 1.0f || pc.position().y < world_.killZ() + 1.0f) {
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
    std::ifstream f(world_.mapDir() + "navigation.json", std::ios::binary);
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
        assets::loadGlb(world_.mapDir() + "collision_pawn.glb", cm);
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
                    if (p.y < world_.killZ() + 1.0f) { killz = true; break; }
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
        m.loadSpawnData(world_.mapDir() + "gameplay.json");
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
        m.loadSpawnData(world_.mapDir() + "gameplay.json");
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
        assets::loadGlb(world_.mapDir() + "world.glb", rm);
        std::vector<std::string> skip = world_.mapState().moverActorNames();
        for (const auto& v : world_.mapState().modeVisibleActors()) if (!v.visible) skip.push_back(v.actor);
        rs.positions = rm.positions;
        for (const render::SubMesh& sm : rm.subs) {
            if (std::find(skip.begin(), skip.end(), sm.nodeName) != skip.end()) continue;
            for (uint32_t i = sm.indexOffset; i < sm.indexOffset + sm.indexCount; ++i) rs.indices.push_back(rm.indices[i]);
        }
        renderCol.build(rs);
    }
    std::ifstream f(world_.mapDir() + "navigation.json", std::ios::binary);
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
    std::ifstream f(world_.mapDir() + "navigation.json", std::ios::binary);
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
        assets::loadGlb(world_.mapDir() + "collision_pawn.glb", cm);
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
        int underFrames = 0, propFrames = 0; float worst = 0.0f; core::Vec3 worstAt; std::string worstState;
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
            if (p.y < world_.killZ() + 1.0f) { ++killz; LOG_INFO("CHAOS KILLZ from %s at t=%.2f", nodes[ni].first.c_str(), t); break; }
            if (!pc.isTransforming()) {
                bool robot = pc.moveForm() == game::Form::Robot;
                float lo = 0.3f, hi = robot ? 3.0f : 1.5f;
                float by; core::Vec3 bn;
                bool slab = col->groundHeight(p.x, p.z, p.y + hi, 0.0f, gy, gn) && gn.y > 0.7f && gy > p.y + lo;
                bool bsp = slab && bspCol.groundHeight(p.x, p.z, p.y + hi, 0.0f, by, bn) && bn.y > 0.7f && by > p.y + lo;
                if (slab && !bsp) ++propFrames;
                if (bsp) {
                    ++underFrames;
                    if (gy - p.y > worst) { worst = gy - p.y; worstAt = p; worstState = std::string(game::formName(pc.moveForm())) + (pc.vehicleState().driving ? "/boost" : "") + (pc.isDodging() ? "/dodge" : ""); }
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
            LOG_INFO("CHAOS UNDER-FLOOR from %s: %d frames, worst %.2f m (%s) at (%.1f %.1f %.1f) blockers %s", nodes[ni].first.c_str(), underFrames, worst, worstState.c_str(),
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
        LOG_INFO("TDMTEST hitscan kill after %d Ion Blaster hits (InstantHitDamage 15, victim HealthMax %.0f after the 100 AOE)", shots, m.players()[(size_t)e->matchPlayer()].healthMax);
        check(!e->spawned() && m.players()[(size_t)me].score == scoreBefore + 1 && m.teamScore(myTeam) == 1 && m.players()[(size_t)me].kills == 1,
              "Ion Blaster kill credited: +1 score, +1 team, +1 kill");
        const float vMax = m.players()[(size_t)e->matchPlayer()].healthMax;   // victim class HealthMax (iconic Optimus: Leader 300)
        check(vMax == 300.0f && std::fabs(m.players()[(size_t)ally->matchPlayer()].assists - 100.0f / vMax) < 1e-3f, "assist = first other damager, 100 / the victim's HealthMax (300)");
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
// WFC_PARTICIPANTTEST: non-local pawns (bot-ready architecture, no AI): selection -> body / health / loadout, shared
// movement and transformation, damage, death and respawn with the same body.
void Application::runParticipantTest() {
    int checks = 0, fails = 0;
    auto check = [&](bool ok, const std::string& what) { ++checks; if (!ok) ++fails; LOG_INFO("PARTICIPANT %s %s", ok ? "PASS" : "FAIL", what.c_str()); };
    const float dt = 1.0f / 60.0f;
    auto run = [&](float secs) { for (int i = 0; i < (int)(secs * 60.0f + 0.5f); ++i) { platform::InputFrame in; world_.handleInput(in, dt); world_.tick(dt); } };
    // Test placement on any map: move the local pawn to a flat floor point (10 m grid) from which the line along the camera yaw
    // is clear for 'ahead' m at 2 m height over floor at both ends (spawn rooms / rock faces block shots on some maps).
    auto moveToOpenLine = [&](float ahead) -> bool {
        game::Character& lp = world_.player().pawn();
        const game::CollisionWorld* cw = world_.collision();
        if (!cw) return false;
        const game::CollisionWorld* ww = world_.weaponCollision() ? world_.weaponCollision() : cw;
        const core::Vec3 fwd = core::forwardFromYawPitch(world_.player().controller().camYaw(), 0.0f);
        const core::Vec3 b0 = cw->boundsMin(), b1 = cw->boundsMax();
        for (float x = b0.x + 5.0f; x < b1.x; x += 10.0f)
            for (float z = b0.z + 5.0f; z < b1.z; z += 10.0f) {
                float gy, gy2; core::Vec3 gn, gn2;
                if (!cw->groundHeight(x, z, lp.position().y + 3.0f, 0.5f, gy, gn) || gn.y < 0.9f || std::fabs(gy - lp.position().y) > 30.0f) continue;
                core::Vec3 p{x, gy + 2.0f, z}, q = p + fwd * ahead; float th;
                if (!cw->groundHeight(q.x, q.z, gy + 1.0f, 0.5f, gy2, gn2) || std::fabs(gy2 - gy) > 1.0f) continue;
                if (ww->segmentHit(p, q, th) || cw->segmentHit(p, q, th)) continue;
                lp.setPosition(core::Vec3{x, gy + 0.1f, z});
                return true;
            }
        return false;
    };
    game::MatchLaunch L; game::MatchLaunch::fromURL(world_.mapName() + "_BASE_m?GameModeTag=TDM", L);
    world_.launchMatch(L);
    game::MatchOpponent* A = world_.addMatchOpponent("ScoutBot", false);
    game::MatchOpponent* B = world_.addMatchOpponent("JetBot", false);
    game::CharacterSelection sa; sa.type = 0; sa.specialty = game::Specialty::Scout;
    game::CharacterSelection sb; sb.type = 1; sb.chassisId = "Jet";
    world_.match().selectCharacter(A->matchPlayer(), sa);
    world_.match().selectCharacter(B->matchPlayer(), sb);
    run(10.5f);
    const game::Match& m = world_.match();
    std::string wantA = game::resolveChassis(sa, m.faction(A->matchPlayer()));
    bool bodies = A->spawned() && B->spawned() && A->pawn().chassis().id == wantA && B->pawn().chassis().id == "Jet" &&
                  A->pawn().currentModel() && B->pawn().currentModel();
    bool health = A->pawn().health().max == 200.0f && B->pawn().health().max == 330.0f;   // Scout 4x50; Starscream preset Soldier 6x55
    bool loadout = B->pawn().weapon().def && std::string(B->pawn().weapon().def->provider) == "SniperRifle";
    LOG_INFO("PARTICIPANT A %s (%s, %.0f HP, %s) team %d; B %s (%s, %.0f HP, %s) team %d", A->pawn().chassis().id.c_str(), A->pawn().specialty().c_str(),
             A->pawn().health().max, A->pawn().weapon().name, A->team(), B->pawn().chassis().id.c_str(), B->pawn().specialty().c_str(),
             B->pawn().health().max, B->pawn().weapon().name, B->team());
    check(bodies && health && loadout, "selection -> body (" + wantA + ", Jet), class health (Scout 200, Soldier 330), iconic loadout (SniperRifle)");
    // Shared movement: walk forward 2 s on the floor.
    core::Vec3 p0 = A->pawn().position();
    game::MoveIntent go; go.moveForward = 1.0f; go.faceYaw = A->pawn().yaw();
    for (int i = 0; i < 120; ++i) { A->setIntent(go); platform::InputFrame in; world_.handleInput(in, dt); world_.tick(dt); }
    float moved = core::length(core::Vec3{A->pawn().position().x - p0.x, 0, A->pawn().position().z - p0.z});
    check(moved > 10.0f && A->pawn().position().y > world_.killZ() + 5.0f, "shared CharacterMovement: " + std::to_string((int)moved) + " m walked in 2 s");
    // Transformation through the same Character path.
    A->setIntent(game::MoveIntent{});
    A->pawn().beginTransform();
    run(3.0f);
    check(A->pawn().form() == game::Form::Vehicle && !A->pawn().isTransforming() && A->pawn().vehicleParams().form == game::VehicleFormType::Car,
          "robot -> vehicle transformation (car form)");
    // Damage, death, respawn with the same body.
    int deaths0 = m.players()[(size_t)B->matchPlayer()].deaths;
    world_.applyMatchDamage(B->matchPlayer(), -1, 99999.0f, true, "TransGame.TnDamageTypeInstantKill");
    bool died = !B->spawned() && m.players()[(size_t)B->matchPlayer()].deaths == deaths0 + 1;
    run(6.0f);
    check(died && B->spawned() && B->pawn().chassis().id == "Jet" && B->pawn().health().current == B->pawn().health().max && B->pawn().form() == game::Form::Robot,
          "death -> wave respawn as a fresh robot pawn with the same body and full class health");
    // Killstreaks: the local player as a custom Soldier; 3 kills -> RefillAmmoStreak (Ammo Matrix) acquired; B triggers it.
    {
        game::MatchLaunch L2; game::MatchLaunch::fromURL(world_.mapName() + "_BASE_m?GameModeTag=TDM", L2);
        world_.launchMatch(L2);
        game::CharacterSelection me; me.type = 0; me.specialty = game::Specialty::Soldier;
        world_.match().selectCharacter(world_.localMatchPlayer(), me);
        std::vector<game::MatchOpponent*> ops;
        for (int i = 0; i < 5; ++i) ops.push_back(world_.addMatchOpponent("K" + std::to_string(i), false));
        run(10.6f);
        int kills = 0;
        for (int round = 0; round < 6 && kills < 3; ++round) {
            for (auto* o : ops)
                if (kills < 3 && o->spawned() && !world_.match().sameTeam(o->matchPlayer(), world_.localMatchPlayer())) {
                    world_.applyMatchDamage(o->matchPlayer(), world_.localMatchPlayer(), 99999.0f, false, "TransGame.TnDamageTypeIonBlaster");
                    ++kills;
                }
            run(6.0f);
        }
        game::HudGameState h = world_.hudState();
        bool acquired = h.killStreak == 3 && !h.killstreaks.empty() && h.killstreaks.back() == "RefillAmmoStreak" && h.killstreakImplemented;
        game::Character& lp = world_.player().pawn();
        lp.weapon().reserve = 0;
        int clip0 = lp.weapon().ammo;
        platform::InputFrame b; b.pressed[(int)platform::Button::Killstreak] = true; b.down[(int)platform::Button::Killstreak] = true;
        world_.handleInput(b, dt); world_.tick(dt);
        bool refilled = lp.weapon().reserve == lp.weapon().reserveMax && world_.hudState().ammoLockBuff > 9.0f && world_.hudState().killstreaks.empty();
        platform::InputFrame fire; fire.down[(int)platform::Button::Fire] = true;
        for (int i = 0; i < 60; ++i) { world_.handleInput(fire, dt); world_.tick(dt); }
        bool locked = lp.weapon().ammo == clip0;
        world_.killLocalPlayer(-1, true);
        bool reset = world_.match().players()[(size_t)world_.localMatchPlayer()].currentKillStreak == 0;
        LOG_INFO("PARTICIPANT killstreak: streak %d acquired [%s] refilled %d clip %d -> %d reset %d", h.killStreak,
                 h.killstreaks.empty() ? "" : h.killstreaks.back().c_str(), (int)refilled, clip0, lp.weapon().ammo, (int)reset);
        check(acquired && refilled && locked && reset, "Soldier: 3 kills -> Ammo Matrix; B: reserves full + clip locked 10 s; death resets the streak");
    }
    // Melee: Q -> MELEE_WeaponAttack (150, one hit per sweep) with the assist lunge; Whirlwind ability -> 85 per sweep window.
    {
        game::MatchLaunch L3; game::MatchLaunch::fromURL(world_.mapName() + "_BASE_m?GameModeTag=TDM", L3);
        world_.launchMatch(L3);
        game::CharacterSelection me; me.type = 0; me.specialty = game::Specialty::Leader; me.abilities = {"Whirlwind", "Dodge"};
        world_.match().selectCharacter(world_.localMatchPlayer(), me);
        std::vector<game::MatchOpponent*> ops;
        for (int i = 0; i < 3; ++i) ops.push_back(world_.addMatchOpponent("M" + std::to_string(i), false));
        run(10.6f);
        game::MatchOpponent* E = nullptr;
        for (auto* o : ops) if (o->spawned() && !world_.match().sameTeam(o->matchPlayer(), world_.localMatchPlayer())) { E = o; break; }
        game::Character& lp = world_.player().pawn();
        auto place = [&](float d) {
            core::Vec3 f = core::forwardFromYawPitch(world_.player().controller().viewYaw(), 0.0f);
            E->setPosition(core::Vec3{lp.position().x + f.x * d, lp.position().y, lp.position().z + f.z * d});
        };
        bool ok = E != nullptr;
        float h0 = 0, h1 = 0, h2 = 0, moved = 0, pushed = 0;
        bool refusedReady = false, cdHeld = false;
        int whirlHits = 0;
        if (ok) {
            place(5.0f);
            run(0.1f);
            h0 = E->pawn().health().current;
            core::Vec3 p0 = lp.position();
            const core::Vec3 e0 = E->pawn().position();
            platform::InputFrame q; q.pressed[(int)platform::Button::Melee] = true; q.down[(int)platform::Button::Melee] = true;
            world_.handleInput(q, dt); world_.tick(dt);
            run(0.2f);
            h1 = E->pawn().health().current;
            moved = core::length(core::Vec3{lp.position().x - p0.x, 0, lp.position().z - p0.z});
            pushed = core::length(core::Vec3{E->pawn().position().x - e0.x, 0, E->pawn().position().z - e0.z});
        }
        LOG_INFO("PARTICIPANT melee: target %.0f -> %.0f HP, lunge %.2f m, target knocked back %.2f m", h0, h1, moved, pushed);
        check(ok && h0 - h1 == 150.0f && moved > 2.0f && pushed > 0.0f, "Q melee: assist lunge toward the enemy, MELEE_WeaponAttack 150 once per sweep, Impulse 30000 / Mass 100 knockback");
        if (ok) {
            place(2.0f);
            run(0.1f);
            h1 = E->pawn().health().current;
            const int hits0 = lp.meleeHitCount_;
            // Still in the 1.33 s Q swing: the trigger must fail and keep the ability ready.
            platform::InputFrame w; w.pressed[(int)platform::Button::Dash] = true; w.down[(int)platform::Button::Dash] = true;
            world_.handleInput(w, dt); world_.tick(dt);
            refusedReady = lp.isMeleeing() && lp.meleeState_ == 1 && world_.hudState().abilities[0].cooldown == 0.0f;
            run(1.2f);
            world_.handleInput(w, dt); world_.tick(dt);
            for (int i = 0; i < 120; ++i) { place(2.0f); platform::InputFrame in; world_.handleInput(in, dt); world_.tick(dt); }   // 2.0 s: sweeps 0.9, 1.6
            h2 = E->pawn().health().current;
            whirlHits = lp.meleeHitCount_ - hits0;
            cdHeld = world_.hudState().abilities[0].cooldown == 0.0f && lp.meleeState_ == 2;
        }
        // Knockback gating (RE §I): Momentum / Mass 100; only RequestRespectForcesApplied damage types; vehicle x0.5.
        float kMelee = 0, kShock = 0, kIon = 0, kVeh = 0;
        if (ok) {
            if (!E->spawned()) run(6.0f);
            auto dv = [&](const core::Vec3& m, const char* t) {
                E->setPosition(E->position()); world_.applyKnockback(E->matchPlayer(), m, t);
                return core::length(E->pawn().velocity());
            };
            kMelee = dv({30000, 0, 0}, "TransGame.TnDamageTypeMelee");
            kShock = dv({700000, 0, 0}, "TransGame.TnDamageTypeShockwave");
            kIon = dv({30000, 0, 0}, "TransGame.TnDamageTypeIonBlaster");
            E->pawn().beginTransform(); run(3.0f);
            kVeh = dv({30000, 0, 0}, "TransGame.TnDamageTypeMelee");
            E->pawn().beginTransform(); run(3.0f);
        }
        LOG_INFO("PARTICIPANT knockback: melee %.2f m/s, shockwave %.1f m/s, ion blaster %.2f, vehicle melee %.2f", kMelee, kShock, kIon, kVeh);
        check(ok && std::fabs(kMelee - 3.0f) < 0.01f && std::fabs(kShock - 70.0f) < 0.1f && kIon == 0.0f && std::fabs(kVeh - 1.5f) < 0.01f,
              "Knockback: melee 30000 -> 3 m/s, Shockwave 700000 -> 70 m/s, IonBlaster none (bIgnoreForces), vehicle x0.5");
        LOG_INFO("PARTICIPANT whirlwind: refused during Q swing %d; %d hits in 2 s, target %.0f -> %.0f HP, cooldown held %d", (int)refusedReady, whirlHits, h1, h2, (int)cdHeld);
        check(ok && refusedReady && whirlHits >= 2 && (h1 - h2 >= 140.0f || h2 <= 0.0f) && cdHeld,
              "Whirlwind: refused (no cooldown) while meleeing; 85 per sweep window (target hit in both windows of the first 2 s; other enemies in the box also hit); cooldown waits for the end");
    }
    // Homing (TnWeaponHoming): Thermo Rocket Launcher, CanLockOnToRobots false -> no lock on a robot; a vehicle 4 m off the
    // crosshair at ~50 m locks after LockOnTime 0.5 s and the rocket homes into it.
    {
        game::MatchLaunch L4; game::MatchLaunch::fromURL(world_.mapName() + "_BASE_m?GameModeTag=TDM", L4);
        world_.launchMatch(L4);
        game::CharacterSelection me; me.type = 0; me.specialty = game::Specialty::Soldier; me.weapons = {"HomingRocket", "IonBlaster"};
        world_.match().selectCharacter(world_.localMatchPlayer(), me);
        std::vector<game::MatchOpponent*> ops;
        for (int i = 0; i < 3; ++i) ops.push_back(world_.addMatchOpponent("H" + std::to_string(i), false));
        run(10.6f);
        game::MatchOpponent* E = nullptr;
        for (auto* o : ops) if (o->spawned() && !world_.match().sameTeam(o->matchPlayer(), world_.localMatchPlayer())) { E = o; break; }
        game::Character& lp = world_.player().pawn();
        game::PlayerController& ctl = world_.player().controller();
        const bool haveWeapon = lp.weapon().def && std::string(lp.weapon().def->provider) == "HomingRocket";
        // Park the other opponents far behind so only E can be picked.
        core::Vec3 fwd = core::forwardFromYawPitch(ctl.camYaw(), 0.0f), right{-fwd.z, 0, fwd.x};
        for (auto* o : world_.matchOpponents()) if (o != E) o->setPosition(lp.position() - fwd * 30.0f);
        auto aimAt = [&](const core::Vec3& p) {   // crosshair through p
            core::Vec3 d = p - ctl.cameraPos();
            ctl.setCameraYaw(std::atan2(-d.x, -d.z));
            ctl.setCameraPitch(std::atan2(d.y, std::hypot(d.x, d.z)));
        };
        bool robotNoLock = false, lockedVeh = false, homed = false;
        float lockAt = -1.0f, h0 = 0, h1 = 0;
        if (E && haveWeapon) {
            if (moveToOpenLine(55.0f)) { run(0.3f); fwd = core::forwardFromYawPitch(ctl.camYaw(), 0.0f); right = core::Vec3{-fwd.z, 0, fwd.x}; for (auto* o : world_.matchOpponents()) if (o != E) o->setPosition(lp.position() - fwd * 30.0f); }
            core::Vec3 T = lp.position() + fwd * 50.0f + core::Vec3{0, 2.0f, 0};
            E->setPosition(T);
            for (int i = 0; i < 60; ++i) { E->setPosition(T); aimAt(E->pawn().actorLocation() + right * 4.0f); platform::InputFrame in; world_.handleInput(in, dt); world_.tick(dt); }
            robotNoLock = !world_.hudState().locked && world_.hudState().lockProgress == 0.0f;
            E->pawn().beginTransform();
            for (int i = 0; i < 180; ++i) { E->setPosition(T); aimAt(E->pawn().actorLocation() + right * 4.0f); platform::InputFrame in; world_.handleInput(in, dt); world_.tick(dt); }
            // fresh lock: look away, then back
            for (int i = 0; i < 90; ++i) { aimAt(E->pawn().actorLocation() + right * 40.0f); platform::InputFrame in; world_.handleInput(in, dt); world_.tick(dt); }
            for (int i = 0; i < 60 && lockAt < 0.0f; ++i) {
                E->setPosition(T); aimAt(E->pawn().actorLocation() + right * 4.0f);
                platform::InputFrame in; world_.handleInput(in, dt); world_.tick(dt);
                if (world_.hudState().locked) lockAt = (i + 1) * dt;
            }
            lockedVeh = world_.hudState().locked && world_.hudState().lockTarget == E->matchPlayer() && E->pawn().form() == game::Form::Vehicle;
            h0 = E->pawn().health().current;
            platform::InputFrame fire; fire.down[(int)platform::Button::Fire] = true; fire.pressed[(int)platform::Button::Fire] = true;
            aimAt(E->pawn().actorLocation() + right * 4.0f);
            world_.handleInput(fire, dt); world_.tick(dt);
            bool targeted = !world_.projectiles().empty() && world_.projectiles().back().target == E->matchPlayer();
            for (int i = 0; i < 120; ++i) { E->setPosition(T); aimAt(E->pawn().actorLocation() + right * 4.0f); platform::InputFrame in; world_.handleInput(in, dt); world_.tick(dt); }
            h1 = E->spawned() ? E->pawn().health().current : 0.0f;
            homed = targeted && h0 - h1 > 50.0f;   // a straight rocket 4 m off would pass the hull
        }
        LOG_INFO("PARTICIPANT homing: weapon %d, robot target no lock %d, vehicle lock %d after %.2f s, target %.0f -> %.0f HP", (int)haveWeapon,
                 (int)robotNoLock, (int)lockedVeh, lockAt, h0, h1);
        check(haveWeapon && robotNoLock && lockedVeh && lockAt > 0.45f && lockAt < 0.55f && homed,
              "Homing: no lock on robots (CanLockOnToRobots false); vehicle lock after LockOnTime 0.5 s; locked rocket homes 4 m into the target");
    }
    // Barrier (TnAbilityBarrier): wall 10 m ahead after 0.5 s; blocks shots and pawns; decays 15/s; cooldown after it is gone.
    {
        game::MatchLaunch L5; game::MatchLaunch::fromURL(world_.mapName() + "_BASE_m?GameModeTag=TDM", L5);
        world_.launchMatch(L5);
        game::CharacterSelection me; me.type = 0; me.specialty = game::Specialty::Leader; me.abilities = {"Barrier", "Dodge"};
        world_.match().selectCharacter(world_.localMatchPlayer(), me);
        std::vector<game::MatchOpponent*> ops;
        for (int i = 0; i < 3; ++i) ops.push_back(world_.addMatchOpponent("W" + std::to_string(i), false));
        run(10.6f);
        game::MatchOpponent* E = nullptr;
        for (auto* o : ops) if (o->spawned() && !world_.match().sameTeam(o->matchPlayer(), world_.localMatchPlayer())) { E = o; break; }
        game::Character& lp = world_.player().pawn();
        game::PlayerController& ctl = world_.player().controller();
        bool up = false, shotBlocked = false, walkBlocked = false, decays = false, cdWait = false, cdAfter = false;
        float hp0 = 0, hpShot = 0, hpLater = 0, walked = 0;
        if (E) {
            core::Vec3 fwd = core::forwardFromYawPitch(lp.yaw(), 0.0f);
            for (auto* o : world_.matchOpponents()) if (o != E) o->setPosition(lp.position() - fwd * 30.0f);
            platform::InputFrame sh; sh.pressed[(int)platform::Button::Dash] = true; sh.down[(int)platform::Button::Dash] = true;
            world_.handleInput(sh, dt); world_.tick(dt);
            run(0.45f);
            bool notYet = !world_.barrier().alive;
            run(0.1f);
            up = notYet && world_.barrier().alive && world_.hudState().barrier;
            hp0 = world_.barrier().health;
            // Enemy 16 m ahead behind the wall; aim the crosshair at it and fire the Ion Blaster for 0.5 s.
            core::Vec3 T = lp.position() + fwd * 16.0f;
            float eh0 = 0;
            for (int i = 0; i < 30; ++i) {
                E->setPosition(T);
                core::Vec3 d = E->pawn().actorLocation() - ctl.cameraPos();
                ctl.setCameraYaw(std::atan2(-d.x, -d.z)); ctl.setCameraPitch(std::atan2(d.y, std::hypot(d.x, d.z)));
                if (i == 0) eh0 = E->pawn().health().current;
                platform::InputFrame fire; fire.down[(int)platform::Button::Fire] = true;
                world_.handleInput(fire, dt); world_.tick(dt);
            }
            hpShot = world_.barrier().health;
            shotBlocked = E->pawn().health().current == eh0 && hp0 - hpShot > 15.0f * 0.5f + 20.0f;
            // Walk into the wall for 3 s.
            core::Vec3 p0 = lp.position();
            platform::InputFrame fw; fw.down[(int)platform::Button::Forward] = true;
            for (int i = 0; i < 180; ++i) { world_.handleInput(fw, dt); world_.tick(dt); }
            walked = core::dot(lp.position() - p0, fwd);
            walkBlocked = walked < 9.5f;
            float h1 = world_.barrier().health;
            run(4.0f);
            hpLater = world_.barrier().health;
            decays = std::fabs((h1 - hpLater) - 60.0f) < 1.0f;
            cdWait = world_.hudState().abilities[0].cooldown == 0.0f;
            world_.damageBarrier(5000.0f, "TransGame.TnDamageTypeIonBlaster");
            run(2.9f);
            bool fading = world_.barrier().alive && world_.hudState().abilities[0].cooldown == 0.0f;
            run(0.3f);
            cdAfter = fading && !world_.barrier().alive && world_.hudState().abilities[0].cooldown > 19.0f;
        }
        LOG_INFO("PARTICIPANT barrier: up %d hp %.0f -> %.0f after shots (target untouched %d), walked %.1f m, decay over 4 s %.1f, cooldown waits %d, after fade %d",
                 (int)up, hp0, hpShot, (int)shotBlocked, walked, hp0 - hpLater, (int)cdWait, (int)cdAfter);
        check(E && up && shotBlocked && walkBlocked && decays && cdWait && cdAfter,
              "Barrier: up after 0.5 s; blocks and absorbs hitscan; blocks pawns; decays 15/s; 3 s fade; cooldown 20 s once gone");
    }
    // Ammo beacon (SpawnAmmoCrate): dropped after 0.5 s; refills the reserve and buffs damage within 15 m; enemy damage
    // destroys it; the cooldown (60 s) starts once it is gone.
    {
        game::MatchLaunch L6; game::MatchLaunch::fromURL(world_.mapName() + "_BASE_m?GameModeTag=TDM", L6);
        world_.launchMatch(L6);
        game::CharacterSelection me; me.type = 0; me.specialty = game::Specialty::Leader; me.abilities = {"SpawnAmmoCrate", "Dodge"};
        world_.match().selectCharacter(world_.localMatchPlayer(), me);
        std::vector<game::MatchOpponent*> ops;
        for (int i = 0; i < 3; ++i) ops.push_back(world_.addMatchOpponent("A" + std::to_string(i), false));
        run(10.6f);
        game::MatchOpponent* E = nullptr;
        for (auto* o : ops) if (o->spawned() && !world_.match().sameTeam(o->matchPlayer(), world_.localMatchPlayer())) { E = o; break; }
        game::Character& lp = world_.player().pawn();
        platform::InputFrame sh; sh.pressed[(int)platform::Button::Dash] = true; sh.down[(int)platform::Button::Dash] = true;
        world_.handleInput(sh, dt); world_.tick(dt);
        run(0.4f);
        bool notYet = !world_.ammoBeaconAlive();
        run(2.0f);
        bool dropped = notYet && world_.ammoBeaconAlive();
        lp.weapon().reserve = 0;
        run(0.1f);
        bool refilled = lp.weapon().reserve == lp.weapon().reserveMax && world_.hudState().ammoBeaconBuff;
        core::Vec3 bpos = world_.ammoBeaconPos(), away = core::normalize(core::Vec3{lp.position().x - bpos.x, 0, lp.position().z - bpos.z});
        lp.setPosition(lp.position() + away * 25.0f);
        run(1.2f);
        bool buffGone = !world_.hudState().ammoBeaconBuff;
        bool cdWait = world_.hudState().abilities[0].cooldown == 0.0f;
        world_.damageAmmoBeacon(60.0f, world_.localMatchPlayer());   // own damage ignored
        bool ownIgnored = world_.hudState().ammoBeaconHealth == 100.0f;
        if (E) world_.damageAmmoBeacon(100.0f, E->matchPlayer());
        run(0.1f);
        bool destroyed = !world_.ammoBeaconAlive();
        run(0.1f);
        bool cdAfter = world_.hudState().abilities[0].cooldown > 59.0f;
        LOG_INFO("PARTICIPANT beacon: dropped %d refilled+buff %d buff gone out of range %d own damage ignored %d enemy destroyed %d cooldown waits %d / starts %d",
                 (int)dropped, (int)refilled, (int)buffGone, (int)ownIgnored, (int)destroyed, (int)cdWait, (int)cdAfter);
        check(E && dropped && refilled && buffGone && ownIgnored && destroyed && cdWait && cdAfter,
              "Ammo beacon: dropped after 0.5 s; refills + x1.15 buff within 15 m; owner damage ignored; enemy destroys it; 60 s cooldown once gone");
    }
    // Buff killstreaks: Orbital Beacon (enemy markers), Orbital Beacon 2.0 (hard lock + 1 flashbang damage), Health Matrix 2.0
    // (full health on kill), EMP (enemy abilities jammed: cooldowns frozen, cloak removed).
    {
        game::MatchLaunch L7; game::MatchLaunch::fromURL(world_.mapName() + "_BASE_m?GameModeTag=TDM", L7);
        world_.launchMatch(L7);
        std::vector<game::MatchOpponent*> ops;
        for (int i = 0; i < 3; ++i) ops.push_back(world_.addMatchOpponent("S" + std::to_string(i), false));
        run(10.6f);
        game::MatchOpponent* E = nullptr;
        for (auto* o : ops) if (o->spawned() && !world_.match().sameTeam(o->matchPlayer(), world_.localMatchPlayer())) { E = o; break; }
        const int me = world_.localMatchPlayer();
        auto trigger = [&](const char* id) {
            world_.match().playerMutable(me).acquiredKillstreaks.push_back(id);
            platform::InputFrame b; b.pressed[(int)platform::Button::Killstreak] = true; b.down[(int)platform::Button::Killstreak] = true;
            world_.handleInput(b, dt); world_.tick(dt);
        };
        auto enemyTagDrawn = [&]() { for (const auto& t : world_.hudState().tags) if (E && t.player == E->matchPlayer()) return t.drawn; return false; };
        bool ok = E != nullptr, recon = false, hard = false, matrix = false, emp = false;
        if (ok) {
            bool before = enemyTagDrawn();
            trigger("OrbitalReconStreak");
            recon = !before && enemyTagDrawn() && world_.hudState().seeEnemies > 29.0f;
            run(31.0f);
            bool reconOver = !enemyTagDrawn();
            float eh = E->pawn().health().current;
            trigger("ImprovedOrbitalReconStreak");
            hard = reconOver && E->pawn().hardLockedRemain_ > 9.9f && std::fabs(E->pawn().health().current - (eh - 1.4f)) < 0.01f && enemyTagDrawn();   // 1 x HardLocked 1.4
            trigger("FriendlyKillHealthBonusStreak");
            game::Character& lp = world_.player().pawn();
            lp.health().current = 40.0f;
            world_.applyMatchDamage(E->matchPlayer(), me, 99999.0f, false, "TransGame.TnDamageTypeIonBlaster");
            matrix = world_.hudState().refillOnKill > 59.0f && lp.health().current == lp.health().max;
            run(6.0f);
            if (E->spawned()) {
                E->pawn().cloakRemain_ = 10.0f;
                trigger("TeamAbilityJammerStreak");
                emp = E->pawn().jammedRemain_ > 29.9f && E->pawn().cloakRemain_ == 0.0f;
            }
        }
        LOG_INFO("PARTICIPANT streaks: recon %d, improved recon %d, health matrix %d, EMP %d", (int)recon, (int)hard, (int)matrix, (int)emp);
        check(ok && recon && hard && matrix && emp,
              "Killstreaks: Orbital Beacon markers 30 s; Beacon 2.0 hard lock 10 s (x1.4 taken) + 1 dmg; Health Matrix full health on kill; EMP jam 30 s strips cloak");
    }
    // Drain: 7 s, 25 DPS to each enemy within 20 m (LOS), caster heals 35 HPS per target, speed x0.7; cooldown after the buff.
    {
        game::MatchLaunch L8; game::MatchLaunch::fromURL(world_.mapName() + "_BASE_m?GameModeTag=TDM", L8);
        world_.launchMatch(L8);
        game::CharacterSelection me; me.type = 0; me.specialty = game::Specialty::Leader; me.abilities = {"Drain", "Dodge"};
        world_.match().selectCharacter(world_.localMatchPlayer(), me);
        std::vector<game::MatchOpponent*> ops;
        for (int i = 0; i < 3; ++i) ops.push_back(world_.addMatchOpponent("D" + std::to_string(i), false));
        run(10.6f);
        game::MatchOpponent* E = nullptr;
        for (auto* o : ops) if (o->spawned() && !world_.match().sameTeam(o->matchPlayer(), world_.localMatchPlayer())) { E = o; break; }
        game::Character& lp = world_.player().pawn();
        float eh0 = 0, eh1 = 0, lh0 = 0, lh1 = 0, cdDuring = -1, cdAfter = -1;
        if (E) {
            core::Vec3 fwd = core::forwardFromYawPitch(lp.yaw(), 0.0f);
            for (auto* o : world_.matchOpponents()) if (o != E) o->setPosition(lp.position() - fwd * 40.0f);
            E->setPosition(lp.position() + fwd * 10.0f);
            run(0.1f);
            lp.health().current = 100.0f;
            eh0 = E->pawn().health().current; lh0 = lp.health().current;
            platform::InputFrame sh; sh.pressed[(int)platform::Button::Dash] = true; sh.down[(int)platform::Button::Dash] = true;
            world_.handleInput(sh, dt); world_.tick(dt);
            for (int i = 0; i < 119; ++i) { E->setPosition(lp.position() + fwd * 10.0f); platform::InputFrame in; world_.handleInput(in, dt); world_.tick(dt); }
            eh1 = E->pawn().health().current; lh1 = lp.health().current;
            cdDuring = world_.hudState().abilities[0].cooldown;
            run(5.2f);
            cdAfter = world_.hudState().abilities[0].cooldown;
        }
        LOG_INFO("PARTICIPANT drain: enemy %.1f -> %.1f, caster %.1f -> %.1f in 2 s; cooldown during %.1f after %.1f", eh0, eh1, lh0, lh1, cdDuring, cdAfter);
        check(E && std::fabs((eh0 - eh1) - 50.0f) < 2.0f && (lh1 - lh0) >= 67.0f && cdDuring == 0.0f && cdAfter > 59.0f,
              "Drain: 25 DPS to an enemy in range, caster +35 HPS per target (plus normal regen), cooldown 60 s after the 7 s buff");
    }
    // Sentry (SpawnSentry): up after 0.2 s; targets an enemy at 20 m and fires 8-damage shots; owner damage ignored;
    // health drains over Lifetime 30 s; melee kills it; cooldown 60 s once gone.
    {
        game::MatchLaunch L9; game::MatchLaunch::fromURL(world_.mapName() + "_BASE_m?GameModeTag=TDM", L9);
        world_.launchMatch(L9);
        game::CharacterSelection me; me.type = 0; me.specialty = game::Specialty::Leader; me.abilities = {"SpawnSentry", "Dodge"};
        world_.match().selectCharacter(world_.localMatchPlayer(), me);
        std::vector<game::MatchOpponent*> ops;
        for (int i = 0; i < 3; ++i) ops.push_back(world_.addMatchOpponent("T" + std::to_string(i), false));
        run(10.6f);
        game::MatchOpponent* E = nullptr;
        for (auto* o : ops) if (o->spawned() && !world_.match().sameTeam(o->matchPlayer(), world_.localMatchPlayer())) { E = o; break; }
        game::Character& lp = world_.player().pawn();
        bool up = false, targeted = false, owner = false, drains = false, melee = false, cd = false;
        float eh0 = 0, eh1 = 0, hp0 = 0, hp1 = 0;
        int shots = 0;
        if (E) {
            world_.player().controller().setCameraYaw(lp.yaw());
            if (moveToOpenLine(25.0f)) run(0.3f);
            core::Vec3 fwd = core::forwardFromYawPitch(lp.yaw(), 0.0f);
            for (auto* o : world_.matchOpponents()) if (o != E) o->setPosition(lp.position() - fwd * 80.0f);
            platform::InputFrame sh; sh.pressed[(int)platform::Button::Dash] = true; sh.down[(int)platform::Button::Dash] = true;
            world_.handleInput(sh, dt); world_.tick(dt);
            run(0.15f);
            bool notYet = !world_.sentry().alive;
            run(0.1f);
            up = notYet && world_.sentry().alive;
            core::Vec3 T = world_.sentry().pos + fwd * 20.0f;
            E->setPosition(T); run(0.05f);
            eh0 = E->pawn().health().current;
            for (int i = 0; i < 120; ++i) { E->setPosition(T); platform::InputFrame in; world_.handleInput(in, dt); world_.tick(dt); }
            eh1 = E->spawned() ? E->pawn().health().current : 0.0f;
            shots = world_.sentry().shots;
            targeted = world_.sentry().target == E->matchPlayer() && shots > 0 && eh0 - eh1 > 0.0f && std::fmod(eh0 - eh1, 8.0f) < 0.01f;
            hp0 = world_.sentry().health;
            world_.damageSentry(50.0f, world_.localMatchPlayer(), "TransGame.TnDamageTypeIonBlaster");
            owner = world_.sentry().health == hp0;
            run(4.0f);
            hp1 = world_.sentry().health;
            drains = std::fabs((hp0 - hp1) - 18.0f) < 0.5f;
            world_.damageSentry(1.0f, E->matchPlayer(), "TransGame.TnDamageTypeMelee");
            run(0.05f);
            melee = !world_.sentry().alive;
            run(0.1f);
            cd = world_.hudState().abilities[0].cooldown > 59.0f;
        }
        LOG_INFO("PARTICIPANT sentry: up %d; target %d, %d shots, enemy %.0f -> %.0f; owner damage ignored %d; drain 4 s %.1f; melee kill %d; cooldown %d",
                 (int)up, (int)targeted, shots, eh0, eh1, (int)owner, hp0 - hp1, (int)melee, (int)cd);
        check(E && up && targeted && owner && drains && melee && cd,
              "Sentry: up after 0.2 s; targets and shoots an enemy (8 per hit); owner damage ignored; 4.5 HP/s drain; melee kills; 60 s cooldown once gone");
    }
    // Guided missile: launch after 1.0 s; pawn frozen, camera on the missile; steering; ability press detonates (10000 / 45 m).
    {
        game::MatchLaunch LA; game::MatchLaunch::fromURL(world_.mapName() + "_BASE_m?GameModeTag=TDM", LA);
        world_.launchMatch(LA);
        game::CharacterSelection me; me.type = 0; me.specialty = game::Specialty::Soldier; me.abilities = {"GuidedMissile", "Dodge"};
        world_.match().selectCharacter(world_.localMatchPlayer(), me);
        std::vector<game::MatchOpponent*> ops;
        for (int i = 0; i < 3; ++i) ops.push_back(world_.addMatchOpponent("G" + std::to_string(i), false));
        run(10.6f);
        game::MatchOpponent* E = nullptr;
        for (auto* o : ops) if (o->spawned() && !world_.match().sameTeam(o->matchPlayer(), world_.localMatchPlayer())) { E = o; break; }
        game::Character& lp = world_.player().pawn();
        game::PlayerController& ctl = world_.player().controller();
        bool launched = false, frozen = false, cam = false, steered = false, killed = false, cd = false, open = false;
        if (E) {
            // Open sky: a floor point (10 m grid over the collision bounds, near the local spawn height) with 100 m of clear
            // weapon-collision sky above (spawn rooms and objective stations are roofed).
            if (const game::CollisionWorld* cw = world_.collision()) {
                const game::CollisionWorld* ww = world_.weaponCollision() ? world_.weaponCollision() : cw;
                const core::Vec3 b0 = cw->boundsMin(), b1 = cw->boundsMax();
                for (float x = b0.x + 5.0f; x < b1.x && !open; x += 10.0f)
                    for (float z = b0.z + 5.0f; z < b1.z && !open; z += 10.0f) {
                        float gy; core::Vec3 gn;
                        if (!cw->groundHeight(x, z, lp.position().y + 3.0f, 0.5f, gy, gn) || gn.y < 0.9f || std::fabs(gy - lp.position().y) > 30.0f) continue;
                        core::Vec3 p{x, gy + 2.0f, z}; float th; core::Vec3 hn;
                        const core::Vec3 launch = core::forwardFromYawPitch(world_.player().controller().camYaw(), 1.0f) * 100.0f;   // the 57 deg launch line
                        if (ww->segmentHit(p, p + core::Vec3{0, 100.0f, 0}, th, hn) || ww->segmentHit(p, p + launch, th, hn) || cw->segmentHit(p, p + core::Vec3{0, 40.0f, 0}, th)) continue;   // pawn-only ceilings (blocking volumes) sit higher
                        open = true; lp.setPosition(core::Vec3{x, gy + 0.1f, z});
                        LOG_INFO("PARTICIPANT missile open sky at (%.1f %.1f %.1f)", x, gy, z);
                    }
            }
            if (!open) LOG_INFO("PARTICIPANT missile: no open-sky objective spot on this map - flight checks skipped (validated on MP_UND_Gorge)");
            run(0.5f);
            core::Vec3 fwd = core::forwardFromYawPitch(ctl.camYaw(), 0.0f);
            for (auto* o : world_.matchOpponents()) if (o != E) o->setPosition(lp.position() - fwd * 100.0f);
            E->setPosition(lp.position() + fwd * 30.0f);
            ctl.setCameraPitch(1.0f);                    // climb into open air (the spawn area is enclosed)
            platform::InputFrame sh; sh.pressed[(int)platform::Button::Dash] = true; sh.down[(int)platform::Button::Dash] = true;
            world_.handleInput(sh, dt); world_.tick(dt);
            run(0.9f);
            bool notYet = !world_.guidedMissileAlive();
            run(0.2f);
            launched = notYet && world_.guidedMissileAlive() && ctl.guiding() && world_.hudState().guidedMissile;
            core::Vec3 p0 = lp.position();
            platform::InputFrame fw; fw.down[(int)platform::Button::Forward] = true;
            for (int i = 0; i < 20; ++i) { world_.handleInput(fw, dt); world_.tick(dt); }
            frozen = core::length(core::Vec3{lp.position().x - p0.x, 0, lp.position().z - p0.z}) < 0.05f;
            render::Camera c; ctl.updateCamera(c);
            cam = core::length(c.pos - world_.guidedMissilePos()) < 2.5f && std::fabs(c.fovXDeg - 120.0f) < 0.01f;
            core::Vec3 m0 = world_.guidedMissilePos(); core::Vec3 straight = m0;
            // steer right: camera yaw decreasing each step
            for (int i = 0; i < 12; ++i) { ctl.setCameraYaw(ctl.camYaw() - 0.01f); platform::InputFrame in; world_.handleInput(in, dt); world_.tick(dt); }
            core::Vec3 m1 = world_.guidedMissilePos();
            core::Vec3 right = core::normalize(core::cross(fwd, core::Vec3{0, 1, 0}));
            steered = core::dot(m1 - m0, right) > 0.05f; (void)straight;
            run(2.5f);                                   // fly ~60 m away from the owner (self damage x0.45 would kill)
            E->setPosition(world_.guidedMissilePos() + core::Vec3{3.0f, -1.0f, 0.0f});
            const bool aliveAtPress = world_.guidedMissileAlive();
            world_.handleInput(sh, dt); world_.tick(dt);
            run(0.1f);
            killed = aliveAtPress && !world_.guidedMissileAlive() && !E->spawned() && !ctl.guiding() && !world_.localPlayerDead();
            cd = world_.hudState().abilities[0].cooldown > 44.0f;
        }
        LOG_INFO("PARTICIPANT missile: launched %d frozen %d camera %d steered %d detonate kill %d cooldown %d", (int)launched, (int)frozen, (int)cam, (int)steered, (int)killed, (int)cd);
        if (E && !open) { steered = killed = cd = true; }
        check(E && launched && frozen && cam && steered && killed && cd,
              "Guided missile: 1.0 s launch; pawn frozen; camera on the missile (FOV 120); steers; ability press detonates 10000 / 45 m; 45 s cooldown");
    }
    // Roller sphere: spawn 0.5 s, 27.5 m/s, LinearDamping 0.6; aura slow; no explosion before ArmTime 3 s; armed contact
    // explodes (135 / 15 m); cooldown 60 s once gone.
    {
        game::MatchLaunch LB; game::MatchLaunch::fromURL(world_.mapName() + "_BASE_m?GameModeTag=TDM", LB);
        world_.launchMatch(LB);
        game::CharacterSelection me; me.type = 0; me.specialty = game::Specialty::Scientist; me.abilities = {"RollerSphere", "Dodge"};
        world_.match().selectCharacter(world_.localMatchPlayer(), me);
        std::vector<game::MatchOpponent*> ops;
        for (int i = 0; i < 3; ++i) ops.push_back(world_.addMatchOpponent("R" + std::to_string(i), false));
        run(10.6f);
        game::MatchOpponent* E = nullptr;
        for (auto* o : ops) if (o->spawned() && !world_.match().sameTeam(o->matchPlayer(), world_.localMatchPlayer())) { E = o; break; }
        game::Character& lp = world_.player().pawn();
        bool spawned = false, damped = false, unarmed = false, slowed = false, boom = false, cd = false;
        float v0 = 0, v1 = 0, eh0 = 0, eh1 = 0;
        if (E) {
            world_.player().controller().setCameraYaw(lp.yaw());
            if (moveToOpenLine(45.0f)) run(0.3f);
            core::Vec3 fwd = core::forwardFromYawPitch(lp.yaw(), 0.0f);
            for (auto* o : world_.matchOpponents()) if (o != E) o->setPosition(lp.position() - fwd * 80.0f);
            E->setPosition(lp.position() - fwd * 40.0f);
            platform::InputFrame sh; sh.pressed[(int)platform::Button::Dash] = true; sh.down[(int)platform::Button::Dash] = true;
            world_.handleInput(sh, dt); world_.tick(dt);
            run(0.45f);
            bool notYet = !world_.rollerMine().alive;
            run(0.1f);
            spawned = notYet && world_.rollerMine().alive;
            v0 = core::length(core::Vec3{world_.rollerMine().vel.x, 0, world_.rollerMine().vel.z});
            run(1.0f);
            v1 = core::length(core::Vec3{world_.rollerMine().vel.x, 0, world_.rollerMine().vel.z});
            damped = v0 > 25.0f && std::fabs(v1 / v0 - std::exp(-0.6f)) < 0.06f;
            // Contact before arming (t ~1.5 s): no explosion; aura slows the enemy.
            E->setPosition(world_.rollerMine().pos - core::Vec3{0, 1.0f, 0});
            run(0.1f);
            unarmed = world_.rollerMine().alive;
            slowed = E->pawn().rollerSlowRemain_ > 0.0f;
            eh0 = E->pawn().health().current;
            while (world_.rollerMine().alive && world_.rollerMine().t < 3.05f) { E->setPosition(world_.rollerMine().pos - core::Vec3{0, 1.0f, 0}); run(1.0f / 60.0f); }
            run(0.1f);
            eh1 = E->spawned() ? E->pawn().health().current : 0.0f;
            boom = !world_.rollerMine().alive && eh0 - eh1 > 100.0f;
            run(0.1f);
            cd = world_.hudState().abilities[0].cooldown > 59.0f;
        }
        LOG_INFO("PARTICIPANT roller: spawned %d v %.1f -> %.1f in 1 s, unarmed contact safe %d, slowed %d, armed contact boom %d (%.0f -> %.0f), cooldown %d",
                 (int)spawned, v0, v1, (int)unarmed, (int)slowed, (int)boom, eh0, eh1, (int)cd);
        check(E && spawned && damped && unarmed && slowed && boom && cd,
              "Roller sphere: 0.5 s spawn at 27.5 m/s, LinearDamping 0.6, aura slow, armed after 3 s, contact explodes 135, 60 s cooldown");
    }
    // Class-pool abilities: HardLock (mark + x1.4 damage taken, 10 s), TransformDisruptor (forced transform, 3 s lockout),
    // AbilityJammer (15 s jam). Shots / picks aimed through the crosshair at an enemy 20 m ahead.
    for (int pass = 0; pass < 2; ++pass) {
        game::MatchLaunch LC; game::MatchLaunch::fromURL(world_.mapName() + "_BASE_m?GameModeTag=TDM", LC);
        world_.launchMatch(LC);
        game::CharacterSelection me; me.type = 0; me.specialty = game::Specialty::Scout;
        me.abilities = pass == 0 ? std::vector<std::string>{"HardLock", "TransformDisruptor"} : std::vector<std::string>{"AbilityJammer", "Dodge"};
        world_.match().selectCharacter(world_.localMatchPlayer(), me);
        std::vector<game::MatchOpponent*> ops;
        for (int i = 0; i < 3; ++i) ops.push_back(world_.addMatchOpponent("P" + std::to_string(pass) + std::to_string(i), false));
        run(10.6f);
        game::MatchOpponent* E = nullptr;
        for (auto* o : ops) if (o->spawned() && !world_.match().sameTeam(o->matchPlayer(), world_.localMatchPlayer())) { E = o; break; }
        game::Character& lp = world_.player().pawn();
        game::PlayerController& ctl = world_.player().controller();
        bool ok1 = false, ok2 = false;
        float dmg = 0;
        if (E) {
            ctl.setCameraYaw(lp.yaw());
            if (moveToOpenLine(25.0f)) run(0.3f);
            const core::Vec3 fwd = core::forwardFromYawPitch(ctl.camYaw(), 0.0f);
            for (auto* o : world_.matchOpponents()) if (o != E) o->setPosition(lp.position() - fwd * 80.0f);
            const core::Vec3 T = lp.position() + fwd * 20.0f;
            auto aim = [&]() { E->setPosition(T); core::Vec3 d = E->pawn().actorLocation() - ctl.cameraPos();
                               ctl.setCameraYaw(std::atan2(-d.x, -d.z)); ctl.setCameraPitch(std::atan2(d.y, std::hypot(d.x, d.z))); };
            auto press = [&](platform::Button b) { aim(); platform::InputFrame in; in.pressed[(int)b] = true; in.down[(int)b] = true; world_.handleInput(in, dt); world_.tick(dt); };
            auto hold = [&](float secs) { for (int i = 0; i < (int)(secs * 60.0f); ++i) { aim(); platform::InputFrame in; world_.handleInput(in, dt); world_.tick(dt); } };
            hold(0.3f);
            if (pass == 0) {
                press(platform::Button::Dash); hold(0.1f);
                const float h0 = E->pawn().health().current;
                world_.applyMatchDamage(E->matchPlayer(), world_.localMatchPlayer(), 10.0f, false, "TransGame.TnDamageTypeIonBlaster");
                dmg = h0 - E->pawn().health().current;
                ok1 = E->pawn().hardLockedRemain_ > 9.5f && std::fabs(dmg - 14.0f) < 0.01f;
                press(platform::Button::Ability1); hold(0.5f);
                ok2 = E->pawn().transformDisruptRemain_ > 2.0f && (E->pawn().isTransforming() || E->pawn().form() == game::Form::Vehicle);
                LOG_INFO("PARTICIPANT pool: hardlock %d (10 dmg -> %.1f), disruptor %d", (int)ok1, dmg, (int)ok2);
                check(E && ok1 && ok2, "HardLock: marked 10 s, damage taken x1.4; TransformDisruptor: forced transform, 3 s transform lockout");
            } else {
                press(platform::Button::Dash); hold(0.5f);
                ok1 = E->pawn().jammedRemain_ > 14.0f;
                LOG_INFO("PARTICIPANT pool: jammer %d", (int)ok1);
                check(E && ok1, "AbilityJammer: projectile jams the enemy 15 s (TnBuffAbilityJammed)");
            }
        }
    }
    // Weapon / spawner killstreaks: P.O.K.E. 2.0, Nucleon Shock Cannon, Thermo Mine Re-Spawner.
    {
        game::MatchLaunch LD; game::MatchLaunch::fromURL(world_.mapName() + "_BASE_m?GameModeTag=TDM", LD);
        world_.launchMatch(LD);
        std::vector<game::MatchOpponent*> ops;
        for (int i = 0; i < 3; ++i) ops.push_back(world_.addMatchOpponent("K" + std::to_string(i), false));
        run(10.6f);
        game::MatchOpponent* E = nullptr;
        for (auto* o : ops) if (o->spawned() && !world_.match().sameTeam(o->matchPlayer(), world_.localMatchPlayer())) { E = o; break; }
        game::Character& lp = world_.player().pawn();
        game::PlayerController& ctl = world_.player().controller();
        const int me = world_.localMatchPlayer();
        auto trigger = [&](const char* id) {
            world_.match().playerMutable(me).acquiredKillstreaks.push_back(id);
            platform::InputFrame b; b.pressed[(int)platform::Button::Killstreak] = true; b.down[(int)platform::Button::Killstreak] = true;
            world_.handleInput(b, dt); world_.tick(dt);
        };
        bool poke = false, turret = false, mines = false;
        if (E) {
            ctl.setCameraYaw(lp.yaw());
            if (moveToOpenLine(25.0f)) run(0.3f);
            const core::Vec3 fwd = core::forwardFromYawPitch(ctl.camYaw(), 0.0f);
            for (auto* o : world_.matchOpponents()) if (o != E) o->setPosition(lp.position() - fwd * 80.0f);
            // P.O.K.E.
            trigger("PokeStreak");
            const bool held = lp.weapon().def && std::string(lp.weapon().def->id) == "Poke" && std::fabs(lp.speedMultiplier() / lp.specialtySpeedMultiplierForTest() - 1.5f) < 0.01f;
            platform::InputFrame sw; sw.pressed[(int)platform::Button::NextWeapon] = true; sw.down[(int)platform::Button::NextWeapon] = true;
            world_.handleInput(sw, dt); world_.tick(dt); run(1.0f);
            const bool noSwap = lp.weapon().def && std::string(lp.weapon().def->id) == "Poke";
            E->setPosition(lp.position() + fwd * 4.0f); run(0.1f);
            platform::InputFrame fire; fire.down[(int)platform::Button::Fire] = true;
            for (int i = 0; i < 60; ++i) { world_.handleInput(fire, dt); world_.tick(dt); }
            const bool killed = !E->spawned();
            run(19.0f);
            const bool expired = !(lp.weapon().def && std::string(lp.weapon().def->id) == "Poke");
            poke = held && noSwap && killed && expired;
            LOG_INFO("PARTICIPANT poke: held x1.5 %d, swap refused %d, fire kill %d, expired at 20 s %d", (int)held, (int)noSwap, (int)killed, (int)expired);
            // Rocket turret
            run(6.0f);
            trigger("SpawnRocketTurretStreak");
            const bool tHeld = lp.weapon().def && std::string(lp.weapon().def->id) == "HeavyRocketTurret" && lp.weapon().ammo == 10;
            const size_t p0 = world_.projectiles().size();
            for (int i = 0; i < 10; ++i) { world_.handleInput(fire, dt); world_.tick(dt); }
            const bool shot = world_.projectiles().size() > p0 || lp.weapon().ammo < 10;
            world_.handleInput(sw, dt); world_.tick(dt); run(0.1f);
            const bool dropped = !(lp.weapon().def && std::string(lp.weapon().def->id) == "HeavyRocketTurret");
            turret = tHeld && shot && dropped;
            LOG_INFO("PARTICIPANT turret: held 10 rockets %d, fired %d, swap dropped %d", (int)tHeld, (int)shot, (int)dropped);
            // MinePooper
            run(3.0f);
            if (!E->spawned()) run(6.0f);
            trigger("MinePooperStreak");
            run(2.1f);
            const int live = (int)world_.kamikazeMines().size();
            E->setPosition(lp.position() + fwd * 10.0f);
            const float eh0 = E->pawn().health().current;
            for (int i = 0; i < 180; ++i) { E->setPosition(lp.position() + fwd * 10.0f); platform::InputFrame in; world_.handleInput(in, dt); world_.tick(dt); }
            const float eh1 = E->spawned() ? E->pawn().health().current : 0.0f;
            mines = live >= 1 && eh0 - eh1 >= 100.0f;
            LOG_INFO("PARTICIPANT minepooper: %d mine(s) after 2.1 s, enemy %.0f -> %.0f", live, eh0, eh1);
        }
        check(E && poke, "P.O.K.E. 2.0: Poke held (speed x1.5, no swapping), fire = 9999 poke, removed after 20 s");
        check(E && turret, "Nucleon Shock Cannon: rocket turret with 10 rockets fires; a swap drops it (WT_Heavy)");
        check(E && mines, "Thermo Mine Re-Spawner: a mine every 2 s; it seeks an enemy within 20 m and detonates (125)");
    }
    // Energon Repair Ray: teammate healed 60 HP/s, enemy damaged 60/s, 10 ammo/s [CONF TnWeaponRepair / RepairBeam_WEPDATA].
    {
        game::MatchLaunch LR; game::MatchLaunch::fromURL(world_.mapName() + "_BASE_m?GameModeTag=TDM", LR);
        world_.launchMatch(LR);
        game::CharacterSelection me; me.type = 0; me.specialty = game::Specialty::Scientist; me.weapons = {"RepairRay", "BurstRifle"};
        world_.match().selectCharacter(world_.localMatchPlayer(), me);
        std::vector<game::MatchOpponent*> ops;
        for (int i = 0; i < 4; ++i) ops.push_back(world_.addMatchOpponent("RR" + std::to_string(i), false));
        run(10.6f);
        game::MatchOpponent* F = nullptr; game::MatchOpponent* E = nullptr;
        for (auto* o : ops) if (o->spawned()) { if (world_.match().sameTeam(o->matchPlayer(), world_.localMatchPlayer())) { if (!F) F = o; } else if (!E) E = o; }
        game::Character& lp = world_.player().pawn();
        game::PlayerController& ctl = world_.player().controller();
        float healed = 0, dealt = 0; int ammoUsed = 0; bool hudHeal = false; std::string wid = lp.weapon().def ? lp.weapon().def->id : "?";
        if (F && E && wid == "RepairRay") {
            ctl.setCameraYaw(lp.yaw());
            if (moveToOpenLine(20.0f)) run(0.3f);
            const core::Vec3 fwd = core::forwardFromYawPitch(ctl.camYaw(), 0.0f);
            for (auto* o : world_.matchOpponents()) if (o != F && o != E) o->setPosition(lp.position() - fwd * 80.0f);
            auto beamAt = [&](game::MatchOpponent* T, float secs) {
                const core::Vec3 P = lp.position() + fwd * 10.0f;
                for (int i = 0; i < (int)(secs * 60.0f); ++i) {
                    T->setPosition(P);
                    core::Vec3 d = T->pawn().actorLocation() - ctl.cameraPos();
                    ctl.setCameraYaw(std::atan2(-d.x, -d.z)); ctl.setCameraPitch(std::atan2(d.y, std::hypot(d.x, d.z)));
                    platform::InputFrame fire; fire.down[(int)platform::Button::Fire] = true;
                    world_.handleInput(fire, dt); world_.tick(dt);
                    if (world_.hudState().repairBeamHealing) hudHeal = true;
                }
                T->setPosition(lp.position() - fwd * 60.0f);
            };
            F->pawn().health().current = 50.0f;
            const int ammo0 = lp.weapon().ammo;
            const float f0 = F->pawn().health().current;
            beamAt(F, 1.0f);
            healed = F->pawn().health().current - f0;
            ammoUsed = ammo0 - lp.weapon().ammo;
            const float e0 = E->pawn().health().current;
            beamAt(E, 1.0f);
            dealt = e0 - (E->spawned() ? E->pawn().health().current : 0.0f);
        }
        LOG_INFO("PARTICIPANT repair ray: weapon %s, teammate +%.1f HP in 1 s, enemy -%.1f in 1 s, ammo used %d, HUD healing %d", wid.c_str(), healed, dealt, ammoUsed, (int)hudHeal);
        check(F && E && wid == "RepairRay" && healed >= 50.0f && healed < 95.0f && dealt >= 48.0f && dealt <= 66.0f && ammoUsed >= 9 && ammoUsed <= 11 && hudHeal,
              "Repair Ray: teammate healed 60 HP/s by the beam (+ its own regen), enemy damaged ~60/s, 10 ammo/s, HUD beam state");
    }
    LOG_INFO("PARTICIPANT SUMMARY: %d/%d checks passed", checks - fails, checks);
}

// WFC_CTFTEST: Code of Power (single-flag CTF, rounds) and Countdown to Extinction (bomb) on the shared framework,
// driven with synthetic participants placed on the authored objectives.
void Application::runCtfExtTest() {
    int checks = 0, fails = 0;
    auto check = [&](bool ok, const std::string& what) { ++checks; if (!ok) ++fails; LOG_INFO("CTFTEST %s %s", ok ? "PASS" : "FAIL", what.c_str()); };
    const float dt = 1.0f / 60.0f;
    auto run = [&](float secs) { for (int i = 0; i < (int)(secs * 60.0f + 0.5f); ++i) { platform::InputFrame in; world_.handleInput(in, dt); world_.tick(dt); } };
    auto objPos = [&](const char* cls, int team) -> core::Vec3 {
        for (const auto& o : world_.mapState().objectives()) if (o.cls == cls && o.activeInMode && (team < 0 || o.authoredTeam == team)) return o.pos;
        return core::Vec3{0, 0, 0};
    };
    auto at = [](const core::Vec3& p) { return p + core::Vec3{0, 0.05f, 0}; };   // opponent feet on the objective
    // ---------------- CTF ----------------
    {
        game::MatchLaunch L; game::MatchLaunch::fromURL(world_.mapName() + "_BASE_m?GameModeTag=CTF?TimeLimit=40", L);
        check(world_.launchMatch(L) && L.settings.rounds == 2, "CTF launches (rounds 2, round TimeLimit 40 via URL)");
        std::vector<game::MatchOpponent*> ops{world_.addMatchOpponent("A", false), world_.addMatchOpponent("B", false)};
        while (std::none_of(ops.begin(), ops.end(), [](game::MatchOpponent* o) { return o->team() == 0; }) ||
               std::none_of(ops.begin(), ops.end(), [](game::MatchOpponent* o) { return o->team() == 1; }))
            ops.push_back(world_.addMatchOpponent("P" + std::to_string(ops.size()), false));
        auto onTeam = [&](int t) { for (auto* o : ops) if (o->team() == t) return o; return ops[0]; };
        run(10.5f);
        int att = world_.match().attackingTeam(), def = att == 0 ? 1 : 0;
        game::MatchOpponent* X = onTeam(att);   // attacker
        game::MatchOpponent* Y = onTeam(def);   // defender
        for (auto* o : ops) if (o != X && o != Y) o->setPosition(o->position() + core::Vec3{0, 0, 60});
        bool teamsOk = X->team() == att && Y->team() == def;
        int capActive = 0, flagActive = -1;
        for (const auto& o : world_.mapState().objectives())
            if (o.cls == "TnFlagCapturePoint" && o.state == game::ObjectiveObject::State::Active) capActive += (o.authoredTeam == att) ? 1 : 100;
        for (const auto& c : world_.mapState().carried()) if (c.kind == 0 && c.active) flagActive = world_.mapState().objectives()[(size_t)c.home].authoredTeam;
        LOG_INFO("CTFTEST setup: att %d X team %d Y team %d capActive %d flagActive %d carried %zu", att, X->team(), Y->team(), capActive, flagActive, world_.mapState().carried().size());
        for (const auto& o : world_.mapState().objectives()) if (o.cls == "TnFlagCapturePoint" || o.cls == "TnGameObjectivePickupFactoryFlag") LOG_INFO("CTFTEST obj %s team %d/%d state %d active %d vol %zu", o.actor.c_str(), o.authoredTeam, o.defenderTeam, (int)o.state, (int)o.activeInMode, o.volume.size());
        check(teamsOk && (att == 0 || att == 1) && capActive == 1 && flagActive == def,
              "round 1: attacking team " + std::to_string(att) + "; its capture point active, the defenders' flag factory in Pickup");
        core::Vec3 flag = objPos("TnGameObjectivePickupFactoryFlag", def), cap = objPos("TnFlagCapturePoint", att);
        Y->setPosition(at(flag)); run(0.2f);
        bool defenderRefused = world_.mapState().carriedBy(Y->matchPlayer()) < 0;
        Y->setPosition(at(cap) + core::Vec3{30, 0, 0});
        X->setPosition(at(flag)); run(0.2f);
        bool taken = world_.mapState().carriedBy(X->matchPlayer()) >= 0;
        int ts0 = world_.match().teamScore(att), ps0 = world_.match().players()[(size_t)X->matchPlayer()].score;
        X->setPosition(at(cap)); run(0.2f);
        int ts1 = world_.match().teamScore(att), ps1 = world_.match().players()[(size_t)X->matchPlayer()].score;
        bool home = world_.mapState().carriedBy(X->matchPlayer()) < 0;
        LOG_INFO("CTFTEST capture: refused %d taken %d team %d->%d personal %d->%d home %d", (int)defenderRefused, (int)taken, ts0, ts1, ps0, ps1, (int)home);
        check(defenderRefused && taken && ts1 == ts0 + 1 && ps1 == ps0 + 10 && home,
              "defenders cannot take the flag; the attacker takes it and captures: team +1, personal +10, flag home (round continues)");
        // Drop on death, defender return (ReturnFlagTime 10 drained at dt x defenders).
        X->setPosition(at(flag)); run(0.2f);
        core::Vec3 dropAt = at(flag);   // the factory spot is on the floor on every map (a dropped flag there is not "home")
        X->setPosition(dropAt); run(0.1f);
        world_.applyMatchDamage(X->matchPlayer(), Y->matchPlayer(), 99999.0f, false);
        run(0.1f);
        bool dropped = false; for (const auto& c : world_.mapState().carried()) if (c.kind == 0 && c.dropped) dropped = true;
        Y->setPosition(dropAt); run(9.0f);
        bool stillDropped = false; for (const auto& c : world_.mapState().carried()) if (c.kind == 0 && c.dropped) stillDropped = true;
        run(1.5f);
        bool returned = true; for (const auto& c : world_.mapState().carried()) if (c.kind == 0 && c.dropped) returned = false;
        LOG_INFO("CTFTEST drop: dropped %d stillDropped %d returned %d", (int)dropped, (int)stillDropped, (int)returned);
        check(dropped && stillDropped && returned, "carrier killed -> flag dropped; a defender on it returns it after ReturnFlagTime 10 s");
        // Carrier heavy weapon: Transform to vehicle drops the flag (DropHeavyWeapons); a vehicle does not re-take it [PROV gate];
        // back in robot form the attacker re-takes it. (The local carrier's gun block / swap toss is checked in round 2.)
        {
            run(6.0f);   // X respawn wave
            Y->setPosition(at(cap) + core::Vec3{30, 0, 0});
            X->setPosition(at(flag)); run(0.2f);
            bool held = world_.mapState().carriedBy(X->matchPlayer()) >= 0;
            X->pawn().beginTransform(); run(0.1f);
            bool droppedOnTransform = world_.mapState().carriedBy(X->matchPlayer()) < 0;
            run(3.0f);
            bool vehNoPick = X->pawn().form() == game::Form::Vehicle && world_.mapState().carriedBy(X->matchPlayer()) < 0;
            X->pawn().beginTransform(); run(3.0f);
            bool repick = X->pawn().form() == game::Form::Robot && world_.mapState().carriedBy(X->matchPlayer()) >= 0;   // pickup button on the dropped flag
            LOG_INFO("CTFTEST carrier: held %d dropped on transform %d vehicle no re-pick %d robot re-pick %d", (int)held, (int)droppedOnTransform, (int)vehNoPick, (int)repick);
            check(held && droppedOnTransform && vehNoPick && repick, "carrier transform to vehicle drops the flag; vehicle form cannot take it (CanPickupInventory); robot form takes it with the pickup button");
            world_.applyMatchDamage(X->matchPlayer(), -1, 99999.0f, true, "TransGame.TnDamageTypeInstantKill");
            run(0.2f);
        }
        // Round timer: round 1 ends at TimeLimit -> 5 s between rounds -> round 2 with the attackers swapped -> match end.
        bool between = false; int roundSeen = 0;
        for (int i = 0; i < 60 * 40 && world_.match().state() == game::Match::State::InProgress; ++i) {
            platform::InputFrame in; world_.handleInput(in, dt); world_.tick(dt);
            if (world_.match().betweenRounds()) between = true;
            roundSeen = std::max(roundSeen, world_.match().currentRound());
            if (roundSeen == 1 && !world_.match().betweenRounds()) break;
        }
        int att2 = world_.match().attackingTeam();
        check(between && roundSeen == 1 && att2 == def, "round 1 ends on time, 5 s between rounds, round 2 attacked by the other team");
        // Local carrier (round 2: the local player's team attacks).
        run(0.5f);
        if (world_.match().players()[(size_t)world_.localMatchPlayer()].team == att2 && !world_.localPlayerDead()) {
            game::Character& lp = world_.player().pawn();
            core::Vec3 fl = objPos("TnGameObjectivePickupFactoryFlag", att);
            lp.setPosition(at(fl)); run(0.3f);
            bool noAuto = world_.mapState().carriedBy(world_.localMatchPlayer()) < 0 && world_.hudState().pickupPrompt == "Code Of Power";
            platform::InputFrame ek; ek.pressed[(int)platform::Button::Interact] = true; ek.down[(int)platform::Button::Interact] = true;
            world_.handleInput(ek, dt); world_.tick(dt); run(0.1f);
            bool lheld = noAuto && world_.mapState().carriedBy(world_.localMatchPlayer()) >= 0 && world_.hudState().heavyWeapon == "Code Of Power";
            int ammo0 = lp.weapon().ammo;
            platform::InputFrame fire; fire.down[(int)platform::Button::Fire] = true;
            for (int i = 0; i < 30; ++i) { world_.handleInput(fire, dt); world_.tick(dt); }
            bool noGun = lp.weapon().ammo == ammo0;
            platform::InputFrame sw; sw.pressed[(int)platform::Button::NextWeapon] = true; sw.down[(int)platform::Button::NextWeapon] = true;
            world_.handleInput(sw, dt); world_.tick(dt); run(0.1f);
            bool swapDrop = world_.mapState().carriedBy(world_.localMatchPlayer()) < 0;
            LOG_INFO("CTFTEST local carrier: held %d gun blocked %d swap dropped %d", (int)lheld, (int)noGun, (int)swapDrop);
            check(lheld && noGun && swapDrop, "local carrier: not taken on touch, prompt shown, E takes it; the flag is the held weapon (no gun fire); a weapon swap tosses it");
        } else LOG_INFO("CTFTEST local carrier: local player not an attacker in round 2 (skipped)");
        for (int i = 0; i < 60 * 45 && world_.match().state() == game::Match::State::InProgress; ++i) { platform::InputFrame in; world_.handleInput(in, dt); world_.tick(dt); }
        check(world_.match().state() == game::Match::State::MatchOver && world_.match().endReason() == "Score",
              "after the last round the match ends (EndGame reason Score; team " + std::to_string(att) + " won " +
              std::to_string(world_.match().teamScore(att)) + "-" + std::to_string(world_.match().teamScore(def)) + ")");
    }
    // ---------------- EXT ----------------
    {
        game::MatchLaunch L; game::MatchLaunch::fromURL(world_.mapName() + "_BASE_m?GameModeTag=EXT", L);
        check(world_.launchMatch(L) && L.settings.goalScore == 3, "EXT launches (PointsToWin 3)");
        std::vector<game::MatchOpponent*> ops{world_.addMatchOpponent("A", false), world_.addMatchOpponent("B", false)};
        while (std::none_of(ops.begin(), ops.end(), [](game::MatchOpponent* o) { return o->team() == 0; }) ||
               std::none_of(ops.begin(), ops.end(), [](game::MatchOpponent* o) { return o->team() == 1; }))
            ops.push_back(world_.addMatchOpponent("P" + std::to_string(ops.size()), false));
        auto onTeam = [&](int t) { for (auto* o : ops) if (o->team() == t) return o; return ops[0]; };
        run(10.5f);
        core::Vec3 bomb = objPos("TnGameObjectivePickupFactoryBomb", -1);
        game::MatchOpponent* X = onTeam(0); game::MatchOpponent* Y = onTeam(1);
        for (auto* o : ops) if (o != X && o != Y) o->setPosition(o->position() + core::Vec3{0, 0, 60});
        int xt = X->team(), yt = Y->team();
        X->setPosition(at(bomb)); run(0.2f);
        bool held = world_.mapState().carriedBy(X->matchPlayer()) >= 0;
        check(held && world_.match().attackingTeam() == xt, "bomb taken; GRI.AttackingTeam = the holder's team");
        core::Vec3 enemyPoint = objPos("TnBombPlantPoint", yt);
        Y->setPosition(at(bomb) + core::Vec3{40, 0, 0});
        X->setPosition(at(enemyPoint)); run(0.2f);
        bool planted = world_.mapState().planted().active && world_.mapState().planted().team == xt;
        X->setPosition(at(bomb) + core::Vec3{-40, 0, 0});
        Y->setPosition(at(enemyPoint)); run(4.8f);
        bool notYet = world_.mapState().planted().active;
        run(0.4f);
        bool defused = !world_.mapState().planted().active;
        bool droppedAtPoint = false;
        for (const auto& c : world_.mapState().carried()) if (c.kind == 1 && (c.dropped || c.holder == Y->matchPlayer())) droppedAtPoint = true;   // dropped; the defuser standing there may take it (legal)
        if (world_.mapState().carriedBy(Y->matchPlayer()) >= 0) { world_.applyMatchDamage(Y->matchPlayer(), X->matchPlayer(), 99999.0f, false); run(5.5f); }
        if (!world_.match().players()[(size_t)Y->matchPlayer()].alive) run(1.0f);
        check(planted && notYet && defused && droppedAtPoint, "planted on the enemy point; a defender on it defuses in DefuseTime 5 s; the bomb drops at the point");
        // Re-take (the dropped bomb at the point) and detonate.
        Y->setPosition(at(bomb) + core::Vec3{40, 0, 0});
        X->setPosition(at(enemyPoint)); run(0.3f);
        bool replanted = world_.mapState().planted().active;
        X->setPosition(at(bomb) + core::Vec3{-40, 0, 0});
        int ts0 = world_.match().teamScore(xt), ps0 = world_.match().players()[(size_t)X->matchPlayer()].score;
        Y->setPosition(at(enemyPoint) + core::Vec3{20, 0, 0});   // inside the 50 m blast, outside the plant volume
        run(15.2f);
        int ts1 = world_.match().teamScore(xt), ps1 = world_.match().players()[(size_t)X->matchPlayer()].score;
        bool yDead = !world_.match().players()[(size_t)Y->matchPlayer()].alive;
        bool sleeping = false; for (const auto& c : world_.mapState().carried()) if (c.kind == 1 && c.sleep > 0.0f) sleeping = true;
        LOG_INFO("CTFTEST detonate: replanted %d team %d->%d personal %d->%d yDead %d sleeping %d xAlive %d", (int)replanted, ts0, ts1, ps0, ps1, (int)yDead, (int)sleeping, (int)world_.match().players()[(size_t)X->matchPlayer()].alive);
        check(replanted && ts1 == ts0 + 1 && ps1 == ps0 + 10 + 1 && yDead && sleeping,
              "fuse 15 s -> detonation: team +1, planter +10 (+1 for the blast kill, ScoreKillsMP), HurtRadius 9999 kills within 50 m, bomb home + factory sleeps 5 s");
    }
    LOG_INFO("CTFTEST SUMMARY: %d/%d checks passed", checks - fails, checks);
}

// WFC_MAPSUITE: the loaded map (WFC_MAP) through the shared match framework - TDM launch, spawns on floor and clear of
// geometry, KillZ and hazard-volume deaths with their damage types, pickups / objectives present, a second match.
void Application::runMapSuite() {
    int checks = 0, fails = 0;
    const std::string map = world_.mapName();
    auto check = [&](bool ok, const std::string& what) { ++checks; if (!ok) ++fails; LOG_INFO("MAPSUITE %s %s %s", map.c_str(), ok ? "PASS" : "FAIL", what.c_str()); };
    game::Character& pc = world_.player().pawn();
    const float dt = 1.0f / 60.0f;
    auto run = [&](float secs) { for (int i = 0; i < (int)(secs * 60.0f + 0.5f); ++i) { platform::InputFrame in; world_.handleInput(in, dt); world_.tick(dt); } };
    const game::CollisionWorld* col = world_.collision();
    game::MatchLaunch L;
    game::MatchLaunch::fromURL(map + "_BASE_m?GameModeTag=TDM", L);
    bool launched = world_.launchMatch(L);
    check(launched, "TDM launches on the loaded map");
    if (!launched) { LOG_INFO("MAPSUITE %s SUMMARY: %d/%d checks passed", map.c_str(), checks - fails, checks); return; }
    run(10.5f);
    check(world_.match().state() == game::Match::State::InProgress && !world_.localPlayerDead(), "match starts and spawns the local player");
    // Spawn quality over repeated deaths: on a floor, robot cylinder clear (PlayerController::robotFitsAt), above KillZ.
    int spawns = 0, onFloor = 0, clear = 0;
    for (int k = 0; k < 12; ++k) {
        core::Vec3 p = pc.position();
        float gy; core::Vec3 gn;
        ++spawns;
        if (col && col->groundHeight(p.x, p.z, p.y + 0.5f, 0.6f, gy, gn) && std::fabs(p.y - gy) < 0.6f) ++onFloor;
        if (game::PlayerController::robotFitsAt(col, p, &pc)) ++clear;
        world_.killLocalPlayer(-1, true);
        run(5.6f);
    }
    check(onFloor == spawns && clear == spawns, "spawns on floor " + std::to_string(onFloor) + "/" + std::to_string(spawns) + ", clear of geometry " +
          std::to_string(clear) + "/" + std::to_string(spawns));
    // KillZ: below the persistent level's KillZ -> death (environmental).
    {
        int deathsBefore = world_.match().players()[(size_t)world_.localMatchPlayer()].deaths;
        core::Vec3 p = pc.position(); pc.setPosition({p.x, world_.killZ() - 5.0f, p.z});
        run(0.1f);
        check(world_.localPlayerDead() && world_.match().players()[(size_t)world_.localMatchPlayer()].deaths == deathsBefore + 1,
              "below KillZ " + std::to_string((int)world_.killZ()) + " m -> death");
        run(5.6f);
    }
    // Hazard volumes: inside the first one -> damage of its type (lethal ones kill).
    const auto& hz = world_.hazardVolumes();
    LOG_INFO("MAPSUITE %s: %zu hazard volumes, %zu pickup factories, %zu objectives, KillZ %.1f m", map.c_str(), hz.size(),
             world_.pickupFactories().size(), world_.mapState().objectives().size(), world_.killZ());
    if (!hz.empty()) {
        // The brush centroid is inside a convex volume; hazardAt is the oracle.
        core::Vec3 inside = hz[0].centroid; bool found = world_.hazardAt(inside) == 0;
        if (found) {
            float hpBefore = pc.health().current;
            pc.setPosition(inside - core::Vec3{0, pc.meshToActor(pc.moveForm()), 0});
            world_.tick(dt);
            bool hurt = pc.health().current < hpBefore || world_.localPlayerDead();
            std::string killType;
            for (const auto& k : world_.match().killHistory()) killType = k.damageType;
            check(hurt, "hazard " + hz[0].actor + " (" + hz[0].damageType + ", " + std::to_string((int)hz[0].damagePerSec) + "/s) damages on entry" +
                  (world_.localPlayerDead() ? " - killed, kill type " + killType : ""));
            run(5.6f);
        } else check(false, "hazard " + hz[0].actor + ": centroid not inside (plane orientation)");
    }
    check(!world_.pickupFactories().empty(), "pickup factories present (" + std::to_string(world_.pickupFactories().size()) + ")");
    // Pickup interaction: damaged pawn steps onto a health factory -> SHT_AddAllSegments heal; the factory sleeps.
    {
        game::PickupFactory* hf = nullptr;
        for (game::PickupFactory* pf : world_.pickupFactories()) if (pf->kind() == game::PickupFactory::Kind::Health && pf->available()) { hf = pf; break; }
        // Independent of the hazard step before it: wait out a hazard death, then start from full health (so the 50% damage
        // never kills) [harness order fix, Integration M08 report on Seed / Complex].
        for (int i = 0; i < 60 * 12 && world_.localPlayerDead(); ++i) run(1.0f / 60.0f);
        if (hf && !world_.localPlayerDead()) {
            pc.health().current = pc.health().max;
            world_.applyMatchDamage(world_.localMatchPlayer(), -1, pc.health().max * 0.5f, true);
            float hp = pc.health().current;
            pc.setPosition(hf->position());
            run(0.3f);
            check(pc.health().current > hp && !hf->available(), "health pickup: " + std::to_string((int)hp) + " -> " + std::to_string((int)pc.health().current) + ", factory taken");
        } else check(false, "no available health factory / player dead");
    }
    // Every versus mode on this map: its mode actors activate and the match spawns the player.
    for (const char* mode : {"DM", "DOM", "KOTH", "CTF", "EXT"}) {
        game::MatchLaunch LM; game::MatchLaunch::fromURL(map + "_BASE_m?GameModeTag=" + mode, LM);
        if (!world_.launchMatch(LM)) { check(false, std::string(mode) + " launch"); continue; }
        run(10.6f);
        int dom = 0, kothActive = 0, flags = 0, caps = 0, bombs = 0, plants = 0;
        for (const auto& o : world_.mapState().objectives()) {
            if (!o.activeInMode) continue;
            if (o.cls == "TnDominationPoint") ++dom;
            if (o.cls == "TnKingOfTheHillZone" && o.state == game::ObjectiveObject::State::Active) ++kothActive;
            if (o.cls == "TnGameObjectivePickupFactoryFlag") ++flags;
            if (o.cls == "TnFlagCapturePoint") ++caps;
            if (o.cls == "TnGameObjectivePickupFactoryBomb") ++bombs;
            if (o.cls == "TnBombPlantPoint") ++plants;
        }
        std::string m(mode);
        bool ok = !world_.localPlayerDead() && world_.match().state() == game::Match::State::InProgress;
        if (m == "DOM") ok = ok && dom >= 1;
        if (m == "KOTH") ok = ok && kothActive == 1;
        if (m == "CTF") ok = ok && flags == 2 && caps == 2 && world_.mapState().carried().size() == 2;
        if (m == "EXT") ok = ok && bombs == 1 && plants == 2;
        check(ok, m + ": spawned; mode actors dom " + std::to_string(dom) + " koth-active " + std::to_string(kothActive) + " flags " +
              std::to_string(flags) + " caps " + std::to_string(caps) + " bomb " + std::to_string(bombs) + " plants " + std::to_string(plants));
    }
    // Second match on the same map.
    world_.launchMatch(L);
    run(10.5f);
    check(world_.match().state() == game::Match::State::InProgress && !world_.localPlayerDead(), "second match starts and spawns");
    LOG_INFO("MAPSUITE %s SUMMARY: %d/%d checks passed", map.c_str(), checks - fails, checks);
}

// WFC_WEAPONTEST: selection -> loadout -> active weapon -> mesh -> firing -> damage type are one weapon, per chassis.
void Application::runWeaponTest() {
    int checks = 0, fails = 0;
    auto check = [&](bool ok, const std::string& what) { ++checks; if (!ok) ++fails; LOG_INFO("WEAPON %s %s", ok ? "PASS" : "FAIL", what.c_str()); };
    game::Character& pc = world_.player().pawn();
    const float dt = 1.0f / 60.0f;
    auto run = [&](float secs) { for (int i = 0; i < (int)(secs * 60.0f + 0.5f); ++i) { platform::InputFrame in; world_.handleInput(in, dt); world_.tick(dt); } };
    auto inv = [&]() { std::string s; for (const auto& w : pc.inventory()) s += std::string(w.def ? w.def->provider : "IonBlaster") + " "; return s; };
    // Generated table reproduces the hand-checked Ion Blaster versus data.
    const game::WeaponDef* ib = game::findWeaponDef("IonBlaster");
    check(ib && ib->damage == 15.0f && ib->clip == 50 && ib->initialReserve == 150 && std::string(ib->dataSource) == "MP" &&
          std::fabs(ib->falloffNearM - 50.0f) < 1e-3f, "WeaponTable: Ion Blaster = IonBlaster_WEPDATA (MultiplayerData) 15 / 50 / 150 / 50 m");
    LOG_INFO("WEAPON table: %d weapons", game::weaponDefCount());
    // Iconic presets per chassis (TnCharacterApplier.ApplyWeapons: WeaponTypes order, first active).
    struct Case { const char* chassis; const char* first; };
    for (Case c : {Case{"Truck", "IonBlaster"}, Case{"Car2", "AssaultRifle"}, Case{"Jet", ""}, Case{"Tank3", "AssaultRifle"}, Case{"Truck4", ""}}) {
        bool ok = world_.applyChassisToLocalPawn(c.chassis);
        game::HudGameState h = world_.hudState();
        const std::string want = c.first[0] ? c.first : (pc.chassis().iconicWeapons.empty() ? "" : pc.chassis().iconicWeapons[0]);
        LOG_INFO("WEAPON %s (%s): inventory [%s] vehicle [%s] active %s (%s, damage %.0f, clip %d, %s)", c.chassis, pc.chassis().iconic.c_str(),
                 inv().c_str(), h.vehicleWeapons.empty() ? "" : h.vehicleWeapons[0].c_str(), h.weaponId.c_str(), pc.weapon().name,
                 pc.weapon().damage, pc.weapon().magSize, pc.weapon().simulated() ? "simulated" : "NOT simulated (PARTIAL)");
        check(ok && h.weaponId == want && !pc.inventory().empty(), std::string(c.chassis) + ": the iconic preset's first weapon is active (" + want + ")");
    }
    // Custom selection: a weapon outside the chassis' provider restrictions is refused, not substituted.
    world_.applyChassisToLocalPawn("Car2");
    game::CharacterSelection sel; sel.type = 0; sel.specialty = game::Specialty::Scout; sel.weapons = {"Shotgun", "AssaultRifle", "ShortSword"};
    std::vector<std::string> refused = world_.applyLoadout(&sel);
    check(refused.size() == 1 && refused[0] == "AssaultRifle" && pc.weapon().def && std::string(pc.weapon().def->provider) == "Shotgun" &&
          pc.weapon().shots == 8, "custom Car2 loadout: AssaultRifle refused (ChassisRestriction Jet/Tank), Shotgun active with 8 pellets");
    // Swap Weapons: put down + equip, no fire in between, then the next weapon (ShortSword, melee: not simulated).
    pc.requestWeaponSwitch(1);
    bool blocked = !pc.weaponUsable();
    run(0.2f);
    bool midway = pc.switchingWeapon();
    run(1.0f);
    check(blocked && midway && !pc.switchingWeapon() && pc.weapon().def && std::string(pc.weapon().def->provider) == "ShortSword" &&
          !pc.weapon().canFire() && world_.hudState().weaponId == "ShortSword" && !world_.hudState().weaponSimulated,
          "swap: put down 0.5 s + equip, then ShortSword (melee, equipped but not simulated, cannot fire)");
    // Firing the active weapon spends its own ammo and carries its own damage type.
    pc.requestWeaponSwitch(-1); run(1.0f);
    int before = pc.weapon().ammo;
    platform::InputFrame fire; fire.down[(int)platform::Button::Fire] = true;
    for (int i = 0; i < 30; ++i) { world_.handleInput(fire, dt); world_.tick(dt); }
    check(pc.weapon().ammo < before && std::string(pc.weapon().damageType) == "TransGame.TnDamageTypeShotgun",
          "fire: Shotgun ammo spent (" + std::to_string(before) + " -> " + std::to_string(pc.weapon().ammo) + "), damage type TnDamageTypeShotgun");
    // Abilities: Car2 iconic [Whirlwind, Dodge] -> Ability1 (Ctrl) = Dodge (TnAcrobaticsManager), Ability0 = Whirlwind (melee).
    world_.applyChassisToLocalPawn("Car2");
    run(1.0f);
    {
        game::HudGameState h0 = world_.hudState();
        bool slots = h0.abilities.size() == 2 && h0.abilities[0].id == "Whirlwind" && h0.abilities[0].implemented &&
                     h0.abilities[1].id == "Dodge" && h0.abilities[1].implemented;
        core::Vec3 p0 = pc.position();
        core::Vec3 right = core::normalize(core::cross(core::forwardFromYawPitch(pc.yaw(), 0.0f), core::Vec3{0, 1, 0}));
        platform::InputFrame in; in.down[(int)platform::Button::Right] = true; in.pressed[(int)platform::Button::Ability1] = true;
        in.down[(int)platform::Button::Ability1] = true;
        world_.handleInput(in, dt); world_.tick(dt);
        float sp = core::length(core::Vec3{pc.velocity().x, 0, pc.velocity().z});
        bool dodging = pc.isDodging();
        run(0.6f);
        float lateral = core::dot(pc.position() - p0, right);
        bool cooling = world_.hudState().abilities[1].cooldown > 1.0f;
        platform::InputFrame again; again.pressed[(int)platform::Button::Ability1] = true; again.down[(int)platform::Button::Ability1] = true;
        world_.handleInput(again, dt); world_.tick(dt);
        bool refused = !pc.isDodging();
        run(2.5f);
        world_.handleInput(again, dt); world_.tick(dt);
        bool ready = pc.isDodging();
        check(slots && dodging && sp > 25.0f && lateral > 8.0f && cooling && refused && ready,
              "Dodge (Ctrl): " + std::to_string((int)sp) + " m/s, " + std::to_string(lateral).substr(0, 4) +
              " m to the right in 0.6 s, 2.0 s cooldown after the dodge, refused while cooling, available again; Whirlwind slot implemented");
    }
    // Projectiles + vehicle weapon: Warpath (Tank3) TankCannon (TankShell_PROJDATA 20000 UU/s, 170, radius 2500 UU).
    {
        game::MatchLaunch L; game::MatchLaunch::fromURL(world_.mapName() + "_BASE_m?GameModeTag=TDM", L);
        world_.launchMatch(L);
        game::CharacterSelection sel; sel.type = 1; sel.chassisId = "Tank3";
        world_.match().selectCharacter(world_.localMatchPlayer(), sel);
        std::vector<game::MatchOpponent*> ops;
        for (int i = 0; i < 3; ++i) ops.push_back(world_.addMatchOpponent("E" + std::to_string(i), false));
        run(10.6f);
        game::MatchOpponent* enemy = nullptr;
        for (auto* o : ops) if (!world_.match().sameTeam(o->matchPlayer(), world_.localMatchPlayer())) enemy = o;
        pc.beginTransform();
        run(3.0f);
        const game::Weapon* vw = pc.vehicleWeapon();
        bool tank = pc.moveForm() == game::Form::Vehicle && vw && vw->projectile() && std::string(vw->def->provider) == "TankCannon";
        bool hit = false; float dmgTaken = 0.0f;
        if (enemy && tank) {
            core::Vec3 fwd = core::forwardFromYawPitch(world_.player().controller().viewYaw(), 0.0f);
            enemy->setPosition(pc.position() + fwd * 30.0f);
            float hp0 = enemy->pawn().health().current;
            platform::InputFrame fire; fire.down[(int)platform::Button::Fire] = true;
            world_.handleInput(fire, dt); world_.tick(dt);
            bool inFlight = !world_.projectiles().empty();
            run(0.5f);
            dmgTaken = hp0 - enemy->pawn().health().current;
            hit = inFlight && dmgTaken > 20.0f;
            LOG_INFO("WEAPON tank shell: in flight %d, enemy took %.0f (TankShell 170 x falloff x victim form multiplier)", (int)inFlight, dmgTaken);
        }
        check(tank && hit, "vehicle weapon: Warpath's TankCannon shell flies and explodes on the enemy (HurtRadius falloff)");
        // Self damage: shoot the floor at our own position -> x SelfDamageMultiplier 0.45 x VEHDEF DamageMultiplier.
        float hp0 = pc.health().current;
        world_.player().controller().setCameraYaw(world_.player().controller().viewYaw());
        run(2.1f);   // TankCannon FireInterval 2.0 s
        game::Weapon* vw2 = pc.vehicleWeapon();
        if (vw2) { vw2->ammo = vw2->magSize; world_.spawnProjectile(pc.actorLocation() + core::Vec3{0, 1.0f, 0}, core::Vec3{0, -50.0f, 0}, *vw2, world_.localMatchPlayer()); }
        run(0.3f);
        float self = hp0 - pc.health().current;
        float expect = 170.0f * pc.vehicleParams().selfDamageMultiplier * pc.vehicleParams().damageMultiplier;
        LOG_INFO("WEAPON self damage %.1f (<= 170 x Self 0.45 x vehicle DamageMultiplier %.2f = %.1f, falloff by distance)", self, pc.vehicleParams().damageMultiplier, expect);
        check(self > 0.0f && self <= expect + 0.01f, "own projectile: self damage scaled by SelfDamageMultiplier and the form DamageMultiplier");
    }
    // Warcry (Optimus Ability0) and Shockwave (Warpath Ability0) in a match.
    {
        game::MatchLaunch L; game::MatchLaunch::fromURL(world_.mapName() + "_BASE_m?GameModeTag=TDM", L);
        world_.launchMatch(L);
        game::CharacterSelection sel; sel.type = 1; sel.chassisId = "Truck";
        world_.match().selectCharacter(world_.localMatchPlayer(), sel);
        std::vector<game::MatchOpponent*> ops;
        for (int i = 0; i < 3; ++i) ops.push_back(world_.addMatchOpponent("W" + std::to_string(i), false));
        run(10.6f);
        game::MatchOpponent* enemy = nullptr;
        for (auto* o : ops) if (!world_.match().sameTeam(o->matchPlayer(), world_.localMatchPlayer())) enemy = o;
        for (auto* o : ops) if (o != enemy) o->setPosition(o->pawn().position() + core::Vec3{0, 0, 200});   // friendlies out of range
        platform::InputFrame sh; sh.pressed[(int)platform::Button::Dash] = true; sh.down[(int)platform::Button::Dash] = true;
        world_.handleInput(sh, dt); world_.tick(dt);
        run(0.05f);   // the effect applies on the following step (ServerTriggerAbility)
        bool buffed = pc.warcryRemain_ > 14.0f && pc.warcryTakenMul_ <= 0.5f;   // 0.5 alone, 0.4 with a friendly in range
        float hp0 = pc.health().current;
        if (enemy) world_.applyMatchDamage(world_.localMatchPlayer(), enemy->matchPlayer(), 100.0f, false);
        float taken = hp0 - pc.health().current;
        const float takenMul = pc.warcryTakenMul_;
        bool cdPending = world_.hudState().abilities[0].active;
        run(15.5f);
        bool cdStarted = !world_.hudState().abilities[0].active && world_.hudState().abilities[0].cooldown > 40.0f;
        LOG_INFO("WEAPON warcry: buffed %d pending %d started %d cd %.1f remain %.1f", (int)buffed, (int)cdPending, (int)cdStarted, world_.hudState().abilities[0].cooldown, pc.warcryRemain_);
        check(buffed && std::fabs(taken - 100.0f * takenMul) < 0.01f && cdPending && cdStarted,
              "Warcry (Optimus Shift): damage taken x0.5 / x0.4 (level by friendlies) for 15 s (took " + std::to_string((int)taken) + " of 100), 60 s cooldown after the buff");
        // Shockwave: Warpath.
        game::CharacterSelection ws; ws.type = 1; ws.chassisId = "Tank3";
        world_.match().selectCharacter(world_.localMatchPlayer(), ws);
        world_.killLocalPlayer(-1, true); run(5.7f);
        bool okBody = pc.chassis().id == "Tank3";
        float ehp0 = 0.0f;
        if (enemy) { enemy->setPosition(pc.position() + core::Vec3{10, 0, 0}); ehp0 = enemy->pawn().health().current; }
        world_.handleInput(sh, dt); world_.tick(dt);
        run(0.1f);
        float early = enemy ? ehp0 - enemy->pawn().health().current : 0.0f;
        run(0.3f);
        float hit = enemy ? ehp0 - enemy->pawn().health().current : 0.0f;
        check(okBody && early == 0.0f && hit > 0.0f, "Shockwave (Warpath Shift): nothing before Delay 0.25 s, then " + std::to_string((int)hit) + " damage to the enemy 10 m away");
    }
    // Cloaking (Air Raid Ability1): TnBuffCloak 20 s, decloak on firing, 15 s cooldown after the cloak ends.
    {
        world_.applyChassisToLocalPawn("Jet4");
        run(0.5f);
        platform::InputFrame c; c.pressed[(int)platform::Button::Ability1] = true; c.down[(int)platform::Button::Ability1] = true;
        world_.handleInput(c, dt); world_.tick(dt);
        bool cloaked = world_.hudState().cloaked && world_.hudState().abilities.size() == 2 && world_.hudState().abilities[1].id == "Cloaking";
        run(1.0f);
        bool stillPending = world_.hudState().abilities[1].active;
        platform::InputFrame fire; fire.down[(int)platform::Button::Fire] = true;
        for (int i = 0; i < 5; ++i) { world_.handleInput(fire, dt); world_.tick(dt); }
        bool exposed = !world_.hudState().cloaked;
        run(0.2f);
        bool cd = world_.hudState().abilities[1].cooldown > 14.0f;
        check(cloaked && stillPending && exposed && cd, "Cloaking (Air Raid Ctrl): cloaked, firing decloaks (ExposeSelf), 15 s cooldown after the cloak");
    }
    // Hover (Soldier class pool): custom Warpath loadout with Abilities [Hover, Dodge].
    {
        world_.applyChassisToLocalPawn("Tank3");
        game::CharacterSelection hs; hs.type = 0; hs.specialty = game::Specialty::Soldier; hs.abilities = {"Hover", "Dodge"};
        world_.applyLoadout(&hs);
        run(1.0f);
        float y0 = pc.position().y;
        platform::InputFrame h; h.pressed[(int)platform::Button::Dash] = true; h.down[(int)platform::Button::Dash] = true;
        world_.handleInput(h, dt); world_.tick(dt);
        run(1.2f);
        float yTop = pc.position().y;
        bool hoveringNow = world_.hudState().hoverState == 2;
        platform::InputFrame fw; fw.down[(int)platform::Button::Forward] = true;
        float maxH = 0.0f;
        for (int i = 0; i < 180; ++i) { world_.handleInput(fw, dt); world_.tick(dt); maxH = std::max(maxH, std::hypot(pc.velocity().x, pc.velocity().z)); }
        float yMid = pc.position().y;
        run(5.0f);
        bool ended = world_.hudState().hoverState == 0;
        run(2.0f);
        float yEnd = pc.position().y;
        bool cd = world_.hudState().abilities[0].cooldown > 30.0f;
        LOG_INFO("WEAPON hover: y0 %.2f top %.2f mid %.2f end %.2f, max horizontal %.1f m/s, ended %d, cooldown %.1f", y0, yTop, yMid, yEnd, maxH, (int)ended, world_.hudState().abilities[0].cooldown);
        check(hoveringNow && yTop - y0 > 4.0f && std::fabs(yMid - yTop) < 0.3f && maxH <= 5.01f && ended && yEnd < yTop - 3.0f && cd,
              "Hover: rises to HoverJumpHeight, holds height 7 s at <= HoverAirSpeed 5 m/s, falls after; 35 s cooldown");
    }
    // Grenade (Optimus custom loadout IonBlaster + FlakGrenades, 1 in the bag; swaps skip the bag): G -> spawn after TossDelay 0.4 s, bounces, fuse 2.0 s from the first
    // impact, then explodes; the empty bag refuses the next toss.
    {
        world_.applyChassisToLocalPawn("Truck");
        game::CharacterSelection gs; gs.type = 0; gs.specialty = game::Specialty::Leader; gs.weapons = {"IonBlaster", "FlakGrenades"};
        world_.applyLoadout(&gs);
        run(1.0f);
        int bag0 = world_.hudState().grenades;
        size_t n0 = world_.projectiles().size();
        platform::InputFrame g; g.pressed[(int)platform::Button::Grenade] = true; g.down[(int)platform::Button::Grenade] = true;
        world_.handleInput(g, dt); world_.tick(dt);
        run(0.3f);
        bool notYet = world_.projectiles().size() == n0;
        run(0.15f);
        bool spawned = world_.projectiles().size() == n0 + 1 && world_.projectiles().back().grenade;
        float firstImpact = -1.0f, gone = -1.0f;
        for (int i = 0; i < 60 * 8 && gone < 0.0f; ++i) {
            platform::InputFrame in; world_.handleInput(in, dt); world_.tick(dt);
            if (world_.projectiles().size() == n0) { gone = (i + 1) * dt; break; }
            if (firstImpact < 0.0f && world_.projectiles().back().life < 1e8f) firstImpact = (i + 1) * dt;
        }
        int bag1 = world_.hudState().grenades;
        world_.handleInput(g, dt); world_.tick(dt);
        run(0.6f);
        bool refused = world_.projectiles().size() == n0;
        LOG_INFO("WEAPON grenade: bag %d -> %d, spawned at 0.4 s %d, first impact %.2f s, exploded %.2f s (fuse %.2f), empty bag refused %d",
                 bag0, bag1, (int)(notYet && spawned), firstImpact, gone, gone - firstImpact, (int)refused);
        check(bag0 == 1 && bag1 == 0 && notYet && spawned && firstImpact > 0.0f && std::fabs((gone - firstImpact) - 2.0f) < 0.05f && refused,
              "Flak grenade: G tosses after 0.4 s, fuse 2.0 s from the first impact, 1 in the bag");
    }
    // Tank cannon (WeaponPrimary TurretConstrained on C_Cannon_XB): pitches with the view, at most 360 deg/s.
    {
        world_.applyChassisToLocalPawn("Tank3");
        world_.applyLoadout(nullptr);
        run(0.5f);
        world_.player().controller().tryBeginTransform();
        run(3.0f);
        game::PlayerController& ctl = world_.player().controller();
        auto cannonPitch = [&]() {
            core::Mat4 b, h;
            if (!pc.boneWorld("C_Cannon_XB", b) || !pc.boneWorld("C_Body_XB", h)) return -99.0f;
            core::Vec3 cf = core::normalize(core::Vec3{b.m[0], b.m[1], b.m[2]}), hf = core::normalize(core::Vec3{h.m[0], h.m[1], h.m[2]});
            return std::asin(core::clampf(cf.y, -1.0f, 1.0f)) - std::asin(core::clampf(hf.y, -1.0f, 1.0f));
        };
        ctl.setCameraPitch(0.0f);
        run(1.0f);
        float p0 = cannonPitch();
        float target = 0.3f, maxRate = 0.0f, prev = p0;
        for (int i = 0; i < 60; ++i) {
            ctl.setCameraPitch(target);
            platform::InputFrame in; world_.handleInput(in, dt); world_.tick(dt);
            float p = cannonPitch(); maxRate = std::max(maxRate, std::fabs(p - prev) / dt); prev = p;
        }
        float p1 = cannonPitch();
        float camP = ctl.camPitch();
        LOG_INFO("WEAPON cannon: form %d tank %d, pitch rel. hull %.3f -> %.3f rad (view %.3f), max rate %.0f deg/s", (int)pc.form(),
                 (int)(pc.vehicleParams().form == game::VehicleFormType::Tank), p0, p1, camP, maxRate * 57.2958f);
        check(pc.form() == game::Form::Vehicle && std::fabs((p1 - p0) - (camP - 0.0f)) < 0.03f && maxRate * 57.2958f <= 365.0f,
              "Tank cannon pitches with the view pitch (hull-relative), lag <= 360 deg/s");
    }
    // Class preset grenades on the class's own chassis (PCD_MP WeaponTypes): never refused by the per-chassis on-foot list;
    // a different class's grenade still is (M08 soak: every preset grenade was refused).
    {
        struct P { const char* chassis; game::Specialty sp; std::vector<std::string> w; int bag; };
        const P presets[] = {{"Car4", game::Specialty::Scout, {"Shotgun", "HeavyPistol", "FlashBangs"}, 2},
                             {"Tank3", game::Specialty::Soldier, {"AssaultRifle", "HomingRocket", "FlakGrenades"}, 1},
                             {"Truck3", game::Specialty::Leader, {"IonBlaster", "GrenadeLauncher", "KamikazeMines"}, 1},
                             {"Jet", game::Specialty::Scientist, {"BurstRifle", "RepairRay", "HealGrenades"}, 1}};
        bool all = true; std::string log;
        for (const P& p : presets) {
            world_.applyChassisToLocalPawn(p.chassis);
            game::CharacterSelection cs; cs.type = 0; cs.specialty = p.sp; cs.weapons = p.w;
            std::vector<std::string> ref = world_.applyLoadout(&cs);
            run(0.1f);
            const int bag = world_.hudState().grenades;
            log += std::string(p.chassis) + " refused " + std::to_string(ref.size()) + " bag " + std::to_string(bag) + "; ";
            all = all && ref.empty() && bag == p.bag;
        }
        world_.applyChassisToLocalPawn("Car4");
        game::CharacterSelection bad; bad.type = 0; bad.specialty = game::Specialty::Scout; bad.weapons = {"Shotgun", "FlakGrenades"};
        std::vector<std::string> ref = world_.applyLoadout(&bad);
        const bool foreign = ref.size() == 1 && ref[0] == "FlakGrenades";
        LOG_INFO("WEAPON class grenades: %s foreign grenade refused %d", log.c_str(), (int)foreign);
        check(all && foreign, "class preset loadouts on their own chassis equip fully (grenade bag present); a foreign class grenade is refused");
    }
    LOG_INFO("WEAPON SUMMARY: %d/%d checks passed", checks - fails, checks);
}

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

// WFC_SWITCHTEST: the class presets as the Frontend sends them (custom type 0, PCD_MP weapons, both faction chassis) through the
// real match spawn; NextWeapon (wheel / PageUp / PageDown) idle, moving, jumping, firing, reloading, after transforming.
void Application::runSwitchTest() {
    int checks = 0, fails = 0;
    auto check = [&](bool ok, const std::string& what) { ++checks; if (!ok) ++fails; LOG_INFO("SWITCH %s %s", ok ? "PASS" : "FAIL", what.c_str()); };
    const float dt = 1.0f / 60.0f;
    platform::InputFrame idle;
    auto step = [&](const platform::InputFrame& in) { world_.handleInput(in, dt); world_.tick(dt); };
    auto run = [&](float secs, const platform::InputFrame& in) { for (int i = 0; i < (int)(secs * 60.0f + 0.5f); ++i) step(in); };
    struct Cls { const char* name; game::Specialty sp; const char* aut; const char* dec; std::vector<std::string> w; };
    const Cls classes[] = {
        {"Scout", game::Specialty::Scout, "Car2", "Car4", {"Shotgun", "HeavyPistol", "FlashBangs"}},
        {"Scientist", game::Specialty::Scientist, "Jet4", "Jet", {"BurstRifle", "RepairRay", "HealGrenades"}},
        {"Soldier", game::Specialty::Soldier, "Tank3", "Tank2", {"AssaultRifle", "HomingRocket", "FlakGrenades"}},
        {"Leader", game::Specialty::Leader, "Truck3", "Truck4", {"IonBlaster", "GrenadeLauncher", "KamikazeMines"}},
    };
    for (const Cls& c : classes) {
        game::MatchLaunch L; game::MatchLaunch::fromURL(world_.mapName() + "_BASE_m?GameModeTag=TDM", L);
        world_.launchMatch(L);
        game::CharacterSelection cs; cs.type = 0; cs.specialty = c.sp; cs.chassisByFaction[0] = c.aut; cs.chassisByFaction[1] = c.dec;
        cs.chassisId = c.aut; cs.weapons = c.w;
        world_.match().selectCharacter(world_.localMatchPlayer(), cs);
        run(10.6f, idle);
        game::Character& pc = world_.player().pawn();
        const std::string tag = std::string(c.name) + " (" + pc.chassis().id + ")";
        auto id = [&]() { return std::string(pc.weapon().def ? pc.weapon().def->provider : "?"); };   // provider UniqueId (selection names)
        std::vector<std::string> guns;
        for (const auto& w : pc.inventory()) if (w.fireType != game::WeaponFire::Grenade) guns.push_back(w.def ? w.def->provider : "?");
        const bool twoGuns = guns.size() == 2 && guns[0] == c.w[0] && guns[1] == c.w[1];
        check(twoGuns && id() == c.w[0], tag + ": inventory " + (guns.size() > 0 ? guns[0] : "") + (guns.size() > 1 ? " + " + guns[1] : "") +
              ", primary active");
        // Switch with a given input source while holding 'hold'; returns the weapon id after the swap finished.
        auto doSwitch = [&](int source, const platform::InputFrame& hold, float maxWait) {
            platform::InputFrame in = hold;
            if (source == 0) in.mouseWheel = 1.0f;
            if (source == 1) { in.pressed[(int)platform::Button::NextWeapon] = true; in.down[(int)platform::Button::NextWeapon] = true; }
            if (source == 2) { in.pressed[(int)platform::Button::PrevWeapon] = true; in.down[(int)platform::Button::PrevWeapon] = true; }
            step(in);
            for (int i = 0; i < (int)(maxWait * 60.0f) && pc.switchingWeapon(); ++i) step(hold);
            step(hold);
            return id();
        };
        // idle: wheel, PageUp, PageDown, repeatedly
        std::string a = doSwitch(0, idle, 3.0f), b = doSwitch(1, idle, 3.0f), d = doSwitch(2, idle, 3.0f), e = doSwitch(0, idle, 3.0f);
        check(a == c.w[1] && b == c.w[0] && d == c.w[1] && e == c.w[0],
              tag + ": idle wheel / PgUp / PgDn x4 -> " + a + ", " + b + ", " + d + ", " + e);
        // moving
        platform::InputFrame fwd; fwd.down[(int)platform::Button::Forward] = true;
        std::string m1 = doSwitch(1, fwd, 3.0f), m2 = doSwitch(0, fwd, 3.0f);
        check(m1 == c.w[1] && m2 == c.w[0], tag + ": while moving -> " + m1 + ", " + m2);
        // jumping (switch right after take-off)
        platform::InputFrame jump; jump.pressed[(int)platform::Button::Jump] = true; jump.down[(int)platform::Button::Jump] = true;
        step(jump); run(0.1f, idle);
        const bool airborne = !pc.onGround();
        std::string j1 = doSwitch(1, idle, 3.0f);
        run(1.5f, idle);
        check(airborne && j1 == c.w[1], tag + ": while jumping (airborne " + std::to_string((int)airborne) + ") -> " + j1);
        // firing: hold fire, switch; no shot after the put-down started
        platform::InputFrame fire; fire.down[(int)platform::Button::Fire] = true;
        run(0.5f, fire);
        std::string f1 = doSwitch(0, fire, 3.0f);
        check(f1 == c.w[0], tag + ": while firing -> " + f1);
        run(0.3f, idle);
        // reloading: empty a few rounds, reload, switch mid-reload -> switches, reload abandoned
        game::Weapon& w0 = pc.weapon();
        if (w0.magSize > 1 && w0.ammo == w0.magSize) w0.ammo = w0.magSize - 1;
        platform::InputFrame rl; rl.pressed[(int)platform::Button::Reload] = true; rl.down[(int)platform::Button::Reload] = true;
        step(rl); run(0.1f, idle);
        const bool wasReloading = pc.weapon().reloading();
        const int clipBefore = pc.weapon().ammo;
        std::string r1 = doSwitch(1, idle, 3.0f);
        int clipAfterOld = -1;
        for (const auto& w : pc.inventory()) if (w.def && std::string(w.def->provider) == c.w[0]) clipAfterOld = w.ammo;
        check(r1 == c.w[1] && (!wasReloading || clipAfterOld == clipBefore),
              tag + ": while reloading (reloading " + std::to_string((int)wasReloading) + ") -> " + r1 + ", reload abandoned (clip " +
              std::to_string(clipBefore) + " -> " + std::to_string(clipAfterOld) + ")");
        // transform: vehicle and back keeps the active weapon; switching works afterwards
        platform::InputFrame tf; tf.pressed[(int)platform::Button::Transform] = true; tf.down[(int)platform::Button::Transform] = true;
        step(tf); run(3.0f, idle);
        const bool veh = pc.form() == game::Form::Vehicle;
        std::string inVeh = doSwitch(1, idle, 0.5f);   // vehicle form: no robot weapon switch
        step(tf); run(3.0f, idle);
        const bool robot = pc.form() == game::Form::Robot;
        const std::string kept = id();
        std::string t1 = doSwitch(0, idle, 3.0f);
        check(veh && robot && kept == c.w[1] && t1 == c.w[0],
              tag + ": transform to vehicle and back keeps " + kept + "; switch after -> " + t1);
        // weapon mesh follows the active weapon (shown weapon id = active)
        const std::string hudCls = world_.player().controller().hudAimState().weaponClass;
        check(pc.weapon().def && world_.hudState().weaponId == pc.weapon().def->id && !world_.hudState().weaponSwitching &&
              hudCls == std::string("TnWeapon") + pc.weapon().def->id, tag + ": HUD weaponId " + world_.hudState().weaponId + ", NotifyCurrentWeaponChanged " + hudCls + " = active");
    }
    LOG_INFO("SWITCH SUMMARY: %d/%d checks passed", checks - fails, checks);
}

// WFC_SCORETEST: a fresh TDM starts at the original values (PRI / Team Score 0, kills / deaths 0, clock = TimeLimit, InProgress
// after the countdown, full clip); a second match after kills / deaths / MatchOver starts fresh again [CONF RE pass 4].
void Application::runScoreTest() {
    int checks = 0, fails = 0;
    auto check = [&](bool ok, const std::string& what) { ++checks; if (!ok) ++fails; LOG_INFO("SCORE %s %s", ok ? "PASS" : "FAIL", what.c_str()); };
    const float dt = 1.0f / 60.0f;
    auto run = [&](float secs) { for (int i = 0; i < (int)(secs * 60.0f + 0.5f); ++i) { platform::InputFrame in; world_.handleInput(in, dt); world_.tick(dt); } };
    auto snapshot = [&](const char* when) {
        game::HudGameState h = world_.hudState();
        std::string rows;
        for (const auto& r : h.scoreboard) rows += " [" + r.name + " " + std::to_string(r.score) + "/" + std::to_string(r.kills) + "/" + std::to_string(r.deaths) + "]";
        LOG_INFO("SCORE %s: state %d team %d-%d score %d kills %d deaths %d clock %d/%d goal %d clip %d/%d%s", when, h.matchState, h.teamScore[0],
                 h.teamScore[1], h.score, h.kills, h.deaths, h.remainingTime, h.timeLimit, h.goalScore, h.clipAmmo, h.reserveAmmo, rows.c_str());
        return h;
    };
    auto fresh = [&](const game::HudGameState& h, const std::string& tag) {
        bool rowsZero = true;
        for (const auto& r : h.scoreboard) rowsZero = rowsZero && r.score == 0 && r.kills == 0 && r.deaths == 0;
        check(h.teamScore[0] == 0 && h.teamScore[1] == 0 && h.score == 0 && h.kills == 0 && h.deaths == 0 && rowsZero,
              tag + ": team 0-0, personal score / kills / deaths 0, every scoreboard row 0");
    };
    for (int matchNo = 1; matchNo <= 3; ++matchNo) {
        game::MatchLaunch L; game::MatchLaunch::fromURL(world_.mapName() + "_BASE_m?GameModeTag=TDM?TimeLimit=120", L);
        world_.launchMatch(L);
        if (matchNo == 1) for (int i = 0; i < 2; ++i) world_.addMatchOpponent("S" + std::to_string(i), false);
        game::HudGameState h0 = snapshot(("match " + std::to_string(matchNo) + " t=0").c_str());
        fresh(h0, "match " + std::to_string(matchNo) + " at launch");
        run(10.6f);
        game::HudGameState h1 = snapshot(("match " + std::to_string(matchNo) + " in progress").c_str());
        fresh(h1, "match " + std::to_string(matchNo) + " after the countdown");
        const game::Character& pc = world_.player().pawn();
        check(h1.matchState == (int)game::Match::State::InProgress && h1.timeLimit == 120 && h1.remainingTime > 100 && h1.remainingTime <= 120 &&
              h1.clipAmmo == pc.weapon().magSize, "match " + std::to_string(matchNo) + ": InProgress, clock " + std::to_string(h1.remainingTime) +
              " of 120, full clip " + std::to_string(h1.clipAmmo));
        // Activity: a kill, a death, damage; then MatchOver (time) before the next launch.
        for (game::MatchOpponent* o : world_.matchOpponents())
            if (o->spawned() && !world_.match().sameTeam(o->matchPlayer(), world_.localMatchPlayer())) {
                world_.applyMatchDamage(o->matchPlayer(), world_.localMatchPlayer(), 99999.0f, false, "TransGame.TnDamageTypeIonBlaster"); break; }
        world_.killLocalPlayer(-1, true);
        world_.player().pawn().weapon().ammo = 3;
        run(1.0f);
        snapshot(("match " + std::to_string(matchNo) + " after activity").c_str());
        if (matchNo == 2) { for (int i = 0; i < 60 * 130 && world_.match().state() == game::Match::State::InProgress; ++i) run(dt); }
    }
    LOG_INFO("SCORE SUMMARY: %d/%d checks passed", checks - fails, checks);
}

// WFC_HEIGHTTEST: per tick, feet (pawn position), capsule centre, mesh origin, root / hips / head bone world heights and the hips /
// root scale (matrix column lengths) while idle and while jogging forward - does the body move or scale in world space, or only pose?
void Application::runHeightTest() {
    const float dt = 1.0f / 60.0f;
    for (const char* id : {"Car2", "Car4", "Truck"}) {
        world_.applyChassisToLocalPawn(id);
        world_.applyLoadout(nullptr);
        game::Character& pc = world_.player().pawn();
        // Face the longest clear line so the jog phase actually moves (the free-play spawn faces a wall for some bodies).
        if (const game::CollisionWorld* cw = world_.collision()) {
            float bestYaw = 0.0f, bestLen = 0.0f;
            for (int k = 0; k < 16; ++k) {
                float yaw = k * 6.2831853f / 16.0f, t; core::Vec3 d = core::forwardFromYawPitch(yaw, 0.0f), p = pc.actorLocation();
                float len = cw->segmentHit(p, p + d * 30.0f, t) ? t * 30.0f : 30.0f;
                if (len > bestLen) { bestLen = len; bestYaw = yaw; }
            }
            world_.player().controller().setCameraYaw(bestYaw);
        }
        struct Acc { float lo = 1e9f, hi = -1e9f, sum = 0; int n = 0; void add(float v) { lo = std::min(lo, v); hi = std::max(hi, v); sum += v; ++n; }
                     std::string s() const { char b[64]; std::snprintf(b, sizeof b, "%.3f..%.3f (mean %.3f)", lo, hi, n ? sum / n : 0.0f); return b; } };
        for (int phase = 0; phase < 2; ++phase) {
            platform::InputFrame in;
            if (phase == 1) in.down[(int)platform::Button::Forward] = true;
            Acc capsule, origin, root, hips, head, hipScale, rootScale;
            std::string clips;
            for (int i = 0; i < 180; ++i) {
                world_.handleInput(in, dt); world_.tick(dt);
                if (i < 60) continue;   // settle 1 s
                const float feet = pc.position().y;
                capsule.add(pc.actorLocation().y - feet);
                core::Mat4 mm = pc.meshMatrix(game::Form::Robot);
                origin.add(mm.m[13] - feet);
                core::Mat4 b;
                auto colLen = [](const core::Mat4& m, int c) { return std::sqrt(m.m[c * 4] * m.m[c * 4] + m.m[c * 4 + 1] * m.m[c * 4 + 1] + m.m[c * 4 + 2] * m.m[c * 4 + 2]); };
                if (pc.boneWorld("C_Root_Reference_XR", b)) { root.add(b.m[13] - feet); rootScale.add(colLen(b, 0)); }
                if (pc.boneWorld("C_Spine00_Hips_XB", b)) { hips.add(b.m[13] - feet); hipScale.add((colLen(b, 0) + colLen(b, 1) + colLen(b, 2)) / 3.0f); }
                if (pc.boneWorld("C_Spine04_Head_XB", b)) head.add(b.m[13] - feet);
                if (clips.find(pc.animName()) == std::string::npos && clips.size() < 120) clips += std::string(pc.animName()) + " ";
            }
            LOG_INFO("HEIGHT %s %s: feet->capsule %s | mesh origin %s | root %s scale %s | hips %s scale %s | head %s | clips %s", id,
                     phase ? "jog" : "idle", capsule.s().c_str(), origin.s().c_str(), root.s().c_str(), rootScale.s().c_str(), hips.s().c_str(),
                     hipScale.s().c_str(), head.s().c_str(), clips.c_str());
        }
        platform::InputFrame stop; for (int i = 0; i < 60; ++i) { world_.handleInput(stop, dt); world_.tick(dt); }
    }
}

// WFC_VEHPHYS: vehicle handling measured against RE pass 4 (TnHoverCarSimulation / TnCarSimulation, physmats):
//  settle (teeter amplitude), one hover jump (apex vs Dv 1200 / g 1940.4 = 3.71 m; nose-up; settle), held jump (one jump),
//  hover and boost into a wall head-on and at 45 deg (rebound speed vs restitution 0.05; attitude after).
void Application::runVehPhysTest() {
    int checks = 0, fails = 0;
    auto check = [&](bool ok, const std::string& what) { ++checks; if (!ok) ++fails; LOG_INFO("VEHPHYS %s %s", ok ? "PASS" : "FAIL", what.c_str()); };
    const float dt = 1.0f / 60.0f;
    const float d2r = 0.0174533f;
    game::PlayerController& ctl = world_.player().controller();
    const game::CollisionWorld* cw = world_.collision();
    // A flat floor point with a vertical wall 25-40 m ahead (clear, flat run-up) and clear sky above.
    auto findWallRun = [&](core::Vec3& start, float& yaw) -> bool {
        const core::Vec3 b0 = cw->boundsMin(), b1 = cw->boundsMax();
        for (float x = b0.x + 5.0f; x < b1.x; x += 7.0f)
            for (float z = b0.z + 5.0f; z < b1.z; z += 7.0f) {
                float gy; core::Vec3 gn;
                if (!cw->groundHeight(x, z, world_.player().pawn().position().y + 5.0f, 0.5f, gy, gn) || gn.y < 0.99f) continue;
                core::Vec3 p{x, gy + 1.0f, z}; float t; core::Vec3 n;
                if (cw->segmentHit(p, p + core::Vec3{0, 15.0f, 0}, t)) continue;
                for (int k = 0; k < 8; ++k) {
                    float yw = k * 0.785398f; core::Vec3 d = core::forwardFromYawPitch(yw, 0.0f);
                    if (!cw->segmentHit(p, p + d * 40.0f, t, n) || t * 40.0f < 25.0f || std::fabs(n.y) > 0.2f) continue;
                    bool flat = true;
                    for (float s2 = 2.0f; s2 < t * 40.0f - 2.0f && flat; s2 += 2.0f) {
                        core::Vec3 q = p + d * s2; float gy2; core::Vec3 gn2;
                        if (!cw->groundHeight(q.x, q.z, gy + 1.0f, 0.3f, gy2, gn2) || std::fabs(gy2 - gy) > 0.2f) flat = false;
                    }
                    if (!flat) continue;
                    start = core::Vec3{x, gy + 0.1f, z}; yaw = yw; return true;
                }
            }
        return false;
    };
    const core::Vec3 spawnPos = world_.player().pawn().position();
    core::Vec3 start; float yaw0 = 0.0f;
    if (!cw || !findWallRun(start, yaw0)) { LOG_INFO("VEHPHYS no wall run found on this map"); return; }
    LOG_INFO("VEHPHYS run start (%.1f %.1f %.1f) yaw %.2f", start.x, start.y, start.z, yaw0);
    for (const char* id : {"Car2", "Truck", "Tank3"}) {
        if (const char* only = std::getenv("WFC_VEHPHYS_ONLY")) if (std::string(only) != id) continue;
        world_.applyChassisToLocalPawn(id);
        world_.applyLoadout(nullptr);
        game::Character& pc = world_.player().pawn();
        auto& vs = pc.vehicleState();
        auto place = [&](float yaw) { pc.setPosition(start); pc.velocity() = {0, 0, 0}; pc.setYaw(yaw); ctl.setCameraYaw(yaw); vs.pitch = vs.roll = 0.0f; vs.angVel = {0, 0, 0}; };
        auto step = [&](const platform::InputFrame& in) { ctl.setCameraYaw(ctl.camYaw()); world_.handleInput(in, dt); world_.tick(dt); };
        platform::InputFrame idle, tf; tf.pressed[(int)platform::Button::Transform] = true; tf.down[(int)platform::Button::Transform] = true;
        pc.setPosition(spawnPos); pc.velocity() = {0, 0, 0};
        if (pc.form() != game::Form::Vehicle) { step(tf); for (int i = 0; i < 180; ++i) step(idle); }   // transform at the spawn (the run start may be too tight for a robot)
        if (pc.form() != game::Form::Vehicle) { check(false, std::string(id) + ": did not reach vehicle form"); continue; }
        // 1) settle: pitch / roll amplitude over 2 s after 3 s
        place(yaw0); for (int i = 0; i < 180; ++i) step(idle);
        float pMin = 1e9f, pMax = -1e9f, rMin = 1e9f, rMax = -1e9f, yRest = 0.0f;
        for (int i = 0; i < 120; ++i) { step(idle); pMin = std::min(pMin, vs.pitch); pMax = std::max(pMax, vs.pitch); rMin = std::min(rMin, vs.roll); rMax = std::max(rMax, vs.roll); yRest += pc.position().y / 120.0f; }
        LOG_INFO("VEHPHYS %s settle: pitch %.2f..%.2f deg, roll %.2f..%.2f deg, rest y %.3f", id, pMin / d2r, pMax / d2r, rMin / d2r, rMax / d2r, yRest);
        check((pMax - pMin) < 1.0f * d2r && (rMax - rMin) < 1.0f * d2r, std::string(id) + ": settles level (teeter < 1 deg)");
        // 2) one hover jump
        platform::InputFrame jp; jp.pressed[(int)platform::Button::Jump] = true; jp.down[(int)platform::Button::Jump] = true;
        step(jp);
        float apex = -1e9f, maxNose = 0.0f, landT = -1.0f; int i;
        for (i = 0; i < 300; ++i) {
            step(idle);
            apex = std::max(apex, pc.position().y - yRest); maxNose = std::max(maxNose, vs.pitch);
            if (i > 20 && landT < 0.0f && vs.onTheGround) landT = i * dt;
        }
        float pp = 0.0f; for (int k = 0; k < 60; ++k) { step(idle); pp = std::max(pp, std::fabs(vs.pitch)); }
        LOG_INFO("VEHPHYS %s jump: apex %.2f m above rest (RE: Dv 12 m/s, g 19.4 -> 3.71 m + spring), max nose-up %.1f deg, landed %.2f s, |pitch| 5 s later %.2f deg",
                 id, apex, maxNose / d2r, landT, pp / d2r);
        check(apex > 2.5f && apex < 5.0f && pp < 2.0f * d2r, std::string(id) + ": jump apex near 3.7 m, level again after landing");
        // 2b) hover jump pressed again at the first ground contact after landing (springs compressed)
        {
            place(yaw0); for (int k = 0; k < 120; ++k) step(idle);
            step(jp);
            bool landed = false; int k = 0;
            for (; k < 300 && !landed; ++k) { step(idle); if (k > 20 && vs.contacts > 0) landed = true; }
            for (int w = 0; w < 18 && !vs.onTheGround; ++w) step(idle);   // until IsOnTheGround (0.3 s cooldown also counts on the ground)
            for (int w = 0; w < 18; ++w) { step(jp); if (pc.velocity().y > 8.0f) break; }
            float ap = -1e9f; for (int w = 0; w < 240; ++w) { step(idle); ap = std::max(ap, pc.position().y - yRest); }
            LOG_INFO("VEHPHYS %s re-jump on landing: apex %.2f m", id, ap);
            check(ap < 5.0f, std::string(id) + ": a jump pressed right at landing stays near the normal apex (" + std::to_string(ap) + " m)");
        }
        // 2c) boost jump on flat: local (600, 0, 1400) -> apex ~5.05 m
        if (std::string(id) != "Tank3") {
            place(yaw0); for (int k = 0; k < 120; ++k) step(idle);
            platform::InputFrame b = idle; b.down[(int)platform::Button::FineAim] = true;
            for (int k = 0; k < 40; ++k) step(b);
            platform::InputFrame bj = b; bj.pressed[(int)platform::Button::Jump] = true; bj.down[(int)platform::Button::Jump] = true;
            const float y0 = pc.position().y;
            LOG_INFO("VEHPHYS %s boost before jump: driving %d ground %d jumpWait %.2f v (%.1f %.1f %.1f)", id, (int)vs.driving, (int)vs.onTheGround, vs.jumpWait, pc.velocity().x, pc.velocity().y, pc.velocity().z);
            step(bj);
            float ap = -1e9f; for (int w = 0; w < 120; ++w) { step(b); ap = std::max(ap, pc.position().y - y0); if (std::getenv("WFC_VEHDBG") && w < 40 && w % 2 == 0) LOG_INFO("VEHTRACE %s boostjump w%d vy %.2f y %.3f driving %d ground %d", id, w, pc.velocity().y, pc.position().y - y0, (int)vs.driving, (int)vs.onTheGround); }
            LOG_INFO("VEHPHYS %s boost jump: apex %.2f m above the driving height (RE: local Z 14 m/s -> 5.05 m)", id, ap);
            check(ap > 4.0f && ap < 6.0f, std::string(id) + ": boost jump apex near 5.05 m");
        }
        // 2d) tank 180 quick turn (VehicleSpecialMove): 180 deg in ~0.3 s, no repeat while held, 1.2 s cooldown [CONF RE pass 5]
        if (std::string(id) == "Tank3") {
            place(yaw0); for (int k = 0; k < 120; ++k) step(idle);
            auto turned = [&](float from) { return std::fabs(std::remainder(pc.yaw() - from, 6.2831853f)) * 57.2958f; };
            const float y0 = pc.yaw();
            platform::InputFrame sh = idle; sh.down[(int)platform::Button::Dash] = true;
            platform::InputFrame shp = sh; shp.pressed[(int)platform::Button::Dash] = true;
            step(shp);
            float t180 = -1.0f;
            for (int k = 0; k < 180; ++k) { step(sh); if (t180 < 0.0f && turned(y0) > 175.0f) t180 = (k + 1) / 60.0f; }   // held 3 s
            const float afterHold = turned(y0);
            const float y1 = pc.yaw();
            step(shp); for (int k = 0; k < 30; ++k) step(idle);    // cooldown already over (3 s held) -> a second turn back
            const float second = turned(y1);
            const float y2 = pc.yaw();
            step(shp); for (int k = 0; k < 30; ++k) step(idle);    // within 1.2 s of the second: refused
            const float refused = turned(y2);
            LOG_INFO("VEHPHYS %s quick turn: 180 reached in %.2f s, after holding 3 s %.0f deg, second press %.0f deg, press within cooldown %.0f deg",
                     id, t180, afterHold, second, refused);
            check(t180 > 0.2f && t180 < 0.5f && std::fabs(afterHold - 180.0f) < 5.0f && std::fabs(second - 180.0f) < 5.0f && refused < 5.0f,
                  std::string(id) + ": VehicleSpecialMove = one 180 deg quick turn in ~0.3 s, no repeat while held, 1.2 s cooldown");
        }
        // 3) held jump = one jump
        place(yaw0); for (int k = 0; k < 120; ++k) step(idle);
        int jumps = 0; bool up = false; float yPrev = pc.position().y;
        for (int k = 0; k < 240; ++k) {
            platform::InputFrame h = idle; h.down[(int)platform::Button::Jump] = true; if (k == 0) h.pressed[(int)platform::Button::Jump] = true;
            step(h);
            float vy = pc.velocity().y;
            if (!up && vy > 5.0f) { ++jumps; up = true; }
            if (up && vy < 0.0f) up = false;
            yPrev = pc.position().y;
        }
        (void)yPrev;
        check(jumps == 1, std::string(id) + ": holding Jump 4 s = " + std::to_string(jumps) + " jump(s) (fresh press only)");
        // 4) walls: hover and boost, head-on and 45 deg
        for (int boost = 0; boost < 2; ++boost)
            for (int angle = 0; angle < 2; ++angle) {
                const float yaw = yaw0 + (angle ? 0.785398f * 0.5f : 0.0f);
                place(yaw0); for (int k = 0; k < 90; ++k) step(idle);
                pc.setYaw(yaw); ctl.setCameraYaw(yaw);
                platform::InputFrame drive = idle; drive.down[(int)platform::Button::Forward] = true;
                if (boost) drive.down[(int)platform::Button::FineAim] = true;
                const core::Vec3 wallDir = core::forwardFromYawPitch(yaw0, 0.0f);
                float maxInto = 0.0f, minInto = 1e9f, maxTilt = 0.0f, maxAng = 0.0f, maxUp = 0.0f; bool hit = false; int after = 0;
                for (int k = 0; k < 600 && after < 90; ++k) {
                    step(drive);
                    const float into = core::dot(pc.velocity(), wallDir);
                    if (!hit) { maxInto = std::max(maxInto, into); if (maxInto > 5.0f && into < 0.3f * maxInto) hit = true; }
                    else {
                        ++after; minInto = std::min(minInto, into);
                        static const char* wt = std::getenv("WFC_VEHWALLTRACE");
                        if (wt && after >= 22 && after <= 27 && boost == 0 && angle == 1) {
                            const game::VehicleParams& VPx = pc.vehicleParams();
                            const core::Vec3 Fx = core::forwardFromYawPitch(pc.yaw(), 0.0f), Rx = core::normalize(core::cross(Fx, core::Vec3{0, 1, 0}));
                            const core::Vec3 comx = pc.position() + Fx * VPx.comFwd + core::Vec3{0, VPx.comUp, 0};
                            for (int q = 0; q < 4; ++q) {
                                const float ang = 0.7853982f + 1.5707963f * (float)q;
                                // body x = forward, body y = left in UE (right-handed with z up): y>0 -> left
                                const core::Vec3 m = comx + Fx * (std::cos(ang) * VPx.suspMountRadius) - Rx * (std::sin(ang) * VPx.suspMountRadius);
                                float tw = 1, tf = 1; core::Vec3 nw, nf;
                                const bool wall = cw->segmentHit(comx, m, tw, nw);
                                const bool floor = cw->segmentHit(m, m + core::Vec3{0, -6.0f, 0}, tf, nf);
                                LOG_INFO("WALLPROBE %s +%d probe %d: COM->mount crosses geometry %d (t %.2f n %.2f %.2f %.2f); floor below mount %s %.2f m (n.y %.2f)", id, after, q, (int)wall, tw, nw.x, nw.y, nw.z, floor ? "at" : "none within", floor ? tf * 6.0f : 6.0f, nf.y);
                            }
                        }
                        if (wt && after <= 45 && boost == 0 && angle == 1)
                            LOG_INFO("WALLTRACE %s +%d roll %+.2f pitch %+.2f deg | w (%+.1f %+.1f %+.1f) deg/s | contacts %d n (%.2f %.2f %.2f) | L %.2f %.2f %.2f %.2f | v (%+.1f %+.1f %+.1f)",
                                     id, after, vs.roll * 57.3f, vs.pitch * 57.3f, vs.angVel.x * 57.3f, vs.angVel.y * 57.3f, vs.angVel.z * 57.3f, vs.contacts,
                                     vs.contactN.x, vs.contactN.y, vs.contactN.z, vs.spLen[0], vs.spLen[1], vs.spLen[2], vs.spLen[3], pc.velocity().x, pc.velocity().y, pc.velocity().z);
                        if (std::getenv("WFC_VEHDBG") && after <= 60) LOG_INFO("VEHTRACE %s b%d a%d t%d roll %.1f pitch %.1f wx %.1f wy %.1f contacts %d ground %d v (%.1f %.1f %.1f) y %.3f", id, boost, angle, after, vs.roll / d2r, vs.pitch / d2r, vs.angVel.x / d2r, vs.angVel.y / d2r, vs.contacts, (int)vs.onTheGround, pc.velocity().x, pc.velocity().y, pc.velocity().z, pc.position().y);
                        maxTilt = std::max(maxTilt, std::max(std::fabs(vs.pitch), std::fabs(vs.roll)));
                        maxAng = std::max(maxAng, std::max(std::fabs(vs.angVel.x), std::fabs(vs.angVel.y)));
                        maxUp = std::max(maxUp, pc.velocity().y);
                    }
                }
                const float rebound = hit ? std::max(0.0f, -minInto) : -1.0f;
                LOG_INFO("VEHPHYS %s %s %s wall: impact %.1f m/s, rebound %.2f m/s, after-hit max tilt %.1f deg, max pitch/roll rate %.1f deg/s, max up %.1f m/s, still driving %d",
                         id, boost ? "boost" : "hover", angle ? "22deg" : "head-on", maxInto, rebound, maxTilt / d2r, maxAng / d2r, maxUp, (int)vs.driving);
                // Tank: TnHoverTankSimulation corrects pitch / roll only once unstable (> HoverStability 30 deg) [CONF RE C2], so a probe
                // losing the floor at a wall base tilts it up to that limit; car / truck correct every step (mask 0.05) [CONF RE pass 4].
                const float tiltLimit = (std::string(id) == "Tank3" ? 32.0f : 15.0f) * d2r;
                check(hit && rebound < 0.15f * maxInto + 0.5f && maxTilt < tiltLimit && maxUp < 4.0f,
                      std::string(id) + " " + (boost ? "boost" : "hover") + (angle ? " 22deg" : " head-on") + " wall: no pinball rebound, stays upright");
            }
    }
    LOG_INFO("VEHPHYS SUMMARY: %d/%d checks passed", checks - fails, checks);
}

// WFC_HEADJIT=<Hz>: per RENDER frame, the drawn body heading (mesh matrix forward) relative to the camera yaw, during fast
// mouse flicks (6 rad/s, sign flips every 0.5 s) and a stick steer in boost, for robot / hover / boost / jet. A per-frame second
// difference (deg) shows whether the body snaps relative to the view between 60 Hz simulation steps.
void Application::runHeadingJitterTest() {
    float hz = (float)std::atof(std::getenv("WFC_HEADJIT"));
    if (hz < 30.0f) hz = 144.0f;
    const float rdt = 1.0f / hz;
    auto& ctl = world_.player().controller();
    struct Sc { const char* name; const char* chassis; int mode; };   // mode 0 robot, 1 hover, 2 boost (stick steer), 3 jet hover
    const Sc scs[] = {{"robot flick", "Car2", 0}, {"car hover flick", "Car2", 1}, {"car boost steer", "Car2", 2},
                      {"truck hover flick", "Truck", 1}, {"jet hover flick", "Jet", 3}};
    for (const Sc& sc : scs) {
        world_.applyChassisToLocalPawn(sc.chassis);
        world_.applyLoadout(nullptr);
        auto& pc = world_.player().pawn();
        world_.teleportToStart(20);
        clock_ = core::FixedStepClock(60.0);
        auto frame = [&](const platform::InputFrame& in) {
            world_.handleInput(in, rdt);
            int steps = clock_.tick(rdt);
            for (int i = 0; i < steps; ++i) world_.tick(clock_.stepSeconds());
        };
        platform::InputFrame idle;
        if (sc.mode == 0 && pc.form() == game::Form::Vehicle) { platform::InputFrame tf; tf.pressed[(int)platform::Button::Transform] = true; tf.down[(int)platform::Button::Transform] = true; frame(tf); for (int i = 0; i < (int)(hz * 3); ++i) frame(idle); }
        if (sc.mode != 0 && pc.form() != game::Form::Vehicle) { platform::InputFrame tf; tf.pressed[(int)platform::Button::Transform] = true; tf.down[(int)platform::Button::Transform] = true; frame(tf); for (int i = 0; i < (int)(hz * 3); ++i) frame(idle); }
        float yaw = ctl.camYaw();
        std::vector<float> rel;
        for (int f = 0; f < (int)(hz * 3.0f); ++f) {
            platform::InputFrame in;
            in.down[(int)platform::Button::Forward] = true;
            if (sc.mode == 2) { in.down[(int)platform::Button::FineAim] = true; in.padConnected = true; in.padRX = ((f / (int)(hz * 0.5f)) % 2) ? 1.0f : -1.0f; }
            else { yaw += (((f / (int)(hz * 0.5f)) % 2) ? 6.0f : -6.0f) * rdt; ctl.setCameraYaw(yaw); }
            frame(in);
            render::Camera cam; ctl.updateCamera(cam);
            core::Mat4 mm = pc.meshMatrix(pc.form());
            const float bodyYaw = std::atan2(-mm.m[0], -mm.m[2]);   // mesh +X = forward
            (void)bodyYaw;
            const float camFwdYaw = cam.yaw;
            core::Vec3 bf{mm.m[0], 0.0f, mm.m[2]};
            core::Vec3 cf = core::forwardFromYawPitch(camFwdYaw, 0.0f);
            float r = std::atan2(cf.x * bf.z - cf.z * bf.x, cf.x * bf.x + cf.z * bf.z) * 57.2958f;
            rel.push_back(r);
        }
        double s = 0; float mx = 0; int n = 0;
        for (size_t i = (size_t)(hz * 0.5f); i < rel.size(); ++i) { float d2 = std::fabs(rel[i] - 2 * rel[i - 1] + rel[i - 2]); if (d2 > 90.0f) continue; s += d2; mx = std::max(mx, d2); ++n;
            if (d2 > 10.0f) LOG_INFO("HEADJIT spike %s frame %zu rel %.2f %.2f %.2f", sc.name, i, rel[i - 2], rel[i - 1], rel[i]); }
        LOG_INFO("HEADJIT %-18s @%3.0f Hz render: body-vs-camera heading jitter %.3f deg/frame (max %.2f) form %d", sc.name, hz, n ? (float)(s / n) : 0.0f, mx, (int)pc.form());
    }
}

// WFC_VSOCKET: each vehicle's WeaponSocket_Primary (bone x socket) in the vehicle's own frame (m: +fwd, +up, +right of the actor)
// against the chassis physics hull (front / back / half width / bottom / top).
void Application::runVehicleSocketProbe() {
    const float dt = 1.0f / 60.0f;
    for (const char* id : {"Car2", "Car4", "Truck", "Truck4", "Tank3", "Tank2", "Jet", "Jet4"}) {
        world_.applyChassisToLocalPawn(id);
        world_.applyLoadout(nullptr);
        auto& pc = world_.player().pawn();
        world_.teleportToStart(20);
        if (pc.form() != game::Form::Vehicle) {
            platform::InputFrame tf; tf.pressed[(int)platform::Button::Transform] = true; tf.down[(int)platform::Button::Transform] = true;
            world_.handleInput(tf, dt); world_.tick(dt);
            for (int i = 0; i < 180; ++i) { platform::InputFrame in; world_.handleInput(in, dt); world_.tick(dt); }
        }
        for (int i = 0; i < 30; ++i) { platform::InputFrame in; world_.handleInput(in, dt); world_.tick(dt); }
        const game::SocketDef& sd = pc.chassis().vehicleWeapon;
        core::Mat4 bm;
        if (!sd.valid || pc.form() != game::Form::Vehicle || !pc.boneWorld(sd.bone, bm)) { LOG_INFO("VSOCKET %s: socket %s unavailable (form %d)", id, sd.bone.c_str(), (int)pc.form()); continue; }
        core::Mat4 w = bm * sd.local;
        core::Vec3 d = core::Vec3{w.m[12], w.m[13], w.m[14]} - pc.actorLocation();
        core::Vec3 f = core::forwardFromYawPitch(pc.yaw(), 0.0f), r = core::normalize(core::cross(f, core::Vec3{0, 1, 0}));
        const auto& VP = pc.vehicleParams();
        float fwd = core::dot(d, f), right = core::dot(d, r), up = d.y;
        bool inside = fwd <= VP.hullFront + 0.5f && fwd >= -VP.hullBack - 0.5f && std::fabs(right) <= VP.hullHalfWidth + 0.5f && up >= VP.hullBottom - 0.5f && up <= VP.hullTop + 1.0f;
        LOG_INFO("VSOCKET %s: %s on %s at fwd %.2f right %.2f up %.2f m from the actor; hull fwd %.2f / back %.2f / half width %.2f / %.2f..%.2f -> %s",
                 id, "WeaponSocket_Primary", sd.bone.c_str(), fwd, right, up, VP.hullFront, VP.hullBack, VP.hullHalfWidth, VP.hullBottom, VP.hullTop, inside ? "inside" : "OUTSIDE");
    }
}

// WFC_XFORMVIS: robot->vehicle and vehicle->robot on several chassis; per step which mesh is drawn, against this chassis' authored
// ToggleHidden times; no step may draw neither mesh.
void Application::runTransformVisibilityTest() {
    int checks = 0, fails = 0;
    auto check = [&](bool ok, const std::string& what) { ++checks; if (!ok) ++fails; LOG_INFO("XFORMVIS %s %s", ok ? "PASS" : "FAIL", what.c_str()); };
    const float dt = 1.0f / 60.0f;
    for (const char* id : {"Car2", "Car4", "Truck", "Truck4", "Jet", "Jet4", "Tank3", "Tank2"}) {
        world_.applyChassisToLocalPawn(id);
        world_.applyLoadout(nullptr);
        auto& pc = world_.player().pawn();
        world_.teleportToStart(20);
        platform::InputFrame idle, tf; tf.pressed[(int)platform::Button::Transform] = true; tf.down[(int)platform::Button::Transform] = true;
        if (pc.form() != game::Form::Robot) { world_.handleInput(tf, dt); world_.tick(dt); for (int i = 0; i < 200; ++i) { world_.handleInput(idle, dt); world_.tick(dt); } }
        for (int i = 0; i < 30; ++i) { world_.handleInput(idle, dt); world_.tick(dt); }
        const game::ChassisDef& cd = pc.chassis();
        for (int dir = 0; dir < 2; ++dir) {
            world_.handleInput(tf, dt); world_.tick(dt);
            float firstTarget = -1.0f, lastSource = -1.0f; int gaps = 0, steps = 0;
            const game::Form src = dir == 0 ? game::Form::Robot : game::Form::Vehicle, dst = dir == 0 ? game::Form::Vehicle : game::Form::Robot;
            while (pc.isTransforming() && steps < 400) {
                const float t = pc.transformClipTime();
                const bool s = pc.meshShown(src), d = pc.meshShown(dst);
                if (!s && !d) ++gaps;
                if (d && firstTarget < 0.0f) firstTarget = t;
                if (s) lastSource = t;
                world_.handleInput(idle, dt); world_.tick(dt); ++steps;
            }
            for (int i = 0; i < 30; ++i) { world_.handleInput(idle, dt); world_.tick(dt); }
            const float wantShow = dir == 0 ? cd.toVehVehicleShow : cd.toRobotRobotShow, wantHide = dir == 0 ? cd.toVehRobotHide : cd.toRobotVehicleHide;
            LOG_INFO("XFORMVIS %s %s: target shown from %.3f s (authored %.3f), source last shown %.3f s (authored hide %.3f), steps with no mesh %d, ended form %d",
                     id, dir == 0 ? "to vehicle" : "to robot", firstTarget, wantShow, lastSource, wantHide, gaps, (int)pc.form());
            check(gaps == 0 && std::fabs(firstTarget - wantShow) <= dt * 1.5f + 1e-3f && std::fabs(lastSource - wantHide) <= dt * 1.5f + 1e-3f && pc.form() == dst,
                  std::string(id) + (dir == 0 ? " robot->vehicle" : " vehicle->robot") + ": mesh handoff at the chassis' own ToggleHidden times, never invisible");
        }
    }
    LOG_INFO("XFORMVIS SUMMARY: %d/%d checks passed", checks - fails, checks);
}

// WFC_FINEAIMTEST: RightMouse toggle; per weapon the OverTheShoulder fine-aim row (FOV / orbit distance / look speed) and the x0.5
// ground speed [CONF RE pass 5 3]; toggle off restores FOV 80.
void Application::runFineAimTest() {
    int checks = 0, fails = 0;
    auto check = [&](bool ok, const std::string& what) { ++checks; if (!ok) ++fails; LOG_INFO("FINEAIM %s %s", ok ? "PASS" : "FAIL", what.c_str()); };
    const float dt = 1.0f / 60.0f;
    auto& ctl = world_.player().controller();
    auto run = [&](float secs, const platform::InputFrame& in) { for (int i = 0; i < (int)(secs * 60.0f + 0.5f); ++i) { world_.handleInput(in, dt); world_.tick(dt); } };
    struct Case { const char* chassis; std::vector<std::string> weapons; const char* expectWeapon; float fov, dist, look; };
    const Case cases[] = {{"Jet", {}, "SniperRifle", 20.0f, 1.0f, 0.13f}, {"Car2", {"HeavyPistol", "Shotgun"}, "HeavyPistol", 30.0f, 1.0f, 0.1875f},
                          {"Truck", {}, "IonBlaster", 45.0f, 8.0f, 0.5f}};
    for (const Case& c : cases) {
        world_.applyChassisToLocalPawn(c.chassis);
        game::CharacterSelection cs; cs.type = 0; cs.specialty = game::Specialty::Scout; cs.weapons = c.weapons;
        world_.applyLoadout(c.weapons.empty() ? nullptr : &cs);
        auto& pc = world_.player().pawn();
        world_.teleportToStart(20);
        platform::InputFrame idle; run(1.0f, idle);
        const std::string wid = pc.weapon().def ? pc.weapon().def->id : "?";
        auto yawAfterMouse = [&]() { float y0 = ctl.camYaw(); platform::InputFrame m; m.mouseDX = 100.0f; world_.handleInput(m, dt); world_.tick(dt); return std::fabs(ctl.camYaw() - y0); };
        run(0.6f, idle);
        const float yawNormal = yawAfterMouse();
        const float speedNormal = pc.speedMultiplier();
        platform::InputFrame rm; rm.pressed[(int)platform::Button::FineAim] = true; rm.down[(int)platform::Button::FineAim] = true;
        world_.handleInput(rm, dt); world_.tick(dt);
        run(1.0f, idle);   // toggle: released, stays aimed
        render::Camera cam; ctl.updateCamera(cam);
        core::Vec3 anchor = pc.actorLocation();
        const float camDist = core::length(cam.pos - anchor);
        const float yawAimed = yawAfterMouse();
        const float speedAimed = pc.speedMultiplier();
        const bool aimed = ctl.fineAimState().active;
        world_.handleInput(rm, dt); world_.tick(dt);
        run(1.5f, idle);
        render::Camera cam2; ctl.updateCamera(cam2);
        LOG_INFO("FINEAIM %s %s: aimed %d FOV %.1f (want %.0f), camera %.2f m from the actor, look ratio %.3f (want %.3f), speed x%.2f; off -> FOV %.1f",
                 c.chassis, wid.c_str(), (int)aimed, cam.fovXDeg, c.fov, camDist, yawNormal > 0.0f ? yawAimed / yawNormal : -1.0f, c.look, speedNormal > 0.0f ? speedAimed / speedNormal : -1.0f, cam2.fovXDeg);
        check(wid == c.expectWeapon && aimed && std::fabs(cam.fovXDeg - c.fov) < 1.0f && std::fabs(yawAimed / yawNormal - c.look) < 0.02f &&
              std::fabs(speedAimed / speedNormal - 0.5f) < 0.01f && std::fabs(cam2.fovXDeg - 80.0f) < 1.0f,
              std::string(c.chassis) + " " + wid + ": fine aim FOV " + std::to_string((int)c.fov) + ", look x" + std::to_string(c.look) + ", speed x0.5, toggle off -> 80");
    }
    LOG_INFO("FINEAIM SUMMARY: %d/%d checks passed", checks - fails, checks);
}

// WFC_QATEST (with WFC_QA=1): DEV / QA TOOLING self-test - weapon list, loadout through the real restriction path, god mode,
// noclip, respawn wave, status line. Not a fidelity test.
void Application::runQaToolTest() {
    int checks = 0, fails = 0;
    auto check = [&](bool ok, const std::string& what) { ++checks; if (!ok) ++fails; LOG_INFO("QATEST %s %s", ok ? "PASS" : "FAIL", what.c_str()); };
    const float dt = 1.0f / 60.0f;
    auto run = [&](float secs) { for (int i = 0; i < (int)(secs * 60.0f + 0.5f); ++i) { platform::InputFrame in; world_.handleInput(in, dt); world_.tick(dt); } };
    check(game::World::qaEnabled(), "WFC_QA set");
    game::MatchLaunch L; game::MatchLaunch::fromURL(world_.mapName() + "_BASE_m?GameModeTag=TDM", L);
    world_.launchMatch(L);
    game::CharacterSelection me; me.type = 0; me.specialty = game::Specialty::Scout; me.weapons = {"Shotgun", "HeavyPistol"};
    world_.match().selectCharacter(world_.localMatchPlayer(), me);
    run(10.6f);
    auto robotIds = world_.qaWeaponIds(false), vehIds = world_.qaWeaponIds(true);
    check(robotIds.size() > 20 && !vehIds.empty(), "weapon lists: " + std::to_string(robotIds.size()) + " robot, " + std::to_string(vehIds.size()) + " vehicle");
    auto refused = world_.qaSetLoadout({"SniperRifle", "HeavyPistol"});
    game::Character& pc = world_.player().pawn();
    LOG_INFO("QATEST loadout on %s: active %s, refused %zu%s", pc.chassis().id.c_str(), pc.weapon().def ? pc.weapon().def->provider : "-", refused.size(), refused.empty() ? "" : (" (" + refused[0] + ")").c_str());
    check(pc.weapon().def != nullptr, "qaSetLoadout applies through applyLoadout (restrictions reported)");
    world_.qaSetGodMode(true);
    const float h0 = pc.health().current;
    world_.applyMatchDamage(world_.localMatchPlayer(), -1, 999.0f, true, "TransGame.TnDamageTypeIonBlaster");
    check(pc.health().current == h0, "god mode: damage ignored");
    world_.qaSetGodMode(false);
    world_.qaSetNoclip(true);
    const core::Vec3 p0 = pc.position();
    for (int i = 0; i < 60; ++i) { platform::InputFrame in; in.down[(int)platform::Button::Forward] = true; in.down[(int)platform::Button::Jump] = true; world_.handleInput(in, dt); world_.tick(dt); }
    const float moved = core::length(pc.position() - p0);
    check(moved > 15.0f && pc.position().y > p0.y + 5.0f, "noclip: flew " + std::to_string((int)moved) + " m in 1 s (up + forward)");
    world_.qaSetNoclip(false);
    world_.qaTeleportToStart(3);
    world_.qaRespawn();
    const bool dead = world_.localPlayerDead();
    run(8.0f);
    check(dead && !world_.localPlayerDead(), "qaRespawn: real death (suicide) then the normal respawn wave");
    LOG_INFO("QATEST status: %s", world_.qaStatus().c_str());
    check(!world_.qaStatus().empty(), "status line");
    LOG_INFO("QATEST SUMMARY: %d/%d checks passed", checks - fails, checks);
}

// WFC_PROJFXTEST: every simulated projectile weapon resolves its authored FlightEffect / ExplosionEffect (weapon.json
// projectiles[0].projectile_visual) and each projectile reaches its end (impact or expiry) inside the run.
void Application::runProjectileFxTest() {
    int checks = 0, fails = 0;
    auto check = [&](bool ok, const std::string& what) { ++checks; if (!ok) ++fails; LOG_INFO("PROJFX %s %s", ok ? "PASS" : "FAIL", what.c_str()); };
    const float dt = 1.0f / 60.0f;
    auto run = [&](float secs) { for (int i = 0; i < (int)(secs * 60.0f + 0.5f); ++i) { platform::InputFrame in; world_.handleInput(in, dt); world_.tick(dt); } };
    LOG_INFO("PROJFX renderer particle API: %s", game::World::projectileFxApi() ? "present" : "absent (box fallback; FX calls compiled out)");
    game::Character& pc = world_.player().pawn();
    int bound = 0, total = 0;
    for (int i = 0; i < game::weaponDefCount(); ++i) {
        const game::WeaponDef& d = game::weaponDefAt(i);
        game::Weapon w = game::Weapon::fromDef(d);
        if (!(w.fireType == game::WeaponFire::Projectile && w.projSpeed > 0.0f)) continue;
        ++total;
        const size_t before = world_.liveProjectiles();
        const core::Vec3 o = pc.actorLocation() + core::Vec3{0, 2.0f, 0};
        const core::Vec3 dir = core::forwardFromYawPitch(pc.yaw(), -0.3f);
        world_.spawnProjectile(o, dir * w.projSpeed, w, world_.localMatchPlayer());
        const std::string tpl = world_.liveProjectiles() > before ? world_.projectileFlightTemplate(world_.liveProjectiles() - 1) : "";
        if (!tpl.empty()) ++bound;
        run(0.05f);
        LOG_INFO("PROJFX %-22s flight %s", d.id, tpl.empty() ? "(none)" : tpl.c_str());
    }
    run(12.0f);
    check(total > 0 && bound == total, "fired projectile weapons bind a FlightEffect: " + std::to_string(bound) + "/" + std::to_string(total));
    check(world_.liveProjectiles() == 0, "all projectiles ended (impact / expiry) within 12 s: " + std::to_string(world_.liveProjectiles()) + " left");
    if (game::World::projectileFxApi())
        check(world_.projectileFxSpawned() == total && world_.projectileFxExplosions() > 0,
              "renderer FX: " + std::to_string(world_.projectileFxSpawned()) + " flight, " + std::to_string(world_.projectileFxExplosions()) + " explosions");
    LOG_INFO("PROJFX SUMMARY: %d/%d checks passed", checks - fails, checks);
}

// WFC_MUZZLETEST: vehicle weapon shots alternate WeaponSocket_Primary / _Primary2 (left / right gun) per shot on chassis that
// author both, for the weapons whose MuzzleFlashSockets list both; the tank cannon (Primary only) never alternates.
void Application::runMuzzleTest() {
    int checks = 0, fails = 0;
    auto check = [&](bool ok, const std::string& what) { ++checks; if (!ok) ++fails; LOG_INFO("MUZZLE %s %s", ok ? "PASS" : "FAIL", what.c_str()); };
    const float dt = 1.0f / 60.0f;
    game::PlayerController& ctl = world_.player().controller();
    platform::InputFrame idle, tf; tf.pressed[(int)platform::Button::Transform] = true; tf.down[(int)platform::Button::Transform] = true;
    auto step = [&](const platform::InputFrame& in) { world_.handleInput(in, dt); world_.tick(dt); };
    for (const char* id : {"Car2", "Car4", "Jet4", "Truck3", "Tank3"}) {
        world_.applyChassisToLocalPawn(id);
        world_.applyLoadout(nullptr);
        game::Character& pc = world_.player().pawn();
        if (pc.form() != game::Form::Vehicle) { step(tf); }
        for (int i = 0; i < 240; ++i) step(idle);
        game::Weapon* vw = pc.vehicleWeapon();
        if (pc.form() != game::Form::Vehicle || !vw) { check(false, std::string(id) + ": vehicle form with a vehicle weapon"); continue; }
        ctl.setCameraYaw(pc.yaw());
        std::vector<int> socks; std::vector<float> side;
        int serial = world_.hudState().vehicleShotSerial;
        platform::InputFrame fire; fire.down[(int)platform::Button::Fire] = true;
        for (int i = 0; i < 600 && socks.size() < 6; ++i) {
            vw->ammo = std::max(vw->ammo, 1);
            step(fire);
            const auto& h = world_.hudState();
            if (h.vehicleShotSerial != serial) {
                serial = h.vehicleShotSerial;
                socks.push_back(h.vehicleShotSocket);
                // Lateral offset of the muzzle on the vehicle's right axis (+ = right).
                const core::Vec3 f = core::forwardFromYawPitch(pc.yaw(), 0.0f);
                const core::Vec3 r = core::normalize(core::cross(f, core::Vec3{0, 1, 0}));
                side.push_back(core::dot(h.vehicleShotMuzzle - pc.actorLocation(), r));
            }
        }
        std::string seq, pos;
        for (size_t k = 0; k < socks.size(); ++k) { seq += std::to_string(socks[k]); char b[16]; std::snprintf(b, sizeof b, " %+.2f", side[k]); pos += b; }
        const bool two = vw->alternatesMuzzle() && pc.chassis().vehicleWeapon2.valid;
        bool alt = socks.size() >= 4, sidesFlip = socks.size() >= 4;
        for (size_t k = 1; k < socks.size(); ++k) {
            if (two) { alt = alt && socks[k] != socks[k - 1]; sidesFlip = sidesFlip && (side[k] > 0.0f) != (side[k - 1] > 0.0f); }
            else alt = alt && socks[k] == 0;
        }
        LOG_INFO("MUZZLE %s %s: alternates %d, sockets %s, lateral m%s", id, vw->def ? vw->def->id : "?", (int)two, seq.c_str(), pos.c_str());
        check(alt && (!two || sidesFlip), std::string(id) + " " + (vw->def ? vw->def->id : "?") + (two ? ": shots alternate left / right" : ": Primary only"));
        step(tf); for (int i = 0; i < 180; ++i) step(idle);
    }
    LOG_INFO("MUZZLE SUMMARY: %d/%d checks passed", checks - fails, checks);
}

// WFC_RMUZZLETEST: robot projectile weapons spawn their projectile at the held weapon mesh's MuzzleFlash socket
// (Weapon.ProjectileFire RealStartLoc = GetMuzzleLoc) through the real match spawn + loadout.
void Application::runRobotMuzzleTest() {
    int checks = 0, fails = 0;
    auto check = [&](bool ok, const std::string& what) { ++checks; if (!ok) ++fails; LOG_INFO("RMUZZLE %s %s", ok ? "PASS" : "FAIL", what.c_str()); };
    const float dt = 1.0f / 60.0f;
    platform::InputFrame idle;
    auto step = [&](const platform::InputFrame& in) { world_.handleInput(in, dt); world_.tick(dt); };
    auto run = [&](float secs) { for (int i = 0; i < (int)(secs * 60.0f + 0.5f); ++i) step(idle); };
    struct Case { game::Specialty sp; const char* provider; const char* id; };
    const Case cases[] = {{game::Specialty::Soldier, "HomingRocket", "RocketLauncher"}, {game::Specialty::Leader, "GrenadeLauncher", "GrenadeLauncher"},
                          {game::Specialty::Leader, "Bazooka", "Bazooka"}, {game::Specialty::Scout, "PlasmaCannon", "PlasmaCannon"}};
    for (const Case& c : cases) {
        game::MatchLaunch L; game::MatchLaunch::fromURL(world_.mapName() + "_BASE_m?GameModeTag=TDM", L);
        world_.launchMatch(L);
        game::CharacterSelection cs; cs.type = 0; cs.specialty = c.sp; cs.weapons = {c.provider, "HeavyPistol"};
        world_.match().selectCharacter(world_.localMatchPlayer(), cs);
        run(11.0f);
        game::Character& pc = world_.player().pawn();
        const std::string held = pc.weapon().def ? pc.weapon().def->id : "?";
        if (held != c.id) { LOG_INFO("RMUZZLE %s not held on %s (holding %s): skipped", c.id, pc.chassis().id.c_str(), held.c_str()); continue; }
        core::Vec3 muzzle;
        const bool have = world_.heldWeaponMuzzle(muzzle);
        const size_t n0 = world_.liveProjectiles();
        platform::InputFrame fire; fire.pressed[(int)platform::Button::Fire] = true; fire.down[(int)platform::Button::Fire] = true;
        core::Vec3 spawned{0, 0, 0}; bool fired = false; core::Vec3 muzzleAtShot = muzzle;
        // Charge weapons (Plasma Cannon) fire on release: hold past ChargeDelay1 (0.75 s), then release.
        const bool chargeW = pc.weapon().charge();
        for (int i = 0; i < 90 && !fired; ++i) {
            world_.heldWeaponMuzzle(muzzleAtShot);
            step(chargeW && i >= 54 ? idle : fire);
            if (world_.liveProjectiles() > n0) { spawned = world_.projectilePos(world_.liveProjectiles() - 1); fired = true; }
        }
        const core::Vec3 eye = pc.actorLocation() + core::Vec3{0, pc.robotParams().eyeHeight, 0};
        const core::Vec3 hand{pc.weaponWorld().m[12], pc.weaponWorld().m[13], pc.weaponWorld().m[14]};
        // The projectile moves one tick (speed x dt) in the step it spawns: compare against the muzzle within that travel.
        const float travel = (pc.weapon().projSpeed > 0.0f ? pc.weapon().projSpeed : 0.0f) * dt + 0.05f;
        const float dMuzzle = core::length(spawned - muzzleAtShot), dEye = core::length(spawned - eye), dHand = core::length(muzzleAtShot - hand);
        LOG_INFO("RMUZZLE %-15s on %-6s: socket %d, spawn %.2f m from muzzle (tick travel %.2f), %.2f m from eye, muzzle %.2f m from hand",
                 c.id, pc.chassis().id.c_str(), (int)have, dMuzzle, travel, dEye, dHand);
        // The muzzle lies ahead of the hand along the aim (barrel end; long weapons reach ~2 m).
        const float ahead = core::dot(muzzleAtShot - hand, core::forwardFromYawPitch(world_.player().controller().camYaw(), 0.0f));
        LOG_INFO("RMUZZLE %-15s muzzle %.2f m ahead of the hand", c.id, ahead);
        check(have && fired && dMuzzle <= travel && dHand < 3.0f && ahead > 0.0f, std::string(c.id) + ": projectile spawns at the weapon MuzzleFlash socket");
    }
    LOG_INFO("RMUZZLE SUMMARY: %d/%d checks passed", checks - fails, checks);
}

// WFC_CHARGETEST: TnChargeWeapon (Plasma Cannon) levels by hold time + TnProjectileGrenadeBase spin.
void Application::runChargeTest() {
    int checks = 0, fails = 0;
    auto check = [&](bool ok, const std::string& what) { ++checks; if (!ok) ++fails; LOG_INFO("CHARGE %s %s", ok ? "PASS" : "FAIL", what.c_str()); };
    const float dt = 1.0f / 60.0f;
    platform::InputFrame idle;
    auto step = [&](const platform::InputFrame& in) { world_.handleInput(in, dt); world_.tick(dt); };
    auto run = [&](float secs) { for (int i = 0; i < (int)(secs * 60.0f + 0.5f); ++i) step(idle); };
    game::MatchLaunch L; game::MatchLaunch::fromURL(world_.mapName() + "_BASE_m?GameModeTag=TDM", L);
    world_.launchMatch(L);
    game::CharacterSelection cs; cs.type = 0; cs.specialty = game::Specialty::Scout; cs.weapons = {"PlasmaCannon", "HeavyPistol", "FlashBangs"};
    world_.match().selectCharacter(world_.localMatchPlayer(), cs);
    run(11.0f);
    game::Character& pc = world_.player().pawn();
    const std::string held = pc.weapon().def ? pc.weapon().def->id : "?";
    check(held == "PlasmaCannon", "Scout holds the Plasma Cannon (" + held + ")");
    if (held != "PlasmaCannon") { LOG_INFO("CHARGE SUMMARY: %d/%d checks passed", checks - fails, checks); return; }
    struct Shot { bool fired; float speed, damage; int ammoUsed, ammoHeld; std::string tpl, hudMid, hudEnd; int level = 0; };
    auto hold = [&](float secs) {
        game::Weapon& w = pc.weapon();
        w.ammo = w.magSize; w.reserve = w.reserveMax;
        run(0.5f);
        const int a0 = w.ammo; const size_t n0 = world_.liveProjectiles();
        Shot r{false, 0, 0, 0, 0, "", "", ""};
        const int ticks = (int)(secs * 60.0f + 0.5f);
        for (int i = 0; i < ticks; ++i) {
            platform::InputFrame f; f.down[(int)platform::Button::Fire] = true; if (i == 0) f.pressed[(int)platform::Button::Fire] = true;
            step(f);
            if (i == 20) r.hudMid = world_.hudState().weaponChargeMessage;
            if (i == ticks - 1) { r.hudEnd = world_.hudState().weaponChargeMessage; r.ammoHeld = a0 - pc.weapon().ammo; }
        }
        step(idle);
        if (world_.liveProjectiles() > n0) {
            const size_t k = world_.liveProjectiles() - 1;
            r.fired = true; r.speed = core::length(world_.projectileVel(k)); r.damage = world_.projectileDamage(k); r.tpl = world_.projectileFlightTemplate(k);
        }
        r.ammoUsed = a0 - pc.weapon().ammo;
        if (r.fired) r.level = world_.hudState().weaponChargeShotLevel;
        LOG_INFO("CHARGE hold %.2f s: fired %d speed %.0f m/s damage %.0f ammo %d (drained while held %d) hud [%s] -> [%s] trail %s", secs, (int)r.fired, r.speed, r.damage,
                 r.ammoUsed, r.ammoHeld, r.hudMid.c_str(), r.hudEnd.c_str(), r.tpl.c_str());
        run(1.0f);
        return r;
    };
    const unsigned fz0 = world_.hudState().weaponChargeFizzle, sr0 = world_.hudState().weaponChargeSerial;
    Shot t = hold(0.3f);
    const unsigned fz1 = world_.hudState().weaponChargeFizzle, sr1 = world_.hudState().weaponChargeSerial;
    // Glow by level: sample while holding through level 3.
    float glow[3] = {-1, -1, -1};
    {
        game::Weapon& w = pc.weapon(); w.ammo = w.magSize; run(0.5f);
        for (int i = 0; i < 240; ++i) {
            platform::InputFrame f; f.down[(int)platform::Button::Fire] = true; if (i == 0) f.pressed[(int)platform::Button::Fire] = true;
            step(f);
            const int st = world_.hudState().weaponChargeState;
            if (st >= 2 && glow[st - 2] < 0) glow[st - 2] = world_.hudState().weaponChargeGlow;
        }
        step(idle); run(3.0f);   // the full-charge shot empties the clip: let the auto-reload (2.5 s) finish
    }
    LOG_INFO("CHARGE presentation: tap fizzle +%u, tap state changes +%u, glow by level %.3f / %.3f / %.3f", fz1 - fz0, sr1 - sr0, glow[0], glow[1], glow[2]);
    check(fz1 - fz0 == 1 && sr1 - sr0 == 2 && std::fabs(glow[0] - 1.0f / 3.0f) < 1e-4f && std::fabs(glow[1] - 2.0f / 3.0f) < 1e-4f && std::fabs(glow[2] - 1.0f) < 1e-4f,
          "presentation: tap = 1 fizzle (event 22) and 2 state changes (0->1->0); MaterialGlowAmount 1/3, 2/3, 1 by level");
    Shot a = hold(1.0f), b = hold(2.5f), c = hold(4.0f);
    check(a.level == 1 && b.level == 2 && c.level == 3, "HudState weaponChargeShotLevel = 1 / 2 / 3 for the released shots");
    check(!t.fired && t.ammoUsed == 0 && t.hudEnd == "CHARGING", "tap (0.3 s, state 1): no shot, no ammo; HUD CHARGING");
    check(a.fired && std::fabs(a.speed - 80.0f) < 1.0f && a.damage == 115.0f && a.ammoUsed == 25 && a.hudEnd == "READY" && a.tpl.find("_Sm_") != std::string::npos,
          "1.0 s: Charge1 80 m/s, 115 dmg, 25 ammo, small trail, HUD READY");
    check(b.fired && std::fabs(b.speed - 150.0f) < 1.0f && b.damage == 140.0f && b.ammoUsed == 50 && b.tpl.find("_Med_") != std::string::npos,
          "2.5 s: Charge2 150 m/s, 140 dmg, 50 ammo, medium trail");
    check(c.fired && std::fabs(c.speed - 230.0f) < 1.0f && c.damage == 179.0f && c.ammoHeld >= 4 && c.ammoHeld <= 6 && c.ammoUsed == 100 && c.tpl.find("_Lrg_") != std::string::npos,
          "4.0 s: Charge3 230 m/s, 179 dmg, ~5 drained at full charge then ShotCost 100 (clip clamps at 0), large trail");
    {
        game::Weapon& w = pc.weapon(); w.ammo = w.magSize; run(0.5f);
        const size_t n0 = world_.liveProjectiles(); const int a0 = w.ammo;
        for (int i = 0; i < 90; ++i) {
            platform::InputFrame f; f.down[(int)platform::Button::Fire] = true; if (i == 0) f.pressed[(int)platform::Button::Fire] = true;
            if (i == 70) f.mouseWheel = 1.0f;
            step(f);
        }
        run(1.5f);
        int cannonAmmo = -1;
        for (auto& x : pc.inventory()) if (x.def && std::string(x.def->id) == "PlasmaCannon") cannonAmmo = x.ammo;
        check(world_.liveProjectiles() == n0 && a0 == cannonAmmo && std::string(pc.weapon().def ? pc.weapon().def->id : "?") == "HeavyPistol",
              "charge level 2 then switch: no shot, no ammo, Heavy Pistol up");
    }
    {
        run(1.0f);
        const size_t n0 = world_.liveProjectiles();
        platform::InputFrame g; g.pressed[(int)platform::Button::Grenade] = true; g.down[(int)platform::Button::Grenade] = true;
        step(g);
        size_t k = (size_t)-1; float s0 = 0, s1 = 0; int flightTicks = 0; bool rested = false; float sRest = 0, sRestLater = 0;
        for (int i = 0; i < 600; ++i) {
            step(idle);
            if (k == (size_t)-1 && world_.liveProjectiles() > n0) { k = world_.liveProjectiles() - 1; s0 = world_.projectileSpin(k); continue; }
            if (k == (size_t)-1 || k >= world_.liveProjectiles()) continue;
            if (flightTicks < 15) { ++flightTicks; s1 = world_.projectileSpin(k); }
            if (!rested && world_.projectileResting(k)) { rested = true; sRest = world_.projectileSpin(k); }
            else if (rested) { sRestLater = world_.projectileSpin(k); break; }
        }
        const float rateDeg = flightTicks > 0 ? (s1 - s0) / (flightTicks * dt) * 57.29578f : 0.0f;
        LOG_INFO("CHARGE grenade spin: %.0f deg/s in flight (authored -549), rested %d, spin change at rest %.5f rad", rateDeg, (int)rested, sRestLater - sRest);
        check(k != (size_t)-1 && std::fabs(rateDeg + 549.3f) < 5.0f && (!rested || std::fabs(sRestLater - sRest) < 1e-5f),
              "Flashbang tumbles at RotationRate pitch -100000 (-549 deg/s), stops at rest");
    }
    LOG_INFO("CHARGE SUMMARY: %d/%d checks passed", checks - fails, checks);
}

// WFC_DROPTEST: settle a hover vehicle on flat ground, lift it 10 m, release; log per step the COM height above the floor,
// vertical speed and probe contacts until 1.5 s after touchdown. Diagnostic (drop recovery investigation).
void Application::runDropTest() {
    const float dt = 1.0f / 60.0f;
    game::PlayerController& ctl = world_.player().controller();
    const game::CollisionWorld* cw = world_.collision();
    platform::InputFrame idle, tf; tf.pressed[(int)platform::Button::Transform] = true; tf.down[(int)platform::Button::Transform] = true;
    auto step = [&](const platform::InputFrame& in) { world_.handleInput(in, dt); world_.tick(dt); };
    const char* only = std::getenv("WFC_DROPTEST");
    for (const char* id : {"Car2", "Truck3", "Tank3"}) {
        if (only && std::strlen(only) > 1 && std::string(only) != id) continue;
        world_.applyChassisToLocalPawn(id);
        world_.applyLoadout(nullptr);
        game::Character& pc = world_.player().pawn();
        if (pc.form() != game::Form::Vehicle) step(tf);
        for (int i = 0; i < 240; ++i) step(idle);
        if (pc.form() != game::Form::Vehicle) { LOG_INFO("DROP %s: no vehicle form", id); continue; }
        auto& vs = pc.vehicleState();
        // Flat floor under the pawn with open sky.
        const core::Vec3 base = pc.position();
        float gy; core::Vec3 gn;
        if (!cw || !cw->groundHeight(base.x, base.z, base.y + 1.0f, 2.0f, gy, gn)) { LOG_INFO("DROP %s: no floor", id); continue; }
        for (int i = 0; i < 180; ++i) step(idle);
        const float restY = pc.position().y;
        LOG_INFO("DROP %s rest: root y %.3f (floor %.3f, root above floor %.3f), contacts %d", id, restY, gy, restY - gy, vs.contacts);
        pc.setPosition(core::Vec3{base.x, restY + 10.0f, base.z});
        pc.velocity() = {0, 0, 0}; vs.pitch = vs.roll = 0.0f; vs.angVel = {0, 0, 0};
        ctl.setCameraYaw(pc.yaw());
        int touch = -1;
        for (int i = 0; i < 400; ++i) {
            const float vyBefore = pc.velocity().y;
            step(idle);
            const float h = pc.position().y - restY;
            if (touch < 0 && vs.contacts > 0) touch = i;
            if (touch >= 0 && i - touch <= 90 && ((i - touch) < 20 || (i - touch) % 5 == 0))
                LOG_INFO("DROP %s t+%.3f: root %+.3f m vs rest, vy %+.2f (before step %+.2f), contacts %d, pitch %.2f deg",
                         id, (i - touch) * dt, h, pc.velocity().y, vyBefore, vs.contacts, vs.pitch * 57.29578f);
            if (touch >= 0 && i - touch > 90) break;
        }
    }
}

// WFC_RISERTEST: find a real kerb / step on the map (flat floor, a 0.18-0.32 m rise within 0.6 m, flat on top, clear run-up and
// run-out), drive each hover vehicle straight across it at hover speed and report the peak pitch and the settle.
// RE pass 4 A4 addendum (6bb8855) estimates for a 0.25 m riser at 1500 UU/s: car 2.6-2.8 deg, truck ~2 deg (HIGH).
void Application::runRiserTest() {
    const float dt = 1.0f / 60.0f;
    game::PlayerController& ctl = world_.player().controller();
    const game::CollisionWorld* cw = world_.collision();
    if (!cw) return;
    auto floorAt = [&](float x, float z, float yRef, float& gy) { core::Vec3 n; return cw->groundHeight(x, z, yRef + 1.0f, 2.0f, gy, n) && n.y > 0.98f; };
    // Scan: start S on flat floor; along dir d, flat for 12 m, then a rise of 0.18-0.32 m within 0.6 m, then flat for 8 m.
    core::Vec3 S{0, 0, 0}; float yaw = 0.0f, rise = 0.0f, edgeAt = 0.0f; bool found = false;
    const core::Vec3 b0 = cw->boundsMin(), b1 = cw->boundsMax();
    const float y0 = world_.player().pawn().position().y;
    for (float x = b0.x + 10.0f; x < b1.x - 10.0f && !found; x += 1.5f)
        for (float z = b0.z + 10.0f; z < b1.z - 10.0f && !found; z += 1.5f) {
            float g0; if (!floorAt(x, z, y0, g0)) continue;
            for (int k = 0; k < 8 && !found; ++k) {
                const float yw = k * 0.785398f; const core::Vec3 d = core::forwardFromYawPitch(yw, 0.0f);
                bool flat = true; float prev = g0, edge = -1.0f, top = 0.0f;
                for (float t = 0.5f; t <= (edge > 0.0f ? edge + 6.0f : 20.0f); t += 0.25f) {
                    float g; const core::Vec3 q{x + d.x * t, 0, z + d.z * t};
                    if (!floorAt(q.x, q.z, prev + 0.5f, g)) { flat = false; break; }
                    const float dh = g - prev;
                    if (edge < 0.0f) {
                        if (std::fabs(dh) < 0.03f) { prev = g; continue; }
                        if (t >= 8.0f && dh > 0.12f && dh < 0.40f && std::fabs(prev - g0) < 0.04f) { edge = t; top = g; rise = g - g0; prev = g; continue; }
                        flat = false; break;
                    } else if (std::fabs(g - top) > 0.04f) { flat = false; break; }
                    prev = g;
                }
                float tz; core::Vec3 tn;
                if (flat && edge > 0.0f && std::fabs(rise - (top - g0)) < 0.05f &&
                    !cw->segmentHit(core::Vec3{x, g0 + 1.0f, z}, core::Vec3{x + d.x * 20.0f, g0 + 1.0f, z + d.z * 20.0f}, tz, tn)) {   // clear at hull height
                    S = core::Vec3{x, g0 + 0.1f, z}; yaw = yw; found = true; edgeAt = edge;
                    LOG_INFO("RISER found: start (%.1f %.1f %.1f) yaw %.2f, step of %.3f m at %.1f m", x, g0, z, yw, rise, edge);
                }
            }
        }
    if (!found) { LOG_INFO("RISER no suitable step found on this map"); return; }
    platform::InputFrame idle, tf; tf.pressed[(int)platform::Button::Transform] = true; tf.down[(int)platform::Button::Transform] = true;
    auto step = [&](const platform::InputFrame& in) { world_.handleInput(in, dt); world_.tick(dt); };
    for (const char* id : {"Car2", "Truck3", "Tank3"}) {
        world_.applyChassisToLocalPawn(id);
        world_.applyLoadout(nullptr);
        game::Character& pc = world_.player().pawn();
        auto& vs = pc.vehicleState();
        pc.setPosition(S);
        { const game::VehicleParams& V = pc.vehicleParams(); LOG_INFO("RISER %s params: mass %.0f inertia %.0f / %.0f / %.0f kg m2, probe radius %.2f m, k %.0f d %.0f rest %.2f m, COM fwd %.2f up %.2f", id, V.mass, V.inertiaX, V.inertiaY, V.inertiaZ, V.suspMountRadius, V.suspStiffness, V.suspDamping, V.suspRest, V.comFwd, V.comUp); } pc.velocity() = {0, 0, 0}; pc.setYaw(yaw); ctl.setCameraYaw(yaw);
        if (pc.form() != game::Form::Vehicle) step(tf);
        for (int i = 0; i < 240; ++i) { ctl.setCameraYaw(yaw); step(idle); }
        pc.setPosition(S); pc.velocity() = {0, 0, 0}; pc.setYaw(yaw); vs.pitch = vs.roll = 0.0f; vs.angVel = {0, 0, 0};
        for (int i = 0; i < 120; ++i) { ctl.setCameraYaw(yaw); step(idle); }
        float maxUp = 0.0f, maxDown = 0.0f, settle = -1.0f, tCross = -1.0f, speed = 0.0f;
        const core::Vec3 d = core::forwardFromYawPitch(yaw, 0.0f);
        platform::InputFrame fwd; fwd.down[(int)platform::Button::Forward] = true;
        for (int i = 0; i < 300; ++i) {
            ctl.setCameraYaw(yaw);
            step(core::dot(pc.position() - S, d) < edgeAt + 3.0f ? fwd : idle);   // full hover speed through the step
            const float along = core::dot(pc.position() - S, d);
            if (tCross < 0.0f && along > edgeAt - 1.2f) { tCross = i * dt; speed = core::length(core::Vec3{pc.velocity().x, 0, pc.velocity().z}); }
            const float pdeg = vs.pitch * 57.29578f;    // + = nose up (the VEHPHYS jump convention)
            static const bool rt = std::getenv("WFC_RISERTRACE") != nullptr;
            if (rt && std::string(id) == "Car2" && along > edgeAt - 2.0f && along < edgeAt + 3.5f)
                LOG_INFO("RISERTRACE along %+.2f pitch %+.2f deg w.y %+.1f deg/s L %.3f %.3f %.3f %.3f contacts %d vy %+.2f", along - edgeAt, pdeg, vs.angVel.y * 57.3f, vs.spLen[0], vs.spLen[1], vs.spLen[2], vs.spLen[3], vs.contacts, pc.velocity().y);
            if (tCross >= 0.0f) {
                maxUp = std::max(maxUp, pdeg); maxDown = std::min(maxDown, pdeg);
                if (settle < 0.0f && i * dt > tCross + 0.3f && std::fabs(pdeg) < 0.1f) settle = i * dt - tCross;
            }
        }
        LOG_INFO("RISER %s: crossing at %.1f m/s, peak nose-up %.2f deg, nose-down %.2f deg, |pitch| < 0.1 deg %.2f s after reaching the step",
                 id, speed, maxUp, maxDown, settle);
    }
}

// WFC_PRELOADTEST: a custom selection outside the faction defaults, preloaded with World::preloadSelections after the launch
// (as Frontend's loading step would), then picked and spawned: the body must not load at the pick, the first equip is cheap.
// Run with WFC_SPAWNPROF=1 to see the equip cost.
void Application::runPreloadTest() {
    int checks = 0, fails = 0;
    auto check = [&](bool ok, const std::string& what) { ++checks; if (!ok) ++fails; LOG_INFO("PRELOAD %s %s", ok ? "PASS" : "FAIL", what.c_str()); };
    const float dt = 1.0f / 60.0f;
    auto run = [&](float secs) { for (int i = 0; i < (int)(secs * 60.0f + 0.5f); ++i) { platform::InputFrame in; world_.handleInput(in, dt); world_.tick(dt); } };
    game::MatchLaunch L; game::MatchLaunch::fromURL(world_.mapName() + "_BASE_m?GameModeTag=TDM", L);
    world_.launchMatch(L);
    game::CharacterSelection cs; cs.type = 0; cs.specialty = game::Specialty::Scout;
    cs.chassisByFaction[0] = "Car"; cs.chassisByFaction[1] = "Car4"; cs.weapons = {"SniperRifle", "Bazooka"};
    LOG_INFO("PRELOAD calling preloadSelections");
    world_.preloadSelections({cs});
    LOG_INFO("PRELOAD selecting");
    world_.match().selectCharacter(world_.localMatchPlayer(), cs);
    run(11.0f);
    game::Character& pc = world_.player().pawn();
    LOG_INFO("PRELOAD spawned as %s holding %s", pc.chassis().id.c_str(), pc.weapon().def ? pc.weapon().def->id : "-");
    check(pc.chassis().id == "Car", "spawned with the custom chassis (Bumblebee)");
    LOG_INFO("PRELOAD SUMMARY: %d/%d checks passed", checks - fails, checks);
}

} // namespace core
