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
    movers_.clear(); objectives_.clear(); modeActors_.clear(); euler_.clear(); mdv_.clear();
    // Rotating domes (ART level) [CONF streets_movers.json]: RotationRate Yaw 2730 UU/s.
    struct Dome { const char* actor; float x, y, z; };
    const Dome domes[] = {{"StaticInterpActor_15810", 12027.0f, -51121.1015625f, -70664.140625f},
                          {"StaticInterpActor_7381", 12027.0f, -49089.1015625f, -69880.140625f},
                          {"StaticInterpActor_8114", 12031.0f, -47345.1015625f, -69312.140625f}};
    const bool streets = path.find("MP_IAC_Streets") != std::string::npos;   // the domes are Streets' ART-level movers
    for (const Dome& d : domes) {
        if (!streets) break;
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
            // Team-owned objectives (flag factory / capture point / plant point): authored DefenderTeamIndex, byte default 0.
            if (std::string(oc.cls) == "TnGameObjectivePickupFactoryFlag" || std::string(oc.cls) == "TnFlagCapturePoint" ||
                std::string(oc.cls) == "TnBombPlantPoint")
                o.authoredTeam = list[i]["effective"]["DefenderTeamIndex"].asInt(0);
            const assets::Json& L = list[i]["location_gltf"];
            o.pos = {L[0].asFloat(), L[1].asFloat(), L[2].asFloat()};
            o.yawDeg = list[i]["yaw_deg"].asFloat();
            o.pointNumber = list[i]["authored"]["PointNumber"].asInt(0);
            {
                std::string v = list[i]["authored"]["ObjectiveVolume"].asString();
                if (v.empty()) v = list[i]["effective"]["ObjectiveVolume"].asString();
                size_t dot = v.rfind('.');
                o.volumeActor = dot == std::string::npos ? v : v.substr(dot + 1);
            }
            o.markerClass = oc.markerClass; o.markerTypeString = oc.typeStr; o.markerString = oc.str; o.requiredRule = oc.rule;
            o.gateRule = oc.gate;
            o.activeInMode = hasRule(oc.gate);
            objectives_.push_back(o);
        }
    }
    applyObjectiveStates();

    // Mode-dependent visibility (BASE Kismet SeqCond_GameRuleActive -> SeqAct_ToggleHidden UnHide).
    const assets::Json& mdv = g["mode_dependent_visibility"];
    for (size_t r = 0; r < mdv.size(); ++r) {
        MdvRule rr;
        rr.rule = mdv[r]["rule"].asString();
        rr.unhide = mdv[r]["action"].asString() == "UnHide";
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
                n.visible = n.initialVisible = !t[i]["initially_hidden"].asBool(true);
                modeActors_.push_back(n);
                va = &modeActors_.back();
            }
            rr.actors.push_back(a);
        }
        mdv_.push_back(rr);
    }
    applyModeVisibility();
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
    // KOTH zones stay Inactive until MatchStarting picks the first Active zone (matchStarting()).
    kothActive_ = -1;
    (void)koth;
}

void MapState::activateKothZone(int idx) {
    // Active.BeginState: ZoneActive, HasBeenActive, SetHidden(false), marker "Active Node", UpdateClaim(true).
    ObjectiveObject& z = objectives_[(size_t)idx];
    z.state = ObjectiveObject::State::Active; z.visible = true; z.touchable = true;
    z.markerAdded = true; z.markerShouldDisplay = true;
    z.kothVisited = true;
    z.defenderTeam = 255;
    z.activeTimeLeft = kothZoneActiveTime_;
    z.periodTimeLeft = 1.0f;                                       // ScoreInterval 1
    kothActive_ = idx;
    kothTimeLeft_ = kothZoneActiveTime_;
}

