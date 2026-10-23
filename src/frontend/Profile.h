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
    // The campaign's ProfileData fields CampaignSelect reads: "<A1..A5|D1..D5>Available / Difficulty / Checkpoint" (false
    // for any other field).
    bool campaignField(const std::string& field, std::string& value) const;
    void set(const std::string& field, const std::string& value);
    bool getBool(const std::string& field) const { return get(field) == "True"; }
    int getInt(const std::string& field) const;
    static bool isOriginalField(const std::string& field);

    // PC SKU display settings (PCSettings.*).
    // frameLimit: PC EXTENSION ([PCSettings] FrameLimit, not an original setting; 0 = no cap, the default).
    // PC EXTENSION graphics options (not in the original; default off = the original look), [PCSettings]:
    //   upscaling: 0 Off, 1 / 2 / 3 FSR 1 Quality / Balanced / Performance, 4 / 5 / 6 FSR 3.1 Quality / Balanced /
    //   Performance (Rendering's IRenderer::setUpscaling; FSR 3.1 only where its runtime ships, never auto-detected);
    //   hdTextures: 0 Off / 1 Faithful (the HD set, original shading) / 2 Enhanced (beyond faithful + Rendering M6 Modern
    //   material maps: one switch) - each level offered where its pack root is installed; default Off, never
    //   set by auto-detect (user decision 2026-10-12); hdTexturesUserSet: the user changed the row; hdPackHintShown:
    //   the one-time "HD texture pack found" message was shown; anisotropy: 4 (the original's, default), 8 or 16;
    //   frameGeneration: 0 Off (default) / 1 On - FSR frame generation (Rendering), decided at start-up (applies at the
    //   next launch), offered only where its runtime ships, never set by auto-detect; rayTracing: 0 Off (default) / 1 Faithful
    //   / 2 Realistic - ray-traced shadows and lighting (Rendering), offered only where the renderer reports it, never
    //   set by auto-detect (user decision 2026-10-12: off unless enabled); showFps: the FPS counter (PC EXTENSION, user
    //   playtest 2026-10-13), default Off.
    bool pcSettingsLoaded = false;   // the profile file had a [PCSettings] section (else: first launch -> auto-detect)
    std::string autoDetectGpu;       // [PCSettings] AutoDetectGpu: the GPU the settings were detected for (PC EXTENSION)
    struct Display {
        int width = 1280, height = 720; bool fullscreen = false; int textureQuality = 2; bool vsync = false; int frameLimit = 0;
        int upscaling = 0; int hdTextures = 0; int anisotropy = 4; int frameGeneration = 0; int rayTracing = 0;
        bool hdTexturesUserSet = false, hdPackHintShown = false, showFps = false;
        bool volumetricLighting = true;   // [PCSettings] VolumetricLighting: Rendering's X3 froxel volumetrics, Realistic only
        int lensFlares = 2;               // [PCSettings] LensFlares: 0 Off, 1 Subtle, 2 Original (default, the shipped look); only when not 2
        int smoke = 0;                    // [PCSettings] Smoke: 0 Original (default, the authored sprites), 1 Enhanced; only when not 0
        int water = 0;                    // [PCSettings] Water: 0 Original (default, the authored materials), 1 Enhanced; only when not 0
        static constexpr int kRayTracingMax = 2;   // Realistic
        static constexpr int kHdTexturesMax = 2;   // Enhanced
        static constexpr int kUpscalingMax = 6;   // FSR 3.1 Performance
        static int clampAnisotropy(int v) { return v >= 16 ? 16 : v >= 8 ? 8 : 4; }
    };
    // Private Match bot settings (PC ADAPTATION, [PCSettings] BotsFriendly / BotsEnemy / BotDifficulty): AI teammates and
    // opponents for offline private matches; difficulty 0 EASY, 1 MEDIUM, 2 HARD (the campaign's names; MP has none).
    // Team modes: bots per faction (autobot / decepticon); free-for-all: enemy (opponents). extended: the Custom Game
    // player limit (false = the original MaxPlayers 10, 5 v 5).
    // editedSinceMap: the player changed a count since the last lobby map change (map-aware Extended counts keep it).
    // Bot AI: no menu row and no profile value (user 2026-10-13: Smart is the default, "I do not really need the option to
    // use Classic bots"). The launch URL carries no ?BotAI, so Gameplay's default (Smart) applies and its WFC_BOTAI
    // (DEV / TEST, identity harness) still selects Classic. An older profile's BotAI line is ignored and dropped on save.
    struct Bots {
        int friendly = 0, enemy = 0, difficulty = 1, autobot = 0, decepticon = 0; bool extended = false, editedSinceMap = false;
        bool iconic = false;   // PC EXTENSION: versus "Iconic Characters" (OnlyAllowIconicCharacters); written only when on
        bool idleKick = false; // online host row "Kick Idle Players" (D5 revised: the original never kicked in private); only when on
    };
    Bots bots;
    // Multiplayer progression ([Progression]): the original keeps it in the online stats archive (XP per specialty,
    // challenge stats / tiers); offline it lives in the local profile [PC ADAPTATION storage, original values].
    ProgressionState progression;
    // Campaign tutorial messages already shown (TnProfileSettings HasShownTutorialMessage / MarkTutorialMessageAsShown:
    // each type once per profile; CAMPAIGN_HUD_PLAN.md E5, C4 decided: profile-scoped). [Tutorials] Shown=, only written
    // when not empty, so versus profiles are unchanged.
    std::vector<std::string> tutorialsShown;
    // Campaign progress (TnProfileSettings; RE CAMPAIGN_DIFFICULTY_RE 2.3 / 3, CAMPAIGN_FRONTEND_FLOW_RE 3). Chapter index =
    // MapId - 96: A1..A5 = 96..100 (Autobot VI-X), D1..D5 = 101..105 (Decepticon I-V; D4 = D3B). [Campaign], written only
    // when it differs from a new profile's, so versus profiles are unchanged.
    struct Campaign {
        static constexpr int kChapters = 10, kFirstMapId = 96;
        int difficulty = 1;                     // setting 12: 0 Easy / 1 Medium (default) / 2 Hard
        bool available[kChapters] = {true, false, false, false, false, true, false, false, false, false};   // A1 / D1 [RE: DefaultSettings]
        bool complete[kChapters] = {};          // setting MapId
        int bestDifficulty[kChapters] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};   // MapId + 32, -1 = never completed
        int checkpoint[kChapters] = {};         // MapId + 16: the highest checkpoint reached (thumb n enabled iff >= n)
        int resumeMap[2] = {-1, -1};            // 55 Autobot / 56 Decepticon: the faction's resume chapter
        static const char* key(int i) { static const char* k[] = {"A1", "A2", "A3", "A4", "A5", "D1", "D2", "D3", "D4", "D5"}; return i >= 0 && i < kChapters ? k[i] : ""; }
        bool isDefault() const;
        static int index(int mapId) { return mapId >= kFirstMapId && mapId < kFirstMapId + kChapters ? mapId - kFirstMapId : -1; }
        // TnProfileSettings.CompleteMission(MapId, Difficulty): the map's bool, its best difficulty (raised only), the next map
        // in the faction's order Available (wrapping, so A5 / D5 re-flag A1 / D1), the faction's resume map [RE DefaultSettings]
        void completeMission(int mapId, int difficulty);
        // ReachedCheckpoint(MapId, Index): the highest checkpoint reached on the map; clears the faction's resume slot
        void reachedCheckpoint(int mapId, int index);
        // GetNextAvailableCampaignMap after a chapter: the next one in the faction's order if Available, else the same chapter
        int nextAvailableChapter(int mapId) const;
    };
    Campaign campaign;
    // [Online] FirstRunShown: the one-time "Online Play" prompt was answered (ONLINE_MP_FIRST_RUN.md step 2); written only
    // when set, so offline profiles are unchanged.
    bool onlineFirstRunShown = false;
    // [Lobby] LastMode: the Game Settings mode last used (Private Match hosts with it at once; ONLINE_LOBBY_FLOW_V2 2.1).
    std::string lobbyLastMode;
    std::string lobbyService;   // [Online] LobbyService: the lobby service address ("" = none: LAN only); written only when set
    // Voice chat (PC EXTENSION, decision D7 + proximity; ONLINE_MP_FLOWS.md 3.12): the Settings > Audio voice rows and the
    // per-player mutes (fingerprints, local only). [Voice], written only when something differs from the defaults.
    struct Voice {
        int mode = 1;                 // 0 Off, 1 Push to Talk (the user's default), 2 Open Mic
        int audience = 3;             // 0 Team Radio, 1 All Radio, 2 Proximity, 3 Radio + Proximity (the proposed default)
        std::string device;           // "" = System Default
        std::string pttKey = "V";
        int volume = 70, proximityVolume = 70;
        std::vector<std::string> muted;
        bool isDefault() const {
            return mode == 1 && audience == 3 && device.empty() && pttKey == "V" && volume == 70 && proximityVolume == 70 && muted.empty();
        }
    };
    Voice voice;
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
