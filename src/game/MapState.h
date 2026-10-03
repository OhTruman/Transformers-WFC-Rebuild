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

// gameplay.json modes (playlists); the slice runs DM unless WFC_GAMEMODE selects another. A mode is defined by its
// authored TnOnlineGameSettings<tag>.Rules (exact TnGameRules classes); every world-state gate is
// GameInfo.HasRule(<exact class>) [CONF RE MILESTONE04_STREETS_RUNTIME_SEMANTICS + authored.db defaults].
enum class MatchMode { DM, TDM, CTF, KOTH, EXT, DOM };
const char* gameModeName(MatchMode m);
const std::vector<std::string>& gameRulesForMode(MatchMode m);

struct MapMover {
    enum class Kind { Rotating, Matinee };
    std::string actor, mesh;
    Kind kind = Kind::Rotating;
    core::Vec3 pivot{0, 0, 0};             // actor location (glTF metres)
    core::Mat4 authored = core::Mat4::identity();   // authored world matrix (Matinee actors; identity if unknown)
    core::Mat4 initialRot = core::Mat4::identity(); // InitialTM rotation (authored Rotation only, no DrawScale)
    float yawRateRad = 0.0f;               // PHYS_Rotating
    // Current pose for Rendering: world matrix delta to pre-multiply the authored placement
    // (world_now = worldDelta * world_authored).
    core::Mat4 worldDelta = core::Mat4::identity();
    int pawnSet = -1, weaponSet = -1;      // moving collision sets (-1 = no collision)
};

// Objective actors and their per-mode runtime state [CONF RE MILESTONE04_STREETS_RUNTIME_SEMANTICS: every check
// is GameInfo.HasRule(<exact rule class>) on the mode's TnOnlineGameSettings Rules].
struct ObjectiveObject {
    enum class State { Active, Inert, Disabled, Hidden, KothInactive };
    std::string actor, cls;                // e.g. TnDominationPoint_15247 / "TnDominationPoint"
    core::Vec3 pos{0, 0, 0};
    float yawDeg = 0.0f;
    State state = State::Inert;
    bool visible = false;                  // rendered (bHidden false)
    bool collision = true;                 // collision kept (Disabled factories: SetCollision(false,false))
    bool touchable = false;                // Touch/UnTouch handled (Inactive states ignore touches)
    bool activeInMode = false;             // the current game mode uses this objective
    // Objective marker (TnObjectiveManager / TnHUD.UpdateObjectiveMarker): class-hard-coded type; the HUD gets
    // _global.UpdateMarker(id, dist, sx, sy, sz, markerTypeString, description).
    const char* markerClass = "";          // e.g. "TransGame.TnObjectiveMarkerTypeDomination"
    const char* markerTypeString = "";     // class name minus "TnObjectiveMarkerType": "Domination", ...
    const char* markerString = "";         // authored MarkerString (factories / KOTH)
    const char* requiredRule = "";         // factories' RequiredGameRuleClass
    bool markerAdded = false;              // AddObjectiveMarker done (mode gate + state)
    bool markerShouldDisplay = false;      // MarkerType.ShouldDisplayMarker for the local observer
    float animClock = 0.0f;                // totems: DeactivatedLoopAnim always starts (seconds playing)
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
    const std::vector<std::string>& gameRules() const { return gameRulesForMode(mode_); }
    bool hasRule(const std::string& cls) const;      // exact class, e.g. "TransGame.TnGameRules_SingleFlagCTF"
    // Authored map actors whose rendered state Gameplay owns this frame (world.glb / class actor name, hidden).
    struct ActorVisibility { std::string actor; bool hidden; };
    std::vector<ActorVisibility> actorVisibility() const;
    const std::vector<MapMover>& movers() const { return movers_; }
    const std::vector<ObjectiveObject>& objectives() const { return objectives_; }
    const std::vector<ModeVisibleActor>& modeVisibleActors() const { return modeActors_; }
    // Domination totems = the TnDominationPoint entries of objectives() (visible only in DOM).
    static std::vector<std::string> moverActorNames();
    // KOTH: TnKingOfTheHillZoneBase.ActivateNewZone — exactly one zone Active (shown + marker), the rest Inactive.
    // MatchStarting picks a random initial zone; what triggers later rotations is not recovered (API only).
    int activeKothZone() const { return kothActive_; }
    void activateNewKothZone();
    float kothActiveTimeLeft() const { return kothTimeLeft_; }
    // Diagnostics: max |off-diagonal| of InitialRot^T * authored linear part, normalised (0 = the recovered
    // rotation reproduces the authored placement up to its DrawScale3D).
    float initialRotationResidual(const MapMover& m) const;

private:
    MatchMode mode_ = MatchMode::DM;
    float clock_ = 0.0f;
    std::vector<MapMover> movers_;
    std::vector<ObjectiveObject> objectives_;
    std::vector<ModeVisibleActor> modeActors_;
    int kothActive_ = -1;
    float kothZoneActiveTime_ = 60.0f;     // TnKingOfTheHillZoneBase ZoneActiveTime (authored CDO)
    float kothTimeLeft_ = 0.0f;
    unsigned kothRng_ = 0x5EED1234u;
    void applyObjectiveStates();
    // SkyBeam InterpTrackMove EulerTrack (degrees X roll / Y pitch / Z yaw). bUseQuatInterpolation: the rotation
    // is SlerpQuat between the bracketing keys with a linear alpha (the tangents are unused).
    struct Key { float in; core::Vec3 out, arrive, leave; };
    std::vector<Key> euler_;
    float matineeLength_ = 9.0022f;
    core::Mat4 evalRelativeRotation(float t) const;
    void pose();
};

// UE rotator (degrees) -> rotation matrix acting on glTF-space vectors (glTF = 0.01 * (X, Z, Y) of UE).
core::Mat4 ueRotationToGltf(float pitchDeg, float yawDeg, float rollDeg);

} // namespace game
