#include "game/MapState.h"
#include "game/Collision.h"
#include "assets/Json.h"
#include "core/Log.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace game {

const char* gameModeName(MatchMode m) {
    switch (m) {
        case MatchMode::DM: return "DM"; case MatchMode::TDM: return "TDM"; case MatchMode::CTF: return "CTF";
        case MatchMode::KOTH: return "KOTH"; case MatchMode::EXT: return "EXT"; case MatchMode::DOM: return "DOM";
    }
    return "?";
}

// TnOnlineGameSettings<tag>.Rules [CONF authored.db TransGame.Default__TnOnlineGameSettings{DM,TDM,CTF,KOTH,EXT,DOM}].
const std::vector<std::string>& gameRulesForMode(MatchMode m) {
    static const std::vector<std::string> dm = {"TransGame.TnGameRules_ScoreKillsDM", "TransGame.TnGameRules_TrackKillsMP",
        "TransGame.TnGameRules_ReportGameProgressTime", "TransGame.TnGameRules_ReportGameProgressKills"};
    static const std::vector<std::string> tdm = {"TransGame.TnGameRules_ScoreKillsTDM", "TransGame.TnGameRules_TrackKillsMP",
        "TransGame.TnGameRules_ReportGameProgressTime", "TransGame.TnGameRules_ReportGameProgressKills"};
    static const std::vector<std::string> ctf = {"TransGame.TnGameRules_ScoreKillsMP", "TransGame.TnGameRules_SingleFlagCTF",
        "TransGame.TnGameRules_ScoreFlags", "TransGame.TnGameRules_TrackKillsMP", "TransGame.TnGameRules_ReportGameProgressTimeCTF"};
    static const std::vector<std::string> koth = {"TransGame.TnGameRules_ScoreKillsMP", "TransGame.TnGameRules_ScoreKingOfTheHill",
        "TransGame.TnGameRules_TrackKillsMP", "TransGame.TnGameRules_ReportGameProgressTime", "TransGame.TnGameRules_ReportGameProgressPoints"};
    static const std::vector<std::string> ext = {"TransGame.TnGameRules_ScoreKillsMP", "TransGame.TnGameRules_ScoreBombingRun",
        "TransGame.TnGameRules_TrackKillsMP", "TransGame.TnGameRules_ReportGameProgressTime"};
    static const std::vector<std::string> dom = {"TransGame.TnGameRules_ScoreKillsMP", "TransGame.TnGameRules_ScoreDomination",
        "TransGame.TnGameRules_TrackKillsMP", "TransGame.TnGameRules_ReportGameProgressTime", "TransGame.TnGameRules_ReportGameProgressPoints"};
    switch (m) {
        case MatchMode::DM: return dm; case MatchMode::TDM: return tdm; case MatchMode::CTF: return ctf;
        case MatchMode::KOTH: return koth; case MatchMode::EXT: return ext; case MatchMode::DOM: return dom;
    }
    return dm;
}

bool MapState::hasRule(const std::string& cls) const {
    for (const std::string& r : gameRules()) if (r == cls) return true;
    return false;
}

