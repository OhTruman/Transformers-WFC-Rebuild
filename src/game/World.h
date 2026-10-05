// Clean-room reconstruction — the world: owns the level, player, and dynamic actors.
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <map>
#include <vector>
#include "core/Math.h"
#include "game/Player.h"
#include "game/Pickup.h"
#include "game/PickupFactory.h"
#include "game/MapState.h"
#include "game/Match.h"
#include "game/ChassisDef.h"
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
#include "game/PickupPresentation.h"
#include "game/VehicleFx.h"
#include "game/VehicleNitro.h"
#include "game/RobotFoley.h"
#include "game/AmbientAudio.h"
#include "game/LevelAudioHost.h"
#include "game/CharacterAudio.h"
#include "game/VehicleAudio.h"
#include <map>

namespace render { class IRenderer; }

namespace game {

// Launch contract (Frontend / Integration -> Gameplay): the original StartLevel URL form
// "MP_IAC_Streets_Base_m?Game=TransContent.TnVersusGame?GameModeTag=TDM?PointsToWin=40?TimeLimit=900.00..."
// [CONF RE MILESTONE05_FRONTEND_MATCH_BOOTSTRAP §3.1 / §5.1]. Missing keys keep the TnOnlineGameSettings defaults.
struct MatchLaunch {
    std::string map = "MP_IAC_Streets";
    std::string modeTag = "TDM";
    MatchSettings settings = MatchSettings::forMode("TDM");
    // [integration M08b] The local player's team from the lobby (TnGameLobby FinalCountdown / SwitchTeam: 0 Autobots,
    // 1 Decepticons; travels with the player). -1: Gameplay's PickTeam (direct boot / harnesses). Team games only.
    int localTeam = -1;
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
    std::string weaponName;                      // current weapon ItemName
    std::string weaponId, weaponIcon;            // provider UniqueId; Hud_GFX kill-feed / weapon icon (death_<Weapon>)
    bool weaponSimulated = true;                 // false: projectile / melee / grenade weapon equipped but not simulated [PARTIAL]
    bool weaponSwitching = false;
    std::vector<std::string> inventory;          // robot weapons (provider ids), CharacterData.WeaponTypes order
    int activeWeapon = 0;
    std::vector<std::string> vehicleWeapons;     // CharacterData.VehicleWeapons
    std::vector<std::string> loadoutRefused;     // selected weapons not equipped (unknown / chassis restriction)
    // Ability0 (Shift) / Ability1 (Ctrl): id, simulated by the rebuild, cooldown remaining (s), cooldown pending (in use).
    struct Ability { std::string id; bool implemented; float cooldown; bool active; };
    std::vector<Ability> abilities;
    bool dodging = false;
    bool cloaked = false;
    int hoverState = 0;
    // Ammo beacon (SpawnAmmoCrate): Rendering draws PROP_NEU_Pickups_p.AmmoPickup.PROP_NEU_AmmoPickup_STAT at ammoBeaconPos
    // (PickupRotationRate yaw 10000); objective marker "Ammo Beacon" for the owner's team.
    float drain = 0.0f;                          // Drain ability active (s left)
    float seeEnemies = 0.0f, refillOnKill = 0.0f, abilitiesJammed = 0.0f, hardLocked = 0.0f;   // killstreak buffs on the local pawn (s left)
    // Roller sphere: Rendering draws FX_RollerMine_p.Mesh.RollerMineAbility_STAT (scale 0.5) at rollerPos.
    int kamikazeMines = 0;                       // live MinePooper mines (positions: World::kamikazeMines())
    float tempWeaponLeft = 0.0f;                 // P.O.K.E. 2.0 seconds left (0 otherwise)
    bool roller = false, rollerArmed = false;
    core::Vec3 rollerPos{0, 0, 0};
    float rollerFuse = 0.0f, rollerHealth = 0.0f, rollerSlow = 0.0f;
    bool guidedMissile = false;                  // the local player is guiding a missile (camera follows it)
    core::Vec3 guidedMissilePos{0, 0, 0};
    float guidedMissileFuse = 0.0f;
    bool sentry = false;                         // the local SpawnSentry turret is up
    float sentryHealth = 0.0f;                   // of 135 (drains 4.5/s: Lifetime 30 s)
    core::Vec3 sentryPos{0, 0, 0};
    int sentryTarget = -1;
    bool ammoBeacon = false;
    core::Vec3 ammoBeaconPos{0, 0, 0};
    float ammoBeaconLife = 0.0f, ammoBeaconHealth = 0.0f;
    bool ammoBeaconBuff = false;                 // TnBuffAmmoBeaconIncreaseDamage on the local pawn
    bool barrier = false;                        // the local Barrier ability's wall is up
    float barrierHealth = 0.0f;                  // BarrierHealth 1000, DegenRate 15/s
    std::string pickupPrompt;                    // TnPickupManager prompt (E): "Code Of Power" / "Bomb" / "" (refreshed 0.1 s in the original)
    std::string heavyWeapon;                     // carried flag / bomb weapon ItemName ("" none): replaces the gun while held
    int grenades = 0;                            // grenade bag reserve (WT_Grenades); -1 = no bag
    int lockTarget = -1;                         // homing weapon: target match player (-1 none)
    float lockProgress = 0.0f;                   // LockOnTimer / LockOnTime
    bool locked = false;                         // lock acquired (the next shot homes)                          // 1 rising to hover, 2 hovering (TnAcrobaticsManager)                        // TnBuffCloak active (Rendering: cloak shader)
    // CTF / EXT: attacking team (GRI.AttackingTeam), rounds, carried objectives, planted bomb (CurrentObjectiveCountdown).
    int attackingTeam = 255, currentRound = 0, rounds = 0;
    // GRI values Hud_GFX reads through <CurrentGame:...> [CONF Default__TnGameReplicationInfoMultiplayer: AttackingTeamIndex -1,
    // CurrentObjectiveCountdown -1; CompetitiveScoreEnabled not authored = engine default 0 (HIGH)]:
    //  attackingTeamIndex = GRI.AttackingTeamIndex (CTF round attackers / EXT bomb holder team, else -1);
    //  currentObjectiveCountdown = whole seconds of the planted bomb fuse (EXT), else -1 [PARTIAL: DOM / KOTH not recovered].
    int attackingTeamIndex = -1, currentObjectiveCountdown = -1;
    bool competitiveScoreEnabled = false;
    bool betweenRounds = false;
    struct CarriedObj { int kind; int holder; int holderTeam; bool dropped; bool active; core::Vec3 pos; float autoReturn, returnLeft, sleep; };
    std::vector<CarriedObj> carried;
    bool bombPlanted = false; float bombFuse = 0.0f, bombDefuse = 0.0f; int bombPlantTeam = 255;
    bool localCarrying = false;
    int killStreak = 0;                          // PRI._CurrentKillStreak
    std::vector<std::string> killstreaks;        // AcquiredKillstreaks (newest last = CurrentKillstreakId)
    bool killstreakImplemented = false;          // the newest one is simulated by the rebuild
    float regenBuff = 0.0f, fastCooldownBuff = 0.0f, ammoLockBuff = 0.0f;   // buff time left (s)
    // Damage taken (TakeDamage -> HUD damage direction): increments per damaging hit; the instigator location at that hit
    // (world) and its bearing relative to the view (radians, 0 = ahead, + = right). Presentation belongs to Hud_GFX.
    int damageTakenCount = 0;
    core::Vec3 lastDamageFrom{0, 0, 0};
    float lastDamageBearing = 0.0f;
    bool vehicleForm = false, transforming = false;
    int cantTransformCount = 0;
    std::string selectedChassis, drawnChassis;   // resolved selection and the body actually spawned (always equal when spawned)
    std::string specialty;                       // applied specialty (Leader / Scientist / Scout / Soldier)
    std::string spawnError;                      // why the selected body cannot spawn (empty = fine)                  // increments per refused transform (HUD NotifyCantTransform + TransformFailedSound)
    float timeToRespawn = -1.0f;                 // <PlayerOwner:TimeToRespawn> (MultiplayerRespawn_GFX)
    bool spectating = false;                     // dead >= MinRespawnDelay 3.0 s: PlayerSpectating (UI event 4)
    // Match
    bool matchActive = false;
    std::string modeTag;
    int matchState = 0;                          // Match::State
    int gameStatus = 0;                          // GRI.SetGameStatus 2 / 3 / 5
    int countdown = 0;                           // <CurrentGame:CurrentCountdown> (pre-match)
    int remainingTime = 0, elapsedTime = 0;      // GRI.RemainingTime (HUD clock = CurrentCountdown in progress)
    int goalScore = 0;                           // <CurrentGame:GoalScore>
    int timeLimit = 0;                           // InitGame TimeLimit (s)
    int faction = 255;                           // resolved character faction (0 Autobot, 1 Decepticon; DM = 1)
    int teamScore[2] = {0, 0};                   // <CurrentGame:Teams>
    int myTeam = 255;                            // <PlayerOwner:TeamID>
    int score = 0, kills = 0, deaths = 0;        // <PlayerOwner:Score>, PRI kills / deaths
    float assists = 0.0f;
    int winnerTeam = -1;                         // GRI.Winner (-1 tie / none)
    std::string result;                          // TnVersusGameOverMessage: "Your team won" / "Your team lost" / "Tie game"
    std::string endReason;                       // EndGame reason: "Score", "" (time), "Forfeit"
    int winnerPlayer = -1;                       // FFA winner (GetWinningPRI; -1 draw)
    float matchOverTimeLeft = 0.0f;              // MatchOver -> ReturnToGameLobby (15 s)
    // Kill feed (TnDeathMessage broadcasts; Hud_GFX rows live 5 s + 1 s fade, max 5 shown), oldest first.
    std::vector<KillFeedEntry> killFeed;
    // Scoreboard rows (InGameStats / EndGameStats PlayerList: PRI name, team, score, kills, deaths).
    struct Row { int player; std::string name; int team; int score, kills, deaths; float assists; bool alive, local; };
    std::vector<Row> scoreboard;
    // TDM player tags (TnObjectiveMarkerTypeTransformerVersus): hidden for self and the dead; allies labelled,
    // enemy markers disabled by default (no TnBuffSeeEnemyObjectiveMarkers / HardLocked / Revenge buffs here).
    struct Tag { int player; std::string name; int team; bool ally; bool drawn; core::Vec3 pos; bool label = true; };
    std::vector<Tag> tags;
    // Objectives of the current mode (DOM nodes, KOTH zones): HUD markers + capture state.
    struct Objective {
        std::string actor, markerType;           // "Domination" / "KingOfTheHill"
        int pointNumber = 0;                     // DOM NodeID
        int ownerTeam = 255;                     // DefenderTeamIndex (255 neutral, 254 contested)
        bool active = false;                     // KOTH: the Active zone
        float captureProgress = 0.0f;            // DOM NormalizedCaptureTime (CurrentCaptureTime / CaptureTime)
        bool beingCaptured = false;              // DOM BeingCaptured (Flashing)
        float timeLeft = 0.0f;                   // KOTH ActiveTimeLeft
        core::Vec3 pos;
    };
    std::vector<Objective> objectives;
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
    // Runtime map directory under <asset root>/Maps (Frontend: from the selected TnDataProvider_MapInfo, e.g.
    // MP_IAC_Streets_Base_m -> "MP_IAC_Streets"). Set before load(); MP_IAC_Streets by default.
    void setMapName(const std::string& dir) { mapName_ = dir; }
    const std::string& mapName() const { return mapName_; }

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
    // Tire squeal (HmPlayerVehicleAudioComponent TireSquealSoundParameter Optimus_Prime_Tire_Squeal,
    // Max 1.57 = pi/2): the wheels' slip angle in radians. Gameplay may supply its own value each step
    // (>= 0) = TnCarForm.Driving.UpdateSounds' CarSimulation.SlipAngle; hovering feeds 0 [CONF]. Until
    // Gameplay provides it the driving value is 0 (the squeal loop runs silent per its volume curve).
    void setTireSlipAngle(float rad) { tireSlipOverride_ = rad; }
    // The multiplayer map this world loads (AssetTools VerticalSlice/Maps/<Map>: world.glb, collision_pawn / _weapon.glb,
    // gameplay.json, physics.json, navigation.json - one contract for every processed MP map). Set before load().
    void setMap(const std::string& m) { mapName_ = m; }
    float killZ() const { return killZ_; }   // persistent level WorldInfo.KillZ (m)
    // Pain-causing PhysicsVolumes of the map (AssetTools maps/<Map>/hazard_volumes.json): convex brush planes, damage
    // per second and damage type. Stock UE3 PhysicsVolume pain: on entry (bEntryPain) and every PainInterval while
    // touching, DamagePerSec * PainInterval [HIGH: stock defaults PainInterval 1, bEntryPain true].
    struct HazardVolume { std::string actor, damageType; float damagePerSec = 0, painInterval = 1.0f; bool entryPain = true;
                          core::Vec3 centroid{0, 0, 0};
                          struct Plane { core::Vec3 n; float w; };
                          std::vector<Plane> planes; };   // outward normal n, offset w (inside: n.p <= w)
    const std::vector<HazardVolume>& hazardVolumes() const { return hazards_; }
    int hazardAt(const core::Vec3& p) const;
    std::string mapDir() const;
    // "MP_UND_Gorge_BASE_m" / "mp_und_gorge" -> "MP_UND_Gorge" when that map's export exists; else the input.
    static std::string canonicalMapName(const std::string& m);

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
    void startLocalMatch(const MatchSettings& s, int localTeam = -1);   // localTeam: the lobby team (-1 = PickTeam)
    bool matchActive() const { return matchActive_; }
    Match& match() { return match_; }
    const Match& match() const { return match_; }
    int localMatchPlayer() const { return localPlayer_; }
    const std::vector<MatchEvent>& matchEvents() const { return matchEvents_; }   // consumed during the last tick
    bool localPlayerDead() const { return matchActive_ && localDead_; }
    void killLocalPlayer(int killer, bool suicide, const std::string& damageType = std::string());   // death of the local pawn
    // Front-end entry: map + mode + settings. Applies the mode's authored world state, resets the map as a fresh level
    // load, and starts the match. False (and nothing changes) for a map that is not loaded or an unsupported mode.
    bool launchMatch(const MatchLaunch& l);
    // TnPlayerPawn.TakeDamage for a match player (local or opponent): teammate damage is discarded except
    // TnDamageTypeAOE; damage reaching the pawn enters its DamageHistory; lethal damage -> Game.Killed(instigator).
    bool applyMatchDamage(int victimPlayer, int instigatorPlayer, float amount, bool aoe, const std::string& damageType = std::string());
    // Projectiles (TnProjectile + its TnProjectileData): straight flight at InitialSpeed (homing lock-on PARTIAL); on any hit
    // HurtRadius(Damage, DamageRadius) with stock UE3 linear falloff [HIGH]; the instigator is not hit by its own shot.
    struct Projectile {
        core::Vec3 pos, vel; float damage, radius, life; std::string damageType; int instigator;
        int target = -1;                       // homing target match player (SetTarget; -1 = flies straight)
        float homingForce = 0, closingDist = 0, closingForce = 0, closingTime = 0, maxSpeed = 0, closingRemain = -1.0f;
        bool lockRobots = false;
        // Grenade (TnProjectileGrenadeBase): gravity scale, bounce, fuse (starts on the first impact), resting, explode on pawn.
        bool grenade = false, explodeOnPawn = false, resting = false;
        float gravityScale = 1.0f, bounce = 1.0f, fuseMin = 0.0f, fuseMax = 0.0f;
    };
    // TnGrenadeThrower: G in robot form -> toss after TossDelay 0.4 s.
    void startLocalGrenadeToss();
    struct BarrierState {
        bool alive = false;
        core::Vec3 pos{0, 0, 0}; float yaw = 0.0f;
        float health = 0.0f, fade = -1.0f, t = 0.0f;   // fade: FadeOutTime countdown once health reached 0 (-1 = up)
        core::Mat4 world, boxInv;                      // mesh world matrix; world -> collision-box local
        core::Vec3 half{0, 0, 0};
    };
    float grenadeTossDelay_ = -1.0f, grenadeCooldown_ = 0.0f;
    BarrierState barrier_;
    // TnDroppedPickupAmmoBeacon (the local owner's) [CONF script + authored].
    struct AmmoBeacon { bool alive = false, landed = false; core::Vec3 pos{0, 0, 0}, vel{0, 0, 0}; float life = 0.0f, health = 0.0f; };
    AmmoBeacon beacon_;
    // TnSentryPawnAbility + TnAiSentryController (the local owner's, Default_TURRETDEF) [CONF RE §J + authored].
    struct Sentry {
        bool alive = false;
        core::Vec3 pos{0, 0, 0};
        float yaw = 0.0f, pitch = 0.0f, health = 0.0f, fireTimer = 0.0f, heat = 0.0f, overheat = 0.0f, t = 0.0f;
        int target = -1, shots = 0;
    };
    Sentry sentry_;
    // TnGuidedMissile (ability / GuidedMissileStreak) [CONF RE §J3 + authored GuidedMissile_PROJDATA / GuidedMissile_STRATEGY].
    struct GuidedMissile { bool alive = false; core::Vec3 pos{0, 0, 0}, vel{0, 0, 0}; float life = 0.0f; };
    GuidedMissile missile_;
    // TnRollerMineAbility (the local owner's) [CONF authored CDOs + RE §J4; PhysX ball HIGH].
    struct RollerMine { bool alive = false; core::Vec3 pos{0, 0, 0}, vel{0, 0, 0}; float t = 0.0f, health = 0.0f; bool onGround = false; };
    RollerMine roller_;
    float rollerDelay_ = -1.0f;
    void tickRollerMine(float dt);
    void explodeRollerMine();
    float missileDelay_ = -1.0f;
    void startGuidedMissile();
    void tickGuidedMissile(float dt);
    void detonateGuidedMissile(const core::Vec3& at);
    float sentryDelay_ = -1.0f;
    assets::SkinnedModel sentryModel_;
    bool sentryModelTried_ = false;
    render::MeshData sentryMesh_;
    void spawnSentry();
    void tickSentry(float dt);
    float beaconDelay_ = -1.0f;
    void tickAmmoBeacon(float dt);
    float barrierDelay_ = -1.0f;
    int barrierDyn_ = -1, barrierDynW_ = -1;
    assets::SkinnedModel barrierModel_;
    bool barrierModelTried_ = false;
    render::MeshData barrierMesh_;
    void spawnBarrier();
    void tickBarrier(float dt);
    core::Vec3 grenadeTarget_{0, 0, 0};
    const Weapon* grenadeBag(const Character& c) const;
    void spawnProjectile(const core::Vec3& pos, const core::Vec3& vel, const Weapon& w, int instigator);
    const std::vector<Projectile>& projectiles() const { return projectiles_; }
    void fireHitscanWith(const Weapon& w, const core::Vec3& origin, const core::Vec3& dirIn);
    // Controller fire entry: one shot of w from origin along dir (projectile spawn or one hitscan trace). Inline dispatch
    // through a hook World installs at load, so harnesses that stub World (tools/fidelity) still link with fireHitscan.
    std::function<void(const Weapon&, const core::Vec3&, const core::Vec3&)> weaponFireHook;
    void fireWeapon(const Weapon& w, const core::Vec3& origin, const core::Vec3& dir) {
        if (weaponFireHook) weaponFireHook(w, origin, dir); else fireHitscan(origin, dir);
    }
    int damageTakenCount_ = 0;
    core::Vec3 lastDamageFrom_{0, 0, 0};
    // TEST / DIAGNOSTIC: a synthetic participant with its own Match player slot (see MatchOpponent.h).
    MatchOpponent* addMatchOpponent(const std::string& name, bool drawn);
    const std::vector<MatchOpponent*>& matchOpponents() const { return opponents_; }
    HudGameState hudState() const;
    // Per-chassis pawn resources (AssetTools Characters/<ChassisId>: robot.glb, vehicle.glb, character.json, ArmBlueprint),
    // loaded on first use and kept for the session. ok == false carries the reason; nothing substitutes another body.
    struct ChassisAssets {
        ChassisDef def;
        assets::SkinnedModel robot, vehicle, arm;
        bool ok = false, hasArm = false;
        std::string error;
    };
    const ChassisAssets* chassisAssets(const std::string& id);
    // TnPawn.ApplyTransformer for the local pawn: models, rigs, collision, stats, weapon socket. False = unavailable.
    bool applyChassisToLocalPawn(const std::string& id);
    // TnPawn.ApplyTransformer for any pawn (local or participant): models, rigs, collision, stats, weapon / arm sockets.
    bool applyChassisToPawn(Character& pc, const std::string& id);
    // ApplySpecialty + ApplyWeapons / ApplyAbilities for any pawn; returns refused weapons.
    std::vector<std::string> applyCharacterTo(Character& pc, const CharacterSelection* sel, MatchPlayer* mp);
    // TnCharacterApplier.ApplyWeapons for the local pawn: CharacterData.WeaponTypes (custom selection, validated against the
    // chassis' TnDataProvider_Weapon restrictions) or the chassis' iconic preset; VehicleWeapons alike. Returns the
    // weapons that were refused (unknown provider / not allowed on this chassis).
    std::vector<std::string> applyLoadout(const CharacterSelection* sel);
    const std::string& localChassis() const { return localChassis_; }
    // [integration M08] EnergonColor TnCharacterApplier pushes for a team (0 Autobots / 1 Decepticons / else the neutral
    // TnTeamInfo): the CONFIRMED class defaults in the AssetTools chassis export (team_colour). false: no data (logged).
    bool teamEnergon(int team, float out[3]) const;
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
    std::string mapName_ = "MP_IAC_Streets";   // runtime directory of the loaded map (Frontend: setMapName)
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

