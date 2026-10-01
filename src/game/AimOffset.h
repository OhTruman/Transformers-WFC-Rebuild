// Clean-room reconstruction — upper-body aim offset (Robot_ANIMTREE TnAnimNodeAimOffset_14979).
//
// [CONF] Profile "Default" (the gun profile): 11 AimComponents, each a 3x3 grid of rotations
// (L/C/R x U/C/D) baked from Shooting_Aim_{L,F,R}_{U,C,D}; HorizontalRange [-1,1], VerticalRange
// [-1,0.8]; RemapPawnAimRange with PawnAimOffsetRange H [-1,0.85] / V [-0.7,1].
// Verified against robot.glb: each rotation is a MESH-SPACE increment applied about the bone in
// hierarchy order (children inherit), e.g. Spine01 CU = the F_U vs F_C mesh delta exactly.
// UE -> glTF quaternion mapping (axis swap Y<->Z, handedness flip): (x,y,z,w) -> (-x,-z,-y,w).
// Tree order: locomotion -> AimOffset -> UpperBodyCustom slot (reload) -> skel controls.
#pragma once
#include <cmath>
#include <string>
#include <vector>
#include "assets/SkinnedModel.h"
#include "core/Math.h"

namespace game {

struct AimComponentDef { const char* bone; core::Quat q[9]; };   // LU LC LD CU CC CD RU RC RD

const AimComponentDef kAimDefault[] = {
    {"C_Spine01_Lumbar01_XB", {{0.017127f, 0.107592f, 0.092798f, 0.989707f},
        {-0.001596f, 0.090108f, 0.007515f, 0.995902f},
        {0.011837f, 0.144863f, 0.017933f, 0.989218f},
        {0.000000f, 0.000000f, 0.092576f, 0.995706f},
        {-0.000000f, -0.000000f, -0.000000f, 1.000000f},
        {0.019691f, -0.002652f, -0.005409f, 0.999788f},
        {-0.028262f, -0.101596f, 0.089489f, 0.990390f},
        {-0.018742f, -0.093375f, 0.016361f, 0.995320f},
        {0.023593f, -0.037636f, 0.002801f, 0.999009f}}},
    {"C_Spine02_Lumbar02_XB", {{0.129923f, 0.221524f, 0.215539f, 0.942120f},
        {0.014414f, 0.187576f, 0.019278f, 0.981955f},
        {0.057684f, 0.148044f, -0.125377f, 0.979304f},
        {0.000000f, -0.000000f, 0.228936f, 0.973442f},
        {-0.000000f, -0.000000f, -0.000000f, 1.000000f},
        {0.017115f, 0.024203f, -0.130970f, 0.990943f},
        {-0.121163f, -0.213890f, 0.233649f, 0.940733f},
        {-0.012910f, -0.224225f, 0.019697f, 0.974253f},
        {0.019026f, -0.122921f, -0.118640f, 0.985116f}}},
    {"C_Spine03_Neck01_XB", {{0.001337f, 0.271903f, -0.013963f, 0.962222f},
        {0.100204f, 0.409925f, -0.103076f, 0.900720f},
        {-0.010725f, 0.164614f, -0.109477f, 0.980205f},
        {-0.000000f, -0.000000f, -0.000000f, 1.000000f},
        {-0.000000f, -0.000000f, -0.000000f, 1.000000f},
        {-0.013695f, 0.027463f, -0.082489f, 0.996119f},
        {-0.037680f, -0.113961f, -0.038033f, 0.992042f},
        {-0.067467f, -0.250967f, -0.079290f, 0.962381f},
        {-0.053959f, -0.129192f, -0.098262f, 0.985263f}}},
    {"C_Spine04_Head_XB", {{0.245083f, 0.071015f, 0.083155f, 0.963315f},
        {-0.000000f, -0.000000f, -0.000000f, 1.000000f},
        {-0.000000f, -0.000000f, -0.000000f, 1.000000f},
        {-0.018467f, 0.003537f, 0.237908f, 0.971106f},
        {-0.000000f, -0.000000f, -0.000000f, 1.000000f},
        {-0.000000f, -0.000000f, -0.000000f, 1.000000f},
        {-0.212749f, 0.004535f, 0.132624f, 0.968054f},
        {-0.000000f, -0.000000f, -0.000000f, 1.000000f},
        {-0.000000f, -0.000000f, -0.000000f, 1.000000f}}},
    {"L_Arm01_Clav_XB", {{-0.003157f, 0.040679f, 0.054258f, 0.997693f},
        {0.003683f, 0.065540f, -0.047695f, 0.996703f},
        {0.038604f, 0.066272f, -0.007142f, 0.997029f},
        {-0.035184f, 0.044647f, 0.037114f, 0.997693f},
        {-0.000000f, -0.000000f, -0.000000f, 1.000000f},
        {0.031504f, 0.029308f, -0.023257f, 0.998803f},
        {-0.058972f, -0.029361f, -0.005885f, 0.997810f},
        {-0.006894f, -0.008978f, -0.000634f, 0.999936f},
        {0.017643f, 0.042428f, -0.018836f, 0.998766f}}},
    {"L_Arm02_Shoulder_XB", {{-0.045090f, -0.201298f, -0.116148f, 0.971574f},
        {-0.013622f, 0.016905f, -0.006307f, 0.999744f},
        {-0.051438f, 0.116739f, 0.007355f, 0.991803f},
        {0.030042f, -0.209468f, -0.106135f, 0.971574f},
        {-0.000000f, -0.000000f, -0.000000f, 1.000000f},
        {-0.021652f, 0.012833f, -0.027884f, 0.999294f},
        {0.135545f, -0.227783f, -0.077467f, 0.961115f},
        {0.013270f, -0.034188f, 0.023925f, 0.999041f},
        {-0.059297f, -0.007298f, -0.102796f, 0.992907f}}},
    {"L_Arm03_Elbow_XB", {{-0.129282f, 0.091804f, -0.070024f, 0.984863f},
        {-0.023994f, 0.033130f, -0.014915f, 0.999052f},
        {0.012509f, -0.024493f, 0.006182f, 0.999603f},
        {-0.049880f, 0.086273f, -0.141824f, 0.984863f},
        {-0.000000f, -0.000000f, -0.000000f, 1.000000f},
        {-0.000068f, -0.022857f, 0.016497f, 0.999603f},
        {0.080366f, 0.078595f, -0.131945f, 0.984863f},
        {0.007714f, 0.023702f, -0.023926f, 0.999403f},
        {-0.024544f, -0.053500f, 0.055176f, 0.996740f}}},
    {"L_Arm04_Hand_XB", {{-0.045723f, 0.001227f, -0.027775f, 0.998567f},
        {-0.017059f, -0.007288f, -0.002841f, 0.999824f},
        {0.022318f, -0.046414f, 0.005402f, 0.998658f},
        {-0.017224f, -0.000867f, -0.050657f, 0.998567f},
        {-0.000000f, -0.000000f, -0.000000f, 1.000000f},
        {0.003278f, -0.044695f, 0.025944f, 0.998658f},
        {0.026272f, -0.003515f, -0.046486f, 0.998567f},
        {0.036366f, 0.120064f, -0.017252f, 0.991950f},
        {-0.020894f, -0.043370f, 0.014218f, 0.998739f}}},
    {"R_Arm01_Clav_XB", {{0.158000f, 0.253953f, 0.145126f, 0.943124f},
        {0.067022f, 0.429943f, -0.078785f, 0.896911f},
        {-0.119126f, 0.469350f, -0.086425f, 0.870661f},
        {0.047951f, 0.084322f, 0.234619f, 0.967236f},
        {-0.000000f, -0.000000f, -0.000000f, 1.000000f},
        {0.025578f, -0.009007f, -0.251494f, 0.967479f},
        {-0.179640f, -0.063472f, 0.193365f, 0.962451f},
        {0.093018f, -0.305060f, 0.037947f, 0.947020f},
        {0.086639f, -0.212937f, -0.084016f, 0.969584f}}},
    {"R_Arm02_Shoulder_XB", {{0.032507f, 0.162743f, -0.046129f, 0.985053f},
        {0.031523f, 0.082986f, -0.040155f, 0.995242f},
        {0.070832f, 0.114433f, -0.029378f, 0.990467f},
        {-0.000000f, -0.000000f, -0.000000f, 1.000000f},
        {-0.000000f, -0.000000f, -0.000000f, 1.000000f},
        {-0.000000f, -0.000000f, -0.000000f, 1.000000f},
        {-0.000000f, -0.000000f, -0.000000f, 1.000000f},
        {-0.005125f, -0.091496f, 0.087210f, 0.991966f},
        {0.003756f, -0.117117f, 0.047673f, 0.991966f}}},
    {"R_Arm03_Elbow_XB", {{0.104208f, 0.006689f, 0.071362f, 0.991970f},
        {-0.068864f, -0.020682f, -0.008409f, 0.997376f},
        {-0.190950f, -0.061951f, 0.051824f, 0.978271f},
        {-0.049435f, 0.014556f, 0.115503f, 0.991970f},
        {-0.000000f, -0.000000f, -0.000000f, 1.000000f},
        {-0.000000f, -0.000000f, -0.000000f, 1.000000f},
        {-0.124540f, 0.016390f, 0.014755f, 0.991970f},
        {-0.000000f, -0.000000f, -0.000000f, 1.000000f},
        {0.226212f, -0.134421f, -0.198906f, 0.944031f}}},
};

class AimOffset {
public:
    void bind(const assets::SkinnedModel& m) {
        model_ = &m;
        nodes_.clear();
        for (const AimComponentDef& c : kAimDefault) nodes_.push_back(m.nodeByName(c.bone));
    }
    bool bound(const assets::SkinnedModel& m) const { return model_ == &m; }