void MapState::matchStarting() {
    // CTF / EXT: one carried objective per flag factory / the bomb factory (state Pickup; CTF activation per round).
    carried_.clear(); planted_ = Planted{};
    for (size_t i = 0; i < objectives_.size(); ++i) {
        const ObjectiveObject& o = objectives_[i];
        if (!o.activeInMode) continue;
        if (o.cls == "TnGameObjectivePickupFactoryFlag" || o.cls == "TnGameObjectivePickupFactoryBomb") {
            Carried c; c.kind = o.cls == "TnGameObjectivePickupFactoryBomb" ? 1 : 0; c.home = (int)i; c.pos = o.pos;
            carried_.push_back(c);
        }
    }
    // TnKingOfTheHillZoneBase.MatchStarting: the first zone to receive it picks InitialZoneIndex = RandRange(-1,
    // len(AllOtherZones)) - itself or one of the others [CONF; RandRange's integer distribution approximated as uniform].
    std::vector<int> koth;
    for (size_t i = 0; i < objectives_.size(); ++i)
        if (objectives_[i].cls == "TnKingOfTheHillZone" && objectives_[i].activeInMode) koth.push_back((int)i);
    if (koth.empty()) return;
    for (int i : koth) { ObjectiveObject& z = objectives_[(size_t)i]; z.kothVisited = false; z.visible = false; z.state = ObjectiveObject::State::KothInactive; }
    kothRng_ = kothRng_ * 1664525u + 1013904223u;
    activateKothZone(koth[(kothRng_ >> 8) % koth.size()]);
}

void MapState::matchEnded() {
    for (ObjectiveObject& o : objectives_)
        if (o.cls == "TnKingOfTheHillZone" && o.activeInMode) {
            o.state = ObjectiveObject::State::KothInactive; o.visible = false; o.touchable = false;
            o.markerAdded = false; o.markerShouldDisplay = false;
        }
    kothActive_ = -1;
}

void MapState::loadObjectiveVolumes(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return;
    std::stringstream ss; ss << f.rdbuf();
    assets::Json ph;
    if (!assets::Json::parse(ss.str(), ph)) return;
    int n = 0;
    for (ObjectiveObject& o : objectives_) {
        o.volume.clear();
        for (size_t v = 0; v < ph["volumes"].size(); ++v) {
            const assets::Json& vol = ph["volumes"][v];
            if (vol["actor"].asString() != o.volumeActor) continue;
            // Brush polygons -> planes oriented away from the brush centroid.
            std::vector<std::vector<core::Vec3>> polys;
            core::Vec3 c{0, 0, 0}; int cn = 0;
            for (size_t p = 0; p < vol["polygons_gltf"].size(); ++p) {
                std::vector<core::Vec3> poly;
                for (size_t k = 0; k < vol["polygons_gltf"][p].size(); ++k) {
                    const assets::Json& q = vol["polygons_gltf"][p][k];
                    poly.push_back({q[0].asFloat(), q[1].asFloat(), q[2].asFloat()});
                    c = c + poly.back(); ++cn;
                }
                polys.push_back(poly);
            }
            if (cn) c = c * (1.0f / cn);
            for (const auto& poly : polys) {
                if (poly.size() < 3) continue;
                core::Vec3 nrm = core::cross(poly[1] - poly[0], poly[2] - poly[0]);
                float l = core::length(nrm);
                if (l < 1e-6f) continue;
                nrm = nrm * (1.0f / l);
                if (core::dot(nrm, c - poly[0]) > 0.0f) nrm = nrm * -1.0f;
                o.volume.push_back({nrm, core::dot(nrm, poly[0])});
            }
            ++n;
            break;
        }
    }
    LOG_INFO("mapstate: %d objective volumes", n);
}