// glTF = 0.01 * (X, Z, Y)_UE (a reflection P swapping Y/Z). A UE rotation R_ue (FRotationMatrix rows = the
// rotated X/Y/Z axes) acts on glTF vectors as P R_ue P.
core::Mat4 ueRotationToGltf(float pitchDeg, float yawDeg, float rollDeg) {
    const float d2r = 0.01745329252f;
    float sp = std::sin(pitchDeg * d2r), cp = std::cos(pitchDeg * d2r);
    float sy = std::sin(yawDeg * d2r), cy = std::cos(yawDeg * d2r);
    float sr = std::sin(rollDeg * d2r), cr = std::cos(rollDeg * d2r);
    // UE axes (UE coordinates).
    core::Vec3 X{cp * cy, cp * sy, sp};
    core::Vec3 Y{sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp};
    core::Vec3 Z{-(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp};
    // v_ue' = vx*X + vy*Y + vz*Z; in glTF: v_g' = P (vgx*X + vgz*Y + vgy*Z).
    auto P = [](const core::Vec3& v) { return core::Vec3{v.x, v.z, v.y}; };
    core::Vec3 cX = P(X), cY = P(Z), cZ = P(Y);   // images of glTF basis x, y, z
    core::Mat4 m;
    m.m[0] = cX.x; m.m[1] = cX.y; m.m[2] = cX.z;
    m.m[4] = cY.x; m.m[5] = cY.y; m.m[6] = cY.z;
    m.m[8] = cZ.x; m.m[9] = cZ.y; m.m[10] = cZ.z;
    return m;
}

namespace {

core::Mat4 linearPart(const core::Mat4& a) { core::Mat4 r = a; r.m[12] = r.m[13] = r.m[14] = 0.0f; return r; }

core::Vec3 ueToGltf(float x, float y, float z) { return {x * 0.01f, z * 0.01f, y * 0.01f}; }

core::Vec3 jv3(const assets::Json& j) { return {j["X"].asFloat(), j["Y"].asFloat(), j["Z"].asFloat()}; }

core::Mat4 transpose3(const core::Mat4& a) {
    core::Mat4 r;
    for (int c = 0; c < 3; ++c) for (int k = 0; k < 3; ++k) r.m[c * 4 + k] = a.m[k * 4 + c];
    return r;
}

// Rotation matrix <-> unit quaternion (x, y, z, w), column-major 3x3 in m[0..10].
struct Quat { float x, y, z, w; };
Quat quatFromMat(const core::Mat4& a) {
    const float* m = a.m;
    float tr = m[0] + m[5] + m[10];
    Quat q;
    if (tr > 0.0f) {
        float s = std::sqrt(tr + 1.0f) * 2.0f;
        q = {(m[6] - m[9]) / s, (m[8] - m[2]) / s, (m[1] - m[4]) / s, 0.25f * s};
    } else if (m[0] > m[5] && m[0] > m[10]) {
        float s = std::sqrt(1.0f + m[0] - m[5] - m[10]) * 2.0f;
        q = {0.25f * s, (m[4] + m[1]) / s, (m[8] + m[2]) / s, (m[6] - m[9]) / s};
    } else if (m[5] > m[10]) {
        float s = std::sqrt(1.0f + m[5] - m[0] - m[10]) * 2.0f;
        q = {(m[4] + m[1]) / s, 0.25f * s, (m[9] + m[6]) / s, (m[8] - m[2]) / s};
    } else {
        float s = std::sqrt(1.0f + m[10] - m[0] - m[5]) * 2.0f;
        q = {(m[8] + m[2]) / s, (m[9] + m[6]) / s, 0.25f * s, (m[1] - m[4]) / s};
    }
    return q;
}
core::Mat4 matFromQuat(const Quat& q) {
    core::Mat4 r;
    float x = q.x, y = q.y, z = q.z, w = q.w;
    r.m[0] = 1 - 2 * (y * y + z * z); r.m[4] = 2 * (x * y - z * w);     r.m[8] = 2 * (x * z + y * w);
    r.m[1] = 2 * (x * y + z * w);     r.m[5] = 1 - 2 * (x * x + z * z); r.m[9] = 2 * (y * z - x * w);
    r.m[2] = 2 * (x * z - y * w);     r.m[6] = 2 * (y * z + x * w);     r.m[10] = 1 - 2 * (x * x + y * y);
    return r;
}
// UE3 SlerpQuat: shortest arc; near-parallel keys fall back to a linear blend.
Quat slerpQuat(Quat a, const Quat& b, float t) {
    float c = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    float s1 = 1.0f;
    if (c < 0.0f) { c = -c; s1 = -1.0f; }
    float s0, sb;
    if (c < 0.9999f) {
        float om = std::acos(c), si = 1.0f / std::sin(om);
        s0 = std::sin((1.0f - t) * om) * si; sb = std::sin(t * om) * si;
    } else { s0 = 1.0f - t; sb = t; }
    sb *= s1;
    Quat r{s0 * a.x + sb * b.x, s0 * a.y + sb * b.y, s0 * a.z + sb * b.z, s0 * a.w + sb * b.w};
    float n = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z + r.w * r.w);
    return {r.x / n, r.y / n, r.z / n, r.w / n};
}

// Objective classes [CONF RE MILESTONE04_STREETS_RUNTIME_SEMANTICS + future_hud_handoff.json]: the hard-coded
// marker class (type string = class name minus "TnObjectiveMarkerType"), authored MarkerString /
// RequiredGameRuleClass, and the exact rule the class gates on (CTF SingleFlagCTF, EXT ScoreBombingRun,
// DOM ScoreDomination, KOTH ScoreKingOfTheHill).
struct ObjClass { const char* cls; const char* markerClass; const char* typeStr; const char* str; const char* rule; const char* gate; };
const ObjClass kObjClasses[] = {
    {"TnGameObjectivePickupFactoryFlag", "TransGame.TnObjectiveMarkerTypeFlag", "Flag", "Code Of Power", "TransGame.TnGameRules_SingleFlagCTF", "TransGame.TnGameRules_SingleFlagCTF"},
    {"TnFlagCapturePoint", "TransGame.TnObjectiveMarkerTypeFlagCapturePoint", "FlagCapturePoint", "", "", "TransGame.TnGameRules_SingleFlagCTF"},
    {"TnGameObjectivePickupFactoryBomb", "TransGame.TnObjectiveMarkerTypeBomb", "Bomb", "Bomb", "TransGame.TnGameRules_ScoreBombingRun", "TransGame.TnGameRules_ScoreBombingRun"},
    {"TnBombPlantPoint", "TransGame.TnObjectiveMarkerTypeBombPlantPoint", "BombPlantPoint", "", "", "TransGame.TnGameRules_ScoreBombingRun"},
    {"TnDominationPoint", "TransGame.TnObjectiveMarkerTypeDomination", "Domination", "", "", "TransGame.TnGameRules_ScoreDomination"},
    {"TnKingOfTheHillZone", "TransGame.TnObjectiveMarkerTypeKingOfTheHill", "KingOfTheHill", "Active Node", "", "TransGame.TnGameRules_ScoreKingOfTheHill"},
};

} // namespace

