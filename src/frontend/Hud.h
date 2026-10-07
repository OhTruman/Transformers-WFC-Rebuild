// Clean-room reconstruction — the in-match HUD movie's engine side (TnHUD role).
//
// Ownership (RE OVERNIGHT_2026-10-04 A0, AssetTools HUD_PACKAGE, CONFIRMED): UI_GFxHud_p.Hud_GFX_1 (TnHUD.HudMovie,
// 1120x720) draws health / overshield, ammo, weapon, crosshair, damage arrows, clock and score panel (data stores),
// kill feed / game messages, announcements and point popups. It is created for the match and shown only in the UI
// states InGame / Spectating (A8: hidden at match end, pre-match screens, pause). TnHUD's data observers call the
// movie's _global notifications on change; this class does the same from Gameplay's authoritative state (HudFrame,
// filled by the application from World::hudState and the match events). Canvas markers (name tags, objective
// markers) are not this movie's (Rendering / Gameplay).
#pragma once
#include <array>
#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "frontend/Bridge.h"

namespace frontend {

class Catalog;
class IMoviePresenter;

struct HudFrame {
    bool valid = false;
    bool alive = true;
    int fullSegments = 4, totalSegments = 4;   // NotifySegmentedHealthChanged
    double currentSegment = 1.0;               // the active segment's fill 0..1
    double overshield = 0.0;                   // normalized (NotifyOverShieldChanged)
    int clip = 0, clipCapacity = 0, reserve = 0, reserveCapacity = 0;
    // The weapon class name ("TnWeaponIonBlaster"; a bare id gets the TnWeapon prefix): NotifyCurrentWeaponChanged. The
    // movie picks the icon export and the crosshair from it (Shotgun / EmpShotgun 1, IonBlaster 2, Bazooka 3, else 0;
    // mounted turrets -> turret reticule; GrenadeLauncher -> range finder) [CONFIRMED Hud_GFX AS, RE 934ecde].
    std::string weapon;
    int aimType = 0;                           // NotifyFineAimChanged: 0 standard, 1 fine aim (TnPCS_FineAim)
    bool vehicleForm = false;                  // NotifyCurrentFormChanged
    bool spectating = false;
    // TnHudDataObservers without a source yet stay empty (nothing is sent); once set, each is sent on change with the
    // original callback and arguments [CONFIRMED decompiled TransGame + Hud_GFX AS].
    struct Ability { std::string id = "None"; double cooldown = 0; double fraction = 1; bool operator!=(const Ability& o) const { return id != o.id || cooldown != o.cooldown || fraction != o.fraction; } };
    std::optional<std::string> progressObserver;   // progress bar observer class (its TransGame.int label), e.g.
                                                   // "TnHudDataObserverDominationCapture"; with progressName for `p
    std::string progressName;
    std::optional<double> progress;                // 0..1 (0 hides the bar)
    std::optional<int> attackingTeamStatus;        // OnAttackingTeam: 1 attacking, 2 defending, 0 none
    std::optional<std::string> killstreakId;       // Killstreak: the reward class (bitmap export, e.g. HealthRegenStreak)
    std::array<std::optional<Ability>, 3> abilities;   // AbilityType0..2: class (TnAbilityHover / "None") + cooldown remaining s + recharged 0..1
    std::optional<int> grenadeAmmo, grenadeType, activeGrenades;
    std::optional<double> playerYaw;               // DamageIndicators: the view yaw in radians
    std::optional<int> lockOnState;                // LockOnState
    // The lock-on target marker (TnHUD UpdateObjectiveMarker -> Hud_GFX UpdateMarker 'LockOn', the only GFx marker type;
    // objective markers are Canvas sprites): the target projected to the viewport (0..1 from the top left), its distance
    // in metres, and whether it is in front of the camera.
    struct LockOnMarker {
        int id = 0; double x = 0, y = 0, distance = 0; bool inFront = true; std::string description;
        bool operator!=(const LockOnMarker& o) const {
            return id != o.id || x != o.x || y != o.y || distance != o.distance || inFront != o.inFront || description != o.description;
        }
    };
    std::optional<LockOnMarker> lockOnMarker;
    std::optional<std::string> targetName;         // TargetName
    std::optional<int> targetType;                 // TargetType
    std::optional<double> targetHealth;            // NotifyTargetHealthChanged(TargetType, health)
    std::optional<bool> weaponJammed;
    std::optional<double> weaponSpread;
    std::optional<std::string> weaponMessage;
    std::optional<double> downedHealth;            // NormalizedDownedHealth
    std::optional<bool> hudScrambled;              // HudScrambled
    std::optional<double> scoringMultiplier;       // CompetitiveScoring
    std::optional<int> increaseDamage;             // IncreaseDamage (type)
    // Diffed into events here: the prompts on screen (a new one -> NotifyContextualCommand(0, 0, text), a gone one ->
    // (0, 1, text)), and the refused-transform counter (each increment -> NotifyCantTransform).
    std::optional<std::vector<std::string>> contextualPrompts;
    std::optional<int> cantTransformCount;
};

struct HudKill {
    std::string killer, victim;
    int killerTeam = -1, victimTeam = -1;
    bool killerLocal = false, victimLocal = false;
    bool suicide = false, environment = false;
    std::string damageType;                    // TnDamageType class ("" = unknown -> [TnDamageType] template)
};

class HudController {
public:
    static constexpr const char* kMovie = "UI_GFxHud_p.Hud_GFX_1";
    void reset();                              // match start / end: nothing pending, everything re-sent
    void setFrame(const HudFrame& f) { frame_ = f; }
    // Values the frontend derives itself (kept across setFrame): the OnAttackingTeam status from MatchValues.
    void setAttackingTeamStatus(std::optional<int> v) { attacking_ = v; }
    void addKill(const HudKill& k, int localTeam) { kills_.push_back({k, localTeam}); }
    void announce(const std::string& text) { announcements_.push_back(text); }   // _global.GameAnnouncement
    void reward(const std::string& text) { rewards_.push_back(text); }           // _global.RewardAnnouncement
    // Event observers (one call per event, in order).
    void damageIndicator(double worldYaw, double amount) { events_.push_back({"NotifyDamageIndicatorAdded", {worldYaw, amount}}); }
    void causedDamage() { events_.push_back({"CausedDamage", {}}); }                      // hit marker
    void contextualCommand(int action, const std::string& text) { events_.push_back({"NotifyContextualCommand", {0, action, text}}); }
    void addObjective(const std::string& text) { events_.push_back({"AddObjective", {text}}); }
    void ammoAdded(int amount, const std::string& weapon) { events_.push_back({"NotifyAmmoAdded", {amount, weapon}}); }
    void cantTransform() { events_.push_back({"NotifyCantTransform", {}}); }
    void transformDisrupted(bool on) { events_.push_back({"TransformDisrupted", {on}}); }
    // OnAttackingTeam (TnGameRules_SingleFlagCTF attaches it): 1 attacking / 2 defending from GRI.AttackingTeam.
    // Rebuild -> Hud_GFX conversions. Yaw: the rebuild's yaw turns left (counter-clockwise from above), Unreal's and the
    // HUD's (_rotation degrees, clockwise) turn right, so the HUD yaw is the negated rebuild yaw.
    static double hudYaw(double rebuildYaw) { return -rebuildYaw; }
    // mc_grenadeIcon GrenadeType: 1 Frag, 2 Flashbang, 3 KMine, 4 Heal (0 none) [CONFIRMED Hud_GFX sprite 376]; from the
    // grenade weapon id [PROV name match; versus bags are frag].
    static int grenadeTypeFor(const std::string& id);
    // SetAbilityIcon loads the bitmap export named by the ability class (TnAbilityBarrier, ...; 'None' empty): the
    // rebuild's short ids ("Barrier") get the TnAbility prefix [CONFIRMED Hud_GFX exports].
    static std::string abilityIconId(const std::string& id);
    // NotifyTargetTypeChanged: 0 friendly (blue crosshair), 1 enemy (red), else white [CONFIRMED Hud_GFX].
    static int targetTypeFor(int targetPlayer, int targetTeam, int myTeam) {
        return targetPlayer < 0 || targetTeam < 0 || targetTeam == 255 ? 2 : targetTeam == myTeam ? 0 : 1;
    }
    static int attackingStatus(int attackingTeamIndex, int myTeam) {
        return attackingTeamIndex < 0 ? 0 : attackingTeamIndex == myTeam ? 1 : 2;
    }
    // One frame: delivers what changed to the HUD movie (open + visible = shown per A8).
    void update(IMoviePresenter* p, const Catalog& cat, bool open, bool visible);
    // TnDeathMessage.GetColoredString: the authored template with coloured names (TnMessageTextColors).
    static std::string killMessage(const Catalog& cat, const HudKill& k, int localTeam);

private:
    HudFrame frame_, sent_;
    std::optional<int> attacking_;
    std::string progressLabel_;   // the bar's last label: kept while it fades out (the observer keeps its own label)
    bool sentValid_ = false, wasOpen_ = false, wasVisible_ = false, wasSpectating_ = false;
    struct PendingKill { HudKill k; int localTeam; };
    std::vector<PendingKill> kills_;
    std::vector<std::string> announcements_, rewards_;
    struct Event { const char* fn; std::vector<BridgeValue> args; };
    std::vector<Event> events_;
};

} // namespace frontend