// Live objective rules (InProgress only). Pawn membership = the pawn's location inside the ObjectiveVolume brush
// [HIGH: Volume.AssociatedActor forwards Touch / UnTouch; the cylinder-vs-brush overlap is approximated by the location].
// ---- Carried objectives (CTF / EXT) ----------------------------------------------------------------------------------
namespace {
constexpr float kFactoryTouchR = 2.0f, kFactoryTouchHH = 1.0f;   // pickup factory CylinderComponent 200 / 100 UU
constexpr float kPawnR = 2.0f, kPawnHH = 2.0f;                   // pawn cylinder (robot) for touches
constexpr float kDroppedTouchR = 0.22f;                          // dropped pickup TouchCylinder: CylinderComponent default 22 UU [HIGH]
bool cylTouch(const core::Vec3& pawn, const core::Vec3& at, float r, float hh) {
    float dx = pawn.x - at.x, dz = pawn.z - at.z;
    return std::sqrt(dx * dx + dz * dz) <= r + kPawnR && std::fabs(pawn.y - at.y) <= hh + kPawnHH;
}
}

int MapState::carriedBy(int player) const {
    for (size_t i = 0; i < carried_.size(); ++i) if (carried_[i].holder == player && player >= 0) return (int)i;
    return -1;
}

void MapState::roundStart(int attackingTeam) {
    ctfAttacking_ = attackingTeam;
    for (ObjectiveObject& o : objectives_) {
        if (!o.activeInMode) continue;
        // TnFlagCapturePointBase.ActivateIfMatchingTeam(AttackingTeam): SetActive(DefenderTeamIndex == attacking team).
        if (o.cls == "TnFlagCapturePoint") o.state = (o.defenderTeam == attackingTeam) ? ObjectiveObject::State::Active : ObjectiveObject::State::Inert;
    }
    for (Carried& c : carried_) {
        if (c.kind != 0) continue;
        // TnGameObjectiveWeaponPickupFactory.ActivateIfMatchingTeam(DefendingTeam): the defenders' factory Pickup, others Sleeping.
        const ObjectiveObject& f = objectives_[(size_t)c.home];
        c.active = f.defenderTeam == (attackingTeam == 0 ? 1 : 0);
        c.holder = -1; c.holderTeam = 255; c.dropped = false; c.pos = f.pos; c.autoReturn = 0.0f; c.returnLeft = 10.0f;
    }
}

int MapState::pickupCandidate(const ObjPawn& p) const {
    if (!p.alive || !p.canPickup) return -1;
    for (size_t ci = 0; ci < carried_.size(); ++ci) {
        const Carried& c = carried_[ci];
        if (c.sleep > 0.0f || !c.active || c.holder >= 0) continue;
        const ObjectiveObject& home = objectives_[(size_t)c.home];
        if (c.kind == 0 && p.team == home.defenderTeam && !c.dropped) continue;
        if (c.kind == 0 && c.dropped && p.team != ctfAttacking_) continue;
        const core::Vec3 at = c.dropped ? c.pos : home.pos;
        if (c.dropped ? cylTouch(p.pos, at, kDroppedTouchR, kDroppedTouchR) : cylTouch(p.pos, at, kFactoryTouchR, kFactoryTouchHH)) return (int)ci;
    }
    return -1;
}

bool MapState::dropCarriedBy(int player, const core::Vec3& at) {
    for (Carried& c : carried_)
        if (c.holder == player) {
            c.holder = -1; c.holderTeam = 255; c.dropped = true; c.pos = at; c.autoReturn = 30.0f; c.returnLeft = 10.0f;
            if (c.pos.y < killZ_ + 1.0f) { c.dropped = false; c.pos = objectives_[(size_t)c.home].pos; }
            LOG_INFO("match: %s (tossed)", c.kind == 0 ? "TnFlagMessage(dropped)" : "TnBombMessage(dropped)");
            return true;
        }
    return false;
}

