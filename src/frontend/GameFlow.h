// Clean-room reconstruction — WFC application flow: boot -> UI_FrontEnd_m -> party lobby -> game lobby -> match ->
// return, as the shipped game does it (travel by URL between levels, each with its own game class and UI controller).
//
// Specification: RE-Workspace notes/MILESTONE05_FRONTEND_MATCH_BOOTSTRAP.md (f1ca8a5), AssetTools FRONTEND.md
// (cc9773e). WFC has no offline multiplayer: every versus match goes frontend -> party lobby -> game lobby -> map,
// all listen servers with online sessions. The rebuild keeps the same game-side state machines, URLs and data and
// drops only the session / reservation calls (marked [online - bypassed]); the private-match path (host's choice)
// is the faithful local reduction (RE "Integration" handoff).
//
// Boundary:
//   * Movies / menus (presenter) talk to the flow ONLY through the original AS->engine bridge calls
//     ("Online.OpenPartyLobby", "Online.SetSelectedMapID", ...; call()) and fscommands (fsCommand()).
//   * The application owns the match world: it takes a MatchLaunch (pendingMatch()), loads it, calls matchLoaded().
//   * Gameplay owns the match rules / lifecycle. Its game status reaches the UI controller via onUIEvent()
//     (TnMultiplayerGame.InProgress -> 3, MatchOver -> 9, ...). Until Gameplay implements PendingMatch/InProgress
//     the application uses a PROVISIONAL adapter (see Application).
#pragma once
#include <map>
#include <memory>
#include <random>
#include <algorithm>
#include <string>
#include <vector>

#include "frontend/Catalog.h"
#include "frontend/Profile.h"
#include "frontend/UIController.h"
#include "frontend/Bridge.h"
#include "frontend/Url.h"

namespace frontend {

enum class LevelKind { None, FrontEnd, PartyLobby, GameLobby, Match };
const char* levelKindName(LevelKind k);

// TnMoviePlayer.StartLoadingMovie(URL) result (RE 3.3).
struct LoadingScreen {
    bool active = false;
    std::string kind;                        // PartyLobby | GameLobby | Map | Generic
    std::string title, message;              // _global.SetLevelText(title, message)
    std::vector<std::string> engageTexts;    // 3 x _global.AddEngageText
    std::string binkMovie;                   // [LoadingMovie] DefaultFileName (TF_LoadingScreen)
    std::string gfxMovie;                    // SetLoadingMovieFilename "UI_GFxLoading_p.LoadScreen_GFX"
    std::string url;
};

// A match the application must load (ServerTravelToMap(match URL)).
// [integration] Match values the in-match movies read (<CurrentGame:*> / <PlayerOwner:*>), pushed every frame from
// Gameplay's authoritative state (World::hudState). Gameplay owns the values; the data stores only serve them.
struct MatchValues {
    bool valid = false;
    bool pending = false;          // PendingMatch (pre-game countdown)
    bool countingDown = false;     // <CurrentGame:IsCountingDown>
    int countdown = 0;             // <CurrentGame:CurrentCountdown>: pre-match countdown, then GRI.RemainingTime
    int goalScore = 0;             // <CurrentGame:GoalScore>
    int teamScore[2] = {0, 0};     // <CurrentGame:Teams> Score
    int myTeam = -1;               // <PlayerOwner:TeamID>
    int score = 0, kills = 0, deaths = 0;
    bool dead = false;
    float timeToRespawn = -1.0f;   // <PlayerOwner:TimeToRespawn>
    std::string gameOverMessage;   // <CurrentGame:GameOverMessage> ("Your team won" / "Your team lost" / "Tie game")
    // GRI objective state (Gameplay HudGameState; defaults = Default__TnGameReplicationInfoMultiplayer [CONF]).
    int attackingTeamIndex = -1;           // <CurrentGame:AttackingTeamIndex>: CTF attackers / EXT bomb team, else -1
    int currentObjectiveCountdown = -1;    // <CurrentGame:CurrentObjectiveCountdown>: EXT bomb fuse seconds, else -1
    bool competitiveScoreEnabled = false;  // <CurrentGame:CompetitiveScoreEnabled> (engine default 0, HIGH)
    // Every PRI of the match (GRI.PRIArray): <CurrentGame:Players> rows for the scoreboard / player lists.
    struct Player {
        std::string name; int team = -1; int score = 0, kills = 0, deaths = 0; bool dead = false, local = false;
        bool bot = false;              // Gameplay ParticipantKind::Bot (PC ADAPTATION: the original lists no bots)
        int level = 0;                 // Gameplay's displayed level (bots: generated)
        std::string specialty;         // applied at the last spawn
    };
    std::vector<Player> players;
};

struct MatchLaunch {
    Url url;
    const MapInfo* map = nullptr;
    const GameSettings* settings = nullptr;
    std::string modeTag;
    int mapId = -1;
    int goalScore = 0;          // GetIntOption("PointsToWin")  (TnMultiplayerGame.InitGame)
    int rounds = 0;             // GetIntOption("Rounds")
    int timeLimit = 0;          // GetIntOption("TimeLimit"), seconds (GameInfo.InitGame)
    int teamIndex = -1;         // 0 Autobots, 1 Decepticons (lobby FinalCountdown; survives seamless travel)
    int gameTeamStatus = 0;
};

// Game-lobby / party-lobby replicated state (the <CurrentGame:*> data store fields the lobby movies read).
struct LobbyState {
    const GameSettings* settings = nullptr;  // PublishGameInfo(tag, settings)
    std::string gameModeTag;
    int gameTeamStatus = 0;
    int playlistId = -1;                     // SelectedPlaylistID (-1 private)
    int mapId = -1;                          // SelectedMapID
    int mapSelectionMethod = 0;              // 0 Rotate, 1 Host's Choice
    bool allowMapVeto = false;
    bool autostartCountdown = false;
    int numRequiredPlayers = 0;
    int numPlayers = 1;                      // the local host
    int lobbyStatus = 0;                     // 1 waiting for N, 3 waiting for host, 4 unbalanced
    bool countingDown = false, countdownRubicon = false, finalCountdown = false;
    int countdown = 0;                       // <CurrentGame:CurrentCountdown> (integer seconds)
    float countdownTimer = 0.0f;
    int localTeam = -1;
    std::string prestreamMap;                // GameEngine.UpdateMapPrestreaming target
};

class GameFlow {
public:
    struct Options {
        bool skipIntroMovies = false;        // treat HasWatchedIntroMovie as true
        unsigned seed = 0;                   // 0 = time-seeded (RandRange / RandomInt / EngageText picks)
    };

