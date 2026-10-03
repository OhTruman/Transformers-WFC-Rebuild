// Authored map movers and runtime actor visibility for the baked world mesh (AssetTools a23c675
// streets_movers.json via tools/render/build_movers.py -> movers.json).
//
// world.glb bakes every placed actor at its load-time transform M0. A moving actor is drawn with the
// world-space delta D(t) = M(t) * M0^-1, computed in UE space (the UE3 FRotationMatrix convention and the
// UE -> glTF axis map reproduce every mover's world.glb node matrix exactly; build_movers.py validation):
//  * PHYS_Rotating (3 DecoSphere domes): actor rotator += RotationRate * t (2730 UU/s = 15 deg/s yaw).
//    [CONFIRMED authored rate; UE3 physRotation]
//  * SeqAct_Interp SkyBeam (3 actors, 9.0022 s, looping, started by SeqEvent_GameplayStarted):
//    InterpTrackMove IMF_RelativeToInitial: result = Relative * Initial (row vectors); bUseQuatInterpolation:
//    rotation = slerp between Euler keys (linear alpha); position = FInterpCurve CIM_CurveAuto Hermite.
//    [track CONFIRMED authored; UE3 evaluation rules HIGH]
//  * bHidden actors (4 objective bases) stay hidden unless Gameplay unhides them (SeqAct_ToggleHidden on
//    TnGameRules_SingleFlagCTF / TnGameRules_ScoreBombingRun) through setActorHidden().
#include "render/gl/WfcPipeline.h"
#include "assets/Json.h"
#include "core/Log.h"

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <sstream>

namespace render {
namespace wfc {

namespace {
const float kU2R = 3.14159265358979f / 32768.0f;   // UE rotation units -> radians

// UE3 FRotationMatrix as a column matrix (columns = local X, Y, Z axes in world), m[col*3+row].
void rotColumns(float pitch, float yaw, float roll, float out[9]) {
    float p = pitch * kU2R, y = yaw * kU2R, r = roll * kU2R;
    float SP = std::sin(p), CP = std::cos(p), SY = std::sin(y), CY = std::cos(y), SR = std::sin(r), CR = std::cos(r);
    const float rows[3][3] = {{CP * CY, CP * SY, SP},
                              {SR * SP * CY - CR * SY, SR * SP * SY + CR * CY, -SR * CP},
                              {-(CR * SP * CY + SR * SY), CY * SR - CR * SP * SY, CR * CP}};
    for (int c = 0; c < 3; ++c)
        for (int k = 0; k < 3; ++k) out[c * 3 + k] = rows[c][k];
}
void mul3(const float a[9], const float b[9], float o[9]) {      // o = a * b (column-major)
    float t[9];
    for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 3; ++r) t[c * 3 + r] = a[0 * 3 + r] * b[c * 3 + 0] + a[1 * 3 + r] * b[c * 3 + 1] + a[2 * 3 + r] * b[c * 3 + 2];
    std::copy(t, t + 9, o);
}
void transpose3(const float a[9], float o[9]) {
    for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 3; ++r) o[c * 3 + r] = a[r * 3 + c];
}
void quatFromMat(const float m[9], float q[4]) {                 // q = (x, y, z, w)
    float r00 = m[0], r11 = m[4], r22 = m[8], tr = r00 + r11 + r22;
    auto M = [&](int r, int c) { return m[c * 3 + r]; };
    if (tr > 0) {
        float s = std::sqrt(tr + 1.0f) * 2.0f;
        q[3] = 0.25f * s; q[0] = (M(2, 1) - M(1, 2)) / s; q[1] = (M(0, 2) - M(2, 0)) / s; q[2] = (M(1, 0) - M(0, 1)) / s;
    } else if (r00 > r11 && r00 > r22) {
        float s = std::sqrt(1.0f + r00 - r11 - r22) * 2.0f;
        q[3] = (M(2, 1) - M(1, 2)) / s; q[0] = 0.25f * s; q[1] = (M(0, 1) + M(1, 0)) / s; q[2] = (M(0, 2) + M(2, 0)) / s;
    } else if (r11 > r22) {
        float s = std::sqrt(1.0f + r11 - r00 - r22) * 2.0f;
        q[3] = (M(0, 2) - M(2, 0)) / s; q[0] = (M(0, 1) + M(1, 0)) / s; q[1] = 0.25f * s; q[2] = (M(1, 2) + M(2, 1)) / s;
    } else {
        float s = std::sqrt(1.0f + r22 - r00 - r11) * 2.0f;
        q[3] = (M(1, 0) - M(0, 1)) / s; q[0] = (M(0, 2) + M(2, 0)) / s; q[1] = (M(1, 2) + M(2, 1)) / s; q[2] = 0.25f * s;
    }
}
void matFromQuat(const float q[4], float m[9]) {
    float x = q[0], y = q[1], z = q[2], w = q[3];
    m[0] = 1 - 2 * (y * y + z * z); m[1] = 2 * (x * y + z * w); m[2] = 2 * (x * z - y * w);
    m[3] = 2 * (x * y - z * w); m[4] = 1 - 2 * (x * x + z * z); m[5] = 2 * (y * z + x * w);
    m[6] = 2 * (x * z + y * w); m[7] = 2 * (y * z - x * w); m[8] = 1 - 2 * (x * x + y * y);
}
void slerp(const float a[4], const float b0[4], float t, float o[4]) {   // shortest path (FQuat Slerp)
    float b[4] = {b0[0], b0[1], b0[2], b0[3]};
    float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    if (d < 0) { for (float& v : b) v = -v; d = -d; }
    float s0, s1;
    if (d < 0.9999f) { float om = std::acos(d), so = std::sin(om); s0 = std::sin((1 - t) * om) / so; s1 = std::sin(t * om) / so; }
    else { s0 = 1 - t; s1 = t; }
    float n = 0;
    for (int i = 0; i < 4; ++i) { o[i] = s0 * a[i] + s1 * b[i]; n += o[i] * o[i]; }
    n = std::sqrt(n);
    for (int i = 0; i < 4; ++i) o[i] /= n;
}
} // namespace

