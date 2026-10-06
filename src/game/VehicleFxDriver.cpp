#include "game/VehicleFxDriver.h"
#include <algorithm>
#include <cmath>

namespace game {
namespace {

using core::Vec3;
Vec3 col(const core::Mat4& m, int c) { return {m.m[c * 4], m.m[c * 4 + 1], m.m[c * 4 + 2]}; }
float clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
constexpr float kUU = 100.0f;               // m -> UU
constexpr float kLinearLimit = 3000.0f;     // ThrusterLinearAccelerationLimit (x modifier 1.0, TnCarForm CDO / TnPlaneForm)
constexpr float kAngularLimit = 6.0f;       // ThrusterAngularAccelerationLimit

// World angular velocity (rad/s) carrying rotation `a` (previous) to `b` over dt: axis-angle of b * a^T.
Vec3 angularVelocity(const core::Mat4& a, const core::Mat4& b, float dt) {
    Vec3 ax[3] = {core::normalize(col(a, 0)), core::normalize(col(a, 1)), core::normalize(col(a, 2))};
    Vec3 bx[3] = {core::normalize(col(b, 0)), core::normalize(col(b, 1)), core::normalize(col(b, 2))};
    // R = B A^T as rows: R_ij = sum_k b_k[i] a_k[j]
    float R[3][3] = {};
    for (int k = 0; k < 3; ++k) {
        const float bi[3] = {bx[k].x, bx[k].y, bx[k].z}, aj[3] = {ax[k].x, ax[k].y, ax[k].z};
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) R[i][j] += bi[i] * aj[j];
    }
    const float tr = R[0][0] + R[1][1] + R[2][2];
    const float ang = std::acos(std::max(-1.0f, std::min(1.0f, (tr - 1.0f) * 0.5f)));
    if (ang < 1e-6f || dt <= 0.0f) return {0, 0, 0};
    Vec3 axis{R[2][1] - R[1][2], R[0][2] - R[2][0], R[1][0] - R[0][1]};
    const float s = core::length(axis);
    if (s < 1e-6f) return {0, 0, 0};
    return axis * (ang / (s * dt));
}

} // namespace

void VehicleFxDriver::setData(const VehicleFxData* d) {
    if (d == d_) return;
    stopAll();
    d_ = d && d->valid ? d : nullptr;
}

bool VehicleFxDriver::socketWorld(const Inputs& in, const std::string& socket, core::Mat4& out) const {
    if (!d_ || !in.boneWorld) return false;
    auto it = d_->sockets.find(socket);
    if (it == d_->sockets.end()) return false;
    core::Mat4 bone;
    if (!in.boneWorld(it->second.bone, bone)) return false;
    out = bone * core::mat4FromArray(it->second.rel);
    return true;
}

void VehicleFxDriver::startSet(const Inputs& in, const std::vector<VehicleFxData::Entry>& set, std::vector<Live>& out) {
    const float color[4] = {in.energon[0], in.energon[1], in.energon[2], 1.0f};
    for (const VehicleFxData::Entry& e : set) {
        core::Mat4 w;
        if (!socketWorld(in, e.socket, w)) continue;
        const int h = rt_.spawnAt(e.templ, col(w, 3), core::normalize(col(w, 0)), core::normalize(col(w, 1)));
        if (h < 0) continue;
        if (rt_.setParam) rt_.setParam(h, "Color", color);              // FxPlayer.Play: Color = EnergonColor
        out.push_back({h, &e, 0.0f});
    }
}

void VehicleFxDriver::stopSet(std::vector<Live>& v) {
    if (rt_.stop) for (const Live& l : v) rt_.stop(l.handle);
    v.clear();
}

void VehicleFxDriver::follow(const Inputs& in, std::vector<Live>& v) {
    if (!rt_.setTransform) return;
    for (const Live& l : v) {
        core::Mat4 w;
        if (socketWorld(in, l.e->socket, w)) rt_.setTransform(l.handle, col(w, 3), core::normalize(col(w, 0)), core::normalize(col(w, 1)));
    }
}

