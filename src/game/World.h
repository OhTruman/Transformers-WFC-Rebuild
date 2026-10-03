// Clean-room reconstruction — the world: owns the level, player, and dynamic actors.
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "core/Math.h"
#include "game/Player.h"
#include "game/Pickup.h"
#include "game/PickupFactory.h"
#include "game/MapState.h"
#include "game/Match.h"
#include "game/MatchOpponent.h"
#include "game/Destructible.h"
#include "game/SpawnPoint.h"
#include "game/Collision.h"
#include "render/Mesh.h"
#include "assets/SkinnedModel.h"
#include "audio/Audio.h"
#include "game/WeaponMesh.h"
#include "game/WeaponFx.h"
#include "game/SoundCues.h"
#include "game/VehicleFx.h"
#include "game/VehicleNitro.h"

namespace render { class IRenderer; }

namespace game {

// Launch contract (Frontend / Integration -> Gameplay): the original StartLevel URL form
// "MP_IAC_Streets_Base_m?Game=TransContent.TnVersusGame?GameModeTag=TDM?PointsToWin=40?TimeLimit=900.00..."
// [CONF RE MILESTONE05_FRONTEND_MATCH_BOOTSTRAP §3.1 / §5.1]. Missing keys keep the TnOnlineGameSettings defaults.
struct MatchLaunch {
    std::string map = "MP_IAC_Streets";
    std::string modeTag = "TDM";
    MatchSettings settings = MatchSettings::forMode("TDM");
    static bool fromURL(const std::string& url, MatchLaunch& out);
};

// Authoritative state for the HUD / frontend layer (no drawing here). Field names follow the HUD bindings in RE
// bootstrap §6 (NotifySegmentedHealthChanged, NotifyOverShieldChanged, ammo notifies, <CurrentGame:...>, <PlayerOwner:...>).
struct HudGameState {
    // Pawn
    bool alive = true;
    float health = 0, healthMax = 0, overshield = 0, normalizedOverShield = 0;
    int activeSegment = 0, segmentCount = 4;
    int clipAmmo = 0, reserveAmmo = 0;
    bool vehicleForm = false, transforming = false;
    float timeToRespawn = -1.0f;                 // <PlayerOwner:TimeToRespawn> (MultiplayerRespawn_GFX)
    // Match
    bool matchActive = false;
    std::string modeTag;
    int matchState = 0;                          // Match::State
    int gameStatus = 0;                          // GRI.SetGameStatus 2 / 3 / 5
    int countdown = 0;                           // <CurrentGame:CurrentCountdown> (pre-match)
    int remainingTime = 0, elapsedTime = 0;      // GRI.RemainingTime (HUD clock = CurrentCountdown in progress)
    int goalScore = 0;                           // <CurrentGame:GoalScore>
    int teamScore[2] = {0, 0};                   // <CurrentGame:Teams>
    int myTeam = 255;                            // <PlayerOwner:TeamID>
    int score = 0, kills = 0, deaths = 0;        // <PlayerOwner:Score>, PRI kills / deaths
    float assists = 0.0f;
    int winnerTeam = -1;                         // GRI.Winner (-1 tie / none)
    std::string result;                          // TnVersusGameOverMessage: "Your team won" / "Your team lost" / "Tie game"
    // TDM player tags (TnObjectiveMarkerTypeTransformerVersus): hidden for self and the dead; allies labelled,
    // enemy markers disabled by default (no TnBuffSeeEnemyObjectiveMarkers / HardLocked / Revenge buffs here).
    struct Tag { int player; std::string name; int team; bool ally; bool drawn; core::Vec3 pos; };
    std::vector<Tag> tags;
};

// A static graybox block (fallback level + future collision volume).
struct Block {
    core::Vec3 center;
    core::Vec3 size;
    core::Vec3 color;
};

class World {
public:
    // Uploads meshes via the renderer. Tries the extracted vertical slice first; if any
    // required asset is missing, falls back to the procedural graybox arena.
    void load(render::IRenderer& renderer);
    // Match mode (authored TnOnlineGameSettings rule set) applied at load: objective / Kismet world state and the
    // player-start class follow it. Set before load(); the default is DM (Deathmatch, TnFreeForAllGame).
    void setMatchMode(MatchMode m) { matchMode_ = m; }
    MatchMode matchMode() const { return matchMode_; }

    void tick(float dt);                       // one fixed step
    void handleInput(const platform::InputFrame& in, float dt);
    void draw(render::IRenderer& r) const;

    // Instant-hit weapon trace from `origin` along `dir` (uses the pawn's weapon for
    // spread, range and range-based damage falloff). Damages the nearest target.
    void fireHitscan(const core::Vec3& origin, const core::Vec3& dir);

