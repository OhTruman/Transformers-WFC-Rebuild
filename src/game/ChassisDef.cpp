// Clean-room reconstruction — per-chassis character definition loader (see ChassisDef.h).
#include "game/ChassisDef.h"
#include "assets/Json.h"
#include "core/Log.h"
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

namespace game {

namespace {

bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss; ss << f.rdbuf(); out = ss.str();
    return true;
}

bool fileExists(const std::string& path) { std::ifstream f(path, std::ios::binary); return (bool)f; }

// TnSuspensionBlueprint (VEH_SHARED_p), class default Stiffness 10000 / RestingLength 30 [CONF authored].
struct SuspensionBp { float rest, stiffness, damping; };
const std::map<std::string, SuspensionBp>& suspensionTable() {
    static const std::map<std::string, SuspensionBp> t = {
        {"VEH_SHARED_p.HoverTruck_Suspension", {250.0f, 10000.0f, 4000.0f}},
        {"VEH_SHARED_p.HoverCar_Supension", {200.0f, 8000.0f, 4000.0f}},
        {"VEH_SHARED_p.HoverTank_Suspension", {250.0f, 20000.0f, 6000.0f}},
    };
    return t;
}

// TnCarPhysicsBlueprint.WheelPhysicsBlueprints (TnWheelPhysicsBlueprint subobjects) [CONF authored]:
// LocalPosition (UU), WheelRadius, MaxSteeringAngle (deg), TireFrictionCoefficient, DriftFrictionScale.
const std::map<std::string, std::vector<WheelDef>>& wheelTable() {
    static const std::map<std::string, std::vector<WheelDef>> t = {
        {"VEH_SHARED_p.Truck_Physics", {{83, -126, 21, 45, 25, 0.0015f, 0.8f}, {83, 126, 21, 45, 25, 0.0015f, 0.8f},
                                        {-177, -137, 21, 45, 0, 0.0015f, 0.5f}, {-177, 137, 21, 45, 0, 0.0015f, 0.5f}}},
        {"VEH_SHARED_p.Car_Physics", {{95, -75, -13, 30, 20, 0.0017f, 0.8f}, {95, 75, -13, 30, 20, 0.0017f, 0.8f},
                                      {-81, -75, -13, 30, 0, 0.002f, 0.5f}, {-81, 75, -13, 30, 0, 0.002f, 0.5f}}},
    };
    return t;
}

struct HullRow { const char* id; float front, back, halfWidth, bottom, top; };
const HullRow kHulls[] = {
#include "game/VehicleHullTable.inc"
};

struct CamRow { const char* id; float h[5], d[5], f[5]; };
const CamRow kCams[] = {
#include "game/CameraTable.inc"
};

std::string contentPath(const std::string& objectPath, const char* ext) {
    // "TR_Sideswipe_ROBO_p.CP_SideswipeArm_SKEL" -> "content/TR_Sideswipe_ROBO_p/CP_SideswipeArm_SKEL<ext>"
    size_t dot = objectPath.find('.');
    if (dot == std::string::npos) return std::string();
    return "content/" + objectPath.substr(0, dot) + "/" + objectPath.substr(dot + 1) + ext;
}

bool socketFrom(const assets::Json& sockets, const char* name, SocketDef& out) {
    for (size_t i = 0; i < sockets.size(); ++i) {
        const assets::Json& s = sockets[i];
        if (s["socket"].asString() != name) continue;
        float loc[3]; int rot[3];
        for (int k = 0; k < 3; ++k) { loc[k] = s["relative_location_ue"][(size_t)k].asFloat(); rot[k] = s["relative_rotation_ue"][(size_t)k].asInt(); }
        out.bone = s["bone"].asString();
        out.local = ueSocketToGltf(loc, rot);
        out.valid = !out.bone.empty();
        return out.valid;
    }
    return false;
}

std::string manifestRoot() {
    if (const char* e = std::getenv("WFC_MANIFESTS")) return e;
    return "F:/Transformers Rebuild/AssetTools/manifests";
}

const assets::Json* rosterPackage() {
    static assets::Json j; static int state = 0;
    if (state == 0) {
        std::string txt;
        state = (readFile(manifestRoot() + "/mp_content/roster_package.json", txt) && assets::Json::parse(txt, j)) ? 1 : -1;
        if (state < 0) LOG_WARN("ChassisDef: roster_package.json unavailable under %s", manifestRoot().c_str());
    }
    return state > 0 ? &j : nullptr;
}

const assets::Json* rosterEntry(const std::string& id) {
    const assets::Json* r = rosterPackage();
    if (!r) return nullptr;
    const assets::Json& ch = (*r)["chassis"];
    if (ch.isObject()) { if (ch.has(id)) return &ch[id]; return nullptr; }
    for (size_t i = 0; i < ch.size(); ++i) if (ch[i]["re_identity"]["UniqueId"].asString() == id) return &ch[i];
    return nullptr;
}

void stringList(const assets::Json& a, std::vector<std::string>& out) {
    out.clear();
    for (size_t i = 0; i < a.size(); ++i) if (a[i].isString()) out.push_back(a[i].asString());
}

} // namespace

