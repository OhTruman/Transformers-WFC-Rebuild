#include "core/Application.h"
#include "core/Config.h"
#include "core/Debug.h"
#include "core/Log.h"
#include "platform/Window.h"
#include "render/Renderer.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

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

        if (!window_->pump(input)) break;
        if (input.wasPressed(platform::Button::Quit)) break;

        if (autoWalk) input.down[(int)platform::Button::Forward] = true;  // scripted move for tests
        if (std::getenv("WFC_NOMOUSE")) { input.mouseDX = 0; input.mouseDY = 0; }   // deterministic tests
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
        if (input.wasPressed(platform::Button::Debug))
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
            }
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

        if (smokeFrames > 0 && frame == smokeFrames)
            if (const char* shot = std::getenv("WFC_SHOT")) renderer_->captureScreenshot(shot);

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
