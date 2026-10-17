// [Gameplay] Robot skeletal gib ragdolls: see GibRagdoll.h.
#include "game/GibRagdoll.h"

#include <algorithm>
#include <cmath>
#include "assets/SkinnedModel.h"
#include "game/Collision.h"

namespace game {

#include "game/GibRagdollTable.inc"

const GibBodyDef* gibBodies(int gib, size_t& n) {
    if (gib == 1) { n = sizeof(kLowBody2Bodies) / sizeof(kLowBody2Bodies[0]); return kLowBody2Bodies; }
    n = sizeof(kBodyBodies) / sizeof(kBodyBodies[0]); return kBodyBodies;
}
const GibJointDef* gibJoints(int gib, size_t& n) {
    if (gib == 1) { n = sizeof(kLowBody2Joints) / sizeof(kLowBody2Joints[0]); return kLowBody2Joints; }
    n = sizeof(kBodyJoints) / sizeof(kBodyJoints[0]); return kBodyJoints;
}
const char* gibMeshPath(int gib) {
    return gib == 1 ? "/../content/TR_Death_Gibs_p/Skeletal/GIB_LowBody2_SKEL.gltf" : "/../content/TR_Death_Gibs_p/Skeletal/GIB_Body_SKEL.gltf";
}

namespace {
// [PROV] solver constants: UE3's default PhysicalMaterial (friction 0.7, restitution 0.3) and RB damping (linear 0.01, angular 0.05);
// gravity as the rebuild's other gib chunks (9.8 m/s^2); 8 substeps of the 60 Hz step.
constexpr float kGravity = 9.8f, kMuS = 0.7f, kMuD = 0.7f, kRestitution = 0.3f, kLinDamp = 0.01f, kAngDamp = 0.05f;
constexpr int kSubsteps = 8;
constexpr float kLockedRad = 0.5f * 0.0174533f;   // a limit authored 0: locked (a small tolerance keeps the solver stable)
constexpr float kSoftScale = 0.3f;                // limits with an authored stiffness / damping (neck, head): a partial correction

core::Quat qconj(const core::Quat& q) { return core::Quat{-q.x, -q.y, -q.z, q.w}; }
core::Quat qnorm(core::Quat q) {
    const float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (l < 1e-9f) return core::Quat{};
    const float k = 1.0f / l; q.x *= k; q.y *= k; q.z *= k; q.w *= k; return q;
}
core::Vec3 qrot(const core::Quat& q, const core::Vec3& v) { return assets::quatRotate(q, v); }
core::Vec3 qrotInv(const core::Quat& q, const core::Vec3& v) { return assets::quatRotate(qconj(q), v); }
// q <- q rotated by the world-space rotation vector `phi` (small-angle update, then normalised).
core::Quat qapply(const core::Quat& q, const core::Vec3& phi) {
    const core::Quat d = assets::quatMul(core::Quat{phi.x, phi.y, phi.z, 0.0f}, q);
    return qnorm(core::Quat{q.x + 0.5f * d.x, q.y + 0.5f * d.y, q.z + 0.5f * d.z, q.w + 0.5f * d.w});
}
// Rotation vector (axis * angle) of a unit quaternion, angle in [0, pi].
core::Vec3 qlog(core::Quat q) {
    if (q.w < 0.0f) { q.x = -q.x; q.y = -q.y; q.z = -q.z; q.w = -q.w; }
    const float s = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
    if (s < 1e-7f) return core::Vec3{2.0f * q.x, 2.0f * q.y, 2.0f * q.z};
    const float a = 2.0f * std::atan2(s, q.w);
    return core::Vec3{q.x, q.y, q.z} * (a / s);
}
core::Quat qexp(const core::Vec3& v) {
    const float a = core::length(v);
    return a < 1e-7f ? core::Quat{} : assets::quatAxisAngle(v * (1.0f / a), a);
}
// Rotation of an orthonormalised 3x3 (column-major Mat4) as a quaternion.
core::Quat quatFromMat(const core::Mat4& M) {
    core::Vec3 c0 = core::normalize(core::Vec3{M.m[0], M.m[1], M.m[2]});
    core::Vec3 c1 = core::normalize(core::Vec3{M.m[4], M.m[5], M.m[6]});
    const core::Vec3 c2 = core::normalize(core::cross(c0, c1));
    c1 = core::cross(c2, c0);
    const float r00 = c0.x, r11 = c1.y, r22 = c2.z, tr = r00 + r11 + r22;
    core::Quat q;
    if (tr > 0.0f) {
        const float s = std::sqrt(tr + 1.0f) * 2.0f;
        q.w = 0.25f * s; q.x = (c1.z - c2.y) / s; q.y = (c2.x - c0.z) / s; q.z = (c0.y - c1.x) / s;
    } else if (r00 > r11 && r00 > r22) {
        const float s = std::sqrt(1.0f + r00 - r11 - r22) * 2.0f;
        q.w = (c1.z - c2.y) / s; q.x = 0.25f * s; q.y = (c1.x + c0.y) / s; q.z = (c2.x + c0.z) / s;
    } else if (r11 > r22) {
        const float s = std::sqrt(1.0f + r11 - r00 - r22) * 2.0f;
        q.w = (c2.x - c0.z) / s; q.x = (c1.x + c0.y) / s; q.y = 0.25f * s; q.z = (c2.y + c1.z) / s;
    } else {
        const float s = std::sqrt(1.0f + r22 - r00 - r11) * 2.0f;
        q.w = (c0.y - c1.x) / s; q.x = (c2.x + c0.z) / s; q.y = (c2.y + c1.z) / s; q.z = 0.25f * s;
    }
    return qnorm(q);
}
// Inverse of a rigid (rotation + translation) matrix.
core::Mat4 rigidInverse(const core::Mat4& M) {
    core::Mat4 r;
    for (int c = 0; c < 3; ++c) for (int k = 0; k < 3; ++k) r.m[c * 4 + k] = M.m[k * 4 + c];
    const core::Vec3 t{M.m[12], M.m[13], M.m[14]};
    r.m[12] = -(r.m[0] * t.x + r.m[4] * t.y + r.m[8] * t.z);
    r.m[13] = -(r.m[1] * t.x + r.m[5] * t.y + r.m[9] * t.z);
    r.m[14] = -(r.m[2] * t.x + r.m[6] * t.y + r.m[10] * t.z);
    return r;
}
bool finite3(const core::Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
} // namespace

bool GibRagdoll::init(const assets::SkinnedModel& model, const core::Mat4& world0, const GibBodyDef* bodies, size_t nBodies,
                      const GibJointDef* joints, size_t nJoints) {
    bodies_.clear(); joints_.clear(); follow_.clear(); contacts_.clear(); asleep_ = false; quiet_ = 0.0f;
    nodeCount_ = model.nodes.size();
    // The reference pose's world transforms (TnSkeletalRigidBody: SetSkeletalMesh rebuilds SpaceBases from the ref pose).
    std::vector<core::Mat4> W(nodeCount_);
    std::vector<char> done(nodeCount_, 0);
    for (size_t i = 0; i < nodeCount_; ++i) {
        std::vector<int> chain;
        for (int n = (int)i; n >= 0 && !done[(size_t)n]; n = model.nodes[(size_t)n].parent) chain.push_back(n);
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            const assets::Node& nd = model.nodes[(size_t)*it];
            const core::Mat4 local = core::mat4FromTRS(nd.t, nd.r, nd.s);
            W[(size_t)*it] = (nd.parent >= 0 ? W[(size_t)nd.parent] : world0) * local;
            done[(size_t)*it] = 1;
        }
    }
    std::vector<int> bodyOfNode(nodeCount_, -1);
    for (size_t b = 0; b < nBodies; ++b) {
        const int node = model.nodeByName(bodies[b].bone);
        if (node < 0) { bodies_.clear(); return false; }
        Body B;
        B.node = node;
        B.q = quatFromMat(W[(size_t)node]);
        const core::Vec3 origin{W[(size_t)node].m[12], W[(size_t)node].m[13], W[(size_t)node].m[14]};
        // Mass: uniform density over the shapes' volumes; the centre of mass is the volume-weighted shape centre.
        float vol = 0.0f; core::Vec3 com{0, 0, 0}; float ext = 0.0f;
        for (int s = 0; s < bodies[b].nShapes; ++s) {
            const GibShapeDef& S = bodies[b].shapes[s];
            const float v = 4.18879f * S.r * S.r * S.r + (S.kind == 1 ? 3.14159f * S.r * S.r * 2.0f * S.halfLen : 0.0f);
            vol += v; com += S.c * v;
        }
        com = vol > 0.0f ? com * (1.0f / vol) : core::Vec3{0, 0, 0};
        B.com = com;
        for (int s = 0; s < bodies[b].nShapes; ++s) {   // contact spheres: a sphere, or a capsule's two caps and its middle
            const GibShapeDef& S = bodies[b].shapes[s];
            const core::Vec3 c = S.c - com;
            B.spheres.push_back({c, S.r});
            if (S.kind == 1) { B.spheres.push_back({c + S.axis * S.halfLen, S.r}); B.spheres.push_back({c - S.axis * S.halfLen, S.r}); }
            ext = std::max(ext, core::length(c) + S.r + S.halfLen);
        }
        const float mass = std::max(1.0f, vol * 1000.0f);
        B.invM = 1.0f / mass;
        B.ext = std::max(ext, 0.05f);
        B.invI = 1.0f / (0.4f * mass * B.ext * B.ext);   // isotropic: a solid sphere of the body's extent [PROV]
        B.x = origin + qrot(B.q, com); B.xPrev = B.x; B.qPrev = B.q;
        bodyOfNode[(size_t)node] = (int)bodies_.size();
        bodies_.push_back(B);
    }
    if (bodies_.empty()) return false;
    // Every node follows a body rigidly from the reference pose: its own, else its nearest body ancestor, else the first body.
    follow_.resize(nodeCount_);
    for (size_t i = 0; i < nodeCount_; ++i) {
        int b = -1;
        for (int n = (int)i; n >= 0 && b < 0; n = model.nodes[(size_t)n].parent) b = bodyOfNode[(size_t)n];
        if (b < 0) b = 0;
        const Body& B = bodies_[(size_t)b];
        const core::Mat4 boneRest = core::mat4FromTRS(B.x - qrot(B.q, B.com), B.q, core::Vec3{1, 1, 1});
        follow_[i].body = b;
        follow_[i].rel = rigidInverse(boneRest) * W[i];
    }
    for (size_t j = 0; j < nJoints; ++j) {
        const int cn = model.nodeByName(joints[j].child), pn = model.nodeByName(joints[j].parent);
        if (cn < 0 || pn < 0 || bodyOfNode[(size_t)cn] < 0 || bodyOfNode[(size_t)pn] < 0) continue;
        Joint J;
        J.child = bodyOfNode[(size_t)cn]; J.parent = bodyOfNode[(size_t)pn];
        const Body& C = bodies_[(size_t)J.child];
        const Body& P = bodies_[(size_t)J.parent];
        const core::Vec3 o{W[(size_t)cn].m[12], W[(size_t)cn].m[13], W[(size_t)cn].m[14]};   // the joint: the child bone origin
        J.aC = qrotInv(C.q, o - C.x);
        J.aP = qrotInv(P.q, o - P.x);
        J.rel0 = assets::quatMul(qconj(P.q), C.q);
        J.tA = core::normalize(joints[j].twistAxis);
        J.sA = core::normalize(joints[j].swingAxis - J.tA * core::dot(joints[j].swingAxis, J.tA));
        J.tB = core::cross(J.tA, J.sA);
        J.s1 = std::max(kLockedRad, joints[j].swing1 * 0.0174533f);
        J.s2 = std::max(kLockedRad, joints[j].swing2 * 0.0174533f);
        J.tw = std::max(kLockedRad, joints[j].twist * 0.0174533f);
        J.soft = joints[j].soft != 0;
        joints_.push_back(J);
    }
    return true;
}

// Plane contacts for the step: each contact sphere's floor below it (groundHeight) and the first wall between the body's centre of
// mass and the sphere's far side (segmentHit), kept when within the sphere radius plus the step's travel.
void GibRagdoll::collide(const CollisionWorld& col, float dt) {
    contacts_.clear();
    for (size_t b = 0; b < bodies_.size(); ++b) {
        const Body& B = bodies_[b];
        const float travel = core::length(B.v) * dt + B.ext * core::length(B.w) * dt + 0.05f;
        for (size_t s = 0; s < B.spheres.size(); ++s) {
            const core::Vec3 p = B.x + qrot(B.q, B.spheres[s].c);
            const float r = B.spheres[s].r;
            float gy; core::Vec3 n;
            if (col.groundHeight(p.x, p.z, p.y + r, 0.0f, gy, n)) {
                if (n.y < 0.0f) n = n * -1.0f;
                if (n.y > 0.05f && (p.y - gy) * n.y < r + travel) contacts_.push_back({(int)b, (int)s, core::Vec3{p.x, gy, p.z}, n});
            }
            const core::Vec3 d = p - B.x;
            const float dl = core::length(d);
            if (dl < 1e-3f) continue;
            const core::Vec3 dir = d * (1.0f / dl);
            const core::Vec3 far = p + dir * (r + travel);
            float t; core::Vec3 wn;
            if (col.segmentHit(B.x, far, t, wn)) {
                if (core::dot(wn, dir) > 0.0f) wn = wn * -1.0f;
                if (wn.y > 0.7f) continue;   // a floor: the ground contact covers it
                contacts_.push_back({(int)b, (int)s, B.x + (far - B.x) * t, core::normalize(wn)});
            }
        }
    }
}

void GibRagdoll::solveJoints(float h) {
    (void)h;
    for (Joint& J : joints_) {
        Body& C = bodies_[(size_t)J.child];
        Body& P = bodies_[(size_t)J.parent];
        {   // Positional: the anchors coincide (a ball joint; the limits below shape it).
            const core::Vec3 rC = qrot(C.q, J.aC), rP = qrot(P.q, J.aP);
            const core::Vec3 dlt = (P.x + rP) - (C.x + rC);
            const float c = core::length(dlt);
            if (c > 1e-6f) {
                const core::Vec3 n = dlt * (1.0f / c);
                const core::Vec3 cC = core::cross(rC, n), cP = core::cross(rP, n);
                const float w = C.invM + C.invI * core::dot(cC, cC) + P.invM + P.invI * core::dot(cP, cP);
                const float lam = c / w;
                C.x += n * (lam * C.invM); C.q = qapply(C.q, cC * (lam * C.invI));
                P.x -= n * (lam * P.invM); P.q = qapply(P.q, cP * (-lam * P.invI));
            }
        }
        {   // Angular: swing (elliptical cone: swing1 about the third axis, swing2 about the swing axis) and twist limits.
            const core::Quat rel = assets::quatMul(qconj(P.q), C.q);
            core::Quat d = assets::quatMul(qconj(J.rel0), rel);
            if (d.w < 0.0f) { d.x = -d.x; d.y = -d.y; d.z = -d.z; d.w = -d.w; }
            const float proj = d.x * J.tA.x + d.y * J.tA.y + d.z * J.tA.z;
            core::Quat tw = qnorm(core::Quat{J.tA.x * proj, J.tA.y * proj, J.tA.z * proj, d.w});
            const float twA = 2.0f * std::atan2(proj, d.w);
            const core::Vec3 sv = qlog(assets::quatMul(d, qconj(tw)));
            float c1 = core::dot(sv, J.tB), c2 = core::dot(sv, J.sA);
            const float e = (c1 / J.s1) * (c1 / J.s1) + (c2 / J.s2) * (c2 / J.s2);
            const float twC = core::clampf(twA, -J.tw, J.tw);
            if (e <= 1.0f && twC == twA) continue;
            if (e > 1.0f) { const float k = 1.0f / std::sqrt(e); c1 *= k; c2 *= k; }
            const core::Quat dd = assets::quatMul(qexp(J.tB * c1 + J.sA * c2), assets::quatAxisAngle(J.tA, twC));
            const core::Quat want = assets::quatMul(assets::quatMul(P.q, J.rel0), dd);
            core::Vec3 phi = qlog(assets::quatMul(want, qconj(C.q)));   // world rotation that takes the child to the limit
            if (J.soft) phi = phi * kSoftScale;
            const float ws = C.invI + P.invI;
            if (ws <= 0.0f) continue;
            C.q = qapply(C.q, phi * (C.invI / ws));
            P.q = qapply(P.q, phi * (-P.invI / ws));
        }
    }
}

void GibRagdoll::solveContacts(float h) {
    (void)h;
    for (Contact& K : contacts_) {
        Body& B = bodies_[(size_t)K.body];
        const Body::Sphere& S = B.spheres[(size_t)K.sphere];
        const core::Vec3 p = B.x + qrot(B.q, S.c);
        const float pen = S.r - core::dot(p - K.p, K.n);
        if (pen <= 0.0f) continue;
        const core::Vec3 cp = p - K.n * S.r;
        const core::Vec3 r = cp - B.x;
        const core::Vec3 rn = core::cross(r, K.n);
        const float w = B.invM + B.invI * core::dot(rn, rn);
        const float dl = pen / w;
        K.lambda += dl; K.rWorld = r;
        B.x += K.n * (dl * B.invM); B.q = qapply(B.q, rn * (dl * B.invI));
        // Static friction: undo the contact point's tangential travel this substep while within the friction cone.
        const core::Vec3 local = qrotInv(B.q, cp - B.x);
        const core::Vec3 prev = B.xPrev + qrot(B.qPrev, local);
        const core::Vec3 cpNow = B.x + qrot(B.q, local);
        core::Vec3 dp = cpNow - prev;
        dp = dp - K.n * core::dot(dp, K.n);
        const float lt = core::length(dp);
        if (lt > 1e-7f) {
            const core::Vec3 tn = dp * (1.0f / lt);
            const core::Vec3 r2 = cpNow - B.x;
            const core::Vec3 rt = core::cross(r2, tn);
            const float wt = B.invM + B.invI * core::dot(rt, rt);
            const float dlt = lt / wt;
            if (dlt < kMuS * K.lambda) { B.x -= tn * (dlt * B.invM); B.q = qapply(B.q, rt * (-dlt * B.invI)); }
        }
    }
}

void GibRagdoll::velocityPass(float h) {
    for (Contact& K : contacts_) {
        if (K.lambda <= 0.0f) continue;
        Body& B = bodies_[(size_t)K.body];
        const core::Vec3 r = K.rWorld;
        const core::Vec3 vp = B.v + core::cross(B.w, r);
        const float vn = core::dot(vp, K.n);
        const core::Vec3 vt = vp - K.n * vn;
        const float vtl = core::length(vt);
        core::Vec3 dv{0, 0, 0};
        if (vtl > 1e-6f) dv = vt * (-std::min(kMuD * K.lambda / h, vtl) / vtl);   // dynamic friction (f_n = lambda / h^2)
        // Restitution on the approach speed before this substep's solve; slow contacts settle (no bounce).
        const float vnTarget = K.vnPre < -2.0f * kGravity * h ? -kRestitution * K.vnPre : 0.0f;
        dv += K.n * (vnTarget - vn);
        const float dvl = core::length(dv);
        if (dvl < 1e-7f) continue;
        const core::Vec3 dn = dv * (1.0f / dvl);
        const core::Vec3 rd = core::cross(r, dn);
        const float w = B.invM + B.invI * core::dot(rd, rd);
        const core::Vec3 imp = dn * (dvl / w);
        B.v += imp * B.invM;
        B.w += core::cross(r, imp) * B.invI;
    }
}

void GibRagdoll::step(float dt, const CollisionWorld* col) {
    if (asleep_ || bodies_.empty()) return;
    dt = core::clampf(dt, 0.0f, 1.0f / 30.0f);
    if (dt <= 0.0f) return;
    if (col && col->valid()) collide(*col, dt); else contacts_.clear();
    const float h = dt / (float)kSubsteps;
    for (int sub = 0; sub < kSubsteps; ++sub) {
        for (Contact& K : contacts_) {
            const Body& B = bodies_[(size_t)K.body];
            const core::Vec3 r = qrot(B.q, B.spheres[(size_t)K.sphere].c) - K.n * B.spheres[(size_t)K.sphere].r;
            K.vnPre = core::dot(B.v + core::cross(B.w, r), K.n);
            K.lambda = 0.0f; K.rWorld = r;
        }
        for (Body& B : bodies_) {
            B.xPrev = B.x; B.qPrev = B.q;
            B.v.y -= kGravity * h;
            B.v = B.v * (1.0f / (1.0f + kLinDamp * h));
            B.w = B.w * (1.0f / (1.0f + kAngDamp * h));
            B.x += B.v * h;
            B.q = qapply(B.q, B.w * h);
        }
        solveJoints(h);
        solveContacts(h);
        for (Body& B : bodies_) {
            B.v = (B.x - B.xPrev) * (1.0f / h);
            core::Quat dq = assets::quatMul(B.q, qconj(B.qPrev));
            if (dq.w < 0.0f) { dq.x = -dq.x; dq.y = -dq.y; dq.z = -dq.z; dq.w = -dq.w; }
            B.w = core::Vec3{dq.x, dq.y, dq.z} * (2.0f / h);
        }
        velocityPass(h);
    }
    float fastest = 0.0f;
    for (Body& B : bodies_) {
        if (!finite3(B.x) || !finite3(B.v) || !std::isfinite(B.q.w)) {   // a solver blow-up: hold the last good pose
            B.x = B.xPrev; B.q = B.qPrev; B.v = B.w = core::Vec3{0, 0, 0}; asleep_ = true;
        }
        fastest = std::max(fastest, core::length(B.v) + core::length(B.w) * B.ext);
    }
    quiet_ = fastest < 0.05f ? quiet_ + dt : 0.0f;
    if (quiet_ > 0.5f) asleep_ = true;   // settled: no further steps (and no further pose uploads)
}

void GibRagdoll::globals(std::vector<core::Mat4>& out) const {
    out.resize(nodeCount_);
    std::vector<core::Mat4> bone(bodies_.size());
    for (size_t b = 0; b < bodies_.size(); ++b)
        bone[b] = core::mat4FromTRS(bodies_[b].x - qrot(bodies_[b].q, bodies_[b].com), bodies_[b].q, core::Vec3{1, 1, 1});
    for (size_t i = 0; i < nodeCount_ && i < follow_.size(); ++i) out[i] = bone[(size_t)follow_[i].body] * follow_[i].rel;
}

} // namespace game