std::vector<std::string> MapState::moverActorNames() {
    return {"StaticInterpActor_15810", "StaticInterpActor_7381", "StaticInterpActor_8114",
            "StaticInterpActor_5249", "StaticInterpActor_13497", "StaticInterpActor_10471"};
}

bool MapState::load(const std::string& path, MatchMode mode) {
    mode_ = mode;
    movers_.clear(); objectives_.clear(); modeActors_.clear(); euler_.clear();
    // Rotating domes (ART level) [CONF streets_movers.json]: RotationRate Yaw 2730 UU/s.
    struct Dome { const char* actor; float x, y, z; };
    const Dome domes[] = {{"StaticInterpActor_15810", 12027.0f, -51121.1015625f, -70664.140625f},
                          {"StaticInterpActor_7381", 12027.0f, -49089.1015625f, -69880.140625f},
                          {"StaticInterpActor_8114", 12031.0f, -47345.1015625f, -69312.140625f}};
    for (const Dome& d : domes) {
        MapMover m;
        m.actor = d.actor; m.mesh = "ENV_IAC_Deco_1_p.StaticMesh.DecoSphereHalf01_STAT";
        m.kind = MapMover::Kind::Rotating;
        m.pivot = ueToGltf(d.x, d.y, d.z);
        m.yawRateRad = 2730.0f / 65536.0f * 6.2831853f;
        movers_.push_back(m);
    }

    std::ifstream f(path, std::ios::binary);
    if (!f) { LOG_WARN("mapstate: cannot read %s", path.c_str()); return false; }
    std::stringstream ss; ss << f.rdbuf();
    assets::Json g;
    if (!assets::Json::parse(ss.str(), g)) return false;

    // SkyBeam Matinee (gameplay.json "matinee", same data as streets_movers.json).
    const assets::Json& mat = g["matinee"][0];
    matineeLength_ = mat["length_s"].asFloat(9.0022f);
    const assets::Json& pts = mat["groups"][0]["tracks"][0]["props"]["EulerTrack"]["Points"];
    for (size_t i = 0; i < pts.size(); ++i)
        euler_.push_back({pts[i]["InVal"].asFloat(), jv3(pts[i]["OutVal"]), jv3(pts[i]["ArriveTangent"]), jv3(pts[i]["LeaveTangent"])});
    const assets::Json& acts = mat["actors"];
    for (size_t i = 0; i < acts.size(); ++i) {
        MapMover m;
        m.actor = acts[i]["actor"].asString(); m.mesh = acts[i]["mesh"].asString();
        m.kind = MapMover::Kind::Matinee;
        const assets::Json& L = acts[i]["location_gltf"];
        m.pivot = {L[0].asFloat(), L[1].asFloat(), L[2].asFloat()};
        float a[16];
        for (int k = 0; k < 16; ++k) a[k] = acts[i]["gltf_matrix"][(size_t)k].asFloat(k % 5 == 0 ? 1.0f : 0.0f);
        m.authored = core::mat4FromArray(a);
        // UE3 InterpTrackMove IMF_RelativeToInitial: InitialTM = FRotationTranslationMatrix(InitialRotation,
        // InitialLocation): the authored Rotation only (DrawScale / DrawScale3D are not part of it).
        const assets::Json& R = acts[i]["rotation_ue"];
        const float u2d = 360.0f / 65536.0f;
        m.initialRot = ueRotationToGltf(R[0].asFloat() * u2d, R[1].asFloat() * u2d, R[2].asFloat() * u2d);
        movers_.push_back(m);
    }

    // Objectives (all modes' objects are placed; activeInMode marks the ones the current mode uses).
    const assets::Json& objs = g["objectives"];
    for (const ObjClass& oc : kObjClasses) {
        const assets::Json& list = objs[oc.cls];
        for (size_t i = 0; i < list.size(); ++i) {
            ObjectiveObject o;
            o.actor = list[i]["actor"].asString(); o.cls = oc.cls;
            const assets::Json& L = list[i]["location_gltf"];
            o.pos = {L[0].asFloat(), L[1].asFloat(), L[2].asFloat()};
            o.yawDeg = list[i]["yaw_deg"].asFloat();
            o.markerClass = oc.markerClass; o.markerTypeString = oc.typeStr; o.markerString = oc.str; o.requiredRule = oc.rule;
            o.activeInMode = hasRule(oc.gate);
            objectives_.push_back(o);
        }
    }
    applyObjectiveStates();

    // Mode-dependent visibility (BASE Kismet SeqCond_GameRuleActive -> SeqAct_ToggleHidden UnHide).
    const assets::Json& mdv = g["mode_dependent_visibility"];
    for (size_t r = 0; r < mdv.size(); ++r) {
        const std::string& rule = mdv[r]["rule"].asString();
        bool active = hasRule(rule);   // SeqCond_GameRuleActive: exact rule class
        bool unhide = mdv[r]["action"].asString() == "UnHide";
        const assets::Json& t = mdv[r]["targets"];
        for (size_t i = 0; i < t.size(); ++i) {
            const std::string& a = t[i]["actor"].asString();
            ModeVisibleActor* va = nullptr;
            for (auto& x : modeActors_) if (x.actor == a) va = &x;
            if (!va) {
                ModeVisibleActor n;
                n.actor = a; n.mesh = t[i]["mesh"].asString();
                const assets::Json& L = t[i]["location_gltf"];
                n.pos = {L[0].asFloat(), L[1].asFloat(), L[2].asFloat()};
                n.visible = !t[i]["initially_hidden"].asBool(true);
                modeActors_.push_back(n);
                va = &modeActors_.back();
            }
            if (active) va->visible = unhide;
        }
    }
    int vis = 0; for (auto& a : modeActors_) vis += a.visible;
    LOG_INFO("mapstate: mode %s, %zu movers, %zu objectives, %zu mode-dependent actors (%d visible), Matinee %.4f s",
             gameModeName(mode_), movers_.size(), objectives_.size(), modeActors_.size(), vis, matineeLength_);
    pose();
    return true;
}

