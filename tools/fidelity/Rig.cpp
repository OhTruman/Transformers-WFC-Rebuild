#include "Rig.h"
#include "core/Config.h"

#include <cstdio>
#include <cstdlib>

namespace fid {

namespace {
bool gModelsDisabled = false;
}

std::string Models::assetRoot() {
    if (const char* e = std::getenv("WFC_ASSETS")) return e;
    return core::config::kAssetRootDefault;
}

void Models::disable() { gModelsDisabled = true; }

const Models* Models::get() {
    static Models* m = nullptr;
    static bool tried = false;
    if (gModelsDisabled) return nullptr;
    if (!tried) {
        tried = true;
        auto* mm = new Models;
        std::string root = assetRoot() + "/Characters/Optimus/";
        mm->ok = assets::loadSkinnedGlb(root + "robot.glb", mm->robot) &&
                 assets::loadSkinnedGlb(root + "vehicle.glb", mm->vehicle);
        if (mm->ok) {
            mm->weaponBone = mm->robot.nodeByName("R_Arm03_Elbow_XB");
            // Mirrors World::loadVerticalSlice: [CONF] WeaponSocket_Primary (character.json)
            // loc_ue [-40,0,0], rot_ue [0, 31311, 5461] -> gltf-space socket matrix.
            const float sm[16] = {-0.990259f, 0.0f,       0.139234f, 0.0f,
                                  -0.069613f, 0.866041f, -0.495102f, 0.0f,
                                  -0.120583f, -0.499972f, -0.857606f, 0.0f,
                                  -0.4f,      0.0f,       0.0f,       1.0f};
            mm->socket = core::mat4FromArray(sm);
            m = mm;
        } else {
            delete mm;
        }
    }
    return m;
}

Rig::Rig(double hz, bool withModels) : dt_((float)(1.0 / hz)) {
    models_ = withModels ? Models::get() : nullptr;
    if (models_) {
        pawn().setFormModels(&models_->robot, &models_->vehicle);
        pawn().setWeaponSocket(models_->weaponBone, models_->socket);
    }
    pawn().setPosition({0, 0, 0});
    pawn().groundY = 0.0f;
    shotBase_ = shotLog().size();
}

void Rig::step(const platform::InputFrame& in) {
    setShotClock(t_);
    game::PlayerController& pc = controller();
    pc.handleInput(in, dt_);
    if (!col_) {
        // Production order (World::tick): applyToPawn (movement + weapon) then animation.
        pc.applyToPawn(world_, dt_);
    } else {
        // Collision variant: same intent mapping as PlayerController::handleInput, but movement
        // resolved against the probe geometry. Keep in sync with PlayerController.cpp.
        using platform::Button;
        bool locked = pawn().isTransforming();
        game::MoveIntent mi;
        float f = (in.isDown(Button::Forward) ? 1.f : 0.f) - (in.isDown(Button::Back) ? 1.f : 0.f);
        float r = (in.isDown(Button::Right) ? 1.f : 0.f) - (in.isDown(Button::Left) ? 1.f : 0.f);
        mi.moveForward = locked ? 0 : f;
        mi.moveRight = locked ? 0 : r;
        mi.faceYaw = pc.camYaw();
        mi.wantBoost = !locked && in.isDown(Button::Sprint);
        mi.wantJump = !locked && in.wasPressed(Button::Jump);
        game::CharacterMovement::update(pawn(), mi, dt_, col_);
        pawn().weapon().tick(dt_);
    }
    pawn().updateAnimation(dt_);
    t_ += dt_;
    ++step_;
    record();
}

void Rig::hold(const platform::InputFrame& in, double seconds) {
    int n = (int)(seconds / dt_ + 0.5);
    platform::InputFrame f = in;
    for (int i = 0; i < n; ++i) {
        step(f);
        for (bool& p : f.pressed) p = false;
        f.mouseDX = f.mouseDY = 0;
    }
}

void Rig::record() {
    const game::Character& c = pawn();
    Frame fr;
    fr.step = step_;
    fr.t = t_;
    fr.pos = c.position();
    fr.vel = c.velocity();
    fr.yaw = c.yaw();
    fr.camYaw = controller().camYaw();
    fr.camPitch = controller().camPitch();
    fr.grounded = c.onGround();
    fr.transforming = c.isTransforming();
    fr.reloading = c.weapon().reloading();
    fr.weaponVisible = c.hasWeapon();
    fr.form = c.form();
    fr.anim = c.animName();
    fr.animT = c.animTime();
    fr.ammo = c.weapon().ammo;
    fr.reserve = c.weapon().reserve;
    fr.spread = c.weapon().spread;
    fr.reloadW = layer::reloadSlotWeight(c);
    fr.moveForm = layer::moveForm(c, 0);
    fr.fineAim = layer::fineAiming(controller(), 0);
    fr.fov = layer::fovXDeg(controller(), 0);
    fr.progress = layer::transformProgress(c, 0);
    fr.wUsable = layer::weaponUsable(c, 0);
    fr.wRestored = layer::weaponRestored(c, 0);
    fr.meshOff = layer::meshOffsetOf(c, 0);
    fr.veh = layer::vehicleSnap(c, 0);
    fr.aimW = layer::aimWeight(c, 0);
    fr.aimPitchN = layer::aimPitchNorm(c, 0);
    fr.shots = (int)(shotLog().size() - shotBase_);
    if (fr.weaponVisible)
        fr.muzzle = core::transformPoint(c.weaponWorld(), core::Vec3{core::config::kMuzzleLocalX,
                                         core::config::kMuzzleLocalY, core::config::kMuzzleLocalZ});
    trace_.push_back(fr);
}

render::Camera Rig::camera() const {
    render::Camera cam;
    const_cast<Rig*>(this)->controller().updateCamera(cam);
    cam.aspect = (float)core::config::kWindowWidth / (float)core::config::kWindowHeight;
    return cam;
}

bool Rig::writeCsv(const std::string& path) const {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "step,t,x,y,z,vx,vy,vz,hspeed,yaw_deg,cam_yaw_deg,cam_pitch_deg,grounded,form,"
                    "transforming,anim,anim_t,ammo,reserve,reloading,spread,shots,weapon_visible,reload_w,aim_w,aim_pitch_n,"
                    "muzzle_x,muzzle_y,muzzle_z,move_form,progress,weapon_usable,weapon_restored,mesh_off_y,"
                    "veh_driving,veh_ride,veh_dash,veh_dash_cd,veh_nitro,veh_nitro_cd,fine_aim,fov\n");
    for (const Frame& r : trace_) {
        std::fprintf(f, "%d,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.3f,%.3f,%.3f,%d,%s,%d,%s,%.4f,"
                        "%d,%d,%d,%.4f,%d,%d,%.3f,%.3f,%.3f,%.4f,%.4f,%.4f,"
                        "%.0f,%.4f,%.0f,%.0f,%.4f,%d,%.4f,%.4f,%.4f,%.4f,%.4f,%.0f,%.2f\n",
                     r.step, r.t, r.pos.x, r.pos.y, r.pos.z, r.vel.x, r.vel.y, r.vel.z,
                     std::sqrt(r.vel.x * r.vel.x + r.vel.z * r.vel.z), core::degrees(r.yaw),
                     core::degrees(r.camYaw), core::degrees(r.camPitch), (int)r.grounded,
                     game::formName(r.form), (int)r.transforming, r.anim.c_str(), r.animT, r.ammo,
                     r.reserve, (int)r.reloading, r.spread, r.shots, (int)r.weaponVisible, r.reloadW, r.aimW, r.aimPitchN,
                     r.muzzle.x, r.muzzle.y, r.muzzle.z,
                     r.moveForm, r.progress, r.wUsable, r.wRestored, r.meshOff.y,
                     (int)r.veh.driving, r.veh.ride, r.veh.dash, r.veh.dashCd, r.veh.nitro, r.veh.nitroCd, r.fineAim, r.fov);
    }
    std::fclose(f);
    return true;
}