    bool init(const Catalog& catalog, const Options& opt);
    void tick(float dt);

    // ---- presenter -> flow (original AS2 bridge + fscommand surface) ----
    // ExternalInterface.call("<Interface>.<Method>", args...). Returns the binding's result ("" when void).
    BridgeValue call(const std::string& fn, const std::vector<std::string>& args = {});
    void fsCommand(const std::string& movie, const std::string& cmd, const std::string& arg = "");
    void movieStopped(const std::string& movie);    // SeqAct_MoviePlayer "Stopped" output
    void uiClosedItself();                          // the open movie closed (e.g. pause "Resume")

    // ---- application / gameplay -> flow ----
    bool hasPendingMatch() const { return pendingMatch_; }
    const MatchLaunch& pendingMatch() const { return match_; }
    void matchLoaded();                             // world loaded: close the loading movie, Multiplayer UI controller
    void matchLoadFailed(const std::string& why);
    // UI events from the game. OnBeginGame (3) in a match marks GRI.bMatchHasBegun (InProgress.BeginState sends it).
    void onUIEvent(int code) { if (code == 3 && level_ == LevelKind::Match) matchHasBegun_ = true; ui_.onUIEvent(code); }
    // TnUIControllerMultiplayer.OnCharacterSelected: before the match begins the pre-game screen follows; after, only
    // the CharacterSelectedInGame notification (the spawn then enters InGame) [CONFIRMED script].
    void characterSelected() { ui_.onCharacterSelected(matchHasBegun_); }
    bool matchHasBegun() const { return matchHasBegun_; }
    // TnPlayerController.SelectCharacter(name, type 0 custom / 1 iconic) -> PRI._SelectedCharacter; Gameplay spawns the
    // chassis of the player's team from it (GetResolvedCharacterFaction = TeamNum).
    //
    // The selected-character contract (Frontend -> Gameplay). Filled once, from the committed character, when the movie
    // calls Customize.SelectCharacter; Gameplay spawns from it and must not derive the character again.
    // - name: the custom slot (CharacterName, e.g. "Scout"); type 0 custom / 1 iconic (iconic: chassis[] = UniqueIds).
    // - Per faction f (0 Autobot, 1 Decepticon): Gameplay uses f = the player's team (GetResolvedCharacterFaction =
    //   TeamNum). bodyAvailable[f] = the roster robot glTF exists; when false the selection is still the player's choice
    //   but no body can be spawned for that faction (reported, never silently replaced).
    // - Colours as stored in the customization data: rgb black (0,0,0,255) = kUseDefaultColor, the material's default
    //   paint (TnCharacterApplier.ExtractColors); otherwise sRGB. palette / x / y = the picker swatch (Create a Character).
    struct CharacterColorSel { int r = 0, g = 0, b = 0, a = 255; int palette = 0; float x = 0, y = 0;
                               bool useDefault() const { return r == 0 && g == 0 && b == 0 && a == 255; } };
    struct SelectedCharacter {
        std::string name, friendlyName;
        int type = 0;
        std::string specialty;
        std::string chassis[2], robotGltf[2], vehicleGltf[2];
        bool bodyAvailable[2] = {false, false};
        CharacterColorSel primary[2], secondary[2];
        std::vector<std::string> weapons, vehicleWeapons, melee, abilities, skills;
        bool valid = false;
    };
    void selectCharacter(const SelectedCharacter& c);
    void clearSelectedCharacter();   // PRI.ClearCharacter
    const SelectedCharacter& selectedCharacter() const { return selected_; }
    // Bumped by every Customize.SelectCharacter (also a re-pick of the same character): the match forwards each new pick.
    uint32_t selectionSerial() const { return selectionSerial_; }
    void showMenu();                                // TnPlayerController.ShowMenu (Escape / Start release)
    // [integration] Gameplay MatchOver -> 15 s -> TnGame.ReturnToGameLobby: ServerTravel to the game lobby
    // (UI_Lobby_m?...?MapId=<map>) [RE M05 blockers F4 / F6].
    void returnToGameLobby();
    void setMatchValues(const MatchValues& v) { matchValues_ = v; }
    const MatchValues& matchValues() const { return matchValues_; }
    bool quitRequested() const { return quit_; }
    void exitNow() { quit_ = true; }   // automation: leave without the Exit Game confirmation