static void tickCarried(std::vector<ObjectiveObject>& objs, std::vector<MapState::Carried>& carried, MapState::Planted& planted,
                        int ctfAttacking, float killZ, float dt, const std::vector<MapState::ObjPawn>& pawns, MapState::ObjectiveScoring& out) {
    using Carried = MapState::Carried;
    auto alivePawn = [&](int player) -> const MapState::ObjPawn* {
        for (const auto& p : pawns) if (p.player == player && p.alive) return &p;
        return nullptr;
    };
    for (size_t ci = 0; ci < carried.size(); ++ci) {
        Carried& c = carried[ci];
        ObjectiveObject& home = objs[(size_t)c.home];
        if (c.sleep > 0.0f) { c.sleep = std::max(0.0f, c.sleep - dt); continue; }
        if (!c.active) continue;
        if (c.holder >= 0) {
            const MapState::ObjPawn* h = alivePawn(c.holder);
            if (!h) {
                // The carrier died: TnDroppedPickup at the last location (TnWeaponFlagBase / Bomb dropped on death).
                const int lastHolder = c.holder;
                c.holder = -1; c.dropped = true; c.autoReturn = 30.0f; c.returnLeft = 10.0f;
                if (c.pos.y < killZ + 1.0f) { c.dropped = false; c.pos = home.pos; }   // fell out of the world: home
                out.messages.push_back({c.kind == 0 ? "TnFlagMessage(dropped)" : "TnBombMessage(dropped)", (int)ci});
                out.actions.push_back({c.kind == 0 ? "FlagDropped" : "BombDropped", lastHolder, c.holderTeam});
                continue;
            }
            c.pos = h->pos;
            // Capture: TnFlagCapturePoint.Touch while _Active with a flag carrier -> ScoreObjective(PRI, 1), Flag.OnScore
            // -> home (reason 3, sleep 0: immediately available) [CONF].
            if (c.kind == 0) {
                for (const ObjectiveObject& o : objs)
                    if (o.activeInMode && o.cls == "TnFlagCapturePoint" && o.state == ObjectiveObject::State::Active && o.contains(h->pos)) {
                        out.objectiveScores.push_back({c.holder, 1});
                        out.messages.push_back({"TnFlagMessage(captured)", h->team});
                        out.actions.push_back({"FlagCapture", c.holder, h->team});
                        c.holder = -1; c.holderTeam = 255; c.dropped = false; c.pos = home.pos;
                        break;
                    }
            } else if (!planted.active) {
                // Plant: the carrier touches the ENEMY plant point (DefenderTeamIndex != carrier team) [CONF].
                for (size_t oi = 0; oi < objs.size(); ++oi) {
                    const ObjectiveObject& o = objs[oi];
                    if (!o.activeInMode || o.cls != "TnBombPlantPoint" || o.defenderTeam == h->team || !o.contains(h->pos)) continue;
                    planted.active = true; planted.point = (int)oi; planted.planter = c.holder; planted.team = h->team;
                    planted.fuse = 15.0f; planted.defuse = 0.0f;                 // FuseTime 15
                    c.holder = -1; c.holderTeam = 255; c.dropped = false; c.active = false;   // the bomb is in the point
                    out.messages.push_back({"TnBombMessage(planted)", h->team});
                    out.actions.push_back({"BombPlant", planted.planter, h->team});
                    break;
                }
            }
            continue;
        }
        const core::Vec3 at = c.dropped ? c.pos : home.pos;
        if (c.dropped) {
            // TnDroppedPickupFlagBase / Bomb: AutoReturnTime 30; flag: defenders touching drain ReturnFlagTime 10 at dt x count,
            // +dt recovery with none; at 0 -> returned. Falling out of the world sends it home (handled by the host).
            c.autoReturn -= dt;
            if (c.kind == 0) {
                int defending = ctfAttacking == 0 ? 1 : 0, n = 0;
                for (const auto& p : pawns) if (p.alive && p.team == defending && cylTouch(p.pos, at, kDroppedTouchR, kDroppedTouchR)) ++n;
                c.returnLeft = n > 0 ? c.returnLeft - dt * n : std::min(10.0f, c.returnLeft + dt);
                if (c.returnLeft <= 0.0f) {
                    c.dropped = false; c.pos = home.pos; out.messages.push_back({"TnFlagMessage(returned)", defending});
                    // FlagReturn: the defenders touching the dropped flag when it returned.
                    for (const auto& p : pawns) if (p.alive && p.team == defending && cylTouch(p.pos, at, kDroppedTouchR, kDroppedTouchR)) out.actions.push_back({"FlagReturn", p.player, p.team});
                    continue;
                }
            }
            if (c.autoReturn <= 0.0f) { c.dropped = false; c.pos = home.pos; out.messages.push_back({c.kind == 0 ? "TnFlagMessage(returned)" : "TnBombMessage(returned)", -1}); continue; }
        }
        // Pickup: flag - attackers only (ValidTouch rejects DefenderTeamIndex); bomb - anyone; dropped re-pick by touch.
        for (const auto& p : pawns) {
            if (!p.alive || !p.canPickup || !p.wantsPickup) continue;
            if (c.kind == 0 && p.team == home.defenderTeam && !c.dropped) continue;
            if (c.kind == 0 && c.dropped && p.team != ctfAttacking) continue;
            bool touch = c.dropped ? cylTouch(p.pos, at, kDroppedTouchR, kDroppedTouchR) : cylTouch(p.pos, at, kFactoryTouchR, kFactoryTouchHH);
            if (!touch) continue;
            c.holder = p.player; c.holderTeam = p.team; c.dropped = false; c.pos = p.pos;
            if (c.kind == 1) out.attackingTeam = p.team;                 // ObjectiveHolderChanged -> GRI.AttackingTeam
            out.messages.push_back({c.kind == 0 ? "TnFlagMessage(taken)" : "TnBombMessage(taken)", p.team});
            out.actions.push_back({c.kind == 0 ? "FlagTaken" : "BombTaken", p.player, p.team});
            break;
        }
    }
    // Planted bomb: fuse; defenders on the point accumulate DefuseTime 5 (reset when nobody); detonation: ScoreObjective
    // (planter, 1), HurtRadius 9999 / 5000 UU AOE, the bomb returns home and its factory sleeps WaitAfterScoreTime 5 [CONF].
    if (planted.active) {
        const ObjectiveObject& pt = objs[(size_t)planted.point];
        int defenders = 0;
        for (const auto& p : pawns) if (p.alive && p.team == pt.defenderTeam && pt.contains(p.pos)) ++defenders;
        planted.defuse = defenders > 0 ? planted.defuse + dt : 0.0f;
        planted.fuse -= dt;
        Carried* bomb = nullptr;
        for (Carried& c : carried) if (c.kind == 1) bomb = &c;
        if (planted.defuse >= 5.0f) {
            // Defused: the bomb spawns at the point (DefuseBombSpawnClass = a dropped TnWeaponBomb).
            planted.active = false;
            if (bomb) { bomb->active = true; bomb->dropped = true; bomb->pos = pt.pos; bomb->autoReturn = 30.0f; }
            out.messages.push_back({"TnBombMessage(defused)", pt.defenderTeam});
            for (const auto& p : pawns) if (p.alive && p.team == pt.defenderTeam && pt.contains(p.pos)) out.actions.push_back({"BombDefuse", p.player, p.team});
        } else if (planted.fuse <= 0.0f) {
            planted.active = false;
            out.objectiveScores.push_back({planted.planter, 1});
            out.radiusDamage.push_back({pt.pos, 50.0f, 9999.0f, planted.planter, "TransGame.TnDamageTypeBombExplosion"});
            if (bomb) { bomb->active = true; bomb->dropped = false; bomb->holder = -1; bomb->pos = objs[(size_t)bomb->home].pos; bomb->sleep = 5.0f; }
            out.messages.push_back({"TnBombMessage(detonated)", planted.team});
            out.actions.push_back({"BombDetonate", planted.planter, planted.team});
        }
    }
}

