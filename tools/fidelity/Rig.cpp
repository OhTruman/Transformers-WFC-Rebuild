#include "Rig.h"
#include "core/Config.h"

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cmath>

// Body-local vehicle COM offset (gameplay Pass 13+ Config kVehComUp / kVehComFwd), 0 on builds without
// it: unqualified lookup inside core::config finds the product constant first, the fallback otherwise.
namespace fid_cfg_fallback {
[[maybe_unused]] constexpr float kVehComUp = 0.0f;
[[maybe_unused]] constexpr float kVehComFwd = 0.0f;
}
namespace core::config {
inline float fidComUp() { using namespace ::fid_cfg_fallback; return kVehComUp; }
inline float fidComFwd() { using namespace ::fid_cfg_fallback; return kVehComFwd; }
}

namespace fid {

namespace {
bool gModelsDisabled = false;
}

// Separate Optimus arm (gameplay Pass 13b+): loaded with the product's loader when the build has it.
// Unqualified dependent calls (ADL) so older trees without loadAnimationsByName / setArmModel compile.
template <class M> auto loadArmAnims(const std::string& p, M& m, int) -> decltype(loadAnimationsByName(p, m), bool()) {
    return loadAnimationsByName(p, m) >= 0;
}
template <class M> bool loadArmAnims(const std::string&, M&, long) { return false; }
template <class C, class M> auto giveArm(C& c, const M* m, int) -> decltype(c.setArmModel(m), bool()) { c.setArmModel(m); return true; }
template <class C, class M> bool giveArm(C&, const M*, long) { return false; }

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
            std::string content = assetRoot() + "/../content/";
            mm->armOk = assets::loadSkinnedGlb(content + "TR_Optimus_ROBO_p/CP_OptimusArm_SKEL.gltf", mm->arm) &&
                        loadArmAnims(content + "TR_HeavyMedium_ANM_p/OptimusArm_ROBO_ANIM.anim.gltf", mm->arm, 0);
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
        if (models_->armOk) giveArm(pawn(), &models_->arm, 0);
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
    fr.drawRoot = layer::drawRootOf(c, 0);
    fr.veh = layer::vehicleSnap(c, 0);
    fr.aimW = layer::aimWeight(c, 0);
    fr.aimPitchN = layer::aimPitchNorm(c, 0);
    fr.shots = (int)(shotLog().size() - shotBase_);
    if (fr.weaponVisible)
        fr.muzzle = core::transformPoint(c.weaponWorld(), core::Vec3{core::config::kMuzzleLocalX,
                                         core::config::kMuzzleLocalY, core::config::kMuzzleLocalZ});
    {
        render::Camera cam = camera();
        fr.camPos = cam.pos;
        fr.viewYaw = cam.yaw;
        fr.camFov = cam.fovXDeg;
        fr.camFocus = layer::actorLocationOf(c, 0) + core::Vec3{0, core::config::kCamHeight, 0};   // camera anchor (approx. Z offset)
    }
    // Drawn pose (Character::draw: currentModel() skinned into poseBuf_ at pos + meshOffset, yaw).
    const assets::SkinnedModel* mdl = c.currentModel();
    const render::MeshData& pose = drawnPose(c);
    fr.drawnModel = (mdl && mdl->valid() && !pose.empty()) ? (models_ && mdl == &models_->vehicle ? 1 : 0) : -1;
    if (fr.drawnModel >= 0) {
        core::Mat4 model = layer::drawMatrixOf(c, c.form(), 0);   // where the current form is drawn
        const std::vector<float>& p = pose.positions;
        // Robust bounds (1st..99th percentile per axis) so a few far-flung vertices do not dominate;
        // vertices > 10 m from the root are counted separately (farVerts / farMax).
        float d2 = 0;
        bool same = fr.drawnModel == prevModel_ && prevPose_.size() == p.size();
        core::Vec3 root = fr.drawRoot;
        std::vector<float> ax[3];
        for (auto& v : ax) v.reserve(p.size() / 3);
        fr.farVerts = 0; fr.farMax = 0;
        for (size_t i = 0; i + 2 < p.size(); i += 3) {
            core::Vec3 w = core::transformPoint(model, core::Vec3{p[i], p[i + 1], p[i + 2]});
            ax[0].push_back(w.x); ax[1].push_back(w.y); ax[2].push_back(w.z);
            float dr = core::length(w - root);
            if (dr > 10.0f) { ++fr.farVerts; fr.farMax = std::max(fr.farMax, dr); }
            if (same) {
                float dx = p[i] - prevPose_[i], dy = p[i + 1] - prevPose_[i + 1], dz = p[i + 2] - prevPose_[i + 2];
                d2 = std::max(d2, dx * dx + dy * dy + dz * dz);
            }
        }
        float lo[3], hi[3];
        for (int k = 0; k < 3; ++k) {
            std::vector<float>& v = ax[k];
            size_t n = v.size(), a = n / 100, b = n - 1 - n / 100;
            std::nth_element(v.begin(), v.begin() + (long)a, v.end()); lo[k] = v[a];
            std::nth_element(v.begin(), v.begin() + (long)b, v.end()); hi[k] = v[b];
        }
        fr.bbMin = {lo[0], lo[1], lo[2]};
        fr.bbMax = {hi[0], hi[1], hi[2]};
        fr.poseDelta = same ? std::sqrt(d2) : -1.0f;
        prevPose_ = p;
    } else {
        prevPose_.clear();
    }
    prevModel_ = fr.drawnModel;
    // Pass 13+/14 state. Meshes drawn = current form's mesh + the partner mesh while partnerShown().
    fr.partner = layer::partnerShown(c, 0);
    fr.arm = layer::armShown(c, 0);
    fr.hand = layer::handShrunk(c, 0);
    fr.rammed = layer::rammedRemain(c, 0);
    fr.att = layer::vehAttitude(c, 0);
    bool both = fr.partner > 0;
    if (both) {
        if (const render::MeshData* pp = partnerPose(c)) {
            game::Form pf = c.form() == game::Form::Robot ? game::Form::Vehicle : game::Form::Robot;
            core::Mat4 model = layer::drawMatrixOf(c, pf, 0);   // partner form's draw matrix
            core::Vec3 root = core::Vec3{model.m[12], model.m[13], model.m[14]};
            for (size_t i = 0; i + 2 < pp->positions.size(); i += 3) {
                core::Vec3 w = core::transformPoint(model, core::Vec3{pp->positions[i], pp->positions[i + 1], pp->positions[i + 2]});
                if (core::length(w - root) > 10.0f) ++fr.partnerFarVerts;
            }
        }
    }
    fr.robotVisible = fr.drawnModel == 0 || (both && fr.drawnModel == 1);
    fr.vehicleVisible = fr.drawnModel == 1 || (both && fr.drawnModel == 0);
    if (c.form() == game::Form::Vehicle || fr.moveForm == 1) {
        // COM = mesh root + body-local COM offset rotated by the body attitude (y component).
        float p = fr.att.valid ? fr.att.pitch : 0.0f, ro = fr.att.valid ? fr.att.roll : 0.0f;
        float comY = c.position().y + core::config::fidComUp() * std::cos(p) * std::cos(ro) + core::config::fidComFwd() * std::sin(p);
        float gy = c.groundY, y; core::Vec3 n;
        game::CollisionWorld& wc = worldCollision(world_);
        if (wc.valid() && wc.groundHeight(c.position().x, c.position().z, comY, 0.5f, y, n)) gy = y;
        fr.comH = comY - gy;
        fr.comY = comY;
    }
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
                    "veh_driving,veh_ride,veh_dash,veh_dash_cd,veh_nitro,veh_nitro_cd,fine_aim,fov,"
                    "cam_x,cam_y,cam_z,cam_focus_x,cam_focus_y,cam_focus_z,cam_fov,drawn_model,pose_delta,"
                    "bb_min_x,bb_min_y,bb_min_z,bb_max_x,bb_max_y,bb_max_z,far_verts,far_max,partner_far_verts,"
                    "partner_shown,arm_shown,hand_shrunk,robot_visible,vehicle_visible,com_h,pitch_deg,roll_deg,ang_x,ang_y,ang_z,"
                    "contacts,spring_mean,rammed_remain\n");
    for (const Frame& r : trace_) {
        std::fprintf(f, "%d,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.3f,%.3f,%.3f,%d,%s,%d,%s,%.4f,"
                        "%d,%d,%d,%.4f,%d,%d,%.3f,%.3f,%.3f,%.4f,%.4f,%.4f,"
                        "%.0f,%.4f,%.0f,%.0f,%.4f,%d,%.4f,%.4f,%.4f,%.4f,%.4f,%.0f,%.2f,"
                        "%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.2f,%d,%.5f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%.2f,%d,"
                        "%.0f,%.0f,%.0f,%d,%d,%.4f,%.3f,%.3f,%.4f,%.4f,%.4f,%d,%.4f,%.3f\n",
                     r.step, r.t, r.pos.x, r.pos.y, r.pos.z, r.vel.x, r.vel.y, r.vel.z,
                     std::sqrt(r.vel.x * r.vel.x + r.vel.z * r.vel.z), core::degrees(r.yaw),
                     core::degrees(r.camYaw), core::degrees(r.camPitch), (int)r.grounded,
                     game::formName(r.form), (int)r.transforming, r.anim.c_str(), r.animT, r.ammo,
                     r.reserve, (int)r.reloading, r.spread, r.shots, (int)r.weaponVisible, r.reloadW, r.aimW, r.aimPitchN,
                     r.muzzle.x, r.muzzle.y, r.muzzle.z,
                     r.moveForm, r.progress, r.wUsable, r.wRestored, r.meshOff.y,
                     (int)r.veh.driving, r.veh.ride, r.veh.dash, r.veh.dashCd, r.veh.nitro, r.veh.nitroCd, r.fineAim, r.fov,
                     r.camPos.x, r.camPos.y, r.camPos.z, r.camFocus.x, r.camFocus.y, r.camFocus.z, r.camFov, r.drawnModel,
                     r.poseDelta, r.bbMin.x, r.bbMin.y, r.bbMin.z, r.bbMax.x, r.bbMax.y, r.bbMax.z, r.farVerts, r.farMax, r.partnerFarVerts,
                     r.partner, r.arm, r.hand, (int)r.robotVisible, (int)r.vehicleVisible, r.comH, core::degrees(r.att.pitch),
                     core::degrees(r.att.roll), r.att.angVel.x, r.att.angVel.y, r.att.angVel.z, r.att.contacts, r.att.springMean, r.rammed);
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