    // TnUIController.ShowPopupUI / ShowCustomPopupUI: one message box (MessageBoxUI UI_GFxShared_p.MessagePrompt_GFX_1)
    // driven by TnMessageBoxActionScriptInterface: _global.DisplayMessage(Title, Message, Buttons, IconType); the
    // movie answers MessageBox.OnA / OnB / OnX / OnY [CONFIRMED script / authored defaults].
    struct PopupButton { std::string text, action; bool closeOnPress = true; };
    struct Popup {
        bool open = false;
        std::string title, message, postConfirm;
        PopupButton a, b, x, y;
        int icon = 0;                 // 0 alert, 1 animating (waiting)
        bool showDefaultButtons = true;
        uint64_t serial = 0;          // changes whenever the movie must be updated
        std::string buttonString() const;   // GenerateButtonString
    };
    const Popup& popup() const { return popup_; }
    void popupButton(char which);     // 'A' 'B' 'X' 'Y'
    void popupClosedByMovie();
    // TnUIController.ShowPopupUI(Title, Message): a message box with the default Ok button.
    void showMessage(const std::string& title, const std::string& message) { Popup p; p.title = title; p.message = message; showPopup(p); }
    bool wantsWorldUnload() const { return unloadWorld_; }   // travel away from a match map
    void worldUnloaded() { unloadWorld_ = false; }