    // Audio: wired by the application; World loads cues and plays them on gameplay events.
    enum class Sfx { Fire, Reload, Transform, Land };
    void setAudio(audio::IAudio* a);
    void playSfx(Sfx s, const core::Vec3& pos);

    Player& player() { return player_; }

    // Truck nitro / ram state (Systems-owned, read-only for Gameplay: nitroActive(), ramActive(),
    // speedScale(), steeringScale() — Gameplay applies the movement effect).
    const VehicleNitro& vehicleNitro() const { return nitro_; }
    // Ram hit (TnTruckForm.AttemptToRam): for Gameplay's vehicle collision code. Returns true when
    // the hit counts — nitro active and `target` not yet hit this nitro — and then plays the ram
    // impact cue (RamSound Auto_Ram_Impact). Damage / momentum stay with Gameplay (values in
    // VehicleNitro::kRamDamage* / kExtraRamZVelocityUU).
    bool notifyRamHit(const void* target, const core::Vec3& pos);
    void gameplayRamContacts();   // Gameplay: nitro ram contacts -> notifyRamHit + ram damage

    // Authored pickup factories (Gameplay-owned state) and their transition events. Events raised during
    // this World::tick (cleared at its start), one per Pickup<->Sleeping transition: Systems plays
    // PickupSound/dialog, Rendering drives the pickup FX/mesh from available().
    const std::vector<PickupFactory*>& pickupFactories() const { return pickupFactories_; }
    const std::vector<PickupEvent>& pickupEvents() const { return pickupEvents_; }
    void raisePickupEvent(const PickupEvent& e) { pickupEvents_.push_back(e); }
    // Authored destructibles (state 0 intact / 1 destroyed / 2 settled) and their transition events.
    const std::vector<Destructible*>& destructibles() const { return destructibles_; }
    const std::vector<DestructibleEvent>& destructibleEvents() const { return destructibleEvents_; }
    void raiseDestructibleEvent(const DestructibleEvent& e) { destructibleEvents_.push_back(e); }
    bool usingSlice() const { return usingSlice_; }

    // Collision for queries by movement; null when none is loaded (graybox fallback).
    const CollisionWorld* collision() const { return collision_.valid() ? &collision_ : nullptr; }
    // Zero-extent (weapon / line-check) collision world; falls back to the movement world.
    const CollisionWorld* weaponCollision() const {
        return weaponCollision_.valid() ? &weaponCollision_ : collision();
    }
    // MP_IAC_Streets runtime map state: movers (poses for Rendering), mode visibility, objective objects.
    const MapState& mapState() const { return mapState_; }
    // Player starts (gameplay.json, 84 = 60 team + 24 FFA) and spawn clusters, for deterministic test spawns.
    struct StartPoint { std::string actor, cls, cluster; core::Vec3 pos; float yaw; };
    const std::vector<StartPoint>& startPoints() const { return starts_; }
    void teleportToStart(int index);   // test/debug spawn selection
    // Local versus match (launch-independent; not started in ordinary free play). The local player joins as player 0:
    // hidden and frozen in PendingMatch, spawned by the match at its chosen start, killed / respawned through it.
    void startLocalMatch(const MatchSettings& s);
    bool matchActive() const { return matchActive_; }
    Match& match() { return match_; }
    const Match& match() const { return match_; }
    int localMatchPlayer() const { return localPlayer_; }
    const std::vector<MatchEvent>& matchEvents() const { return matchEvents_; }   // consumed during the last tick
    bool localPlayerDead() const { return matchActive_ && localDead_; }
    void killLocalPlayer(int killer, bool suicide);     // death of the local pawn (harness / health / KillZ)
    // Front-end entry: map + mode + settings. Applies the mode's authored world state, resets the map as a fresh level
    // load, and starts the match. False (and nothing changes) for a map that is not loaded or an unsupported mode.
    bool launchMatch(const MatchLaunch& l);
    // TnPlayerPawn.TakeDamage for a match player (local or opponent): teammate damage is discarded except
    // TnDamageTypeAOE; damage reaching the pawn enters its DamageHistory; lethal damage -> Game.Killed(instigator).
    bool applyMatchDamage(int victimPlayer, int instigatorPlayer, float amount, bool aoe);
    // TEST / DIAGNOSTIC: a synthetic participant with its own Match player slot (see MatchOpponent.h).
    MatchOpponent* addMatchOpponent(const std::string& name, bool drawn);
    const std::vector<MatchOpponent*>& matchOpponents() const { return opponents_; }
    HudGameState hudState() const;
    // Authored collision actor(s) (collision_pawn.glb node: BlockingVolume_*, BSP, prop actor names) whose
    // bounds contain p (expanded by pad metres): for tracing blocked / incorrect areas back to authored objects.
    struct ColActor { std::string name, kind, mesh; core::Vec3 lo, hi; };
    const std::vector<ColActor>& collisionActors() const { return colActors_; }
    std::string collisionActorsAt(const core::Vec3& p, float pad, int maxNames = 4) const;

private:
    bool loadVerticalSlice(render::IRenderer& renderer);
    void buildGraybox();
    bool loadSpawn(const std::string& spawnJsonPath, core::Vec3& outPos, float& outYaw);
    void respawnPlayer();