bool Pipeline::loadMovers(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    std::stringstream ss; ss << f.rdbuf();
    assets::Json J;
    if (!assets::Json::parse(ss.str(), J)) return false;
    auto lower = [](std::string s) { std::transform(s.begin(), s.end(), s.begin(), ::tolower); return s; };
    const assets::Json& rot = J["rotating"];
    for (size_t i = 0; i < rot.size(); ++i) {
        MoverRT m;
        m.actor = lower(rot[i]["actor"].asString());
        m.kind = 0;
        for (int k = 0; k < 3; ++k) {
            m.L[k] = rot[i]["location_ue"][(size_t)k].asFloat();
            m.rot0[k] = rot[i]["rotation_ue"][(size_t)k].asFloat();
            m.rate[k] = rot[i]["rate_ue"][(size_t)k].asFloat();
        }
        movers_.push_back(m);
    }
    const assets::Json& mt = J["matinee"];
    for (size_t i = 0; i < mt.size(); ++i) {
        const assets::Json& e = mt[i];
        MoverRT m;
        m.actor = lower(e["actor"].asString());
        m.kind = 1;
        m.length = e["length_s"].asFloat(9.0f);
        m.looping = e["looping"].asBool(true);
        for (int k = 0; k < 3; ++k) {
            m.L[k] = e["location_ue"][(size_t)k].asFloat();
            m.rot0[k] = e["rotation_ue"][(size_t)k].asFloat();
        }
        const assets::Json& ek = e["euler_keys_deg"];
        for (size_t j = 0; j < ek.size(); ++j) {
            MoverRT::RotKey rk;
            rk.t = ek[j]["t"].asFloat();
            // FRotator::MakeFromEuler(X roll, Y pitch, Z yaw) in degrees
            float roll = ek[j]["v"][0].asFloat(), pitch = ek[j]["v"][1].asFloat(), yaw = ek[j]["v"][2].asFloat();
            float A[9];
            const float d2u = 65536.0f / 360.0f;
            rotColumns(pitch * d2u, yaw * d2u, roll * d2u, A);
            quatFromMat(A, rk.q);
            m.rotKeys.push_back(rk);
        }
        const assets::Json& pk = e["pos_keys"];
        for (size_t j = 0; j < pk.size(); ++j) {
            MoverRT::PosKey p;
            p.t = pk[j]["t"].asFloat();
            for (int k = 0; k < 3; ++k) {
                p.v[k] = pk[j]["v"][(size_t)k].asFloat();
                p.arrive[k] = pk[j]["arrive"][(size_t)k].asFloat();
                p.leave[k] = pk[j]["leave"][(size_t)k].asFloat();
            }
            m.posKeys.push_back(p);
        }
        movers_.push_back(m);
    }
    const assets::Json& hid = J["hidden"];
    for (size_t i = 0; i < hid.size(); ++i) authoredHiddenActors_.insert(lower(hid[i]["actor"].asString()));
    // Kismet: SeqCond_GameRuleActive (ScoreBombingRun "EXT" / SingleFlagCTF "CTF") -> SeqAct_ToggleHidden UnHide
    std::vector<std::string> unhide;
    const assets::Json& ur = J["hidden_unhide_rules"]["rules"];
    for (size_t i = 0; i < ur.size(); ++i) unhide.push_back(ur[i].asString());
    for (const std::string& a : authoredHiddenActors_) ruleGatedActors_[a] = unhide;
    // TnDominationPoint energon totems: Conquest only (TnGameRules_ScoreDomination)
    for (const char* a : {"tndominationpoint_15247", "tndominationpoint_6703", "tndominationpoint_678"})
        ruleGatedActors_[a] = {"TransGame.TnGameRules_ScoreDomination"};
    LOG_INFO("wfc: movers: %zu (rotating + Matinee), %zu authored-hidden actors", movers_.size(), authoredHiddenActors_.size());
    return true;
}