platform::InputFrame Rig::press(platform::Button b) {
    platform::InputFrame f;
    f.pressed[(int)b] = true;
    return f;
}

platform::InputFrame Rig::down(std::initializer_list<platform::Button> bs) {
    platform::InputFrame f;
    for (platform::Button b : bs) f.down[(int)b] = true;
    return f;
}

static void quad(render::MeshData& m, core::Vec3 a, core::Vec3 b, core::Vec3 c, core::Vec3 d) {
    uint32_t base = (uint32_t)m.vertexCount();
    for (const core::Vec3& v : {a, b, c, d}) { m.positions.push_back(v.x); m.positions.push_back(v.y); m.positions.push_back(v.z); }
    for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) m.indices.push_back(base + i);
}

void BoxScene::floor(float y, float h) {
    quad(mesh, {-h, y, -h}, {-h, y, h}, {h, y, h}, {h, y, -h});   // CCW from above -> +Y normal
}

void BoxScene::ramp(float x0, float x1, float z0, float len, float deg) {
    float h = len * std::tan(core::radians(deg));
    float z1 = z0 - len;
    quad(mesh, {x0, 0, z0}, {x1, 0, z0}, {x1, h, z1}, {x0, h, z1});
    box({x0, 0, z1 - 20}, {x1, h, z1});   // landing platform at the top
}

void BoxScene::box(const core::Vec3& n, const core::Vec3& x) {
    quad(mesh, {n.x, x.y, n.z}, {n.x, x.y, x.z}, {x.x, x.y, x.z}, {x.x, x.y, n.z});   // top
    quad(mesh, {n.x, n.y, n.z}, {x.x, n.y, n.z}, {x.x, n.y, x.z}, {n.x, n.y, x.z});   // bottom
    quad(mesh, {n.x, n.y, x.z}, {x.x, n.y, x.z}, {x.x, x.y, x.z}, {n.x, x.y, x.z});   // +Z
    quad(mesh, {n.x, n.y, n.z}, {n.x, x.y, n.z}, {x.x, x.y, n.z}, {x.x, n.y, n.z});   // -Z
    quad(mesh, {x.x, n.y, n.z}, {x.x, x.y, n.z}, {x.x, x.y, x.z}, {x.x, n.y, x.z});   // +X
    quad(mesh, {n.x, n.y, n.z}, {n.x, n.y, x.z}, {n.x, x.y, x.z}, {n.x, x.y, n.z});   // -X
}

} // namespace fid