// Per-mode state table [CONF RE MILESTONE04_STREETS_RUNTIME_SEMANTICS §3-4].
void MapState::applyObjectiveStates() {
    using S = ObjectiveObject::State;
    std::vector<int> koth;
    for (size_t i = 0; i < objectives_.size(); ++i) {
        ObjectiveObject& o = objectives_[i];
        const bool on = o.activeInMode;
        o.markerAdded = false; o.markerShouldDisplay = false;
        if (o.cls == "TnDominationPoint") {
            // PostBeginPlay: StopAnim + PlayAnim(DeactivatedLoopAnim, loop) in every mode; !HasRule(ScoreDomination)
            // -> 'Inactive' (SetHidden(true); Touch/UnTouch ignored; collision cylinder kept). DOM: marker added.
            o.state = on ? S::Active : S::Hidden;
            o.visible = on; o.collision = true; o.touchable = on;
            o.markerAdded = on; o.markerShouldDisplay = on;              // Domination: always displayed
        } else if (o.cls == "TnGameObjectivePickupFactoryFlag" || o.cls == "TnGameObjectivePickupFactoryBomb") {
            // TnGameObjectiveWeaponPickupFactory.PostBeginPlay: !HasRule(RequiredGameRuleClass) -> 'Disabled'
            // (SetHidden + SetCollision(false,false)).
            o.state = on ? S::Active : S::Disabled;
            o.visible = on; o.collision = on; o.touchable = on;
            o.markerAdded = on;                                          // authored MarkerType [PROV add timing]
            o.markerShouldDisplay = on;                                  // [UNKNOWN: carrier/team rules not traced]
        } else if (o.cls == "TnFlagCapturePoint") {
            // PostBeginPlay returns unless SingleFlagCTF; the marker exists only while _Active, and displays only
            // to a pawn holding a flag. _Active's driver is not recovered: starts inactive.
            o.state = on ? S::Active : S::Inert;
            o.visible = false; o.collision = true; o.touchable = on;
            o.markerAdded = false; o.markerShouldDisplay = false;
        } else if (o.cls == "TnBombPlantPoint") {
            // PostBeginPlay gated on ScoreBombingRun -> marker added. ShouldDisplayMarker: false while
            // GRI.AttackingTeam is 255/-1 (no rounds in the slice), else the defended point / planted bomb.
            o.state = on ? S::Active : S::Inert;
            o.visible = false; o.collision = true; o.touchable = on;
            o.markerAdded = on; o.markerShouldDisplay = false;
        } else if (o.cls == "TnKingOfTheHillZone") {
            // Default bHidden; KOTH: 'Inactive' (hidden) except the Active zone (shown + marker), other modes hidden.
            o.state = on ? S::KothInactive : S::Hidden;
            o.visible = false; o.collision = true; o.touchable = false;
            if (on) koth.push_back((int)i);
        }
    }
    kothActive_ = -1;
    if (!koth.empty()) {
        kothRng_ = kothRng_ * 1664525u + 1013904223u;                  // MatchStarting: random initial zone
        int pick = koth[(kothRng_ >> 8) % koth.size()];
        ObjectiveObject& z = objectives_[(size_t)pick];
        z.state = ObjectiveObject::State::Active; z.visible = true; z.touchable = true;
        z.markerAdded = true; z.markerShouldDisplay = true;            // KOTH: displayed while Active
        kothActive_ = pick;
        kothTimeLeft_ = kothZoneActiveTime_;
    }
}