float VehicleFxDriver::tick(float dt, const Inputs& in) {
    using K = VehicleFormSignals::Kind;
    if (!bound() || !d_) return -1.0f;
    // Body kinematics (world, UU): linear acceleration from velocity, angular from the body rotation.
    Vec3 accel{0, 0, 0}, alpha{0, 0, 0};
    if (havePrev_ && dt > 0.0f) {
        accel = (in.velocity - prevVel_) * (kUU / dt);
        const Vec3 omega = angularVelocity(prevBody_, in.body, dt);
        alpha = (omega - prevOmega_) * (1.0f / dt);
        prevOmega_ = omega;
    }
    havePrev_ = true; prevVel_ = in.velocity; prevBody_ = in.body;
    const float k = in.kind == K::Jet ? 0.3f : 0.1f;                       // VLerp alpha (plane 0.3, car 0.1)
    smoothA_ = smoothA_ + (accel - smoothA_) * k;
    smoothAlpha_ = smoothAlpha_ + (alpha - smoothAlpha_) * k;

    // HoverFX / BoostFx state per form.
    const bool hoverOn = in.fxAllowed && (in.kind == K::Tank || in.hovering);
    const bool boostOn = in.fxAllowed && in.boostState;
    if (hoverOn && hover_.empty()) startSet(in, d_->hover, hover_);
    if (!hoverOn && !hover_.empty()) stopSet(hover_);
    if (boostOn && boost_.empty()) startSet(in, d_->boost, boost_);
    if (!boostOn && !boost_.empty()) stopSet(boost_);
    if (in.fxAllowed && in.jumpStart) startSet(in, d_->jump, jump_);
    if (in.formEnded) stopSet(jump_);                                      // OnEndPlay: Stop(JumpFX)
    if (in.kind == K::Truck) {
        if (in.nitroActive && ram_.empty() && in.fxAllowed) startSet(in, d_->ram, ram_);   // StartNitro
        if (!in.nitroActive && !ram_.empty()) stopSet(ram_);                               // StopNitro
    }
    for (auto& l : jump_) l.age += dt;
    jump_.erase(std::remove_if(jump_.begin(), jump_.end(), [](const Live& l) { return l.age > 3.0f; }), jump_.end());   // one-shots ran out
    follow(in, hover_); follow(in, boost_); follow(in, jump_); follow(in, ram_);

    // Driving.UpdateBoostFx (car / truck only): Color = lerp(EnergonColor, Yellow, n), alpha lerp(100, 255, n) / 255.
    if ((in.kind == K::Car || in.kind == K::Truck) && !boost_.empty() && rt_.setParam) {
        const float n = clamp01(in.normJumpRemaining);
        const float c[4] = {in.energon[0] + (1.0f - in.energon[0]) * n, in.energon[1] + (1.0f - in.energon[1]) * n,
                            in.energon[2] * (1.0f - n), (100.0f + 155.0f * n) / 255.0f};
        for (const Live& l : boost_) rt_.setParam(l.handle, "Color", c);
    }
    // Hovering.UpdateFx (car / truck / jet): per-socket Size, the largest is the audio BoosterAmount.
    float largest = -1.0f;
    if (in.kind != K::Tank && !hover_.empty()) {
        largest = 0.0f;
        const Vec3 bodyPos = col(in.body, 3);
        const Vec3 g = in.gravity * kUU;
        for (const Live& l : hover_) {
            core::Mat4 w;
            if (!socketWorld(in, l.e->socket, w)) continue;
            const Vec3 dir = core::normalize(col(w, 0)) * -1.0f;           // -SocketRotation (thrust direction)
            const Vec3 p = col(w, 3) - bodyPos;
            const float lin = clamp01(core::dot(smoothA_ - g, dir) / kLinearLimit);
            const float plen = core::length(p);
            const float ang = plen > 1e-5f ? clamp01(core::dot(core::cross(p * (1.0f / plen), dir), smoothAlpha_) / kAngularLimit) : 0.0f;
            const float amount = std::min(1.0f, lin + ang);
            const VehicleFxData::Socket& so = d_->sockets.at(l.e->socket);
            const float size[4] = {amount * so.scale[0], amount * so.scale[1], amount * so.scale[2], 1.0f};
            if (rt_.setParam) rt_.setParam(l.handle, "Size", size);
            largest = std::max(largest, std::sqrt(size[0] * size[0] + size[1] * size[1] + size[2] * size[2]));
        }
    }
    return largest;
}

void VehicleFxDriver::stopAll() {
    stopSet(hover_); stopSet(boost_); stopSet(jump_); stopSet(ram_);
    havePrev_ = false; smoothA_ = smoothAlpha_ = prevOmega_ = {0, 0, 0};
}

int VehicleFxDriver::live() const { return (int)(hover_.size() + boost_.size() + jump_.size() + ram_.size()); }

} // namespace game
