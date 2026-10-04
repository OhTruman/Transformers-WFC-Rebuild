// Clean-room reconstruction — WFC frontend data catalog: map providers, playlists, game-settings classes,
// localization. Everything here is READ from the AssetTools frontend manifests at startup (nothing is a
// hard-coded button list), so a newly recovered map or mode appears in selection without code changes.
//
// Sources (read-only):
//   AssetTools/manifests/frontend_maps.json        TransLevels.ini TnDataProvider_MapInfo + TransGame.int   [CONFIRMED]
//   AssetTools/manifests/frontend_modes.json       TransPlaylists.ini, TnOnlineGameSettings* defaults          [CONFIRMED]
//   AssetTools/manifests/frontend_loading.json     EngageText tips, LoadingMovie config                        [CONFIRMED]
//   AssetTools/manifests/frontend_localization.json UIText / TransGame INT+FRA                                 [CONFIRMED]
//   RE notes/MILESTONE05_FRONTEND_MATCH_BOOTSTRAP.md (fields the manifests do not carry yet, section 4)        [CONFIRMED RE]
//
// The manifest root defaults to F:/Transformers Rebuild/AssetTools/manifests; override with WFC_FRONTEND_MANIFESTS.
#pragma once
#include <map>
#include <string>
#include <vector>

namespace frontend {

// TnDataProvider_MapInfo (one TransLevels.ini section).
struct MapInfo {
    int iniOrder = 0;                       // menu order = section order [HIGH]
    std::string mapFilename;                // e.g. "MP_IAC_Streets_Base_m" (travel URL map)
    int mapId = -1;                         // online id 501..513
    int presenceId = -1;
    int faction = -1;
    std::vector<std::string> compatibleGameTypes;   // "DM","TDM",... (GetCompatibleMaps filter)
    std::string imagePath;                  // UI_LevelThumbnails_p.<name>
    std::string thumbnailPng;               // ExtractedAssets-relative
    std::string friendlyName, friendlyNameFra;
    bool cooked = false;                    // cooked package present in the dump
    // Rebuild: HasRequiredAssets() is native in WFC (DLC check). The rebuild equivalent is "this map's
    // reconstructed runtime data exists" (ExtractedAssets/VerticalSlice/Maps/<runtimeDir>/world.glb).
    std::string runtimeDir;
    bool hasRequiredAssets = false;
    bool compatibleWith(const std::string& tag) const;
};

// One host-configurable setting of a TnOnlineGameSettings class: a LocalizedSettingsMapping (context with value
// names) or a PVMT_PredefinedValues PropertyMapping (e.g. PointsToWin 20/30/40/50) [CONFIRMED authored].
struct SettingField {
    std::string name;                       // "TimeLimit", "PointsToWin", ...
    std::string header;                     // ColumnHeaderText ("Time Limit"); empty = not shown in host options
    int id = 0;
    bool property = false;
    std::vector<std::string> values;        // value names ("15 minutes") or predefined numbers as text ("40")
    std::vector<double> numeric;            // property predefined values
    int defaultIndex = 0;
};

// TnOnlineGameSettings<tag>{,Public,Private} (authored defaults).
struct GameSettings {
    std::string className;                  // e.g. "TnOnlineGameSettingsTDMPrivate"
    std::string tag;                        // GameModeTag
    std::string gameClass;                  // TransContent.TnVersusGame
    std::string lobbyGameClass;             // TransContent.TnGameLobbyGameTeam            [RE note 4]
    std::string lobbyMapName = "UI_Lobby_m";  // [TransGame.TnOnlineGameSettingsBase]     [RE note 2.3]
    std::string teamType;                   // GTS_TeamGame ...                            [RE note 4]
    int teamTypeValue = 3;                  // StringToGameTeamStatus: FFA 1, SingleTeam 2, Team 3, Campaign 4 [CONFIRMED]
    std::vector<std::string> rules;         // head first
    std::vector<int> timeLimits;            // seconds
    int timeLimitDefaultIndex = 1;          // [RE note 4: default idx 1]
    int pointsToWin = -1;                   // default score setting (-1 = mode has none)   [RE note 4]
    std::string scoreOptionName = "PointsToWin";   // "Rounds" for CTF
    int numPublicConnections = 0, numPrivateConnections = 0;
    int mapSelectionMethod = 0;             // Public 0 Rotate, Private 1 Host's Choice   [RE note 2.4]
    bool isPrivate = false, isPublic = false;
    int numRequiredPlayers = 0;
    std::vector<SettingField> fields;       // host options, in mapping order (contexts, then properties)
    const SettingField* field(const std::string& n) const {
        for (const SettingField& f : fields) if (f.name == n) return &f;
        return nullptr;
    }
};

// TnDataProvider_OnlinePlaylist (TransPlaylists.ini).
struct Playlist {
    int id = -1;
    std::string section, name, tag, settingsClass;
    bool visibleInMenu = false;
    std::string displayName, displayNameFra;
};

// TnDataProvider_GameModeInfo (FriendlyName per mode class + SettingsConfigName).
struct GameModeInfo {
    std::string gameClassShort;             // TnTeamGame ...
    std::string settingsConfigName;         // "TDM" (empty for SV/OMF/Coop)
    std::string friendlyName, friendlyNameFra;
};

class Catalog {
public:
    // Loads every manifest; returns false when the map/mode manifests are missing (frontend unavailable).
    bool load(const std::string& manifestRoot, const std::string& extractedRoot, const std::string& runtimeMapRoot);
    static std::string defaultManifestRoot();
    static std::string defaultExtractedRoot();