void MapState::activateNewKothZone() {
    if (kothActive_ < 0) return;
    std::vector<int> others;
    for (size_t i = 0; i < objectives_.size(); ++i)
        if (objectives_[i].cls == "TnKingOfTheHillZone" && (int)i != kothActive_) others.push_back((int)i);
    if (others.empty()) return;
    ObjectiveObject& old = objectives_[(size_t)kothActive_];
    old.state = ObjectiveObject::State::KothInactive; old.visible = false; old.touchable = false;
    old.markerAdded = false; old.markerShouldDisplay = false;      // Inactive.BeginState removes the marker
    kothRng_ = kothRng_ * 1664525u + 1013904223u;
    kothActive_ = others[(kothRng_ >> 8) % others.size()];
    ObjectiveObject& z = objectives_[(size_t)kothActive_];
    z.state = ObjectiveObject::State::Active; z.visible = true; z.touchable = true;
    z.markerAdded = true; z.markerShouldDisplay = true;            // Active.BeginState adds the marker
    kothTimeLeft_ = kothZoneActiveTime_;
}

std::vector<MapState::ActorVisibility> MapState::actorVisibility() const {
    std::vector<ActorVisibility> v;
    for (const ModeVisibleActor& a : modeActors_) v.push_back({a.actor, !a.visible});
    for (const ObjectiveObject& o : objectives_)
        if (o.cls == "TnDominationPoint" || o.cls == "TnKingOfTheHillZone" || o.cls == "TnGameObjectivePickupFactoryFlag" ||
            o.cls == "TnGameObjectivePickupFactoryBomb")
            v.push_back({o.actor, !o.visible});
    return v;
}

float MapState::initialRotationResidual(const MapMover& m) const {
    core::Mat4 d = transpose3(m.initialRot) * linearPart(m.authored);
    float diag = 0.0f, off = 0.0f;
    for (int c = 0; c < 3; ++c)
        for (int k = 0; k < 3; ++k) {
            float x = std::fabs(d.m[c * 4 + k]);
            if (c == k) diag = std::max(diag, x); else off = std::max(off, x);
        }
    return diag > 0.0f ? off / diag : 1.0f;
}

