// Clean-room reconstruction — the local player profile (TnProfileSettings role) and the PC SKU's display settings.
//
// ORIGINAL WFC OPTIONS (CONFIRMED: SettingsMenu_GFX bindings <OnlinePlayerData:ProfileData.*>, defaults from
// TnProfileSettings, RE MILESTONE05_PLAYTEST_RE section 4): FX / Dialogue / Music Volume 80, Subtitles off, Controller
// Vibration on, UseAlternateControlScheme (Scheme A), InvertY Car / Plane / Tank / Robot off, CameraSensitivity 30,
// GammaSetting 50 (HasAdjustedGamma false).
// ORIGINAL PC SKU OPTIONS (CONFIRMED: SettingsMenu_GFX WIN branch, HmExternalInterface.PCSettings.*): resolution,
// fullscreen, texture quality (0 low / 1 medium / 2 high), VSync - committed together by "Commit Changes".
// PC EXTENSIONS (not in the shipped menus) live in their own section and are never shown as original options.
// Values are strings in the movies' own vocabulary ("80", "True" / "False"). Stored in wfc_profile.ini.
#pragma once
#include "frontend/Progression.h"
#include <functional>
#include <istream>
#include <map>
#include <string>
#include <vector>

namespace frontend {

class LocalProfile {
public:
    static constexpr const char* kFile = "wfc_profile.ini";
    void load();
    // Parses a profile; returns true when an older format was migrated (load() then saves it once).
    bool loadFrom(std::istream& in);
    void save() const;

    // <OnlinePlayerData:ProfileData.Field> (original fields; unknown fields are stored but traced).
    std::string get(const std::string& field) const;
    void set(const std::string& field, const std::string& value);
    bool getBool(const std::string& field) const { return get(field) == "True"; }
    int getInt(const std::string& field) const;
    static bool isOriginalField(const std::string& field);

    // PC SKU display settings (PCSettings.*).
    // frameLimit: PC EXTENSION ([PCSettings] FrameLimit, not an original setting; 0 = no cap, the default).
    // PC EXTENSION graphics options (not in the original; default off = the original look), [PCSettings]:
    //   upscaling: 0 Off, 1 FSR 1 Quality, 2 FSR 1 Balanced, 3 FSR 1 Performance (later DLSS / FSR 3 values follow);
    //   hdTextures: the HD texture set; frameGeneration / rayTracing: reserved (no menu row yet, always off).
    struct Display {
        int width = 1280, height = 720; bool fullscreen = false; int textureQuality = 2; bool vsync = false; int frameLimit = 0;
        int upscaling = 0; bool hdTextures = false; int frameGeneration = 0; bool rayTracing = false;
    };
    // Private Match bot settings (PC ADAPTATION, [PCSettings] BotsFriendly / BotsEnemy / BotDifficulty): AI teammates and
    // opponents for offline private matches; difficulty 0 EASY, 1 MEDIUM, 2 HARD (the campaign's names; MP has none).
    // Team modes: bots per faction (autobot / decepticon); free-for-all: enemy (opponents). extended: the Custom Game
    // player limit (false = the original MaxPlayers 10, 5 v 5).
    // editedSinceMap: the player changed a count since the last lobby map change (map-aware Extended counts keep it).
    struct Bots { int friendly = 0, enemy = 0, difficulty = 1, autobot = 0, decepticon = 0; bool extended = false, editedSinceMap = false; };
    Bots bots;
    // Multiplayer progression ([Progression]): the original keeps it in the online stats archive (XP per specialty,
    // challenge stats / tiers); offline it lives in the local profile [PC ADAPTATION storage, original values].
    ProgressionState progression;
    // Original chassis locks ([PCSettings] OriginalChassisLocks): the authored LockedChassis (Car5 / Jet8 via campaign
    // completion, 16 more with no known unlock path) apply only when set; default off = every chassis available
    // (PC ADAPTATION, pending the user's decision).
    bool originalChassisLocks = false;
    Display display;
    // The local player's display name (GetPlayerAlias / PRI.PlayerName). The original took it from the signed-in
    // Xbox Live gamertag; the offline PC reconstruction has no such service: [Identity] Name in the profile file,
    // else WFC_PLAYERNAME, else "Player" [PC RECONSTRUCTION FALLBACK].
    std::string playerName() const;
    std::string identityName;
    // Accounts menu (PC SKU). The original's accounts were Demonware online accounts bound to the product key
    // (TnAccountActionScriptBinding: CreateOnlineAccount, Login), and the signed-in account name was the player's name.
    // Offline the rebuild keeps local account names instead [PC ADAPTATION]: create / delete / sign in / sign out;
    // the signed-in name is the player name, and the last signed-in account signs in again at the next launch.
    std::vector<std::string> accounts;
    std::string loggedInAccount;

    // Called after the movie applies / saves the profile (Game.ApplyProfileSettings, Console.SaveProfileSettings) and
    // after PCSettings commits: the application pushes the values to their runtime owners.
    std::function<void(const LocalProfile&)> onApplied;
    void apply() { save(); if (onApplied) onApplied(*this); }

private:
    std::map<std::string, std::string> values_;
};

} // namespace frontend
