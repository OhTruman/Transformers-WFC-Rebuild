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
#include "game/CharacterAudio.h"
#include "game/VehicleAudio.h"
#include "game/VehicleFormAudio.h"
#include "game/WeaponAudio.h"
#include "game/AbilityAudio.h"
#include "game/VehicleFxDriver.h"
#include <map>
#include <set>

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
    // The movie's own Bink sound (see LevelAudioHost::startMovieAudio); a match world plays no movie today.
    bool startMovieAudio(const std::string& moviePath) { return levelAudio_.startMovieAudio(moviePath); }
    void stopMovieAudio() { levelAudio_.stopMovieAudio(); }
    // Match messages Gameplay broadcasts (the sound side; see MatchAudio.h for the original rules):
    //   TnGameRules HandleStartGame / HandleGameNearlyComplete / HandleEndGame -> gameTypeMessage(class, 0 / 1 / 2, ...);
    //   TnGameRules_ReportGameProgress* -> progressAnnouncement(switch); any TnAnnouncer.PlayEvent -> announcerEvent.
    MatchAudio& matchAudio() { return levelAudio_.match(); }
    // The player character's audio (CharacterAudio profile by roster chassis key, e.g. "Truck" Optimus, "Car"
    // Bumblebee, "Tank" Megatron): footsteps / foley / transform / vehicle sounds follow it. Default: "Truck".
    void setPlayerCharacterAudio(const std::string& chassisKey);
    // The player's current weapon class (its WeaponSounds: WP_Fire / WP_LowAmmoFire / WP_LoopingTail / fine aim).
    void setPlayerWeaponAudio(const std::string& weaponClass);

    // ---- Systems M08d hooks: Gameplay reports state / events, Systems plays (docs/handoff/SYSTEMS_M08D_*) ----
    // Vehicle form: Gameplay's per-step vehicle state (form kind, Hovering / Driving / Flying, dash, roll, nitro, jump,
    // ascend / descend) -> the form class's component calls; events() for vehicle FX.
    void tickVehicleAudio(float dt, const VehicleFormSignals& signals);
    const VehicleFormEvents& vehicleEvents() const { return vehicleEvents_; }
    // Per-chassis vehicle FX (VehicleFxDriver): Rendering's particle runtime, bound by the host; then every step the
    // vehicle state (before tickVehicleAudio: the returned hover BoosterAmount feeds VehicleFormSignals::thrusterAmount).
    void setVehicleFxRuntime(VehicleFxDriver::Runtime r) { vehicleFxDriver_.setRuntime(std::move(r)); }
    float tickVehicleEffects(float dt, const VehicleFxDriver::Inputs& in);
    const VehicleFxDriver& vehicleFxDriver() const { return vehicleFxDriver_; }
    // The equipped vehicle weapon (CharacterData.VehicleWeapons) for vehicle-form fire sounds.
    void setPlayerVehicleWeaponAudio(const std::string& weaponClass);
    // Load the cues of the loadout's weapon classes (robot + vehicle weapons, grenades) for this level.
    void preloadWeaponAudio(const std::vector<std::string>& weaponClasses);
    void ensureWeaponAudio(const std::string& weaponClass);
    const std::string& firingWeaponClass(bool vehicleForm) const;
    // One shot (instant hit or projectile launch) of the weapon actually fired.
    int onWeaponFired(const std::string& weaponClass, bool lowAmmo, bool vehicleForm, const core::Vec3& muzzle);
    // Projectiles (key = Gameplay's projectile identity): flight loop from spawn, explosion on Explode.
    void onProjectileSpawned(int key, const std::string& weaponClass, const core::Vec3& pos);
    void onProjectileMoved(int key, const core::Vec3& pos);
    void onProjectileExploded(int key, const std::string& weaponClass, const core::Vec3& pos);
    // [Systems M08i] Abilities / buffs (Gameplay owns them; RE pass 5 s12). A successful ability trigger ("Barrier"):
    // its OnTriggerSound at the pawn.
    void onAbilityTriggered(const std::string& abilityId);
    // The local pawn's buff state, every tick (buff class "TnBuffCloak"; team 0 Autobot / 1 Decepticon): transitions play
    // the Apply loop / Unapply sound.
    void setLocalBuffAudio(const std::string& buffClass, bool active, int team);
    void onLocalPawnBuffsLost();                       // death: buff loops stop, no Unapply sound
    void onAbilitiesJammed();                          // an ability press refused because abilities are blocked
    void setLocalHoverAudio(int hoverState);           // 0 none, 1 JumpingToHover, 2 Hovering (every tick)
    // The local player killed a pawn: the kill-confirm sound (victim form / character chassis "Car2", "Jet4", "Tank3").
    void onLocalKilledPawn(bool headshot, bool victimRobotForm, const std::string& victimChassisId);
    void onTransformFailed();                          // the local PressTransform was refused
    // Drain, every tick while the local Drain buff runs: targets this tick (HealSound), and each victim (DamageSound).
    void onDrainTick(int targets);
    void onDrainVictimTick(const core::Vec3& victimPos);
    void onProjectileRemoved(int key);
    // World hit / grenade bounce (fuseStarted: the grenade's first impact).
    void onProjectileHitWall(const std::string& weaponClass, const core::Vec3& pos, bool fuseStarted);
    // Beam weapon (Repair Ray), every tick while held: firing, target 0 none / 1 friendly / 2 enemy.
    void onBeamWeapon(const std::string& weaponClass, bool firing, int target);
    const WeaponAudio& weaponAudio() const { return weaponAudio_; }
    const VehicleAudio& vehicleAudio() const { return vehicleAudio_; }
    // Weapon equip / put-down (Gameplay): the held weapon mesh's WP_Equip / WP_PutDown animation sounds.
    void weaponAnimEvent(WeaponSoundTimeline::Event e) { weaponSounds_.play(e); }
    const char* weaponCue(const char* event) const;
    const CharacterAudioProfile& audioProfile() const {
        return audioProfile_ ? *audioProfile_ : CharacterAudio::defaultProfile();
    }
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
    int transformNotify_ = 0;                // next transform-clip notify to fire
    std::string weaponClass_ = "TransContent.TnWeaponIonBlaster";
    std::string vehicleWeaponClass_;
    std::set<std::string> weaponAudioLoaded_;      // per level (cleared with the level's cues)
    std::vector<std::string> loadoutWeaponClasses_; // the player's loadout (robot + vehicle weapons)
    WeaponAudio weaponAudio_;
    AbilityAudio abilityAudio_;
    VehicleFormAudio vehicleForm_;
    VehicleFormEvents vehicleEvents_;
    VehicleFxDriver vehicleFxDriver_;
    bool vehicleFxData_ = false;
    bool fxPrevShown_ = false;   // [Systems M08e]
    // TnHitEffectPlayer.LastHitEffectTimes per victim (here: the damage targets) per effect entry [CONF script].
    std::map<std::pair<const void*, int>, float> lastHitEffect_;
    // The held weapon's mesh-animation sounds (reload / idle / equip / put-down notifies), by weapon class.
    WeaponSoundTimeline weaponSounds_;
    std::vector<const std::string*> weaponSoundsFired_;
    float hitClock_ = 0.0f;
    const CharacterAudioProfile* audioProfile_ = nullptr;
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