void MapState::tickObjectives(float dt, const std::vector<ObjPawn>& pawns, ObjectiveScoring& out) {
    if (!carried_.empty()) tickCarried(objectives_, carried_, planted_, ctfAttacking_, killZ_, dt, pawns, out);
    for (size_t idx = 0; idx < objectives_.size(); ++idx) {
        ObjectiveObject& o = objectives_[idx];
        if (!o.activeInMode) continue;
        if (o.cls == "TnDominationPoint") {
            // TnDominationPointBase.Tick / UpdateOccupiers / UpdateScoring [CONF bytecode]; CaptureTime 20, ScoreInterval 3,
            // ScoreAmount 1, PersonalScoreAmount 2 (authored TnDominationPointBase defaults).
            std::vector<const ObjPawn*> occ;
            for (const ObjPawn& p : pawns) if (p.alive && o.contains(p.pos)) occ.push_back(&p);
            if (occ.empty()) o.captureTime = 0.0f;
            else {
                auto attackersOf = [&](int def) { std::vector<const ObjPawn*> a; for (auto* p : occ) if (p->team != def) a.push_back(p); return a; };
                std::vector<const ObjPawn*> att = attackersOf(o.defenderTeam);
                if (att.empty()) { o.claimingTeam = 255; o.captureTime = 0.0f; }
                else {
                    bool noDefender = false;
                    if (o.defenderTeam == 255) {             // neutral: treated as owned by the other team for this claim
                        noDefender = true;
                        o.defenderTeam = att[0]->team == 0 ? 1 : 0;
                        att = attackersOf(o.defenderTeam);
                    }
                    int defenders = 0;
                    for (auto* p : occ) defenders += p->team == o.defenderTeam;
                    if (defenders == 0) {
                        if (o.claimingTeam != o.defenderTeam) { o.captureTime = dt * att.size(); o.claimingTeam = o.defenderTeam; }
                        else o.captureTime += dt * att.size();
                    }
                    if (o.captureTime >= 20.0f) {
                        int newTeam = att[0]->team;
                        o.defenderTeam = newTeam;                // SetTeam -> DefendingTeamChanged
                        o.captureTime = 0.0f; o.scoreTime = 0.0f;
                        for (auto* p : att) { out.personalScores.push_back({p->player, 2}); out.actions.push_back({"NodeCapture", p->player, p->team}); }   // AddDominationPointCapture + AddScore(2)
                        out.messages.push_back({"TnDominationMessage", (newTeam == 0 ? 0 : 1) + 10 * o.pointNumber});
                    } else if (noDefender) {
                        o.defenderTeam = 255;
                    }
                }
            }
            if (o.defenderTeam != 255) {
                o.scoreTime += dt;
                if (o.scoreTime >= 3.0f) { out.teamScores.push_back({o.defenderTeam, 1}); o.scoreTime = 0.0f; }
            }
        } else if (o.cls == "TnKingOfTheHillZone" && (int)idx == kothActive_) {
            // Active.Tick [CONF bytecode]: ActiveTimeLeft / PeriodTimeLeft, UpdateClaim, ScoreZone every ScoreInterval 1 s
            // (each eligible pawn: Game.ScoreObjective(PRI, PointsPerInterval 1)), ActivateNewZone at 0.
            o.activeTimeLeft -= dt;
            o.periodTimeLeft -= dt;
            kothUpdateClaim(o, pawns, out);
            if (o.periodTimeLeft <= 0.0f) {
                o.periodTimeLeft = 1.0f;
                if (o.defenderTeam != 255 && o.defenderTeam != 254)
                    for (const ObjPawn& p : pawns) if (p.alive && o.contains(p.pos)) { out.objectiveScores.push_back({p.player, 1}); ++zoneStay_[p.player]; }
            }
            kothTimeLeft_ = o.activeTimeLeft;
            // ZoneHold: a stay ends when the player leaves the zone, dies, or the zone deactivates (points scored during it).
            for (auto it = zoneStay_.begin(); it != zoneStay_.end();) {
                bool inside = false; int team = 255;
                for (const ObjPawn& p : pawns) if (p.player == it->first) { inside = p.alive && o.contains(p.pos); team = p.team; }
                if (!inside || o.activeTimeLeft <= 0.0f) { out.actions.push_back({"ZoneHold", it->first, team, it->second}); it = zoneStay_.erase(it); }
                else ++it;
            }
            if (o.activeTimeLeft <= 0.0f) { activateNewKothZone(); break; }
        }
    }
}