const SpecialtyDef* specialtyDef(const std::string& id) {
    // TransGame.TnSpecialty<Class> defaults + TR_Health_p.Health_<Class> [CONF authored / mp_classes.json].
    static const SpecialtyDef t[4] = {
        {"Leader", 0.95f, {60, 60, 60, 60, 60}, 200.0f, "HeavyPistol"},
        {"Scientist", 0.90f, {60, 60, 60}, 200.0f, "RepairRay"},
        {"Scout", 1.00f, {50, 50, 50, 50}, 200.0f, "EMPShotgun"},
        {"Soldier", 0.90f, {55, 55, 55, 55, 55, 55}, 200.0f, "HomingRocket"},
    };
    for (const SpecialtyDef& s : t) if (id == s.id) return &s;
    return nullptr;
}

core::Mat4 ueSocketToGltf(const float locUE[3], const int rotUE[3]) {
    // FRotationMatrix(Pitch, Yaw, Roll); glTF basis = (UE X, UE Z, UE Y) (umodel v = 0.01 * (x, z, y)).
    const float k = 6.2831853f / 65536.0f;
    float P = rotUE[0] * k, Y = rotUE[1] * k, R = rotUE[2] * k;
    float SP = std::sin(P), CP = std::cos(P), SY = std::sin(Y), CY = std::cos(Y), SR = std::sin(R), CR = std::cos(R);
    core::Vec3 ax{CP * CY, CP * SY, SP};
    core::Vec3 ay{SR * SP * CY - CR * SY, SR * SP * SY + CR * CY, -SR * CP};
    core::Vec3 az{-(CR * SP * CY + SR * SY), CY * SR - CR * SP * SY, CR * CP};
    auto cv = [](const core::Vec3& a) { return core::Vec3{a.x, a.z, a.y}; };
    core::Vec3 c0 = cv(ax), c1 = cv(az), c2 = cv(ay);
    core::Vec3 t{locUE[0] * 0.01f, locUE[2] * 0.01f, locUE[1] * 0.01f};
    float m[16] = {c0.x, c0.y, c0.z, 0, c1.x, c1.y, c1.z, 0, c2.x, c2.y, c2.z, 0, t.x, t.y, t.z, 1};
    return core::mat4FromArray(m);
}