void MapState::registerCollision(CollisionWorld& pawn, CollisionWorld* weapon,
                                 const std::vector<std::pair<std::string, std::vector<core::Vec3>>>& pawnTris,
                                 const std::vector<std::pair<std::string, std::vector<core::Vec3>>>& weaponTris) {
    auto add = [](CollisionWorld& w, const MapMover& m, const std::vector<std::pair<std::string, std::vector<core::Vec3>>>& src) {
        for (const auto& [name, tris] : src) {
            if (name != m.actor || tris.empty()) continue;
            std::vector<core::Vec3> local(tris.size());
            for (size_t i = 0; i < tris.size(); ++i) local[i] = tris[i] - m.pivot;   // authored pose, pivot-relative
            return w.addDynamicSet(local, core::Mat4::translate(m.pivot));
        }
        return -1;
    };
    int n = 0;
    for (MapMover& m : movers_) {
        m.pawnSet = add(pawn, m, pawnTris);
        if (weapon) m.weaponSet = add(*weapon, m, weaponTris);
        n += m.pawnSet >= 0;
    }
    LOG_INFO("mapstate: %d movers carry moving collision", n);
}

// UE3 FInterpCurve::Eval for CIM_CurveAuto keys: CubicInterp(P0, T0*Diff, P1, T1*Diff, Alpha) between keys,
// clamped to the first/last key outside the range.
// UE3 UInterpTrackMove::GetKeyTransformAtTime with bUseQuatInterpolation: before the first / after the last key
// use that key; else the first i with t < Key[i+1].InVal, Alpha = clamp((t - In_i) / (In_i+1 - In_i)),
// SlerpQuat(Quat(MakeFromEuler(Out_i)), Quat(MakeFromEuler(Out_i+1)), Alpha). Euler X/Y/Z = roll/pitch/yaw.
core::Mat4 MapState::evalRelativeRotation(float t) const {
    auto rot = [](const core::Vec3& e) { return ueRotationToGltf(e.y, e.z, e.x); };
    if (euler_.empty()) return core::Mat4::identity();
    if (euler_.size() == 1 || t < euler_.front().in) return rot(euler_.front().out);
    if (t > euler_.back().in) return rot(euler_.back().out);
    for (size_t i = 0; i + 1 < euler_.size(); ++i) {
        if (t >= euler_[i + 1].in) continue;
        float dtk = euler_[i + 1].in - euler_[i].in;
        float a = dtk > 0.0f ? std::min(1.0f, std::max(0.0f, (t - euler_[i].in) / dtk)) : 0.0f;
        return matFromQuat(slerpQuat(quatFromMat(rot(euler_[i].out)), quatFromMat(rot(euler_[i + 1].out)), a));
    }
    return rot(euler_.back().out);
}

void MapState::pose() {
    for (MapMover& m : movers_) {
        core::Mat4 r;   // world-space linear delta about the pivot
        if (m.kind == MapMover::Kind::Rotating) {
            // PHYS_Rotating: Rotation += RotationRate * dt (world yaw about the actor location).
            r = ueRotationToGltf(0.0f, m.yawRateRad * clock_ * 57.2957795f, 0.0f);
        } else {
            // IMF_RelativeToInitial: ResultTM = RelativeTM * InitialTM (row vectors), i.e. the relative rotation
            // in the actor's frame -> world delta = R0 * R_rel * R0^T with R0 the authored rotation (no scale).
            // The PosTrack offset (<= 0.008 UU) is not applied.
            core::Mat4 rel = evalRelativeRotation(std::fmod(clock_, matineeLength_));
            r = m.initialRot * rel * transpose3(m.initialRot);
        }
        m.worldDelta = core::Mat4::translate(m.pivot) * r * core::Mat4::translate(m.pivot * -1.0f);
    }
}

void MapState::tick(float dt, CollisionWorld& pawn, CollisionWorld* weapon) {
    clock_ += dt;
    for (ObjectiveObject& o : objectives_) if (o.cls == "TnDominationPoint") o.animClock += dt;   // idle loop (hidden or not)
    // KOTH: the Active zone stays ZoneActiveTime (60 s, authored) then ActivateNewZone [HIGH: authored
    // ZoneActiveTime / ActiveTimeLeft fields; the timer body is not traced in the RE note].
    if (kothActive_ >= 0) {
        kothTimeLeft_ -= dt;
        if (kothTimeLeft_ <= 0.0f) activateNewKothZone();
    }
    pose();
    for (const MapMover& m : movers_) {
        core::Mat4 colPose = m.worldDelta * core::Mat4::translate(m.pivot);   // pivot-relative verts
        if (m.pawnSet >= 0) pawn.setDynamicPose(m.pawnSet, colPose);
        if (weapon && m.weaponSet >= 0) weapon->setDynamicPose(m.weaponSet, colPose);
    }
}

} // namespace game
