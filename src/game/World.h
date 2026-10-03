// Clean-room reconstruction — the world: owns the level, player, and dynamic actors.
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "core/Math.h"
#include "game/Player.h"
#include "game/Pickup.h"
#include "game/SpawnPoint.h"
#include "game/Collision.h"
#include "render/Mesh.h"
#include "assets/SkinnedModel.h"
#include "audio/Audio.h"
#include "game/WeaponMesh.h"
#include "game/WeaponFx.h"
#include "game/SoundCues.h"
#include "game/PickupPresentation.h"
#include "game/VehicleFx.h"
#include "game/VehicleNitro.h"
#include "game/RobotFoley.h"
#include "game/AmbientAudio.h"
#include "game/LevelAudioHost.h"
#include "game/VehicleAudio.h"

namespace render { class IRenderer; }

namespace game {

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

    void tick(float dt);                       // one fixed step
    void handleInput(const platform::InputFrame& in, float dt);
    void draw(render::IRenderer& r) const;

    // Instant-hit weapon trace from `origin` along `dir` (uses the pawn's weapon for
    // spread, range and range-based damage falloff). Damages the nearest target.
    void fireHitscan(const core::Vec3& origin, const core::Vec3& dir);

    // Audio: wired by the application; World loads the original SoundCues and plays them on
    // gameplay / animation events, attached to their owner where the original attaches them.
    // `loadSliceMap`: also load MP_IAC_Streets' audio (the hard-wired vertical slice). A frontend boot passes false
    // and loads its first level itself (loadMapAudio("UI_FrontEnd_m")).
    void setAudio(audio::IAudio* a, bool loadSliceMap = true);

    // ==== SYSTEMS AUDIO CONTRACT (frontend / integration). The frontend owns screen state; Systems owns playback ====
    // ==== and lifetime. Every level (UI or match) goes through the same calls; nothing is level-specific.       ====
    // Level audio after a travel: the level's manifests (AssetTools <assets>/Maps/<level>/audio.json and / or the
    // compiled-in Systems level manifest): emitters, zones, reverb presets, pools, Kismet audio ops (music, beds,
    // timelines), cue bank. Any previously loaded level is unloaded first (music player included). Its level-start
    // Kismet audio (SeqEvent_GameplayStarted) runs on the next audio tick.
    bool loadMapAudio(const std::string& level);
    // Leave the level: unload its audio and every transient Systems state (player sounds, Systems FX, queues, the
    // level's music player); afterwards no voice, instance, queued event, level preset, level cue or sample remains.
    void unloadMapAudio();
    // A level Kismet trigger the frontend owns: "FsCommand:<cmd>" (GFx fscommand, e.g. "FsCommand:enterFrontEnd"),
    // "MovieStopped:<movie>" (e.g. "MovieStopped:FMV_intro"). Returns the op inputs it reached (0: none authored).
    int levelAudioEvent(const std::string& trigger) { return levelAudio_.event(trigger, listenerPos_); }
    // GFx Sound.PlaySound(name) / StopSound(name, fade) (TnSoundActionScriptBinding).
    int playUiSound(const char* name) { return levelAudio_.playUiSound(name); }
    bool stopUiSound(const char* name, float fade) { return levelAudio_.stopUiSound(name, fade); }
    // A Bink movie (logos, intro, loading screen) starts / stops: Engine.MovieSettings MovieMixerPreset
    // CINE_MUTE_FOR_BINK (Master volume 0, fade-in 0 s, fade-out 1 s) is enabled / disabled.
    void setMoviePlaying(bool playing) { levelAudio_.setMoviePlaying(playing); }
    // During a loading screen: decode the next level's streamed music now (avoids the first-play decode stall).
    bool prefetchLevelAudio(const std::string& level) { return levelAudio_.prefetch(level); }
    // The listener (frontend camera) for audio-only ticking; World::tick sets it from the game camera itself.
    void setAudioListener(const core::Vec3& p) { listenerPos_ = p; }
    // Frontend / lobby / loading frames (no match ticking): level audio (Kismet ops, pools, timelines, emitters),
    // music player, mixer fades, cue instances and voices.
    void tickAudioOnly(float dt);
    using AudioState = LevelAudioHost::State;
    AudioState audioState() const { return levelAudio_.state(); }
    // ==== end of contract ====
    // Round / match reset on the same map: player-side Systems audio + FX state restart, Kismet audio reset;
    // the map's bed keeps playing (see AmbientAudio::resetMatch).
    void resetSystemsForMatch();
    const std::string& audioMapName() const { return levelAudio_.level(); }
    const SoundCues& soundCues() const { return cues_; }
    const AmbientAudio& ambientAudio() const { return levelAudio_.ambient(); }

    Player& player() { return player_; }

    // Pickup SOUND (Systems): a Gameplay PickupEvent Taken -> Inventory.AnnouncePickup, the factory class's
    // PickupSound attached to the receiving (player) pawn. Respawn plays nothing. Pickup effects and their
    // state are Rendering's (WfcMapFx). Returns the cue instance (-1 = none authored / unknown class).
    int playPickupSound(const char* factoryClass, const core::Vec3& receiverPos);
    // (integration/milestone-04 glue calls pickupFx_.onTaken(actorName, ...); kept for that call site)
    PickupPresentation& pickupPresentation() { return pickupFx_; }
    SoundCues& cues() { return cues_; }