    std::map<std::string, std::unique_ptr<ChassisAssets>> chassisCache_;
    std::map<std::string, std::unique_ptr<assets::SkinnedModel>> weaponModels_;   // raw umodel weapon meshes + AnimSets
    std::string shownWeapon_ = "IonBlaster";
    std::vector<std::string> loadoutRefused_;
    unsigned seenWeaponChange_ = 0;
    const assets::SkinnedModel* weaponModelFor(const WeaponDef& d);
    void syncShownWeapon();
    std::string localChassis_;
    std::vector<HazardVolume> hazards_;
    std::vector<Projectile> projectiles_;
    void tickProjectiles(float dt);
    void tickAbilityEffects(float dt);
    void tickHomingLock(float dt);
    // PlayerTargeting.GetHomingLockTarget picker (index 4) about the crosshair; robots only when allowRobots.
    int pickHomingTarget(bool allowRobots, float range) const;
    // TnAbilityAbilityJammer / TnAbilityTransformDisruptor projectiles (enemies only, no damage).
    struct BuffShot { core::Vec3 pos, vel; float radius, life; int kind; };   // kind 0 jammer, 1 transform disruptor
    std::vector<BuffShot> buffShots_;
    // TnProjectileKamikazeMineKillstreak (MinePooper) [CONF RE §K]: hover 2 s, then seek an enemy within SearchRadius 2000 UU
    // at HomingSpeed 2300 UU/s; 125 / 500 UU on contact; Health 50; LifeSpan 60.
    struct KamikazeMine { core::Vec3 pos, vel; float t = 0.0f, health = 50.0f; int target = -1; };
    std::vector<KamikazeMine> mines_;
    void tickKillstreakItems(float dt);
    void tickBuffShots(float dt);
    const Character* matchPawn(int matchPlayer) const;
    // TnWeaponHoming lock state of the local pawn's active weapon.
    int lockCandidate_ = -1, lockTarget_ = -1;
    float lockTimer_ = 0.0f, holdLockTimer_ = 0.0f;
    bool locked_ = false;
    void startLocalMelee(bool whirlwind);
    void tickLocalMelee(float dt);
    bool deferredKillstreak_ = false;
    int lockedClip_ = 0;
public:
    void applyKnockback(int victim, const core::Vec3& momentumUU, const std::string& damageType);   // RE §I gated knockback
    // TnAbilityBarrier / TnBarrierSpawnable (the local owner's) [CONF script + authored; RE §I3].
    const BarrierState& barrier() const { return barrier_; }
    bool ammoBeaconAlive() const { return beacon_.alive; }
    core::Vec3 ammoBeaconPos() const { return beacon_.pos; }
    void damageAmmoBeacon(float amount, int instigator);
    const Sentry& sentry() const { return sentry_; }
    bool guidedMissileAlive() const { return missile_.alive; }
    const RollerMine& rollerMine() const { return roller_; }
    const std::vector<KamikazeMine>& kamikazeMines() const { return mines_; }
    void damageRollerMine(float amount, int instigator);
    core::Vec3 guidedMissilePos() const { return missile_.pos; }
    void damageSentry(float amount, int instigator, const std::string& type);
    bool sentryRayHit(const core::Vec3& o, const core::Vec3& d, float range, float& t) const;
    bool barrierRayHit(const core::Vec3& o, const core::Vec3& d, float range, float& t) const;
    void damageBarrier(float amount, const std::string& type);
    // TnPlayerController.TriggerKillstreak for the local player: the newest acquired streak; RequiresRobotForm streaks in
    // vehicle form transform first and trigger after (DeferredTriggerKillstreak). Returns the triggered id or "".
    std::string triggerLocalKillstreak();
private:
    void radiusDamage(const core::Vec3& at, float damage, float radius, int instigator, const std::string& type);
    int localHazard_ = -1;
    float localPainTimer_ = 0.0f;
    void loadHazards();
    void tickHazards(float dt);
    render::IRenderer* renderer_ = nullptr;
    std::map<std::string, render::TextureHandle> texCache_;
    int texLoaded_ = 0, texFailed_ = 0;
    render::TextureHandle resolveTexture(const std::string& uri);
    void resolveModelTextures(assets::SkinnedModel& m);

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
