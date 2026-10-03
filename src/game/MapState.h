// Clean-room reconstruction — MP_IAC_Streets runtime world state (Gameplay-owned): movers, mode-dependent
// visibility, objective objects. One clock (seconds since SeqEvent_GameplayStarted) drives both the poses
// Rendering draws and the moving collision. Provenance: AssetTools a23c675 (streets_movers.json,
// streets_kismet.json, mp_iac_streets_complete.json, future_hud_handoff.json) and the slice gameplay.json.
//
//  * 3 rotating domes: StaticInterpActor_15810 / _7381 / _8114 (ART), PHYS_Rotating, RotationRate Yaw 2730 UU/s
//    = 15 deg/s, DecoSphereHalf01_STAT; collide + block (collision_pawn/_weapon "prop_simple") [CONF].
//  * SkyBeam: BASE Kismet SeqEvent_GameplayStarted -> ActivateRemoteEvent "StartBeam" -> SeqAct_Interp_3464
//    (bLooping, InterpLength 9.0022 s), group "SkyBeam" InterpTrackMove IMF_RelativeToInitial on
//    StaticInterpActor_5249 (DecoSphereHalf01, collides) and _13497 / _10471 (LightBeam_Cone) [CONF].
//  * 3 domination totems: the SkeletalMeshComponent of each TnDominationPoint, placed in every mode; their
//    animation and DOM behaviour are native/not exported — Gameplay exposes placement + the shared clock.
//  * 4 objective-base InterpActors (Base_A, Base_D, 2x RepairNodeB): authored bHidden, unhidden by
//    SeqCond_GameRuleActive (TnGameRules_ScoreBombingRun "EXT" / TnGameRules_SingleFlagCTF "CTF") ->
//    SeqAct_ToggleHidden at GameplayStarted. Non-colliding [CONF].
#pragma once
#include "core/Math.h"

#include <string>
#include <vector>

namespace game {

class CollisionWorld;

// gameplay.json modes (playlists); the slice runs DM (TnFreeForAllGame) unless WFC_GAMEMODE selects another.
enum class MatchMode { DM, TDM, CTF, KOTH, EXT, DOM };
const char* gameModeName(MatchMode m);

struct MapMover {
    enum class Kind { Rotating, Matinee };
    std::string actor, mesh;
    Kind kind = Kind::Rotating;
    core::Vec3 pivot{0, 0, 0};             // actor location (glTF metres)
    core::Mat4 authored = core::Mat4::identity();   // authored world matrix (Matinee actors; identity if unknown)
    float yawRateRad = 0.0f;               // PHYS_Rotating
    // Current pose for Rendering: world matrix delta to pre-multiply the authored placement
    // (world_now = worldDelta * world_authored).
    core::Mat4 worldDelta = core::Mat4::identity();
    int pawnSet = -1, weaponSet = -1;      // moving collision sets (-1 = no collision)
};

struct ObjectiveObject {
    std::string actor, cls;                // e.g. TnDominationPoint_15247 / "TnDominationPoint"
    core::Vec3 pos{0, 0, 0};
    float yawDeg = 0.0f;
    // future HUD (TnObjectiveMarkerType*, MarkerString, RequiredGameRuleClass) [CONF future_hud_handoff]
    const char* markerType = "";
    const char* markerString = "";
    const char* requiredRule = "";
    bool activeInMode = false;             // the current game mode uses this objective
};

struct ModeVisibleActor {
    std::string actor, mesh;
    core::Vec3 pos{0, 0, 0};
    bool visible = false;                  // bHidden false after the GameplayStarted Kismet
};

class MapState {
public:
    bool load(const std::string& gameplayJson, MatchMode mode);
    // Split the movers' triangles out of the static collision meshes into moving sets.
    void registerCollision(CollisionWorld& pawn, CollisionWorld* weapon,
                           const std::vector<std::pair<std::string, std::vector<core::Vec3>>>& pawnTris,
                           const std::vector<std::pair<std::string, std::vector<core::Vec3>>>& weaponTris);
    void tick(float dt, CollisionWorld& pawn, CollisionWorld* weapon);

    float clock() const { return clock_; }  // seconds since GameplayStarted (Matinee / rotation / totems)
    MatchMode mode() const { return mode_; }
    const std::vector<MapMover>& movers() const { return movers_; }
    const std::vector<ObjectiveObject>& objectives() const { return objectives_; }
    const std::vector<ModeVisibleActor>& modeVisibleActors() const { return modeActors_; }
    // Domination totems = the TnDominationPoint entries of objectives() (placed in every mode).
    static std::vector<std::string> moverActorNames();

private:
    MatchMode mode_ = MatchMode::DM;
    float clock_ = 0.0f;
    std::vector<MapMover> movers_;
    std::vector<ObjectiveObject> objectives_;
    std::vector<ModeVisibleActor> modeActors_;
    // SkyBeam InterpTrackMove EulerTrack (degrees X roll / Y pitch / Z yaw), CIM_CurveAuto keys.
    struct Key { float in; core::Vec3 out, arrive, leave; };
    std::vector<Key> euler_;
    float matineeLength_ = 9.0022f;
    core::Vec3 evalEuler(float t) const;
    void pose();
};

// UE rotator (degrees) -> rotation matrix acting on glTF-space vectors (glTF = 0.01 * (X, Z, Y) of UE).
core::Mat4 ueRotationToGltf(float pitchDeg, float yawDeg, float rollDeg);

} // namespace game
