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
#include <deque>
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
    std::string weapon;                        // TnWeapon class suffix (NotifyCurrentWeaponChanged, icon export)
    bool vehicleForm = false;                  // NotifyCurrentFormChanged
    bool spectating = false;
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
    void addKill(const HudKill& k, int localTeam) { kills_.push_back({k, localTeam}); }
    void announce(const std::string& text) { announcements_.push_back(text); }   // _global.GameAnnouncement
    void reward(const std::string& text) { rewards_.push_back(text); }           // _global.RewardAnnouncement
    // One frame: delivers what changed to the HUD movie (open + visible = shown per A8).
    void update(IMoviePresenter* p, const Catalog& cat, bool open, bool visible);
    // TnDeathMessage.GetColoredString: the authored template with coloured names (TnMessageTextColors).
    static std::string killMessage(const Catalog& cat, const HudKill& k, int localTeam);

private:
    HudFrame frame_, sent_;
    bool sentValid_ = false, wasOpen_ = false, wasVisible_ = false, wasSpectating_ = false;
    struct PendingKill { HudKill k; int localTeam; };
    std::vector<PendingKill> kills_;
    std::vector<std::string> announcements_, rewards_;
};

} // namespace frontend