    // Truck nitro / ram state (Systems-owned, read-only for Gameplay: nitroActive(), ramActive(),
    // speedScale(), steeringScale() — Gameplay applies the movement effect).
    const VehicleNitro& vehicleNitro() const { return nitro_; }
    // Ram hit (TnTruckForm.AttemptToRam): for Gameplay's vehicle collision code. Returns true when
    // the hit counts — nitro active and `target` not yet hit this nitro — and then plays the ram
    // impact cue (RamSound Auto_Ram_Impact). Damage / momentum stay with Gameplay (values in
    // VehicleNitro::kRamDamage* / kExtraRamZVelocityUU).
    bool notifyRamHit(const void* target, const core::Vec3& pos);
    bool usingSlice() const { return usingSlice_; }
    // Tire squeal (HmPlayerVehicleAudioComponent TireSquealSoundParameter Optimus_Prime_Tire_Squeal,
    // Max 1.57 = pi/2): the wheels' slip angle in radians. Gameplay may supply its own value each step
    // (>= 0) = TnCarForm.Driving.UpdateSounds' CarSimulation.SlipAngle; hovering feeds 0 [CONF]. Until
    // Gameplay provides it the driving value is 0 (the squeal loop runs silent per its volume curve).
    void setTireSlipAngle(float rad) { tireSlipOverride_ = rad; }

    // Collision for queries by movement; null when none is loaded (graybox fallback).
    const CollisionWorld* collision() const { return collision_.valid() ? &collision_ : nullptr; }

private:
    bool loadVerticalSlice(render::IRenderer& renderer);
    void buildGraybox();
    bool loadSpawn(const std::string& spawnJsonPath, core::Vec3& outPos, float& outYaw);
    void respawnPlayer();

    Player player_;
    std::vector<Block> blocks_;
    std::vector<std::unique_ptr<Actor>> actors_;
    std::vector<SpawnPoint> spawns_;
    CollisionWorld collision_;

    core::Vec3 spawnPos_{0, 0, 0};
    float spawnYaw_ = 0.0f;
    float killZ_ = -1e9f;   // respawn if player falls below this Y

    render::MeshHandle mapMesh_ = render::kInvalidMesh;
    render::MeshHandle weaponMesh_ = render::kInvalidMesh;
    core::Vec3 mapColor_{0.55f, 0.57f, 0.6f};
    bool usingSlice_ = false;

    assets::SkinnedModel robotModel_;
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
    // Authored level particle emitters (map_fx.json: 8 x Steam_Sm_FX).

    audio::IAudio* audio_ = nullptr;
    SoundCues cues_;
    LevelAudioHost levelAudio_{cues_};     // the loaded level's audio, music player, UI sounds (Systems contract)
    PickupPresentation pickupFx_;           // stateless: onTaken resolves a factory class or actor name
    // Cue owners (SoundCues::Emitter::owner): attached AudioComponents follow these every tick.
    // Owner ids: the player pawn (its mesh origin, or a bone/socket of the displayed skeleton) and
    // the Ion Blaster (its mesh origin, or a WeaponMesh socket such as MuzzleFlash).
    enum CueOwner { kOwnPawn = 0, kOwnWeapon = 1 };
    bool resolveCueOwner(int owner, const std::string& socket, const core::Vec3& offset, core::Vec3& out) const;
    SoundCues::Emitter atPawn(const core::Vec3& up = {0, 0, 0}, const char* socket = "") const;
    SoundCues::Emitter atWeapon(const char* socket = "") const;
    // Robot movement foley (footsteps / jump / landing / idle / pivots) from the authored notifies.
    RobotFoley robotFoley_;
    std::vector<const char*> foleyCues_;
    // Transformation cue (HmAnimNotify_Sound on the Optimus transform clips) for the current fold.
    bool transformCuePlayed_ = false;
    int transformCue_ = -1;
    float trackT_ = 0.0f;
    bool prevTransforming_ = false;
    Form transformTarget_ = Form::Robot;
    bool prevFineAim_ = false;
    float tireSlipOverride_ = -1.0f;
    float tireSlip_ = 0.0f;
    void tickCharacterAudio(float dt);

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

    // Vehicle audio component (boost, engine states, jump, land, booster, nitro, ram, tire squeal).
    VehicleAudio vehicleAudio_;
    int vehLoadState_ = 0;
    bool prevDashing_ = false;
    float lastAudioMs_ = 0.0f;
    int occlusionRays_ = 0;
    void tickVehicleBoost(float dt);
    bool burstActive_ = false;
    float sinceShot_ = 0.0f;
    core::Vec3 listenerPos_{0, 0, 0};
    float mapAudioCycleT_ = 0.0f, matchResetCycleT_ = 0.0f;   // soak-test hooks (WFC_MAPAUDIO_CYCLE / WFC_MATCHRESET_CYCLE)
};

} // namespace game
