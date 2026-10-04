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
#include <string>
#include <vector>

#include "frontend/Catalog.h"
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
    void onUIEvent(int code) { ui_.onUIEvent(code); }
    void characterSelected() { ui_.onCharacterSelected(false); }   // TnUIControllerMultiplayer.OnCharacterSelected
    void showMenu();                                // TnPlayerController.ShowMenu (Escape / Start release)
    // [integration] Gameplay MatchOver -> 15 s -> TnGame.ReturnToGameLobby: ServerTravel to the game lobby
    // (UI_Lobby_m?...?MapId=<map>) [RE M05 blockers F4 / F6].
    void returnToGameLobby();
    void setMatchValues(const MatchValues& v) { matchValues_ = v; }
    const MatchValues& matchValues() const { return matchValues_; }
    bool quitRequested() const { return quit_; }
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
    const std::vector<std::string>& openMovies() const { return openMovies_; }   // GFxAction_OpenMovie / OpenUI
    bool frontEndStarted() const { return frontEndStarted_; }
    // Level Kismet triggers the frontend owns, in order ("FsCommand:<cmd>", "MovieStopped:<movie>").
    const std::vector<std::string>& kismetTriggers() const { return kismetTriggers_; }
    bool hasWatchedIntroMovie() const { return watchedIntro_; }
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
    void quitToMainMenu();
    const GameSettings* settingsByConfigName(const std::string& name, bool privateMatch) const;
    std::string buildLobbyUrl(const GameSettings& gs) const;
    std::string buildMatchUrl(const GameSettings& gs) const;
    void openMovie(const std::string& movie);
    void closeMovie(const std::string& movie);
    void setUIController(UIControllerClass cls);
    int pickTeam();

    const Catalog* cat_ = nullptr;
    Options opt_;
    std::mt19937 rng_;
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
    int gameTeamStatus_ = 0;                          // GRI.SetGameTeamStatus (party lobby)
    std::map<std::string, std::map<std::string, int>> settingValues_;   // class -> field -> value index
    MatchLaunch match_;
    bool pendingMatch_ = false, unloadWorld_ = false, quit_ = false;

    // Frontend Kismet state.
    std::vector<std::string> movieQueue_;
    std::vector<std::string> kismetTriggers_;
    std::string kismetMovie_;
    std::vector<std::string> openMovies_;
    bool frontEndStarted_ = false, watchedIntro_ = false, pendingWatchedWrite_ = false;
    bool startScreenPassed_ = false;   // controller / profile / storage assigned (ShowDeviceSelectionUI)
    float blackOutTimer_ = -1.0f;
};

} // namespace frontend