void MapState::kothUpdateClaim(ObjectiveObject& z, const std::vector<ObjPawn>& pawns, ObjectiveScoring& out) {
    int claim = 255;
    for (const ObjPawn& p : pawns) {
        if (!p.alive || !z.contains(p.pos)) continue;               // IsEligibleForScoring: not dead
        if (claim == 255) claim = p.team;
        else if (claim != p.team) { claim = 254; break; }          // contested
    }
    if (claim != z.defenderTeam) {
        z.defenderTeam = claim;                                    // SetTeam -> DefendingTeamChanged
        // TnKingOfTheHillMessage: Autobot 0 / Decepticon 1 / contested 2 (neutral: dialog only).
        if (claim == 0 || claim == 1 || claim == 254) out.messages.push_back({"TnKingOfTheHillMessage", claim == 254 ? 2 : claim});
    }
}

void MapState::applyModeVisibility() {
    for (ModeVisibleActor& a : modeActors_) a.visible = a.initialVisible;
    for (const MdvRule& r : mdv_)
        if (hasRule(r.rule))                                   // SeqCond_GameRuleActive: exact rule class
            for (const std::string& n : r.actors)
                for (ModeVisibleActor& a : modeActors_) if (a.actor == n) a.visible = r.unhide;
}

void MapState::setMode(MatchMode mode) {
    mode_ = mode;
    for (ObjectiveObject& o : objectives_) o.activeInMode = hasRule(o.gateRule);
    applyObjectiveStates();
    applyModeVisibility();
    LOG_INFO("mapstate: mode -> %s", gameModeName(mode_));
}

