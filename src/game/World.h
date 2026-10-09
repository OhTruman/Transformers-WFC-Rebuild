// Clean-room reconstruction — the world: owns the level, player, and dynamic actors.
#pragma once
#include <array>
#include <functional>
#include <deque>
#include <memory>
#include "platform/Input.h"
#include <thread>
#include <string>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>
#include "core/Math.h"
#include "game/Player.h"
#include "game/Pickup.h"
#include "game/PickupFactory.h"
#include "game/MapState.h"
#include "game/Match.h"
#include "game/ChassisDef.h"
#include "game/MatchOpponent.h"
#include "game/BotRoster.h"
#include "game/BotBrain.h"
#include "game/BotNav.h"
#include "game/Progression.h"
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
#include "game/VehicleFormAudio.h"
#include "game/WeaponAudio.h"
#include "game/AbilityAudio.h"
#include "game/VehicleFxDriver.h"
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
    BotLaunch bots;                // ?BotsFriendly ?BotsEnemy ?BotDifficulty (PC ADAPTATION; absent = no bots)
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
    // Repair Ray beam this frame (for Rendering's RepairBeam Beam2 ribbon / Systems' beam sounds): start = muzzle, end = hit;
    // healing = a teammate is being repaired (WP event 1), else an enemy / world hit (event 2).
    bool repairBeam = false, repairBeamHealing = false;
    core::Vec3 repairBeamStart{0, 0, 0}, repairBeamEnd{0, 0, 0};
    int repairBeamTarget = -1;
    // Last vehicle-weapon shot (for the muzzle flash / tracer glue): its socket (0 = WeaponSocket_Primary, 1 = _Primary2),
    // the socket's world position, and a serial that increments once per shot.
    // Charge weapon (Plasma Cannon) state: 0 idle, 1 charging, 2-4 charge levels 1-3; HUD message (GetHudMessage).
    int weaponChargeState = 0;
    std::string weaponChargeMessage;
    // Charge presentation (TnChargeWeapon.UpdateChargeEffects) for Rendering / Systems:
    //   weaponChargeGlow     MaterialGlowAmount for the held weapon mesh's SetMaterialParameter(1, ...): 0, 1/3, 2/3, 1.
    //   weaponChargeSerial   +1 on every state change. Sounds by transition: -> 1 PlayWeaponEvent(9); -> 3 stop 9, play 10;
    //                        -> 4 stop 10, play 11; -> 0 (EndState) stop 9 / 10 / 11, play 12. Muzzle flash
    //                        StartMuzzleFlash(0 / 1 / 2 / 3 / 12) for state 1 / 2 / 3 / 4 / 0.
    //   weaponChargeFizzle   +1 when released before level 1: PlayWeaponEvent(22) instead of a shot.
    float weaponChargeGlow = 0.0f;
    unsigned weaponChargeSerial = 0, weaponChargeFizzle = 0;
    //   weaponChargeShotLevel  level 1-3 of the last released shot (with the weapon shot serial / projectile launch): fire mode
    //                          0 / 1 / 2 -> WP_Fire / WP_FireSecondary / WP_FireTertiary.
    int weaponChargeShotLevel = 0;
    int vehicleShotSerial = 0, vehicleShotSocket = 0;
    core::Vec3 vehicleShotMuzzle{0, 0, 0};
    // Current-weapon HUD values are for the HELD weapon (PC.Pawn.Weapon): the vehicle weapon in vehicle form.
    bool vehicleWeaponHeld = false;
    int clipMax = 0, reserveMax = 0;
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

// The bot code's view of the pawn it drives: a participant (implicit) or the local player (WFC_PLAYERBOT).
struct BotBody {
    Character* pc = nullptr;
    int player = -1;
    BotBody(Character& c, int p) : pc(&c), player(p) {}
    BotBody(MatchOpponent& o);   // NOLINT: implicit, every bot call site passes a participant
    Character& pawn() const { return *pc; }
    int matchPlayer() const { return player; }
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