// Delta for one mover at time t: x' = L + o + B (x - L) in UE space, returned in glTF space.
core::Mat4 Pipeline::moverDelta(const MoverRT& m, float t) const {
    float A0[9], A0t[9], B[9];
    rotColumns(m.rot0[0], m.rot0[1], m.rot0[2], A0);
    transpose3(A0, A0t);
    float o[3] = {0, 0, 0};
    if (m.kind == 0) {
        float At[9];
        rotColumns(m.rot0[0] + m.rate[0] * t, m.rot0[1] + m.rate[1] * t, m.rot0[2] + m.rate[2] * t, At);
        mul3(At, A0t, B);
    } else {
        float tau = m.looping && m.length > 0 ? std::fmod(t, m.length) : std::min(t, m.length);
        // rotation: slerp between the bracketing Euler keys (UE3 bUseQuatInterpolation)
        float q[4] = {0, 0, 0, 1};
        const auto& K = m.rotKeys;
        if (!K.empty()) {
            if (tau <= K.front().t) std::copy(K.front().q, K.front().q + 4, q);
            else if (tau >= K.back().t) std::copy(K.back().q, K.back().q + 4, q);
            else {
                size_t i = 1;
                while (i < K.size() && tau >= K[i].t) ++i;
                float a = (tau - K[i - 1].t) / std::max(K[i].t - K[i - 1].t, 1e-6f);
                slerp(K[i - 1].q, K[i].q, a, q);
            }
        }
        float Arel[9], tmp[9];
        matFromQuat(q, Arel);
        mul3(A0, Arel, tmp);
        mul3(tmp, A0t, B);
        // position: FInterpCurve (CIM_CurveAuto keys: Hermite with the authored tangents), actor-local
        float p[3] = {0, 0, 0};
        const auto& P = m.posKeys;
        if (!P.empty()) {
            if (tau <= P.front().t) std::copy(P.front().v, P.front().v + 3, p);
            else if (tau >= P.back().t) std::copy(P.back().v, P.back().v + 3, p);
            else {
                size_t i = 1;
                while (i < P.size() && tau >= P[i].t) ++i;
                const auto& k0 = P[i - 1];
                const auto& k1 = P[i];
                float diff = k1.t - k0.t, a = (tau - k0.t) / std::max(diff, 1e-6f);
                float a2 = a * a, a3 = a2 * a;
                float h00 = 2 * a3 - 3 * a2 + 1, h10 = a3 - 2 * a2 + a, h01 = -2 * a3 + 3 * a2, h11 = a3 - a2;
                for (int c = 0; c < 3; ++c)
                    p[c] = h00 * k0.v[c] + h10 * k0.leave[c] * diff + h01 * k1.v[c] + h11 * k1.arrive[c] * diff;
            }
        }
        for (int r = 0; r < 3; ++r) o[r] = A0[0 * 3 + r] * p[0] + A0[1 * 3 + r] * p[1] + A0[2 * 3 + r] * p[2];
    }
    // UE -> glTF: P(x) = (x0, x2, x1) / 100 ; B_g = C B C ; t_g = P(L + o) - B_g P(L)
    auto Cidx = [](int i) { return i == 0 ? 0 : (i == 1 ? 2 : 1); };
    float Bg[9];
    for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 3; ++r) Bg[c * 3 + r] = B[Cidx(c) * 3 + Cidx(r)];
    float Lg[3] = {m.L[0] * 0.01f, m.L[2] * 0.01f, m.L[1] * 0.01f};
    float Lo[3] = {(m.L[0] + o[0]) * 0.01f, (m.L[2] + o[2]) * 0.01f, (m.L[1] + o[1]) * 0.01f};
    core::Mat4 D = core::Mat4::identity();
    for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 3; ++r) D.m[c * 4 + r] = Bg[c * 3 + r];
    for (int r = 0; r < 3; ++r)
        D.m[12 + r] = Lo[r] - (Bg[0 * 3 + r] * Lg[0] + Bg[1 * 3 + r] * Lg[1] + Bg[2 * 3 + r] * Lg[2]);
    return D;
}

void Pipeline::updateMovers() {
    static const bool frozen = std::getenv("WFC_NOMOVERS") != nullptr;
    moverDelta_.clear();
    if (frozen) return;
    for (const MoverRT& m : movers_) moverDelta_[m.actor] = moverDelta(m, mapTime());   // Gameplay's map clock
}

void Pipeline::setActorHidden(const std::string& actor, bool hidden) {
    std::string a = actor.substr(actor.rfind('.') == std::string::npos ? 0 : actor.rfind('.') + 1);   // full path or name
    std::transform(a.begin(), a.end(), a.begin(), ::tolower);
    actorHidden_[a] = hidden;
}

bool Pipeline::actorHidden(const std::string& actorLower) const {
    auto it = actorHidden_.find(actorLower);
    if (it != actorHidden_.end()) return it->second;           // explicit Gameplay state
    static const bool show = std::getenv("WFC_SHOWHIDDEN") != nullptr;
    if (show) return false;
    auto g = ruleGatedActors_.find(actorLower);
    if (g != ruleGatedActors_.end()) {                        // shown only while one of its game rules is active
        for (const std::string& r : g->second) if (ruleActive(r)) return false;
        return true;
    }
    return authoredHiddenActors_.count(actorLower) > 0;
}

} // namespace wfc
} // namespace render
