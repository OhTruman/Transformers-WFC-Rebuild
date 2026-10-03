#include "core/Application.h"
#include "core/Config.h"
#include "core/Debug.h"
#include "core/Log.h"
#include "platform/Window.h"
#include "render/Renderer.h"
#include "game/VehicleTests.h"
#include "game/PickupFactory.h"
#include "game/Destructible.h"

#include <algorithm>
#include <string>

#include <chrono>

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace core {

bool Application::init() {
    LOG_INFO("WFC Rebuild starting (clean-room skeleton)");
    if (std::getenv("WFC_VEHTEST")) { game::runVehicleTests(); return false; }   // measurements only
    window_ = platform::createWindow(config::kWindowWidth, config::kWindowHeight, config::kWindowTitle);
    if (!window_) { LOG_ERROR("window creation failed"); return false; }

    renderer_ = render::createGLRenderer();
    if (!renderer_) { LOG_ERROR("renderer creation failed"); return false; }

    audio_ = audio::createAudio();
    if (std::getenv("WFC_DEBUGDRAW")) core::DebugFlags::get().enabled = true;

    world_.load(*renderer_);
    if (std::getenv("WFC_PICKUPTEST")) { runPickupTest(); return false; }   // measurements only
    if (std::getenv("WFC_TRAVERSE")) { runTraverseTest(); return false; }   // measurements only
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
        if (const char* s = std::getenv("WFC_AUTOBOOST")) if (frame >= std::atol(s)) input.down[(int)platform::Button::FineAim] = true;   // vehicle Boost (RMB held) from frame N
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

void Application::shutdown() {
    delete audio_; audio_ = nullptr;
    delete renderer_; renderer_ = nullptr;
    delete window_; window_ = nullptr;
    LOG_INFO("Shutdown complete");
}

} // namespace core