    // ---- Systems M08d hooks: Gameplay reports state / events, Systems plays ----
    void tickVehicleAudio(float dt, const VehicleFormSignals& signals);
    const VehicleFormEvents& vehicleEvents() const { return vehicleEvents_; }
    // Per-chassis vehicle FX (VehicleFxDriver): Rendering's particle runtime, bound by the host; then every step the
    // vehicle state (before tickVehicleAudio: the returned hover BoosterAmount feeds VehicleFormSignals::thrusterAmount).
    void setVehicleFxRuntime(VehicleFxDriver::Runtime r) { vehicleFxDriver_.setRuntime(std::move(r)); }
    float tickVehicleEffects(float dt, const VehicleFxDriver::Inputs& in);
    const VehicleFxDriver& vehicleFxDriver() const { return vehicleFxDriver_; }
    void setPlayerVehicleWeaponAudio(const std::string& weaponClass);
    // Load the cues of the loadout's weapon classes (robot + vehicle weapons, grenades) for this level.
    void preloadWeaponAudio(const std::vector<std::string>& weaponClasses);
    void ensureWeaponAudio(const std::string& weaponClass);
    const std::string& firingWeaponClass(bool vehicleForm) const;
    // One shot (instant hit or projectile launch) of the weapon actually fired.
    int onWeaponFired(const std::string& weaponClass, bool lowAmmo, bool vehicleForm, const core::Vec3& muzzle, int fireMode = 0);
    // Projectiles (key = Gameplay's projectile identity): flight loop from spawn, explosion on Explode.
    void onProjectileSpawned(int key, const std::string& weaponClass, const core::Vec3& pos);
    void onProjectileMoved(int key, const core::Vec3& pos);
    void onProjectileExploded(int key, const std::string& weaponClass, const core::Vec3& pos);
    // [Systems M09b] Non-local participants (bots): every participant's loadout weapon classes at match load (their cues are
    // decoded then, not on a bot's first shot; re-applied when the level's audio loads), one fire sound per shot at the
    // shot origin (TnWeapon.PlayFiringSound, positional), and per trace that hit something its impact sound: a pawn ->
    // the hit-effect sound (as the local path), the world / a destructible (victimPlayer -1) -> the weapon's
    // DefaultImpactSound. Projectile flight / explosion sounds come through onProjectileSpawned / Exploded as for the local pawn.
    void preloadParticipantWeaponAudio(const std::vector<std::string>& weaponClasses);
    // [Systems M09c] Spawn-hitch fix: at match load, decode (on a worker) the character + weapon cue waves of every selection
    // that can spawn (faction presets, CaC slots, bot rosters: chassis keys and weapon classes), so the spawn-frame
    // setPlayerCharacterAudio / preloadWeaponAudio loads find them in the device cache. Applied now if the level's audio is
    // loaded, else when it loads. Returns waves queued.
    int preloadSelectionAudio(const std::vector<std::string>& chassisKeys, const std::vector<std::string>& weaponClasses);
    // [integration 09b] one selection (chassis + robot / vehicle weapon ids) -> Systems M09c preloadSelectionAudio.
    void queueSelectionAudio(const std::string& chassis, const std::vector<std::string>& weapons,
                             const std::vector<std::string>& vehicleWeapons);
    void onParticipantFired(const std::string& weaponClass, const core::Vec3& from);
    void onParticipantImpact(const std::string& weaponClass, const core::Vec3& at, int victimPlayer);
    // [Systems M09d] A non-local participant's (bot's) successful ability trigger: its OnTriggerSound and the notifies of its
    // Skill_<id> animation (Warcry chest hits / WAR_CRY_STATE_START, Shockwave SHIELD_PUSH, ...), resolved through that
    // body's own sound sets, at their authored times, attached to the caster [CONF RE pass 5 s12: notify sounds play for any
    // pawn; OnTriggerSound is replicated]. Buff sounds are not played for non-local pawns (OnlyPlaySoundOnLocalPlayer;
    // cloak is the exception and needs the bot's cloak state - not wired). `chassisKey` = the caster's chassis (Car2, Truck5).
    void onParticipantAbility(int player, const std::string& abilityId, const std::string& chassisKey, const core::Vec3& pos);
    // Releases the delayed participant notifies; call once per simulation step (with the participant shot loop).
    void tickParticipantAudio(float dt);
    // [Systems M09e] Per tick, for each live participant (idempotent: sounds start / stop on a change only):
    //   setParticipantBuffAudio(player, "TnBuffCloak", pawn.cloakRemain_ > 0, team, pos) - the cloak loop (Autobot / Decepticon
    //   cue by the participant's team) is heard by everyone; other buff classes are local-player only (silent here);
    //   setParticipantHoverAudio(player, pawn.hoverState_, pos) - the hover lift loop + land (0 none, 1 JumpingToHover,
    //   2 Hovering). onParticipantGone(player) when it dies / despawns: its loops stop with no Unapply / land sound.
    void setParticipantBuffAudio(int player, const std::string& buffClass, bool active, int team, const core::Vec3& pos);
    void setParticipantHoverAudio(int player, int hoverState, const core::Vec3& pos);
    void onParticipantGone(int player);
    // [Systems M09f] A participant's body audio, once per step: robot foley (footsteps / jump / land / pivots / idle from its
    // animation notifies), the transform notifies, and the vehicle component (engine / tread / boost / jump / roll ...) from
    // `vehicle` (the same VehicleFormSignals the local pawn's audio is driven by; .vehicle false outside vehicle form).
    // Attached to the pawn. Beyond kParticipantBodyCullM from the listener (beyond every body cue's audible range) nothing
    // runs and its vehicle loops stop - the 33-participant budget. `alive` false: everything stops silently.
    void tickParticipantBodyAudio(int player, const std::string& chassisKey, const Character& pawn,
                                  const VehicleFormSignals& vehicle, bool alive, float dt);
    int participantBodiesActive() const;          // diagnostics: participants inside the cull radius
    // [Systems M09j] A participant's weapon-mesh and action-layer sounds, once per step after tickParticipantBodyAudio (same
    // 70 m cull): the held weapon class's TnWeaponMesh event-anim notifies on each new shot / reload (reload mechanics, pump /
    // bolt; WeaponEventAnims, as the local weapon - the fire SHOOT itself stays onParticipantFired's) and the one-shot action
    // layer's notifies (melee swing, grenade throw; RobotFoley.actionLayer). Skill_* / Nav_Boost_F / Transform_Whirlwind_ROBO
    // action clips are skipped: onParticipantAbility plays those (no doubles). `weaponClass` "TransContent.TnWeapon<Id>"; the
    // serials are Weapon::shotSerial / reloadSerial; `actionClip` "" when no action plays.
    void tickParticipantWeaponAudio(int player, const std::string& weaponClass, unsigned shotSerial, unsigned reloadSerial,
                                    const std::string& actionClip, float actionTime, float dt);
    // Optional: where participant `player`'s pawn is now (sounds follow it); without it they stay at the cast position.
    std::function<bool(int player, core::Vec3& out)> participantPositionHook;
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
    void onDodgeStarted();                             // the local dodge began: Nav_Boost_* notifies (charged-jump footstep)
    // The local pawn's one-shot action clip (melee / Skill_* / whirlwind / grenade throw; "" none) and its time, every tick.
    void onActionClip(const std::string& clip, float t);
    // An ability whose animation Gameplay does not play: that clip's notifies on trigger ("Skill_Shockwave").
    void onAbilityAnimFallback(const std::string& clip);
    // The held charge weapon's state (0 idle, 1 charging, 2..4 levels) every tick, and a fizzle (released before level 1).
    void setChargeWeaponAudio(const std::string& weaponClass, int state);
    void onChargeFizzle(const std::string& weaponClass);
    // The local roller mine every tick (alive, age s, position) and its explosion.
    void setRollerMineAudio(bool alive, float t, const core::Vec3& pos);
    void onRollerMineExploded(const core::Vec3& pos);
    // The local guided missile every tick (alive, position) and its detonation.
    void setGuidedMissileAudio(bool alive, const core::Vec3& pos);
    void onGuidedMissileExploded(const core::Vec3& pos);
    // The local barrier (alive, fading = health 0) and sentry (alive, target -1 none) every tick; each sentry shot.
    void setBarrierAudio(bool alive, bool fading, const core::Vec3& pos);
    void setSentryAudio(bool alive, int target, const core::Vec3& pos);
    void onSentryShot(const core::Vec3& muzzle, bool worldHit, const core::Vec3& hit);
    // [Systems M09h] Every deployed ability actor of ANY owner (the local player or a participant), per tick - one live
    // instance per (kind, owner). Replaces the local-only setters above (do not drive the same local actor through both).
    //   beginAbilityActorAudio();
    //   for each instance: setAbilityActorAudio(kind, ownerPlayer, ownerIsLocal, state)
    //   (roller / missile detonation: onAbilityActorExploded(kind, ownerPlayer, ownerIsLocal, pos))
    //   endAbilityActorAudio();  - an instance not reported this tick stops silently (despawned)
    // A destroyed / expired actor should be reported once with alive = false (sentry: loop fade + DestroyedSound) before it
    // leaves the list. The actors outlive their owner's death unless Gameplay ends them (state-driven; onParticipantGone does
    // not touch them). Ammo Crate has no per-instance sound (AMMO_DEPLOY is its OnTriggerSound; PICK_UP is authored silent).
    enum class AbilityActor { RollerMine, GuidedMissile, Barrier, Sentry };
    struct AbilityActorState {
        bool alive = true;
        bool fading = false;           // barrier: health 0, fading out (DestroySound)
        float age = 0.0f;              // roller mine: seconds since spawn (ArmSound 3 s, buildup 8.5 s)
        int target = -1;               // sentry: current enemy target id, -1 none (ActivateSound on a new enemy)
        core::Vec3 pos{0, 0, 0};
    };
    void beginAbilityActorAudio();
    void setAbilityActorAudio(AbilityActor kind, int ownerPlayer, bool ownerIsLocal, const AbilityActorState& s);
    void onAbilityActorExploded(AbilityActor kind, int ownerPlayer, bool ownerIsLocal, const core::Vec3& pos);
    void endAbilityActorAudio();
    // The local player activated kill streak `id` (TnDataProvider_Killstreak UniqueId): its Self announcement
    // (team: the activator's, 0 Autobots / 1 Decepticons, for the FactionAnnouncementSound fallback).
    void onLocalKillstreakActivated(const std::string& id, int team);
    // [Systems M09i] Another participant's killstreak (TnKillstreakActivated* per receiving client [CONF RE pass 5 s12
    // addendum 11]): the local player hears FriendlyAnnouncementSound when OnSameTeam (a team game, same team), else
    // EnemyAnnouncementSound (FFA: everyone else is an enemy); a role with none falls back to the activator team's
    // FactionAnnouncementSound when authored. Through the announcer queue. The streaks' effects make no extra world sound
    // here: their buffs are OnlyPlaySoundOnLocalPlayer (heard through the receiving local pawn's buff audio), their spawned
    // actors (missile / mines / turret) through setAbilityActorAudio.
    void onParticipantKillstreakActivated(const std::string& id, int activatorTeam, bool sameTeamAsLocal);
    void setOvershieldAudio(float overshieldHealth);   // the local pawn's overshield health, every tick (alive)
    void onDodgeHitWall();                             // the local dodge hit a wall (robot form)
    // TnGrenadeBag.PerformToss (local): WP_Fire on a toss, WP_NoAmmoFire on a refused one (no grenades / cooldown / heavy).
    void onGrenadeToss(const std::string& grenadeClass, bool refused);
    // A pawn died at `pos` (local or not): vehicle-form death sound / robot melee-death sound.
    void onPawnDeath(const std::string& chassisId, bool vehicleForm, const std::string& damageType, const core::Vec3& pos);
    // A non-weapon hit (melee / whirlwind / slam / ram) on pawn `victimKey` of chassis `victimChassis`: its hit effect.
    void onPawnHitEffect(const std::string& damageType, const std::string& victimChassis, int victimKey, const core::Vec3& pos);
    // Kamikaze mine `key` every tick (position, target found), its explosion, its silent removal (fizzle).
    void setKamikazeMineAudio(int key, const core::Vec3& pos, bool targetFound);
    void onKamikazeMineExploded(int key, const core::Vec3& pos);
    void onKamikazeMineRemoved(int key);
    // Drain, every tick while the local Drain buff runs: targets this tick (HealSound), and each victim (DamageSound).
    void onDrainTick(int targets);
    void onDrainVictimTick(const core::Vec3& victimPos);
    void tickAbilityAudio();                           // [Systems M08i glue] Gameplay state -> ability / buff sounds
    int abilityAudioSerial_ = 0, jammedAudioSerial_ = 0, transformFailAudioSerial_ = 0;
    bool abilityAudioDead_ = false, dodgeAudio_ = false;
    int dodgeWallAudio_ = 0;                           // [Systems M08n] last seen Character::dodgeWallHits_
    long long chargeFizzleAudio_ = -1;                 // [Systems M08k glue] last seen Weapon::chargeFizzle (-1: not synced)
    void onProjectileRemoved(int key);
    // World hit / grenade bounce (fuseStarted: the grenade's first impact).
    void onProjectileHitWall(const std::string& weaponClass, const core::Vec3& pos, bool fuseStarted);
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
    void killLocalPlayer(int killer, bool suicide, const std::string& damageType = std::string(), const Match::KillContext* ctx = nullptr);   // death of the local pawn
    // The participant snapshot Match records with each gameplay event (pawn state of the local player or an opponent).
    ParticipantSnapshot participantSnapshot(int player) const;
    const Character* participantPawn(int player) const;   // the live pawn of a match player, or null
    Match::KillContext killContext(int instigator, int victim, const std::string& damageType) const;
    void recordSpawnEvent(int player);
public:
    // Presentation interpolation (Character::beginStep / setRenderAlpha) for every match pawn; Application passes
    // FixedStepClock::alpha() each rendered frame. Harnesses that never call it present the current step (alpha 1).
    void setRenderAlpha(float a);
    uint32_t eventLogSerial_ = 0;   // WFC_EVENTLOG: last serial logged
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
        std::string weaponClass;               // [Systems M08d] the firing weapon's class (projectile sounds)
        int audioKey = 0;
        float gravityScale = 1.0f, bounce = 1.0f, fuseMin = 0.0f, fuseMax = 0.0f;
        int visual = -1;     // projVisuals_ index (the firing weapon's authored projectile_visual)
        // TnProjectileGrenadeBase: bRotationFollowsVelocity false (the actor keeps its spawn rotation); Tick adds RotationRate x
        // dt to the mesh component's rotation; OnHitThing at rest zeroes it [CONF script + Default__TnProjectileDataGrenadeLauncher].
        float yaw0 = 0.0f, pitch0 = 0.0f, spin = 0.0f, spinRate = 0.0f;
        int fxHandle = -1;   // live FlightEffect particle system (renderer handle), -1 = none
    };
    // weapon.json projectiles[0].projectile_visual: FlightEffect (the projectile's visible body + trail), ExplosionEffect
    // (EmitterPool.SpawnEmitter at HitLocation, rotator(HitNormal)), and the class-default static mesh where one exists
    // (the thrown grenades) [CONF AssetTools + RE projectile_effect_bindings].
    struct ProjectileVisual { std::string weapon, flight, explosion; render::MeshHandle body = render::kInvalidMesh; };
    std::vector<ProjectileVisual> projVisuals_;
    void loadProjectileVisuals(const std::string& root, const std::function<void(std::vector<render::Material>&)>& resolveTextures);
    int projectileVisualFor(const char* weaponId) const;
    void projectileFxStart(Projectile& p);
    void projectileFxMove(const Projectile& p);
    void projectileFxEnd(Projectile& p, const core::Vec3& at, const core::Vec3& normal, bool explode);
    int projectileFxSpawned_ = 0, projectileFxExplosions_ = 0;   // diagnostics (WFC_PROJFXTEST)
    // Renderer particle calls made by the step (projectile flight / explosion): World-side ids mapped to renderer handles. While a
    // background part runs they are queued and replayed in order at the join (the renderer is main-thread only); else immediate.
    struct FxOp { int kind = 0; int vid = -1; std::string tmpl; core::Vec3 p{0, 0, 0}, f{0, 0, 0}, u{0, 0, 0}; int team = -1; bool explosion = false; };
    std::vector<FxOp> fxOps_;
    int pellet_ = 0;
    std::map<int, int> fxReal_;
    int fxNextVid_ = 0;
    int fxStepSpawn(const std::string& tmpl, const core::Vec3& p, const core::Vec3& f, const core::Vec3& u, int team, bool explosion);
    void fxStepMove(int vid, const core::Vec3& p, const core::Vec3& f, const core::Vec3& u);
    void fxStepStop(int vid);
    void runFxOp(const FxOp& op);
    bool fxLive(int vid) const { return vid >= 0 && fxReal_.count(vid) > 0; }
    int fxTeamOf(int player) const;   // ParticleModuleSwitchableColorScaleOverLife channel: team (team game) / faction (FFA)
    // TnGrenadeThrower: G in robot form -> toss after TossDelay 0.4 s.
    void startLocalGrenadeToss();
    void releaseGrenade(Character& gp, int player, const Weapon& gb, const core::Vec3& target);
    void startMeleeFor(Character& pc, int self, bool whirlwind, float viewYaw);
    void tickMeleeFor(Character& pc, int self, float dt);
    struct BarrierState {
        bool alive = false;
        core::Vec3 pos{0, 0, 0}; float yaw = 0.0f;
        float health = 0.0f, fade = -1.0f, t = 0.0f;   // fade: FadeOutTime countdown once health reached 0 (-1 = up)
        core::Mat4 world, boxInv;                      // mesh world matrix; world -> collision-box local
        core::Vec3 half{0, 0, 0};
        int owner = -1;                                // the participant whose ability spawned it
        float delay = -1.0f;                           // SpawnDelay countdown (>= 0 while pending)
        int dyn = -1, dynW = -1;                       // its dynamic collision sets (pawn / weapon worlds), pooled
        std::vector<int> passThrough;                  // pawns (match players) inside its box when it spawned: they walk out (user decision)
        int deadTicks = 0;                             // steps since it went away (kept one step with alive=false, then removed)
        render::MeshData mesh;
    };
    float grenadeTossDelay_ = -1.0f, grenadeCooldown_ = 0.0f;
    std::vector<BarrierState> barriers_;     // one per owner