void MapState::resetForNewMatch() {
    zoneStay_.clear();
    clock_ = 0.0f;
    for (ObjectiveObject& o : objectives_) {
        o.animClock = 0.0f; o.kothVisited = false;
        o.defenderTeam = o.authoredTeam; o.claimingTeam = 255; o.captureTime = 0.0f; o.scoreTime = 0.0f;
    }
    carried_.clear(); planted_ = Planted{}; ctfAttacking_ = 255;
    applyObjectiveStates();
    applyModeVisibility();
    pose();
}

void MapState::activateNewKothZone() {
    if (kothActive_ < 0) return;
    // ActivateNewZone [CONF RE MILESTONE05 §3]: candidates = other zones not HasBeenActive; if none, clear HasBeenActive
    // on every zone and pick a random other zone. Every zone is visited once per cycle; no back-to-back repeat.
    std::vector<int> others, fresh;
    for (size_t i = 0; i < objectives_.size(); ++i)
        if (objectives_[i].cls == "TnKingOfTheHillZone" && (int)i != kothActive_) {
            others.push_back((int)i);
            if (!objectives_[i].kothVisited) fresh.push_back((int)i);
        }
    if (others.empty()) return;
    if (fresh.empty()) { for (ObjectiveObject& o : objectives_) if (o.cls == "TnKingOfTheHillZone") o.kothVisited = false; fresh = others; }
    others = fresh;
    ObjectiveObject& old = objectives_[(size_t)kothActive_];
    old.state = ObjectiveObject::State::KothInactive; old.visible = false; old.touchable = false;
    old.markerAdded = false; old.markerShouldDisplay = false;      // Inactive.BeginState removes the marker
    kothRng_ = kothRng_ * 1664525u + 1013904223u;
    kothActive_ = others[(kothRng_ >> 8) % others.size()];
    activateKothZone(kothActive_);                                 // Active.BeginState adds the marker
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
    // (KOTH zone time and scoring run in tickObjectives while the match is InProgress.)
    pose();
    for (const MapMover& m : movers_) {
        core::Mat4 colPose = m.worldDelta * core::Mat4::translate(m.pivot);   // pivot-relative verts
        if (m.pawnSet >= 0) pawn.setDynamicPose(m.pawnSet, colPose);
        if (weapon && m.weaponSet >= 0) weapon->setDynamicPose(m.weaponSet, colPose);
    }
}

} // namespace game