    // ---- state (presenter, validation) ----
    LevelKind level() const { return level_; }
    const std::string& levelMap() const { return levelUrl_.map(); }
    const Url& levelUrl() const { return levelUrl_; }
    const LoadingScreen& loading() const { return loading_; }
    const LobbyState& lobby() const { return lobby_; }
    const UIController& ui() const { return ui_; }
    const Catalog& catalog() const { return *cat_; }
    const MatchLaunch& currentMatch() const { return match_; }
    // Kismet-driven frontend presentation (UI_FrontEnd_m Main_Sequence).
    const std::string& kismetMovie() const { return kismetMovie_; }        // SeqAct_MoviePlayer currently playing
    const std::vector<std::string>& queuedMovies() const { return movieQueue_; }   // the chain still to play
    // Game.PlayMovie (Extras Movies / Credits): a full-screen movie in HmPlayerController movie mode.
    const std::string& scriptMovie() const { return scriptMovie_; }
    void scriptMovieStopped();
    // Engine -> ActionScript invokes the flow raises (movie object, function path); the presenter delivers them.
    std::vector<std::pair<std::string, std::string>> takeUiInvokes() { std::vector<std::pair<std::string, std::string>> v; v.swap(uiInvokes_); return v; }
    const std::vector<std::string>& openMovies() const { return openMovies_; }   // GFxAction_OpenMovie / OpenUI
    bool frontEndStarted() const { return frontEndStarted_; }
    // Level Kismet triggers the frontend owns, in order ("FsCommand:<cmd>", "MovieStopped:<movie>").
    const std::vector<std::string>& kismetTriggers() const { return kismetTriggers_; }
    bool hasWatchedIntroMovie() const { return watchedIntro_; }
    // Private Match bots (PC ADAPTATION). Player limit: ORIGINAL = MaxPlayers 10 (5 v 5) [CONFIRMED Xe-TransGame.ini], the
    // default; EXTENDED (Custom Game, user: 32 v 32) = up to 31 / 32 bots a side, 63 in FFA, bounded by Gameplay's own clamp. Team modes (GameTeamStatus
    // 3): bots per faction, the human's faction one fewer at the original limit; free-for-all (1): opponents; other: none.
    enum class BotRows { None, FreeForAll, Teams };
    BotRows botRows() const;
    int humanFaction() const { return lobby_.localTeam == 1 ? 1 : 0; }   // 0 Autobots, 1 Decepticons
    // Gameplay's MatchSettings maxPerTeam / maxPlayers (players incl. the human): the extended limits never exceed them.
    void setBotCapacity(int perTeam, int maxPlayers, int botsPerTeam = 0) {
        gpPerTeam_ = std::max(1, perTeam); gpMaxPlayers_ = std::max(2, maxPlayers); gpBotsPerTeam_ = botsPerTeam > 0 ? botsPerTeam : gpPerTeam_;
    }
    int botMax(const std::string& field) const;   // "autobot" / "decepticon" / "enemy" / "difficulty" / "extended"
    void setBotSetting(const std::string& field, int value);   // clamped; saved
    // Map-aware Extended bot counts (user decision, PC ADAPTATION): AssetTools' recommended players per versus map
    // (per side incl. the human; FFA total; optional per mode).
    struct RecommendedPlayers { int perSide = 0, ffa = 0; std::map<std::string, std::pair<int, int>> modes; };
    void setRecommendedPlayers(std::map<int, RecommendedPlayers> byMapId) { recommended_ = std::move(byMapId); }
    bool recommendedFor(int mapId, const std::string& mode, int& perSide, int& ffa) const;
    std::string botRecommendationText() const;   // "RECOMMENDED: 8 V 8" / "RECOMMENDED: 12 PLAYERS" / ""
    struct BotCounts { int autobot = 0, decepticon = 0, enemy = 0; };
    // The counts a recommendation gives: the human's side one bot fewer; FFA total minus the human; within the caps.
    static BotCounts recommendedBots(int perSide, int ffa, bool teams, int humanFaction, int maxAutobot, int maxDecepticon, int maxEnemy);
    void applyRecommendedBots(const char* why);
    LocalProfile& profile() { return profile_; }
    const LocalProfile& profile() const { return profile_; }
    std::string stateSummary() const;
    // SettingsDataStore (TnDataStore_GameSettings): the current settings object and its host-option values. Values
    // persist per settings class for the session, as the data store's settings objects do [HIGH].
    const GameSettings* currentSettings() const { return currentSettings_; }
    int settingIndex(const GameSettings* gs, const std::string& field) const;
    bool setSettingValue(const std::string& field, const std::string& valueName);