    Player player_;
    std::vector<Block> blocks_;
    std::vector<std::unique_ptr<Actor>> actors_;
    std::vector<PickupFactory*> pickupFactories_;     // owned by actors_
    std::vector<PickupEvent> pickupEvents_;
    std::vector<Destructible*> destructibles_;        // owned by actors_
    std::vector<DestructibleEvent> destructibleEvents_;
    void loadPickupFactories(const std::string& gameplayJson);
    void loadDestructibles(const std::string& physicsJson, const std::string& contentRoot);
    std::vector<SpawnPoint> spawns_;
    CollisionWorld collision_;
    CollisionWorld weaponCollision_;
    MapState mapState_;
    MatchMode matchMode_ = MatchMode::DM;
    Match match_;
    std::vector<MatchOpponent*> opponents_;   // owned by actors_
    mutable int pushedRulesMode_ = -1;
    void resetForNewLevel();
    std::vector<MatchEvent> matchEvents_;
    bool matchActive_ = false, localDead_ = false;
    int localPlayer_ = -1;
    void tickMatch(float dt);
    std::vector<ColActor> colActors_;
    void syncMapPresentation(render::IRenderer& r) const;   // Gameplay world state -> renderer, each frame
    std::vector<StartPoint> starts_;
    int startCursor_ = 0;
    void loadStartPoints(const std::string& gameplayJson);

    core::Vec3 spawnPos_{0, 0, 0};
    float spawnYaw_ = 0.0f;
    float killZ_ = -1e9f;   // respawn if player falls below this Y

    render::MeshHandle mapMesh_ = render::kInvalidMesh;
    render::MeshHandle weaponMesh_ = render::kInvalidMesh;
    core::Vec3 mapColor_{0.55f, 0.57f, 0.6f};
    bool usingSlice_ = false;

    assets::SkinnedModel robotModel_;
    assets::SkinnedModel armModel_;      // CP_OptimusArm_SKEL (TnArmAttachment), Gameplay
    assets::SkinnedModel vehicleModel_;

    // Ion Blaster as an animated skeletal mesh (own Fire/Reload/Idle anims, sockets, notifies).
    assets::SkinnedModel weaponModel_;
    WeaponMesh weaponAnim_;
    unsigned weaponSeenShot_ = 0, weaponSeenReload_ = 0;
    std::vector<WeaponNotify> notifies_;
    void tickWeaponPresentation(float dt);
    void handleWeaponNotify(const WeaponNotify& n);
    // World transform of a weapon socket (MuzzleFlash/ShellSocket/MagSocket); false if unavailable.
    bool weaponSocketWorld(const char* socket, core::Mat4& out) const;

    // Original weapon effects (muzzle flash, tracer, impact squib) from the cooked FX data.
    WeaponFx fx_;

    audio::IAudio* audio_ = nullptr;
    audio::Sound
                 sndTransform_ = audio::kInvalidSound, sndLand_ = audio::kInvalidSound;
    bool prevGrounded_ = true;
    bool prevTransforming_ = false;
    SoundCues cues_;

    // Vehicle-form presentation (OptimusTruckForm BoostFx / HoverFX / JumpFX + boost sounds).
    VehicleFx vehicleFx_;
    int boostInst_[2] = {-1, -1};
    int hoverInst_[6] = {-1, -1, -1, -1, -1, -1};
    int jumpInst_[3] = {-1, -1, -1};
    bool hoverActive_ = false;
    bool vehiclePrevGrounded_ = true;
    int jumpCount_ = 0;
    bool boostActive_ = false;
    VehicleNitro nitro_;         // follows Gameplay's vehicleState().nitroRemain (presentation side)
    int ramInst_ = -1;

    // Vehicle engine audio (HmPlayerVehicleAudioComponent DriveSounds / JumpRev / land sounds).
    enum class EngineState { Off, OnLoad, OffLoad, JumpRev, Boost };
    EngineState engineState_ = EngineState::Off;
    int engineCue_ = -1;
    float airTime_ = 0.0f;
    void tickEngineAudio(float dt, bool vehicle, bool boost, bool grounded, bool tookOff, bool landed);
    float boostAge_ = 0.0f;
    bool boostWheelsChecked_ = false;
    int boostLoopCue_ = -1;
    void tickVehicleBoost(float dt);
    bool burstActive_ = false;
    float sinceShot_ = 0.0f;
    core::Vec3 listenerPos_{0, 0, 0};
};

} // namespace game