bool loadChassisDef(const std::string& vsRoot, const std::string& id, ChassisDef& d) {
    d = ChassisDef{};
    d.id = id;
    const std::string dir = vsRoot + "/Characters/" + id + "/";
    std::string txt;
    assets::Json c;
    if (!readFile(dir + "character.json", txt) || !assets::Json::parse(txt, c)) {
        d.loadError = "no exported character.json for chassis " + id + " (AssetTools vs_roster_export)";
        return false;
    }
    const std::string extRoot = vsRoot + "/../";
    d.iconic = c["iconic_id"].asString();
    d.customBody = c["display"]["custom_body"]["INT"].asString();
    d.faction = c["faction"].asString() == "Decepticon" ? 1 : (c["faction"].asString() == "Autobot" ? 0 : 3);
    d.defaultSpecialty = c["class (chassis DefaultSpecialty)"].asString();
    d.mpCharacter = c["mp_status"].asString().rfind("MP character", 0) == 0;
    d.lockedChassis = c["locks"]["LockedChassis"].asBool();
    d.lockedCharacter = c["locks"]["LockedCharacter"].asBool();
    d.robotGlb = c["robot"]["glb"].asString();
    d.vehicleGlb = c["vehicle"]["glb"].asString();
    if (d.robotGlb.empty() || !fileExists(extRoot + d.robotGlb)) { d.loadError = "robot.glb missing for " + id; return false; }
    if (d.vehicleGlb.empty() || !fileExists(extRoot + d.vehicleGlb)) { d.loadError = "vehicle.glb missing for " + id; return false; }

    const assets::Json& arm = c["robot"]["arm_blueprint"];
    if (arm.isObject()) {
        d.armGltf = contentPath(arm["Mesh"].asString(), ".gltf");
        d.armAnimGltf = contentPath(arm["Animations"].asString(), ".anim.gltf");
        if (!fileExists(extRoot + d.armGltf)) d.armGltf.clear();
        if (!fileExists(extRoot + d.armAnimGltf)) d.armAnimGltf.clear();
    }
    socketFrom(c["robot"]["sockets"], "WeaponSocket_Primary", d.weaponPrimary);
    socketFrom(c["robot"]["sockets"], "WeaponSocket_Secondary", d.weaponSecondary);
    socketFrom(c["vehicle"]["sockets"], "WeaponSocket_Primary", d.vehicleWeapon);
    socketFrom(c["robot"]["sockets"], "MeleeSocket_SmallRobot", d.meleeSmall);
    socketFrom(c["robot"]["sockets"], "MeleeSocket_LargeRobot", d.meleeLarge);
    socketFrom(c["robot"]["sockets"], "PositionSocket", d.positionSocket);

    // ---- Robot (ROBODEF scalars, acrobatics, momentum; collision from the roster identity) ----
    const assets::Json& st = c["stats"];
    const assets::Json& rs = st["robot_scalars (ROBODEF)"];
    RobotParams& R = d.robot;
    R.groundSpeed = rs["BaseGroundSpeed"].asFloat(1400) * 0.01f;
    R.accel = rs["AccelRate"].asFloat(12000) * 0.01f;
    R.airSpeed = rs["AirSpeed"].asFloat(1200) * 0.01f;
    R.airControl = rs["AirControl"].asFloat(0.4f);
    R.terminalVel = rs["TerminalVelocity"].asFloat(6000) * 0.01f;
    R.damageMultiplier = rs["DamageMultiplier"].asFloat(1.0f);
    R.selfDamageMultiplier = rs["SelfDamageMultiplier"].asFloat(0.45f);
    float eye = rs["BaseEyeHeight"].asFloat(150) * 0.01f;
    const assets::Json& acro = st["acrobatics"]["values"];
    R.jumpHeight = acro["JumpHeight"].asFloat(500) * 0.01f;
    R.dodgeSpeed = acro["DodgeSpeed"].asFloat(3000) * 0.01f;
    R.dodgeTime = acro["DodgeTime"].asFloat(0.5f);
    R.hoverJumpHeight = acro["HoverJumpHeight"].asFloat(500) * 0.01f;
    R.hoverDuration = acro["HoverDuration"].asFloat(7.0f);
    R.hoverAirSpeed = acro["HoverAirSpeed"].asFloat(500) * 0.01f;
    const assets::Json& mom = st["momentum"]["values"];
    R.momGroundFwd = mom["OnGround"]["Forward"].asFloat(R.momGroundFwd);
    R.momGroundNeutral = mom["OnGround"]["Neutral"].asFloat(R.momGroundNeutral);
    R.momGroundBack = mom["OnGround"]["Backward"].asFloat(R.momGroundBack);
    R.momAirFwd = mom["InAir"]["Forward"].asFloat(R.momAirFwd);
    R.momAirNeutral = mom["InAir"]["Neutral"].asFloat(R.momAirNeutral);
    R.momAirBack = mom["InAir"]["Backward"].asFloat(R.momAirBack);
    if (const assets::Json* re = rosterEntry(id)) {
        const assets::Json& col = (*re)["re_identity"]["collision"];
        R.radius = col["CollisionRadius"].asFloat(R.radius * 100.0f) * 0.01f;
        R.halfHeight = col["CollisionHeight"].asFloat(R.halfHeight * 100.0f) * 0.01f;
    } else {
        d.loadError = "roster_package.json has no entry (collision) for " + id;
        return false;
    }
    R.eyeHeight = eye;   // above the cylinder centre (UE BaseEyeHeight is relative to the pawn Location)

    // ---- Vehicle ----
    const std::string form = c["vehicle"]["vehicle_form"].asString();
    VehicleParams& V = d.vehicle;
    V.form = form == "car" ? VehicleFormType::Car : form == "tank" ? VehicleFormType::Tank
           : form == "jet" ? VehicleFormType::Jet : VehicleFormType::Truck;
    const assets::Json& vsc = st["vehicle_scalars (VEHDEF)"];
    V.damageMultiplier = vsc["DamageMultiplier"].asFloat(1.0f);
    V.selfDamageMultiplier = vsc["SelfDamageMultiplier"].asFloat(0.45f);
    float chassisOffset = vsc["ChassisOffset"].asFloat(0.0f);   // unset = 0 (no class default authored)
    const assets::Json& vp = st["vehicle_physics"];
    const assets::Json& hov = vp.has("HoverBlueprint") ? vp["HoverBlueprint"]["values"]
                            : vp.has("Blueprint") ? vp["Blueprint"]["values"] : vp["HoverVehicleBlueprint"]["values"];
    V.hoverSpeed = hov["MaxLinearSpeed"].asFloat(1500) * 0.01f;
    V.hoverAccel = hov["MaxLinearAcceleration"].asFloat(3000) * 0.01f;
    V.dashSpeed = hov["DashSpeed"].asFloat(5000) * 0.01f;
    V.dashTime = hov["DashDuration"].asFloat(0.3f);
    V.driftDuration = hov["DriftDuration"].asFloat(0.5f);
    V.jumpSpeed = hov["JumpLinearSpeed"].asFloat(1200) * 0.01f;
    V.jumpAngSpeed = hov["JumpAngularSpeed"].asFloat(1.0f);
    V.suspMountRadius = hov["SuspensionRadius"].asFloat(200) * 0.01f;
    V.maxBoostSpeed = hov["MaxBoostSpeed"].asFloat(0.0f) * 0.01f;
    V.recoilVelocity = hov["RecoilVelocity"].asFloat(0.0f) * 0.01f;
    V.hoverRollTime = hov["RollDuration"].asFloat(0.6f);
    V.hoverRollSpeed = hov["RollLinearSpeed"].asFloat(3000) * 0.01f;
    if (vp.has("FlyingVehicleBlueprint")) {
        const assets::Json& fl = vp["FlyingVehicleBlueprint"]["values"];
        V.flySpeed = fl["MaxSpeed"].asFloat(4000) * 0.01f;
        V.flyAccel = fl["MaxAcceleration"].asFloat(3000) * 0.01f;
        V.flyDrag = fl["DragCoefficient"].asFloat(600);
        V.pitchDuePitch = fl["PitchDueToPitchValue"].asFloat(27); V.yawDueYaw = fl["YawDueToYawValue"].asFloat(16);
        V.rollDueYaw = fl["RollDueToYawValue"].asFloat(77); V.extraRotLerp = fl["ExtraRotationLerpValue"].asFloat(0.1f);
        V.maxPitchDeg = fl["MaxPitchValue"].asFloat(60); V.fullPitchDeg = fl["FullPitchThreshold"].asFloat(45);
        V.flyRollTime = fl["RollDuration"].asFloat(0.8f); V.flyRollSpeed = fl["RollLinearSpeed"].asFloat(3000) * 0.01f;
        V.flyRollAngSpeed = fl["RollAngularSpeed"].asFloat(8);
    }
    auto sit = suspensionTable().find(hov["SuspensionBlueprint"].asString());
    if (sit != suspensionTable().end()) {
        V.suspRest = sit->second.rest * 0.01f; V.suspStiffness = sit->second.stiffness; V.suspDamping = sit->second.damping;
    }
    if (vp.has("CarBlueprint")) {
        const std::string carBp = vp["CarBlueprint"]["object"].asString();
        const assets::Json& cb = vp["CarBlueprint"]["values"];
        V.mass = cb["Mass"].asFloat(1.0f);
        V.inertiaX = cb["InertiaTensor"]["X"].asFloat(1.0f) * 1e-4f;
        V.inertiaY = cb["InertiaTensor"]["Y"].asFloat(1.0f) * 1e-4f;
        V.inertiaZ = cb["InertiaTensor"]["Z"].asFloat(1.0f) * 1e-4f;
        V.comFwd = cb["CenterOfMass"]["X"].asFloat(0.0f) * 0.01f;
        V.comUp = (cb["CenterOfMass"]["Z"].asFloat(0.0f) - chassisOffset) * 0.01f;
        V.driveSpeed = cb["MaxSpeed"].asFloat(500) * 0.01f;
        V.driveAccel = cb["MaxAcceleration"].asFloat(500) * 0.01f;
        V.driveJumpFwd = cb["JumpLinearVelocity"]["X"].asFloat(1200) * 0.01f;
        V.driveJumpUp = cb["JumpLinearVelocity"]["Z"].asFloat(1200) * 0.01f;
        V.driveJumpAngVel = cb["JumpAngularVelocity"].asFloat(2.0f);
        V.airTurnAccel = cb["AirControlTurnAcceleration"].asFloat(8.0f);
        V.airStrafeAccel = cb["AirControlStrafeAcceleration"].asFloat(2000.0f) * 0.01f;
        V.angularDamping = cb["AngularDamping"].asFloat(5.0f);
        V.rollDuration = cb["RollDuration"].asFloat(0.7f);
        auto wit = wheelTable().find(carBp);
        if (wit != wheelTable().end()) V.wheels = wit->second;
    } else {
        // Tank / jet: no TnCarPhysicsBlueprint; the rigid body takes the hover values above.
        V.mass = core::config::kVehMass;
    }

    for (const CamRow& cr : kCams)
        if (id == cr.id) {
            const float d2r = 0.0174533f;
            auto mk = [&](const float* v) { return CamStrategy{v[0], v[1], v[2] * d2r, v[3] * d2r, v[4]}; };
            d.camHover = mk(cr.h); d.camDrive = mk(cr.d);
            if (cr.f[1] > 0.0f) d.camFly = mk(cr.f);
        }
    for (const HullRow& h : kHulls)
        if (id == h.id) { V.hullFront = h.front; V.hullBack = h.back; V.hullHalfWidth = h.halfWidth; V.hullBottom = h.bottom; V.hullTop = h.top; V.hullFromPhysics = true; }
    const assets::Json& w = c["weapons"];
    const assets::Json& ip = c["iconic_preset"];
    d.iconicSpecialty = ip["Specialty"].asString().empty() ? d.defaultSpecialty : ip["Specialty"].asString();
    stringList(ip["WeaponTypes"], d.iconicWeapons);
    stringList(ip["VehicleWeapons"], d.iconicVehicleWeapons);
    stringList(ip["Abilities"], d.iconicAbilities);
    stringList(w["allowed_on_foot_by_provider (TnDataProvider_Weapon FactionRestriction + ChassisRestriction; WeaponType != 3)"], d.allowedOnFoot);
    d.classDefaultSecondary = w["class_default_secondary"].asString();
    if (!d.weaponPrimary.valid) { d.loadError = "robot WeaponSocket_Primary missing for " + id; return false; }
    return true;
}

} // namespace game