    // Diagnostics: emit a full state snapshot to the trace.
    void traceSnapshot(const char* why) const;

private:
    // Travel (TnGame.ClientTravelToMap / ServerTravelToMap): StartLoadingMovie + world replacement.
    void travel(const std::string& url, bool server);
    void startLoadingMovie(const Url& url);
    void beginLevel();                              // the pending level finished loading
    void closeLoadingMovie();

    // Per-level game classes.
    void frontEndBegin();                           // TnFrontEndGame + UI_FrontEnd_m Kismet
    void frontEndStart();                           // [FRONTEND START] (both MovieLoader branches)
    void partyLobbyBegin();                         // TnPartyLobbyGame
    void gameLobbyBegin();                          // TnGameLobbyGameTeam
    void gameLobbyTick(float dt);
    void chooseNextMap(int prevMapId);              // SelectRandomMap
    void setMapId(int id);
    void hostRequestsGameStart();
    void startLevel();                              // TnGameLobbyGame.StartLevel -> match URL

    // TnOnlineActionScriptBinding / TnGameActionScriptBinding functions.
    void openPartyLobby(int gameTeamStatus);
    void editGameMode(const std::string& configName);
    void editPlaylist(int playlistId);
    void playPrivateGame(const std::string& settingsName);
    void playPlaylist(int playlistId, bool forceHost);
    void hostOnlineGame(const GameSettings* gs);
    void quitToMainMenu();             // Game.QuitToMainMenu: TnQuitMessageBox
    void quitGame();                   // TnPlayerController.QuitGame(0) after the confirmation
    void showPopup(const Popup& p);
    void closePopup();
    const GameSettings* settingsByConfigName(const std::string& name, bool privateMatch) const;
    std::string buildLobbyUrl(const GameSettings& gs) const;
    std::string buildMatchUrl(const GameSettings& gs) const;
    void openMovie(const std::string& movie);
    void closeMovie(const std::string& movie);
    void setUIController(UIControllerClass cls, bool inGameLobby = false);
    int pickTeam();

    const Catalog* cat_ = nullptr;
    Options opt_;
    std::mt19937 rng_;
    std::map<int, RecommendedPlayers> recommended_;
    double clock_ = 0.0;

    LevelKind level_ = LevelKind::None;
    Url levelUrl_;
    LevelKind pendingLevel_ = LevelKind::None;
    Url pendingUrl_;
    bool travelPending_ = false;
    int travelDelayFrames_ = 0;
    LoadingScreen loading_;
    std::string loadingMovieName_ = "UI_GFxLoading_p.LoadScreen_GFX";   // SeqAct_SetLoadingMovieFilename

    UIController ui_;
    LobbyState lobby_;
    const GameSettings* currentSettings_ = nullptr;   // SettingsDataStore current (EditGameMode / PlayPrivateGame)
    MatchValues matchValues_;
    LocalProfile profile_;
    SelectedCharacter selected_;
    uint32_t selectionSerial_ = 0;
    int gpPerTeam_ = 32, gpMaxPlayers_ = 64, gpBotsPerTeam_ = 32;   // Gameplay clamp (detected); defaults = the extended target (32 v 32)
    int gameTeamStatus_ = 0;                          // GRI.SetGameTeamStatus (party lobby)
    std::map<std::string, std::map<std::string, int>> settingValues_;   // class -> field -> value index
    MatchLaunch match_;
    bool pendingMatch_ = false, unloadWorld_ = false, quit_ = false;

    // Frontend Kismet state.
    std::vector<std::string> movieQueue_;
    std::vector<std::string> kismetTriggers_;
    std::string kismetMovie_;
    std::string scriptMovie_;
    Popup popup_;
    bool quitPending_ = false;        // TnQuitMessageBox.bWaitingForSafeQuit
    bool matchHasBegun_ = false;
    std::vector<std::pair<std::string, std::string>> uiInvokes_;
    std::vector<std::string> openMovies_;
    bool frontEndStarted_ = false, watchedIntro_ = false, pendingWatchedWrite_ = false;
    bool startScreenPassed_ = false;   // controller / profile / storage assigned (ShowDeviceSelectionUI)
    float blackOutTimer_ = -1.0f;
};

} // namespace frontend
