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
#include "assets/Gltf.h"

#include <cmath>
#include <cstdio>
#include <utility>
#include <array>
#include <vector>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <cstdio>

namespace core {

bool Application::init() {
    LOG_INFO("WFC Rebuild starting (clean-room skeleton)");
    window_ = platform::createWindow(config::kWindowWidth, config::kWindowHeight, config::kWindowTitle);
    if (!window_) { LOG_ERROR("window creation failed"); return false; }

    renderer_ = render::createGLRenderer();
    if (!renderer_) { LOG_ERROR("renderer creation failed"); return false; }

    audio_ = audio::createAudio();
    if (std::getenv("WFC_DEBUGDRAW")) core::DebugFlags::get().enabled = true;

    world_.load(*renderer_);
    world_.setAudio(audio_);
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
                LOG_INFO("MEMCYCLE %s loaded %.1f MB unloaded %.1f MB", m.c_str(), loaded, privMB());
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
        struct PreviewBody { render::MeshData mesh; core::Mat4 model; render::CharacterColors colors; };
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
                if (renderer_->loadContentMesh(s.substr(0, a), body.mesh)) {
                    body.model = renderer_->actorMatrix(core::Vec3{x, y, z}, core::Vec3{0, yaw, 0});
                    previews.push_back(std::move(body));
                } else LOG_WARN("WFC_SCENEPREVIEW: cannot load %s", s.substr(0, a).c_str());
                if (p1 == std::string::npos) break;
                p0 = p1 + 1;
            }
            renderer_->setFrontendSceneDraw([](render::IRenderer& r) {
                for (size_t i = 0; i < previews.size(); ++i) {
                    r.setDrawOwner(1 + (int)i);
                    r.setCharacterColors(previews[i].colors);
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
    double last = nowSeconds();
    platform::InputFrame input;

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

        if (!window_->pump(input)) break;
        if (input.wasPressed(platform::Button::Quit)) break;

        if (autoWalk) input.down[(int)platform::Button::Forward] = true;  // scripted move for tests
        static const bool lockstepInput = std::getenv("WFC_LOCKSTEP") != nullptr;
        if (std::getenv("WFC_NOMOUSE") || lockstepInput) { input.mouseDX = 0; input.mouseDY = 0; }   // deterministic tests
        if (std::getenv("WFC_AUTOSTRAFE")) input.down[(int)platform::Button::Right] = true;
        if (std::getenv("WFC_AUTOBACK")) input.down[(int)platform::Button::Back] = true;
        if (std::getenv("WFC_AUTOFIRE")) input.down[(int)platform::Button::Fire] = true;
        if (std::getenv("WFC_AUTOBOOST")) input.down[(int)platform::Button::FineAim] = true;   // vehicle Boost (RMB held)
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
        int steps = clock_.tick(realDt);
        float step = clock_.stepSeconds();
        for (int i = 0; i < steps; ++i) {
            world_.tick(step);
            gameMode_.tick(world_, step);
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
                     "hspeed=%.2f moveForm=%s fineAim=%d fov=%.1f drv=%d ride=%.2f dash=%.2f nitro=%.2f wpn=%d",
                     frame, p.x, p.y, p.z, (int)pawn.onGround(), game::formName(pawn.form()),
                     pawn.animName(), pawn.animTime(), pawn.weapon().ammo, pawn.weapon().reserve,
                     (int)pawn.weapon().reloading(), pawn.yaw(), pawn.aimWeight(), pawn.aimPitchNorm(),
                     pawn.reloadWeight(), pawn.legYaw() * 57.2958f, pawn.aimYawNorm(),
                     (int)pawn.turningInPlace(), (int)pawn.recoiling(),
                     std::sqrt(pawn.velocity().x * pawn.velocity().x + pawn.velocity().z * pawn.velocity().z),
                     game::formName(pawn.moveForm()), (int)world_.player().controller().fineAiming(),
                     world_.player().controller().fovXDeg(),
                     (int)pawn.vehicleState().driving, pawn.vehicleState().rideHeight,
                     pawn.vehicleState().dashRemain, pawn.vehicleState().nitroRemain, (int)pawn.weaponUsable());
        }

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

        if (const char* sa = std::getenv("WFC_SHOWACTOR"))       // diagnostic: Gameplay-style unhide (e.g. a KOTH zone)
            renderer_->setActorHidden(sa, false);
        if (const char* ds = std::getenv("WFC_DESTRUCTSTATE"))   // diagnostic: destructible presentation state
            if (frame == 1) renderer_->setDestructibleState("TnStaticDestructibleActor_14465", std::atoi(ds));
        // Diagnostic: WFC_PICKUPTEST=<factory actor>,<take frame>,<respawn frame> drives the pickup presentation
        // (SetPickupHidden / SetPickupVisible) the way Gameplay's PickupEvents will.
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

void Application::shutdown() {
    delete audio_; audio_ = nullptr;
    delete renderer_; renderer_ = nullptr;
    delete window_; window_ = nullptr;
    LOG_INFO("Shutdown complete");
}

} // namespace core