    const std::vector<MapInfo>& maps() const { return maps_; }           // ini section order
    const std::vector<Playlist>& playlists() const { return playlists_; } // TnOnlinePlaylistManager.Playlists order
    // TnDataProvider_<Kind> objects authored in the shipped config (TransCustomization.ini / TransWeapons.ini, file
    // order) with their localized fields (TransGame.int, same section names): Weapon, Ability, Skill, Chassis,
    // Specialty, Killstreak... Fields as authored (repeated keys joined with ',').
    struct Provider { std::string name; std::vector<std::pair<std::string, std::string>> fields; std::string get(const std::string& k) const; };
    const std::vector<Provider>& providers(const std::string& kind) const;
    const std::vector<GameModeInfo>& gameModes() const { return modes_; }
    const MapInfo* mapById(int id) const;
    const MapInfo* mapByFilename(const std::string& f) const;            // case-insensitive
    const MapInfo* mapByRuntimeDir(const std::string& dir) const;
    const GameSettings* settings(const std::string& className) const;
    // "TDM" + private -> TnOnlineGameSettingsTDMPrivate (falls back to the base class).
    const GameSettings* settingsForTag(const std::string& tag, bool privateMatch) const;
    const Playlist* playlistById(int id) const;
    int defaultQuickmatchPlaylistId() const { return defaultQuickmatchPlaylist_; }

    // TnDataStore_MenuItems GetCompatibleMaps: providers whose CompatibleGameTypes contain tag, skipping disabled
    // (IsProviderDisabled = !HasRequiredAssets) ones when skipDisabled.
    std::vector<const MapInfo*> compatibleMaps(const std::string& tag, bool skipDisabled) const;

    // Localization ("$UIText.LoadScreen.LoadingMap" or section/key); INT. Empty when unknown.
    std::string localize(const std::string& file, const std::string& section, const std::string& key) const;
    std::string localizeKey(const std::string& dollarKey) const;
    // TnOnlineGameSettings<tag>.default.FriendlyName (loading title) [RE note 3.3].
    std::string modeFriendlyName(const std::string& tag) const;
    const std::vector<std::string>& engageTexts() const { return engageTexts_; }

    std::string loadingMovieDefault() const { return loadingDefault_; }       // TF_LoadingScreen
    std::string loadingMovieInitial() const { return loadingInitial_; }       // TF_InitialStartup
    std::string extractedRoot() const { return extractedRoot_; }
    std::string manifestRoot() const { return manifestRoot_; }

private:
    std::map<std::string, std::vector<Provider>> providers_;
    void loadProviders(const std::string& extractedRoot);
    void loadSettingsDefaults(const std::string& manifestRoot);
    std::vector<MapInfo> maps_;
    std::vector<Playlist> playlists_;
    std::vector<GameModeInfo> modes_;
    std::map<std::string, GameSettings> settings_;
    int defaultQuickmatchPlaylist_ = 1;
    std::map<std::string, std::string> loc_;   // "UIText.Section.Key" -> INT text
    std::vector<std::string> engageTexts_;
    std::string loadingDefault_, loadingInitial_;
    std::string manifestRoot_, extractedRoot_;
};

} // namespace frontend