    // Pawn aim (UE3 convention: 1.0 = 90 deg). Remapped through the profile's pawn range.
    static float remap(float v, float pawnLo, float pawnHi, float lo, float hi) {
        v = core::clampf(v, pawnLo, pawnHi);
        return v >= 0.0f ? (pawnHi > 0 ? v / pawnHi * hi : 0.0f) : (pawnLo < 0 ? v / pawnLo * lo : 0.0f);
    }
    static core::Vec3 profileAim(float pawnX, float pawnY) {
        return {remap(pawnX, -1.0f, 0.85f, -1.0f, 1.0f), remap(pawnY, -0.7f, 1.0f, -1.0f, 0.8f), 0.0f};
    }

    // Apply to a local pose (weight 0..1) for profile aim (x: -1 left .. 1 right, y: -1 down .. 1 up).
    void apply(assets::LocalPose& P, float ax, float ay, float weight) const {
        if (!model_ || weight <= 0.0f) return;
        for (size_t i = 0; i < nodes_.size(); ++i) {
            int b = nodes_[i];
            if (b < 0 || (size_t)b >= P.r.size()) continue;
            core::Quat q = sample(kAimDefault[i], ax, ay);
            if (weight < 1.0f) q = nlerp(core::Quat{}, q, weight);
            // Mesh-space increment: L' = inv(Pg) * Q * Pg * L, with Pg the (already aimed) parent.
            core::Quat pg = parentGlobal(P, b);
            P.r[(size_t)b] = norm(mul(conj(pg), mul(q, mul(pg, P.r[(size_t)b]))));
        }
    }

private:
    static core::Quat mul(const core::Quat& a, const core::Quat& b) {
        return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
    }
    static core::Quat conj(const core::Quat& q) { return {-q.x, -q.y, -q.z, q.w}; }
    static core::Quat norm(core::Quat q) {
        float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
        if (n > 1e-8f) { q.x /= n; q.y /= n; q.z /= n; q.w /= n; }
        return q;
    }
    static core::Quat nlerp(core::Quat a, core::Quat b, float t) {
        if (a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0) { b.x = -b.x; b.y = -b.y; b.z = -b.z; b.w = -b.w; }
        return norm({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t});
    }
    // UE3 AimOffset grid: blend the horizontal row pair, then the vertical pair.
    static core::Quat sample(const AimComponentDef& c, float x, float y) {
        x = core::clampf(x, -1.0f, 1.0f); y = core::clampf(y, -1.0f, 1.0f);
        auto row = [&](int u) {   // u: 0 = U, 1 = C, 2 = D
            const core::Quat& L = c.q[0 + u]; const core::Quat& C = c.q[3 + u]; const core::Quat& R = c.q[6 + u];
            return x < 0.0f ? nlerp(C, L, -x) : nlerp(C, R, x);
        };
        core::Quat mid = row(1);
        return y > 0.0f ? nlerp(mid, row(0), y) : nlerp(mid, row(2), -y);
    }
    core::Quat parentGlobal(const assets::LocalPose& P, int b) const {
        core::Quat g{};
        std::vector<int> chain;
        for (int p = model_->nodes[(size_t)b].parent; p >= 0; p = model_->nodes[(size_t)p].parent) chain.push_back(p);
        for (size_t i = chain.size(); i-- > 0;) g = mul(g, P.r[(size_t)chain[i]]);
        return g;
    }

    const assets::SkinnedModel* model_ = nullptr;
    std::vector<int> nodes_;
};

} // namespace game