public:
    const std::vector<BarrierState>& barriers() const { return barriers_; }
public:
    mutable int lastBarrierHit_ = -1;
    std::vector<int> freeBarrierDyn_, freeBarrierDynW_;
    void requestBarrier(int owner);            // TnAbilityBarrier for any participant
    std::string triggerKillstreakFor(int player);   // the newest acquired killstreak of any participant ("" when none / unsupported)
    void damageBarrierAt(size_t idx, float amount, const std::string& type);
    bool qaNoclip_ = false, qaGod_ = false;   // DEV / QA TOOLING
    int vehicleShotSerial_ = 0, vehicleShotSocket_ = 0;
    int vehicleFlashSerial_ = 0;   // [Systems M08h] the last vehicle shot whose muzzle flash was spawned (one per shot)
    // [Systems M08h] The current vehicle shot's socket in world space (WeaponSocket_Primary / _Primary2 per
    // vehicleShotSocket_, posed vehicle bone x socket - the transform Gameplay's noteVehicleShot origin comes from).
    // False outside vehicle form or before the first vehicle shot.
    bool vehicleShotSocketWorld(core::Mat4& out) const;
    // [Systems M08h] HmWeaponMesh.PlayFireEffects for a vehicle shot: the fired weapon's MuzzleFlash template at its
    // CurrentSocket, once per shot (serial). Returns the muzzle position (tracer start) or false.
    bool spawnVehicleMuzzleFlash(const std::string& weaponClass, core::Vec3& muzzleOut);
    core::Vec3 vehicleShotMuzzle_{0, 0, 0};
    // TnDroppedPickupAmmoBeacon (the local owner's) [CONF script + authored].
    struct AmmoBeacon { bool alive = false, landed = false; core::Vec3 pos{0, 0, 0}, vel{0, 0, 0}; float life = 0.0f, health = 0.0f;
                        int owner = -1; float delay = -1.0f; int deadTicks = 0; };
    std::vector<AmmoBeacon> beacons_;           // one per owner (TnAbilitySpawnAmmoCrate for any participant)
    void requestAmmoBeacon(int owner);
    void generateExtraStarts();
    void separatePawns();
    void botPathUpkeep(BotBody o, BotBrain& b, float dt);
    bool qaBotsFrozen_ = false, qaBotOverlay_ = false;   // DEV / QA TOOLING                       // pawn-vs-pawn blocking (cylinder push-out after movement)                 // extended matches: deterministic extra spawn points (Match::setGeneratedStarts)
    void damageAmmoBeaconAt(size_t idx, float amount, int instigator);
    const AmmoBeacon& localBeacon() const;
    // TnSentryPawnAbility + TnAiSentryController (the local owner's, Default_TURRETDEF) [CONF RE §J + authored].
    struct Sentry {
        bool alive = false;
        core::Vec3 pos{0, 0, 0};
        float yaw = 0.0f, pitch = 0.0f, health = 0.0f, fireTimer = 0.0f, heat = 0.0f, overheat = 0.0f, t = 0.0f;
        int target = -1, shots = 0;
        int owner = -1;                 // the match player whose ability spawned it (any participant)
        float delay = -1.0f;            // SpawnDelay countdown (>= 0 while pending)
        render::MeshData mesh;          // its posed WEP_DeployedTurret mesh
        bool poseFinal = false;         // the (non-looping) Activate clip has ended: the pose is constant, skinned once
        float sightCounter = 0.0f;      // the periodic sight check (SightCounter, ~0.2 s, seeded 0.2 x FRand) [HIGH stock UE3, RE 6756336]
        std::vector<int> seen;          // pawns the last sight check saw (LineOfSightTo within SightRadius 300 m), by match player
        int deadTicks = 0;              // steps since it died (kept one step with alive=false for presentation consumers)
    };
    std::vector<Sentry> sentries_;      // one per owner
    mutable int lastSentryHit_ = -1;
    void requestSentry(int owner);      // TnAbilitySpawnSentry for any participant
    void spawnSentry(Sentry& s);
    void damageSentryAt(size_t idx, float amount, int instigator, const std::string& type);
    // TnGuidedMissile (ability / GuidedMissileStreak) [CONF RE §J3 + authored GuidedMissile_PROJDATA / GuidedMissile_STRATEGY].
    struct GuidedMissile { bool alive = false; core::Vec3 pos{0, 0, 0}, vel{0, 0, 0}; float life = 0.0f; };
    GuidedMissile missile_;
    // TnRollerMineAbility for any participant (one per owner) [CONF authored CDOs + RE §J4; PhysX ball HIGH].
    struct RollerMine { bool alive = false; core::Vec3 pos{0, 0, 0}, vel{0, 0, 0}; float t = 0.0f, health = 0.0f; bool onGround = false;
                        int owner = -1; float delay = -1.0f; int deadTicks = 0; };
    std::vector<RollerMine> rollers_;
    void tickRollerMine(float dt);
    void explodeRollerMine();
    void explodeRollerAt(size_t idx);
    void damageRollerAt(size_t idx, float amount, int instigator);
    void requestRoller(int owner);
    const std::vector<RollerMine>& rollerMines() const { return rollers_; }   // every participant's (Rendering / Systems audio: t = age)
    int participantRollerSpawns_ = 0;   // diagnostics: rollers spawned by participants
    int participantRollersLive() const { int n = 0; for (const RollerMine& m : rollers_) n += m.alive && m.owner != localPlayer_; return n; }

    float missileDelay_ = -1.0f;
    void startGuidedMissile();
    void tickGuidedMissile(float dt);
    void detonateGuidedMissile(const core::Vec3& at);
    assets::SkinnedModel sentryModel_;
    bool sentryModelTried_ = false;
    void tickSentry(float dt);
    void tickAmmoBeacon(float dt);
    assets::SkinnedModel barrierModel_;
    bool barrierModelTried_ = false;
    void spawnBarrier(BarrierState& b);
    void tickBarrier(float dt);
    core::Vec3 grenadeTarget_{0, 0, 0};
    const Weapon* grenadeBag(const Character& c) const;
    void spawnProjectile(const core::Vec3& pos, const core::Vec3& vel, const Weapon& w, int instigator);
    const std::vector<Projectile>& projectiles() const { return projectiles_; }
    void fireHitscanWith(const Weapon& w, const core::Vec3& origin, const core::Vec3& dirIn);
    // Energon Repair Ray beam tick (TnWeaponRepair.ProcessBeamHit / TnWeaponBeam.ProcessBeamHit).
    // Inline dispatch through a hook World installs at load (harnesses that stub World, e.g. tools/fidelity, still link).
    std::function<void(const Weapon&, const core::Vec3&, const core::Vec3&)> repairBeamHook;
    void fireRepairBeam(const Weapon& w, const core::Vec3& origin, const core::Vec3& dir) { if (repairBeamHook) repairBeamHook(w, origin, dir); }
    void fireRepairBeamImpl(const Weapon& w, const core::Vec3& origin, const core::Vec3& dir);
    struct RepairBeam { bool active = false, healing = false, locked = false; core::Vec3 start{0, 0, 0}, end{0, 0, 0}; int target = -1; float time = 0.0f; };
    RepairBeam repairBeam_;
    // Repair Ray presentation (RepairBeam_WEPMESH TracerTemplates WP_Looping = FX_RepairBeam_p.FX.Tracer_RepairBeam_FX; squib
    // Squib_RepairTeam_FX healing / DefaultSquib Squib_RepairEnemy_FX otherwise) through Rendering's segment API.
    mutable int repairBeamFx_ = -1;
    bool repairSquibPending_ = false;
    mutable bool repairSquibDraw_ = false;
    bool repairSquibHealing_ = false;
    // Controller fire entry: one shot of w from origin along dir (projectile spawn or one hitscan trace). Inline dispatch
    // through a hook World installs at load, so harnesses that stub World (tools/fidelity) still link with fireHitscan.
    std::function<void(const Weapon&, const core::Vec3&, const core::Vec3&)> weaponFireHook;
    // Weapon.GetMuzzleLoc for the held robot weapon: its mesh's MuzzleFlash socket (WeaponDef muzzle bone + offset, posed) at
    // the hand socket. False when the shown mesh is not the active weapon (mid switch) or has no socket.
    std::function<bool(core::Vec3&)> heldWeaponMuzzleHook;   // installed by World::load (stub harnesses still link)
    bool heldWeaponMuzzle(core::Vec3& out) const { return heldWeaponMuzzleHook && heldWeaponMuzzleHook(out); }
    bool heldWeaponMuzzleImpl(core::Vec3& out) const;
    void fireWeapon(const Weapon& w, const core::Vec3& origin, const core::Vec3& dir) {
        if (weaponFireHook) weaponFireHook(w, origin, dir); else fireHitscan(origin, dir);
    }
    int damageTakenCount_ = 0;
    core::Vec3 lastDamageFrom_{0, 0, 0};
    // TEST / DIAGNOSTIC: a synthetic participant with its own Match player slot (see MatchOpponent.h).
    MatchOpponent* addMatchOpponent(const std::string& name, bool drawn);
    // Offline bot participants (PC ADAPTATION): generated identities (BotRoster) added as ParticipantKind::Bot Match players with
    // their class selection, clamped to MatchSettings::maxPerTeam / maxPlayers. Returns the number added.
    int addBots(const BotLaunch& b);
    void removeBots();
    int botDifficulty() const { return botDifficulty_; }
    const BotBrain* botBrain(int player) const;
    const std::vector<BotBrain>& botBrains() const { return bots_; }
    const BotNav& botNav() const { return botNav_; }
    // The MoveIntent a participant pawn simulated this step (bots: the AI's throttle / strafe / boost / steer, same meaning as the local
    // controller's input), or null. For presentation consumers (Systems engine / boost audio).
    const MoveIntent* participantIntent(int player) const {   // O(1) via the player -> opponent index
        const MatchOpponent* o = opponentByPlayer(player);
        return o && o->spawned() ? &o->intent() : nullptr;
    }
    bool ensureBotNav();
    // Per-step bot cost (diagnostics; WFC_BOTTEST / perf).
    double botMsAverage() const { return botTicks_ ? botMsAccum_ / (double)botTicks_ : 0.0; }
    double botMsMax() const { return botMsMax_; }
    void resetBotTiming() { botMsAccum_ = 0.0; botMsMax_ = 0.0; botTicks_ = 0; }
    // Participant (non-local) shots this step, for presentation layers (tracers / muzzle / sounds of bots): weapon id, the trace
    // or launch start, the end point and whether it hit something. Robot-weapon mesh FX for bots are not drawn yet [PARTIAL].
    // hitPlayer: pawn hit (-1 world / none). pellet: index within one fire event (NumShotsToFire): stock Weapon.InstantFire plays the fire
    // effects (muzzle flash + tracer toward that hit) for pellet 0 only; every pellet plays its impact [CONF RE 9a776fb TARGETED_PASS5].
    struct ParticipantShot { int player; std::string weapon; core::Vec3 from, to; bool impact; int hitPlayer = -1; int pellet = 0; };
    // The pellet index of the hitscan traces being fired (set by the NumShotsToFire loops, 0 otherwise).
    void setPelletIndex(int k) { pellet_ = k; }
    int pelletIndex() const { return pellet_; }
    const std::vector<ParticipantShot>& participantShots() const { return participantShots_; }
    // Participant shot presentation (muzzle flash / tracer / impact of bots' weapons). When set, called once per participant shot
    // with the shooter's MuzzleFlash socket world matrix (X = barrel forward; the eye frame when no weapon mesh is shown); the
    // integration glue points it at the player's per-weapon authored FX lookup. Unset: the WeaponDef's authored MuzzleFlash /
    // Tracer templates are spawned by name through the renderer particle API; a weapon with no template draws nothing (logged once).
    std::function<void(const ParticipantShot&, const core::Mat4&)> participantShotFxHook;
    // A participant (bot) triggered an ability: (match player, ability id, chassis id, caster position). Presentation hook for
    // the trigger sound / Skill_<id> notifies (Systems World::onParticipantAbility, bound by the integration glue).
    std::function<void(int, const std::string&, const std::string&, const core::Vec3&)> participantAbilityHook;
    // Diagnostics: spawned participants with a weapon shown (robot form) and how many of them have a posed weapon mesh.
    void participantWeaponStats(int& shown, int& withMesh) const {
        shown = withMesh = 0;
        for (const MatchOpponent* o : opponents_) if (o->spawned() && o->pawn().hasWeapon()) {
            ++shown; auto it = partWeapons_.find(o->matchPlayer()); withMesh += it != partWeapons_.end() && it->second.anim.valid() && !it->second.anim.pose().empty();
        }
    }
    // Progression feed (Frontend contract): XP events (grouped by transactionId per kill) and challenge stat increments for every
    // participant, produced from the event record. Frontend applies the local player's to the profile (CanGainXp rule, current
    // specialty). Drained by the caller.
    // (legacy drains: now read the presented queues, which every step fills; see presented())
    // (awards_ is drained into presented() at the end of every step; it is touched here only while no background part runs)
    std::vector<XpAward> drainXpAwards() { std::vector<XpAward> v; v.swap(presented_.xpAwards); if (!remainderRunning_) { auto r = awards_.drainXp(); v.insert(v.end(), r.begin(), r.end()); } return v; }
    std::vector<StatAward> drainStatAwards() { std::vector<StatAward> v; v.swap(presented_.statAwards); if (!remainderRunning_) { auto r = awards_.drainStats(); v.insert(v.end(), r.begin(), r.end()); } return v; }

    // ---- Presented state / commands (docs/ASYNC_SIM_STEP.md, step 1: filled synchronously at the end of every step) ----
    // Everything the main thread reads during a frame (HUD, frontend, glue) comes from presented(); writes go through submit().
    // Queues hold every step since the last consumePresented(), in step order (two steps in one frame no longer lose the first
    // step's events). When the step runs asynchronously (step 3) the main thread reads only this, between joins.
    struct PresentedFrame {
        std::vector<MatchPlayer> players;               // match().players() after the last step (scoreboard / results / lobby rows)
        std::vector<core::Vec3> positions;              // by match player: the pawn position (valid when present[i])
        std::vector<uint8_t> present;                   // 1 = the player has a live pawn
        std::vector<int> faction;                       // match().faction(p): TDM the team, FFA 1 (Decepticon)
        HudGameState hud;                               // hudState() after the last step
        int localPlayer = -1;
        float localHealth = 0.0f, localHealthMax = 0.0f;
        std::vector<float> localSegmentTops;            // health segment tops (HudFrame)
        int localMag = 0, localReserveMax = 0;          // the local weapon's magSize / reserveMax
        std::string localWeaponId;                      // the local weapon's def id ("" none)
        std::string localChassis;
        HudAimState aim;                                // player().controller().hudAimState()
        int matchState = 0;                             // Match::State
        std::string modeTag;
        int teamScore[2] = {0, 0};
        int elapsedTime = 0, remainingTime = 0;
        // step-ordered queues since the last consumePresented()
        std::vector<MatchEvent> matchEvents;
        std::vector<GameplayEvent> gameplayEvents;
        std::vector<XpAward> xpAwards;
        std::vector<StatAward> statAwards;
        std::vector<KillFeedEntry> kills;               // killHistory() entries since the last consume (weapon / damage type)
        // TnHudDataObserver* sources for the Hud_GFX callbacks (Frontend draws, Integration maps to HudFrame). Values = after the last
        // step; the two event lists are step-ordered queues like matchEvents (cleared by consumePresented()).
        struct HudObservers {
            struct Progress { std::string labelId; float value = 0.0f; std::string name; };   // labelId "" = no bar
            Progress progress;                          // DominationCapture / BombDefuseTimer / SingleFlagCTF (ReviveBuddy: no downed state)
            int attackingTeam = -1;
            std::string killstreakId;                   // the newest acquired reward ("" none)
            bool killstreakAvailable = false;           // the rebuild simulates it
            struct Ability { std::string id; float cooldownLeft = 0.0f, cooldownTime = 0.0f; bool active = false; bool implemented = false; };
            std::vector<Ability> abilities;             // slots 0 / 1
            struct Grenade { int ammo = -1; std::string type; int activeCount = 0; };   // ammo -1 = no bag
            Grenade grenade;
            // TnHudDataObserverLockOnState: 0 none, 1 locking, 2 locked. targetPos = the target's targetable location (pawn centre, as
            // the lock / homing use it); distance from the local pawn (UpdateMarker DistanceToObj).
            struct LockOn { int state = 0; int target = -1; float progress = 0.0f; core::Vec3 targetPos{0, 0, 0}; float distance = 0.0f; };
            LockOn lockOn;
            // TnHudDataObserverTargetType [CONF RE e5cb5fd]: ETargetTypeForHud 0 Friend / 1 Enemy / 2 None. Target = the crosshair pawn (the
            // rebuild has no aim assist), else the repair target; pawns only (players, AI, sentries). name = PRI.PlayerName only on a direct
            // crosshair hit of a pawn with a PRI; healthType ETnHealthDisplayType 0 Friendly / 1 Enemy / 2 None, health -1 with None.
            struct Target { int type = 2; int player = -1; bool sentry = false; std::string name; int team = 255; int healthType = 2; float health = -1.0f; };
            Target target;
            struct WeaponState { bool jammed = false; float spread = 0.0f; std::string message; };
            WeaponState weapon;
            struct Buff { std::string id; float time = 0.0f; };   // local pawn buffs with time left (s)
            std::vector<Buff> buffs;
            bool scrambled = false;                     // HUD scramble: no source in the rebuild yet [PARTIAL]
            float downedHealth = -1.0f;                 // -1: no downed state in versus (as the rebuild)
            std::vector<std::string> contextual;        // contextual-command prompts (pickup)
            float scoringMultiplier = 1.0f;
            int cantTransformCount = 0;                 // increments per refused transform (NotifyCantTransform)
        } hud2;
        // Objective / pawn markers (TnObjectiveManager -> TnHUD.UpdateObjectiveMarker): the markers displayed to the local viewer this
        // step. setup: 0 ally, 1 enemy, 2 neutral (per viewer). pos: the base world position (pawn: targetable centre; objectives: their
        // location; carried: the carrier or the dropped / home position). health -1 = none; progress -1 = none.
        // Field for field render::MarkerRequest (agents/rendering HudMarkers.h): the glue copies them.
        struct Marker {
            std::string key;                            // stable identity ("pawn:12", "obj:<actor>", "carried:0")
            std::string type;                           // the TnObjectiveMarkerType* class
            std::string setup;                          // "AllyMarkerSetup" / "TransformerEnemyMarkerSetup" / "MarkerSetup"
            core::Vec3 base{0, 0, 0};                   // MarkerBase.Location (glTF metres)
            float labelZ = -1.0f;                       // Versus pawns: CollisionHeight x 0.5; < 0 = authored
            std::string label;                          // PRI.PlayerName / node number
            bool drawHealthBar = false;                 // ally health bar (viewer specialty Scientist)
            float health = 1.0f;                        // 0..1
            std::vector<std::pair<std::string, std::array<float, 4>>> params;   // "Neutral", "Flashing", "CaptureProgress"
            std::string action;                         // Attack / Capture / Defend / Defuse / Escort / Kill / Plant / Return / Idle [PROV]
            float pulseT = -1.0f;
            bool removing = false;                      // (no generic removal fade in the original, RE: always false; Tombstone uses lifeSpan)
            float removedT = 0.0f;
            float lifeSpan = -1.0f;                     // Tombstone: seconds left (FadeOutTime via LifeSpan); -1 none
            int relation = -1;                          // 0 friendly, 1 enemy, 2 neutral
            int player = -1;                            // pawn markers (the match player)
            int owner = -1;                             // draw owner (the pawn's match player): TransformerHealthBar / EnemyMarkerHysterisis
        };
        std::vector<Marker> markers;
        struct DamageTaken { float yaw = 0.0f; float amount = 0.0f; int instigator = -1; core::Vec3 from{0, 0, 0}; };   // yaw relative to the view
        struct DamageCaused { int victim = -1; float amount = 0.0f; bool killed = false; };   // hit markers
        std::vector<DamageTaken> damageTaken;
        std::vector<DamageCaused> damageCaused;
        unsigned steps = 0;                             // steps since the last consumePresented()
        bool extendedLobby = false;                     // World::extendedLobby() at this step
    };
    const PresentedFrame& presented() const { return presented_; }
    // An extended lobby (the PC extension: more than 10 participants, or launched with ExtendedPlayers): the original's visible FX
    // thinning (EmitterPool cap, EffectIsRelevant for non-local impacts / casings) applies only here (user decision 2026-10-08).
    bool extendedLobby() const { return matchActive_ && (match_.settings().extendedSlots || match_.players().size() > 10); }
    // The weapon mesh archetype's impact / fire effect rules [CONF authored, RE 19c8fbf]: TnWeaponMesh MaxImpactEffectDistance 2500 UU
    // (impact squib + decal + sound of a non-local shooter), MaxFireEffectDistance 1000 UU (shell casings); squibs per second capped by
    // ImpactSquibPercentage, live squibs by ImpactSquibMaxCount (a per-weapon-mesh EmitterPool bucket). LODDistanceFactor 1.0; behind
    // the camera only within 1600 UU. By rebuild weapon id.
    struct ImpactFxRule { float maxImpactDistUU = 2500.0f, maxFireDistUU = 1000.0f, squibPercentage = 1.0f; int squibMaxCount = 10; };
    static ImpactFxRule impactFxRule(const std::string& weaponId) {
        ImpactFxRule r;
        if (weaponId == "AssaultRifle" || weaponId == "HeavyMG" || weaponId == "IonBlaster" || weaponId == "AssaultRifleVehicle" ||
            weaponId == "TurretIonBase" || weaponId == "TurretIonGun" || weaponId == "LightSentry") { r.squibPercentage = 0.6f; r.squibMaxCount = 5; }
        else if (weaponId == "PlaneMachineGun" || weaponId == "AssaultRiflePlane") { r.squibPercentage = 0.6f; r.squibMaxCount = 6; }
        else if (weaponId == "EmpShotgun") r.squibMaxCount = 13;
        else if (weaponId == "RepairSentry") r.squibMaxCount = 5;
        return r;
    }
    // Async step (docs/ASYNC_SIM_STEP.md step 3, variant B; on by default, WFC_ASYNCSTEP=0 / WFC_SIMTHREADS=0 = synchronous). A step = the local part (tickPrefix: commands, map,
    // abilities, match, the local controller) on the main thread, then the background part (bots, participants, weapons, FX, audio
    // glue, projectiles, actors, presented() fill). The main loop draws between the two and launches the background part after
    // World::draw; the next frame joins it first. The order of operations is exactly tick()'s, so the simulation is identical.
    //   frame: joinStep(); handleInput(); n x { tickPrefix(dt) }; draw; launchStep();
    // tickPrefix finishes a pending background part inline first (catch-up steps), so any interleaving keeps tick()'s order.
    void tickPrefix(float dt);
    void launchStep();          // the pending background part: on the sim thread (async) or inline
    void joinStep();            // wait for it, publish presented(), run the deferred main-thread work
    bool stepPending() const { return remainderPending_; }
    bool stepRunning() const { return remainderRunning_; }   // main-thread code must not touch simulation state while true
    // WFC_PLAYERBOT=<difficulty 0..2>: the bot brain drives the LOCAL player through its normal input (movement keys, camera yaw / pitch,
    // fire, jump, boost, dash, transform): the normal controller, follow camera, weapon and HUD paths run as in play (a "real play"
    // workload for fps rows / PGO). Call once per frame before handleInput, with no step running. Returns false when inactive.
    bool playerBotInput(platform::InputFrame& in, float dt);
    static int playerBotDifficulty();   // -1 = off
    static bool asyncStepEnabled();
    // Per-phase step timing (the WFC_TICKPROF slots) switched on from code, summed since the last reset (WFC_SCALETEST).
    static void setStepProfiling(bool on);
    static void stepProfileReset();
    static std::vector<std::pair<std::string, double>> stepProfileSums();
    void consumePresented() { presented_.matchEvents.clear(); presented_.gameplayEvents.clear(); presented_.kills.clear();
                              presented_.damageTaken.clear(); presented_.damageCaused.clear(); presented_.steps = 0; }
    // A command for the simulation: applied in submission order at the start of the next step (select a character, QA actions,
    // look settings, audio volumes / preloads, test damage). Deterministic: the same commands land at the same step boundary.
    void submit(std::function<void(World&)> command) { commands_.push_back(std::move(command)); }
    PresentedFrame presented_;
    PresentedFrame presentedBack_;            // async: filled by the background part, published at joinStep
    void publishPresented();
    void stepRemainder(float dt);
    void finishRemainder();                   // stepRemainder + fill + palette invalidation (any thread)
    bool remainderPending_ = false, remainderRunning_ = false, fillBack_ = false;
    float pendingDt_ = 0.0f;
    struct AsyncStepThread;
    std::shared_ptr<AsyncStepThread> asyncThread_;
    std::thread::id mainThread_ = std::this_thread::get_id();
    bool onMainThread() const { return std::this_thread::get_id() == mainThread_; }
    std::vector<const WeaponDef*> deferredWeaponLoads_;
    // Glue hooks fired by the background part (participant shot FX / abilities: renderer FX, audio) run on the main thread at the join,
    // in order: on the sim thread they would race the renderer / Systems. Presentation only.
    std::vector<std::function<void()>> deferredHooks_;
    template <class F> void presentHook(F&& f) { if (remainderRunning_ && !onMainThread()) deferredHooks_.push_back(std::forward<F>(f)); else f(); }
    double lastRemainderMs_ = 0.0, prefixMsAcc_ = 0.0, beginStepMsAcc_ = 0.0;
    bool palettesPending_ = false;   // WFC_BGPALETTE: built by a finished background part, not yet published
    double prefixSecMs_[12] = {}, prefixMark_ = 0.0;   // WFC_ASYNCLOG: the local part by section   // a model needed by the background part: loaded at the join (GL)
    void preloadHeldWeaponsOfPawns();
    std::vector<const WeaponDef*> preloadedDefs_;   // weapon defs whose model is cached (sorted; a pointer search, not the string map)
    void ensureAbilityModels();
    void debugForceStreak();         // WFC_FORCESTREAK (diagnostic)
    void clearMatchActors();   // a new match starts with no projectiles / ability actors / weapon views of the previous one
    bool barrierOverlaps(const BarrierState& br, const Character& c) const;
    std::vector<int> barrierIgnoreFor(int player) const;   // the pawn-world collision sets of barriers this pawn is walking out of      // barrier / sentry meshes + textures (GL): main thread, before any background part spawns one
    BotBrain playerBot_;             // WFC_PLAYERBOT's brain for the local player
    // Bots waiting for the shared incremental path search, first come first served (the slot went to the first bot in list order:
    // with 60+ bots repathing, late-listed bots starved for 20 s+ - WFC_STUCKWATCH, idle vehicles with an empty path).
    std::deque<int> botSearchQueue_;
    float playerBotTransformCd_ = 0.0f;
    float playerBotFireHold_ = 0.0f;   // s Fire stays held after the brain's last shot wish (bursts; auto weapons need it held)
    bool playerBotFireDown_ = false;
    void tickAbilityActors(float dt);   // every participant's ability actors: the background part
    std::vector<std::function<void(World&)>> commands_;
    size_t presentedGameplayEventCount_ = 0, presentedKillCount_ = 0;
    std::vector<PresentedFrame::DamageTaken> pendingDamageTaken_;
    std::vector<PresentedFrame::Marker> prevMarkers_;   // the last step's markers (removal fade)
    std::vector<PresentedFrame::DamageCaused> pendingDamageCaused_;
    void fillPresented(PresentedFrame& p);
    const AwardProducer& awards() const { return awards_; }
    const std::vector<MatchOpponent*>& matchOpponents() const { return opponents_; }
    HudGameState hudState() const;
    // Per-chassis pawn resources (AssetTools Characters/<ChassisId>: robot.glb, vehicle.glb, character.json, ArmBlueprint),
    // loaded on first use and kept for the session. ok == false carries the reason; nothing substitutes another body.
    struct ChassisAssets {
        ChassisDef def;
        assets::SkinnedModel robot, vehicle, arm;
        bool ok = false, hasArm = false;
        unsigned lastUse = 0;   // cacheGen_ of the last match that used it (evictUnusedAssets)
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
    bool chassisEnergonDefault(const std::string& chassis, float out[3]) const;
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
    int botDifficulty_ = 1;
    AwardProducer awards_;
    size_t xpLogged_ = 0;
    std::vector<BotBrain> bots_;
    BotNav botNav_;
    bool botNavTried_ = false;
    int botPathBudget_ = 0;
    int botSearchOwner_ = -1;          // the bot whose path search is in progress (BotNav time-sliced search)
    bool botSearchVehicle_ = false;
    void botPathFailed(BotBrain& b, bool vehicle);
    void applyWarcry(Character& pc, int self);      // TnAbilityWarcry for any participant
    void applyShockwave(Character& pc, int self);   // TnAbilityShockwave.Shockwave for any participant
    bool botTryAbility(BotBody o, BotBrain& b, const char* id);   // TnAbilityManager.TriggerAbility rules for a bot's slot
    double botMsAccum_ = 0.0, botMsMax_ = 0.0; long botTicks_ = 0;
    std::vector<ParticipantShot> participantShots_;
    // Participants' held weapons: the player's weapon models (weaponModelFor) posed per participant, with their fire / reload
    // event animations, drawn at each pawn's weapon socket.
    struct ParticipantWeaponView { std::string id; WeaponMesh anim; unsigned seenShot = 0, seenReload = 0; };
    std::map<int, ParticipantWeaponView> partWeapons_;
    struct PendingShotFx { std::string weapon; core::Mat4 muzzle; core::Vec3 to; bool tracer = true; int team = -1; };
    mutable std::vector<PendingShotFx> partShotFx_;   // filled per step, spawned at draw (renderer particle API)
    // Participants' Repair Ray beams (the player's beam presentation per bot): source / target refreshed each beam tick, alive for
    // 1.5 fire intervals after the last tick; the looping tracer segment is spawned / moved / stopped at draw.
    struct ParticipantBeam { core::Vec3 start{0, 0, 0}, end{0, 0, 0}; float time = 0.0f; mutable int fx = -1; };
    std::map<int, ParticipantBeam> partBeams_;
public:
    // Diagnostics: participants with a live Repair Ray beam.
    int participantBeamsLive() const { int n = 0; for (const auto& kv : partBeams_) n += kv.second.time > 0.0f; return n; }
    // Diagnostics: live sentries / barriers owned by participants other than the local player.
    int participantSentriesLive() const { int n = 0; for (const Sentry& s : sentries_) n += s.alive && s.owner != localPlayer_; return n; }
    int participantBarriersLive() const { int n = 0; for (const BarrierState& b : barriers_) n += b.alive && b.owner != localPlayer_; return n; }
    int participantBeaconsLive() const { int n = 0; for (const AmmoBeacon& b : beacons_) n += b.alive && b.owner != localPlayer_; return n; }
private:
    void tickParticipantWeapons(float dt);
    void addBotBrain(int player, int difficulty);
    void tickBots(float dt);
    void botThink(BotBody o, BotBrain& b);
    void botSteer(BotBody o, BotBrain& b, float dt, MoveIntent& in);
    void botAimAndFire(BotBody o, BotBrain& b, float dt);
    void botFire(BotBody o, BotBrain& b, Weapon& w, const core::Vec3& aimPoint);
    BotGoal botObjectiveGoal(BotBrain& b, const Character& pc);
    bool botModeGoal(BotBrain& b, const Character& pc, BotGoal& g);   // CTF / EXT / KOTH / DOM objective goals (false: none)
    core::Vec3 botSnap(const core::Vec3& p) const;                   // a point on the bot nav near p (p itself when none)
    core::Vec3 botEye(const Character& c) const;
    bool botLineOfSight(const core::Vec3& from, const core::Vec3& to) const;
    void fireHitscanAs(int instigator, const Character& shooter, const Weapon& w, const core::Vec3& origin, const core::Vec3& dir);
    mutable int pushedRulesMode_ = -1;
    mutable int pushedPoolCap_ = -1;     // [integration 09c] last setEmitterPoolCap pushed
    bool fxSpawnPooled_ = false;
    std::unordered_map<int, float> squibAccum_;   // [integration 09c] per-shooter ImpactSquibPercentage accumulator (extended lobbies)         // [integration 09c] the next generic spawnAt is an EmitterPool spawn (impact squib)
    // [integration 09c] syncMapPresentation's per-frame keys, built once per factory / objective set (no string building per frame).
    struct MapFxKeys { const void* src = nullptr; std::string custom, highlight; };
    mutable std::vector<MapFxKeys> pickupFxKeys_, objectiveFxKeys_;
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
    // Robot bodies assembled from shared AnimSets: each .anim.gltf is parsed once (loadAnimationFile) and its clips are
    // appended to every skeleton that uses it, instead of re-parsing each chassis' robot.glb with its baked copy of ~300
    // clips (~1.2 s of the ~2 s first load, WFC_SPAWNPROF). Load scheduling only, not original behaviour.
    struct SharedAnimFile { assets::AnimFile file; std::map<std::string, size_t> byName; bool ok = false; unsigned lastUse = 0; };
    std::map<std::string, std::unique_ptr<SharedAnimFile>> animFiles_;
    const SharedAnimFile* sharedAnimFile(const std::string& path);
public:
    bool loadRobotShared(const ChassisDef& def, assets::SkinnedModel& out);   // false -> the caller uses robot.glb
    // WFC_ANIMSHARECHECK: robot.glb vs loadRobotShared for one chassis; returns a one-line report, ok = identical within eps.
    std::string compareRobotShared(const std::string& chassisId, bool& ok);
private:
    // Load (and prewarm) the held-weapon models of these provider / class ids ahead of their first equip: the first time a
    // weapon becomes the held weapon its model loads on that frame (3-38 ms, HeavyMG 107 ms; WFC_WEAPONLOADPROF).
    void preloadHeldWeaponModels(const std::vector<std::string>& weapons);
public:
    // Preload (cache + prewarm) the bodies and held-weapon models these selections would spawn with for the local player's
    // faction - for Frontend's match loading step with the player's saved custom (CaC) characters, so picking one in the lobby
    // does not load on that frame. Safe to call any time after the match launched; already-cached entries cost nothing.
    // Load scheduling only, not original behaviour.
    void preloadSelections(const std::vector<CharacterSelection>& selections);
private:
    std::vector<std::string> preloadedSelection_;   // the local selection's weapons last preloaded
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
    struct KamikazeMine { core::Vec3 pos, vel; float t = 0.0f, health = 50.0f; int target = -1; int audioKey = 0; };   // [Systems M08p] audioKey
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

    // ---- DEV / QA TOOLING - not original WFC behaviour, never part of a fidelity claim. Every call is a no-op unless the process
    // was started with WFC_QA=1 (Frontend's QA tool window drives these). They reuse the real systems: the loadout goes through
    // applyLoadout (provider restrictions apply), respawn through the real death / respawn wave, teleport through the authored
    // player starts. ----
    static bool qaEnabled();
    std::vector<std::string> qaWeaponIds(bool vehicle = false) const;        // WeaponTable provider ids (robot or vehicle weapons)
    std::vector<std::string> qaSetLoadout(const std::vector<std::string>& providerIds);   // returns the refused ids; kept for respawns
    void qaRespawn();                                                         // suicide (no score) -> the normal respawn wave
    // Live character swap: the selection goes through the real PRI._SelectedCharacter path (Match::selectCharacter), then
    // qaRespawn - the next spawn applies it (body, loadout, abilities, colours, vehicle form) exactly like a normal pick.
    void qaSetCharacter(const CharacterSelection& sel);
    std::vector<CharacterSelection> qaCharacterChoicesAlways() const;   // the four class presets (no QA gate; diagnostics / tests)
    // Choices for the swap: the four class presets (PCD_MP chassis per faction + WeaponTypes), the local faction resolved at spawn.
    std::vector<CharacterSelection> qaCharacterChoices() const;
    void qaTeleportToStart(int index);                                       // authored player start #index (wraps)
    void qaSetNoclip(bool on);                                               // UFO camera-relative flight, no collision / gravity
    void qaSetGodMode(bool on);                                              // the local pawn ignores damage
    void qaKillAllBots();                                                    // every bot dies (no score / XP), normal respawn wave
    void qaFreezeBots(bool on);                                              // bots stop thinking / moving / firing; pawns stay, take damage
    bool qaBotsFrozen() const { return qaBotsFrozen_; }
    void qaSetBotOverlay(bool on);                                           // per-bot debug draw (target line, nav path, waypoint)
    bool qaBotOverlay() const { return qaBotOverlay_; }
    void qaTeleportToAim();                                                  // the local pawn to the point under the crosshair
    struct QaBotLabel { core::Vec3 pos; std::string text; int player; };     // overlay labels (world position; the panel projects them)
    std::vector<QaBotLabel> qaBotLabels() const;
    void drawQaBotOverlay(render::IRenderer& r) const;
    // A vehicle weapon shot left this socket (PlayerController; reported in HudState for the flash / tracer).
    void noteVehicleShot(int socket, const core::Vec3& muzzle) { ++vehicleShotSerial_; vehicleShotSocket_ = socket; vehicleShotMuzzle_ = muzzle; }
    // Projectile FX diagnostics: renderer has the particle API, FlightEffects spawned, ExplosionEffects spawned, live projectiles.
    static bool projectileFxApi();
    int projectileFxSpawned() const { return projectileFxSpawned_; }
    int projectileFxExplosions() const { return projectileFxExplosions_; }
    size_t liveProjectiles() const { return projectiles_.size(); }
    double profileWeaponModelLoad(const WeaponDef& d);   // diagnostics (WFC_SPAWNPROF): first-use load time of a weapon model, ms
    core::Vec3 projectilePos(size_t i) const { return i < projectiles_.size() ? projectiles_[i].pos : core::Vec3{0, 0, 0}; }
    core::Vec3 projectileVel(size_t i) const { return i < projectiles_.size() ? projectiles_[i].vel : core::Vec3{0, 0, 0}; }
    float projectileDamage(size_t i) const { return i < projectiles_.size() ? projectiles_[i].damage : 0.0f; }
    float projectileSpin(size_t i) const { return i < projectiles_.size() ? projectiles_[i].spin : 0.0f; }
    bool projectileResting(size_t i) const { return i < projectiles_.size() && projectiles_[i].resting; }
    const std::string& projectileFlightTemplate(size_t i) const { static const std::string none; return i < projectiles_.size() && projectiles_[i].visual >= 0 ? projVisuals_[(size_t)projectiles_[i].visual].flight : none; }
    bool qaNoclip() const { return qaNoclip_; }
    bool qaGodMode() const { return qaGod_; }
    std::string qaStatus() const;                                            // map / mode / body / form / weapon / position
    // TnAbilityBarrier / TnBarrierSpawnable (the local owner's) [CONF script + authored; RE §I3].
    const BarrierState& barrier() const;   // the local player's live barrier (an empty one when none)
    bool ammoBeaconAlive() const { return localBeacon().alive; }
    core::Vec3 ammoBeaconPos() const { return localBeacon().pos; }
    const std::vector<AmmoBeacon>& ammoBeacons() const { return beacons_; }   // every participant's (Rendering draws each)
    void damageAmmoBeacon(float amount, int instigator);
    const Sentry& sentry() const;   // the local player's sentry (an empty one when none)
    Character* participantPawnMutable(int player) { return const_cast<Character*>(participantPawn(player)); }
    // match player -> its MatchOpponent (participantPawn in O(1); it was a scan per call, quadratic in big lobbies); rebuilt whenever
    // opponents_ changes.
    std::vector<MatchOpponent*> oppByPlayer_;
    // The participant of a match player in O(1) (nullptr for the local player / none): for glue that maps players to their actors.
    MatchOpponent* opponentByPlayer(int player) const { return player >= 0 && (size_t)player < oppByPlayer_.size() ? oppByPlayer_[(size_t)player] : nullptr; }
    void rebuildOppIndex() {
        oppByPlayer_.clear();
        for (MatchOpponent* o : opponents_) {
            const int p = o->matchPlayer();
            if (p < 0) continue;
            if ((size_t)p >= oppByPlayer_.size()) oppByPlayer_.resize((size_t)p + 1, nullptr);
            oppByPlayer_[(size_t)p] = o;
        }
    }
    bool guidedMissileAlive() const { return missile_.alive; }
    const RollerMine& rollerMine() const;   // the local player's (HUD / tests)
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
    // Session caches are bounded per match (user decision 2026-10-08: no growth across matches): launchMatch bumps cacheGen_ after the
    // previous match's pawns are gone, everything the new match loads or reuses is stamped, and evictUnusedAssets frees the rest
    // (models with their renderer mesh caches, shared anim files, textures no kept model or the map uses).
    unsigned cacheGen_ = 1;
    std::map<std::string, unsigned> weaponUse_;
    std::set<std::string> pinnedTex_;          // the map's (and the boot-time) textures: never evicted
    void evictUnusedAssets();
public:
    // WFC_EVICTTEST: chassis / weapon model loads and evictions so far (a load after a match began is a mid-match hitch).
    int chassisLoads() const { return chassisLoads_; }
    int weaponLoads() const { return weaponLoads_; }
    size_t chassisCached() const { return chassisCache_.size(); }
    size_t texturesCached() const { return texCache_.size(); }
private:
    int chassisLoads_ = 0, weaponLoads_ = 0;
    void releaseModelGpu(const assets::SkinnedModel& m);
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
    std::string vehicleWeaponClass_;
    std::set<std::string> weaponAudioLoaded_;      // per level (cleared with the level's cues)
    std::vector<std::string> loadoutWeaponClasses_; // the player's loadout (robot + vehicle weapons)
    std::vector<std::string> participantWeaponClasses_;   // [Systems M09b] every other participant's loadout
    std::vector<std::string> selectionChassis_, selectionWeapons_;   // [Systems M09c] warmed at level-audio load
    std::map<std::pair<int, int>, float> participantHitEffect_;   // (victim player, hit-effect entry) -> last play (hitClock_)
    struct ParticipantNotify { float delay; std::string cue; int player; core::Vec3 pos; };
    std::vector<ParticipantNotify> participantNotifies_;          // [Systems M09d] delayed Skill_ notifies of bots
    std::set<std::string> participantProfiles_;                   // bot chassis whose cue set is registered this level
    bool localCloakAnim_ = false;                                 // [Systems M09e] cloak on -> Nav_CloakActivate notifies
    struct ParticipantBody {                                       // [Systems M09f]
        std::string key;
        RobotFoley foley;
        VehicleAudio vehicle;
        VehicleFormAudio form;
        bool culled = true, prevTransforming = false, prevGrounded = true;
        core::Vec3 pos{0, 0, 0};                                   // body position this step (M09j emitters)
        std::string weaponClass;
        WeaponSoundTimeline weaponSounds;
        unsigned seenShot = 0, seenReload = 0;
        long long seenNitro = -1;                                  // [M09k] last nitroSerial (-1: none seen yet)
        bool weaponSeen = false;
        int transformNotify = 0;
        Form transformTarget = Form::Robot;
    };
    std::map<int, ParticipantBody> participantBodies_;
    static constexpr float kParticipantBodyCullM = 70.0f;
    int bodyCueLoadsThisStep_ = 0;
    std::vector<const char*> partScratch_;                          // participant audio scratch (reused per call)
    std::vector<const std::string*> partFired_;                                 // new body cue sets registered this step (max 1)
    std::set<int> participantCloakAnim_;                          // participants whose cloak is on
    // Nav_CloakActivate / Nav_CloakDeactivate notifies (CQC_TRANSFORM_CLOAK_*) for a pawn, if a profile carries those clips.
    // In VERSUS they are correctly silent: the clips (AI_CQC_ROBO_ANIM*) and BL_CHR_CQC cues are cooked only into campaign /
    // Escalation levels, never a versus map [CONFIRMED AssetTools], and gen_character_audio.py does not import
    // notify_only_clips. Importing them for versus would be a PC ADAPTATION.
    void playCloakAnimNotifies(bool on, const CharacterAudioProfile& p, const SoundCues::Emitter& at, float dist);
    static constexpr int kOwnParticipantBase = SoundCues::kParticipantOwnerBase;   // SoundCues owner id for participant `p` = base + p
    WeaponAudio weaponAudio_;
    AbilityAudio abilityAudio_;
    VehicleFormAudio vehicleForm_;
    VehicleFormEvents vehicleEvents_;
    VehicleFxDriver vehicleFxDriver_;
    bool vehicleFxData_ = false;
    bool fxPrevRolling_ = false, fxPrevShown_ = false;   // [Systems M08e]
    int projAudioKey_ = 0;
    int kothAudioZone_ = -1, kothAudioDefender_ = 255, objCountdownAudio_ = -1;   // [Systems M08f]
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
    bool audioPrevGrounded_ = true;          // [Systems M08d] vehicle take-off for every chassis (audio)
    unsigned quickTurnSeen_ = 0;             // [Systems M08d / Gameplay 24b] tank 180 quick turns already sounded
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
