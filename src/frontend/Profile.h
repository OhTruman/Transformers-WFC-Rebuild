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
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace frontend {

class LocalProfile {
public:
    static constexpr const char* kFile = "wfc_profile.ini";
    void load();
    void save() const;

    // <OnlinePlayerData:ProfileData.Field> (original fields; unknown fields are stored but traced).
    std::string get(const std::string& field) const;
    void set(const std::string& field, const std::string& value);
    bool getBool(const std::string& field) const { return get(field) == "True"; }
    int getInt(const std::string& field) const;
    static bool isOriginalField(const std::string& field);

    // PC SKU display settings (PCSettings.*).
    struct Display { int width = 1280, height = 720; bool fullscreen = false; int textureQuality = 2; bool vsync = false; };
    Display display;
    // The local player's display name (GetPlayerAlias / PRI.PlayerName). The original took it from the signed-in
    // Xbox Live gamertag; the offline PC reconstruction has no such service: [Identity] Name in the profile file,
    // else WFC_PLAYERNAME, else "Player" [PC RECONSTRUCTION FALLBACK].
    std::string playerName() const;
    std::string identityName;

    // Called after the movie applies / saves the profile (Game.ApplyProfileSettings, Console.SaveProfileSettings) and
    // after PCSettings commits: the application pushes the values to their runtime owners.
    std::function<void(const LocalProfile&)> onApplied;
    void apply() { save(); if (onApplied) onApplied(*this); }

private:
    std::map<std::string, std::string> values_;
};

} // namespace frontend
