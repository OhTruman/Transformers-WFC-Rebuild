#include "frontend/GameFlow.h"
#include "frontend/FlowTrace.h"
#include "core/Log.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>

namespace frontend {

namespace {

// Xe-TransEngine.ini [URL] Map / LocalMap / TransitionMap [CONFIRMED].
constexpr const char* kFrontEndMap = "UI_FrontEnd_m";
// TnPlayerController.GetPartyMapName + PartyLobbyGameType -> BuildPartyLobbyURL [CONFIRMED script].
constexpr const char* kPartyLobbyUrl = "UI_PartyLobby_m?game=TransContent.TnPartyLobbyGame?listen";
// UI_FrontEnd_m Kismet movie chain (SeqAct_MoviePlayer, each on the previous one's Stopped) [CONFIRMED].
const char* const kIntroMovies[] = {"Logo_Activision", "Logo_Hasbro", "Logo_HighMoon", "FMV_intro"};
// GameLobby timings [CONFIRMED Xe-TransGame.ini / authored defaults, RE 2.4].
constexpr int kShortCountdown = 10;          // BeginShortCountdown (private, host started)
constexpr int kLobbyIntermissionTime = 45;   // BeginIntermissionCountdown (public autostart)

std::string lowerStr(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }

LevelKind levelForUrl(const Url& u) {
    std::string m = lowerStr(u.map());
    if (m == "ui_frontend_m") return LevelKind::FrontEnd;
    if (m == "ui_partylobby_m") return LevelKind::PartyLobby;
    if (m == "ui_lobby_m" || m == "ui_campaignlobby_m") return LevelKind::GameLobby;
    return LevelKind::Match;
}

} // namespace

const char* levelKindName(LevelKind k) {
    switch (k) {
    case LevelKind::None: return "None";
    case LevelKind::FrontEnd: return "FrontEnd";
    case LevelKind::PartyLobby: return "PartyLobby";
    case LevelKind::GameLobby: return "GameLobby";
    case LevelKind::Match: return "Match";
    }
    return "?";
}

bool GameFlow::init(const Catalog& catalog, const Options& opt) {
    cat_ = &catalog;
    opt_ = opt;
    rng_.seed(opt.seed ? opt.seed : (unsigned)std::chrono::steady_clock::now().time_since_epoch().count());
    profile_.load();
    // HasWatchedIntroMovie is a per-process flag in the original: one zero-initialised global (0x83757450) written
    // only by SetHasWatchedIntroMovie and the controller-assignment tick, never saved or loaded [CONFIRMED ORIGINAL,
    // native decompile]. Every launch therefore plays the logos and the intro chain again.
    watchedIntro_ = opt.skipIntroMovies;
    FlowTrace::emit("boot", {{"map", kFrontEndMap}, {"watchedIntro", FlowTrace::boolean(watchedIntro_)},
                             {"seed", std::to_string(opt.seed)}});
    // Engine boot: [URL] Map=UI_FrontEnd_m. The initial startup movie ([LoadingMovie] InitialStartupFileName
    // TF_InitialStartup) is the native boot loading movie.
    travel(kFrontEndMap, false);
    loading_.kind = "InitialStartup";
    loading_.binkMovie = cat_->loadingMovieInitial();
    loading_.gfxMovie.clear();
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Travel / loading

void GameFlow::travel(const std::string& urlText, bool server) {
    Url url = Url::parse(urlText);
    FlowTrace::emit("travel", {{"url", url.toString()}, {"server", FlowTrace::boolean(server)},
                               {"from", levelKindName(level_)}});
    if (level_ == LevelKind::Match) unloadWorld_ = true;   // the match world is replaced
    // Every travel replaces the world and its PlayerController; the UI controller goes with it - and any open message
    // box (its movie belongs to the old world): a modal left over from a script / bridge travel must not answer the
    // next Accept in the new level.
    if (popup_.open) { FlowTrace::emit("popup.closedByTravel", {{"title", popup_.title}}); closePopup(); }
    openMovies_.clear();
    kismetMovie_.clear();
    movieQueue_.clear();
    pendingUrl_ = url;
    pendingLevel_ = levelForUrl(url);
    travelPending_ = true;
    travelDelayFrames_ = 2;    // the loading movie is presented before the level load runs
    startLoadingMovie(url);
}

void GameFlow::startLoadingMovie(const Url& url) {
    // TnMoviePlayer.StartLoadingMovie(URL) [CONFIRMED script, RE 3.3].
    loading_ = LoadingScreen{};
    loading_.active = true;
    loading_.url = url.toString();
    loading_.binkMovie = cat_->loadingMovieDefault();     // TF_LoadingScreen (native Bink)
    loading_.gfxMovie = loadingMovieName_;                 // UI_GFxLoading_p.LoadScreen_GFX overlay
    std::string game = url.option("Game", url.option("game"));
    int mapId = url.intOption("MapId", -1);
    auto threeTips = [&](const std::string& tag) {
        // 3 x AddEngageText(EngageText[RandomInt(len)]), repeats possible. The mode's EngageText list:
        // TnOnlineGameSettings<tag> inherits TnOnlineGameSettingsBase's 26 strings.
        std::vector<std::string> tips;
        std::string own = cat_->localize("TransGame", "TnOnlineGameSettings" + tag, "EngageText[0]");
        if (!own.empty()) {
            for (int i = 0; i < 64; ++i) {
                std::string t = cat_->localize("TransGame", "TnOnlineGameSettings" + tag, "EngageText[" + std::to_string(i) + "]");
                if (t.empty()) break;
                tips.push_back(t);
            }
        } else tips = cat_->engageTexts();
        for (int i = 0; i < 3 && !tips.empty(); ++i)
            loading_.engageTexts.push_back(tips[std::uniform_int_distribution<size_t>(0, tips.size() - 1)(rng_)]);
    };
    if (game.find("TnPartyLobbyGame") != std::string::npos) {
        loading_.kind = "PartyLobby";   // EnqueuePartyLobbyLoadingMovie: empty title/message + 3 base engage texts
        threeTips("Base");
    } else if (game.find("TnGameLobbyGame") != std::string::npos) {
        loading_.kind = "GameLobby";
        threeTips("Base");
    } else if (mapId != -1) {
        // EnqueueLoadingMapMovie(MapId, tag): title = settings FriendlyName, message = Repl(LoadingMap, "`m", map).
        loading_.kind = "Map";
        std::string tag = url.option("GameModeTag");
        loading_.title = cat_->modeFriendlyName(tag);
        const MapInfo* mi = cat_->mapById(mapId);
        std::string msg = cat_->localize("UIText", "LoadScreen", "LoadingMap");
        size_t p = msg.find("`m");
        if (p != std::string::npos) msg.replace(p, 2, mi ? mi->friendlyName : "");
        loading_.message = msg;
        threeTips(tag);
    } else {
        loading_.kind = "Generic";
    }
    FlowTrace::emit("loading.start", {{"kind", loading_.kind}, {"title", loading_.title}, {"message", loading_.message},
                                      {"bink", loading_.binkMovie}, {"gfx", loading_.gfxMovie},
                                      {"tips", std::to_string(loading_.engageTexts.size())}});
    for (const std::string& t : loading_.engageTexts) FlowTrace::emit("loading.engageText", {{"text", t}});
}

void GameFlow::closeLoadingMovie() {
    if (!loading_.active) return;
    loading_.active = false;
    FlowTrace::emit("loading.close", {{"kind", loading_.kind}, {"level", levelKindName(level_)}});
}

void GameFlow::beginLevel() {
    travelPending_ = false;
    level_ = pendingLevel_;
    levelUrl_ = pendingUrl_;
    FlowTrace::emit("level.begin", {{"level", levelKindName(level_)}, {"map", levelUrl_.map()}, {"url", levelUrl_.toString()}});
    switch (level_) {
    case LevelKind::FrontEnd: frontEndBegin(); break;
    case LevelKind::PartyLobby: partyLobbyBegin(); break;
    case LevelKind::GameLobby: gameLobbyBegin(); break;
    default: break;
    }
}

void GameFlow::tick(float dt) {
    clock_ += dt;
    FlowTrace::setClock(clock_);
    if (quitPending_) {
        // TnQuitMessageBox.Tick: FinalizeQuitToMainMenu once TnGame.IsSafeToQuit (no blocking saves offline).
        quitPending_ = false;
        closePopup();
        quitGame();
    }
    if (travelPending_) {
        if (travelDelayFrames_ > 0) { --travelDelayFrames_; return; }
        if (pendingLevel_ == LevelKind::Match) {
            if (!pendingMatch_ && !unloadWorld_) {
                // The application loads the world (blocking); the loading movie stays up until matchLoaded().
                pendingMatch_ = true;
                FlowTrace::emit("match.launch", {{"url", match_.url.toString()}, {"map", match_.map ? match_.map->mapFilename : "?"},
                                                 {"runtimeDir", match_.map ? match_.map->runtimeDir : "?"},
                                                 {"mode", match_.modeTag}, {"goalScore", std::to_string(match_.goalScore)},
                                                 {"timeLimit", std::to_string(match_.timeLimit)},
                                                 {"team", std::to_string(match_.teamIndex)}});
            }
            return;
        }
        if (unloadWorld_) return;   // wait for the application to release the match world
        beginLevel();
        // CanCloseLoadingMovie: the UI level is fully loaded (seamless travel through TransitionMap) [HIGH].
        closeLoadingMovie();
        return;
    }
    if (blackOutTimer_ >= 0.0f) {
        blackOutTimer_ -= dt;
        if (blackOutTimer_ < 0.0f) {
            // Black-Out (Completed|Aborted) -> GFxAction_OpenMovie UI_GFxFrontEnd_p.MovieLoader_GFX_1.
            openMovie("UI_GFxFrontEnd_p.MovieLoader_GFX_1");
        }
    }
    if (level_ == LevelKind::GameLobby) gameLobbyTick(dt);
}

// ---------------------------------------------------------------------------------------------------------------
// UI_FrontEnd_m (TnFrontEndGame + Main_Sequence Kismet)

void GameFlow::setUIController(UIControllerClass cls, bool inGameLobby) {
    ui_.initialize(cls,
        [this](const std::string& m) {
            openMovies_.push_back(m);
            FlowTrace::emit("ui.open", {{"movie", m}, {"state", uiStateName(ui_.state())}});
        },
        [this]() {
            std::string m = openMovies_.empty() ? std::string() : openMovies_.back();
            if (!openMovies_.empty()) openMovies_.pop_back();
            FlowTrace::emit("ui.close", {{"movie", m}});
        },
        [](bool v) { FlowTrace::emit("ui.hud", {{"visible", FlowTrace::boolean(v)}}); },
        [this](UIState from, UIState to) {
            FlowTrace::emit("ui.state", {{"from", uiStateName(from)}, {"to", uiStateName(to)},
                                         {"controller", uiControllerClassName(ui_.cls())}, {"level", levelKindName(level_)}});
        }, inGameLobby);
}

void GameFlow::frontEndBegin() {
    frontEndStarted_ = false;
    lobby_ = LobbyState{};          // the lobby GRI went with the previous world
    currentSettings_ = nullptr;
    // PlayerController -> TnPlayerController.UpdateUiController: TnFrontEndGame inherits TnUIController; the
    // FrontEnd subclass carries FrontEndUI (TnUIControllerFrontEnd). StartingState NotInGame.
    setUIController(UIControllerClass::FrontEnd);
    // TnFrontEndGame: InitGame (CheckForAutomatedTestingStart timer), PostLogin -> WaitingForController (no pawn,
    // ShouldSpectateOnLogin). WaitingForController.Tick assigns the first connected controller; the PC has no online
    // login to wait for in the rebuild [online - bypassed]: WaitingForBaseOnlineService / WaitingForNetwork /
    // ReadingAccountNames / LoggingIn* are skipped.
    FlowTrace::emit("frontend.game", {{"state", "WaitingForController"}});
    // Controller assigned -> SetHasWatchedIntroMovie() -> state None. The flag lives for the session only: a later
    // return to the frontend (after a match) takes MovieLoader's enterFrontEnd branch [CONFIRMED ORIGINAL].
    FlowTrace::emit("frontend.game", {{"state", "None"}, {"controllerAssigned", "true"}});
    pendingWatchedWrite_ = true;
    // Kismet: SeqEvent_GameplayStarted -> Interp "Black-Out" (InterpLength 0.00105 s, client-side) -> OpenMovie.
    blackOutTimer_ = 0.00105f;
}

void GameFlow::openMovie(const std::string& movie) {
    openMovies_.push_back(movie);
    FlowTrace::emit("gfx.open", {{"movie", movie}});
}

void GameFlow::closeMovie(const std::string& movie) {
    auto it = std::find(openMovies_.begin(), openMovies_.end(), movie);
    if (it != openMovies_.end()) openMovies_.erase(it);
    FlowTrace::emit("gfx.close", {{"movie", movie}});
}

void GameFlow::fsCommand(const std::string& movie, const std::string& cmd, const std::string& arg) {
    FlowTrace::emit("fscommand", {{"movie", movie}, {"cmd", cmd}, {"arg", arg}});
    kismetTriggers_.push_back("FsCommand:" + cmd);
    if (cmd == "enterMovieSequence") {
        // -> RemoteEvent closeMovieLoader (GFxAction_CloseMovie), then Logo_Activision -> Logo_Hasbro -> Logo_HighMoon
        // -> FMV_intro, each started by the previous one's Stopped output -> [FRONTEND START].
        closeMovie("UI_GFxFrontEnd_p.MovieLoader_GFX_1");
        movieQueue_.assign(std::begin(kIntroMovies), std::end(kIntroMovies));
        kismetMovie_ = movieQueue_.front();
        movieQueue_.erase(movieQueue_.begin());
        FlowTrace::emit("movie.play", {{"movie", kismetMovie_}});
    } else if (cmd == "enterFrontEnd") {
        // -> [FRONTEND START] + RemoteEvent closeMovieLoader.
        frontEndStart();
        closeMovie("UI_GFxFrontEnd_p.MovieLoader_GFX_1");
    } else if (cmd == "startMainMenuCamera") {
        // FrontEnd_GFX -> SeqAct_Interp_9395 (energon ring / circuit materials + meshes). Rendering of the
        // UI_FrontEnd_m scene is not available yet (no exported frontend level) - logged only.
        FlowTrace::emit("frontend.kismet", {{"action", "SeqAct_Interp_9395 startMainMenuCamera"}});
    }
}

void GameFlow::movieStopped(const std::string& movie) {
    if (movie != kismetMovie_) return;
    FlowTrace::emit("movie.stopped", {{"movie", movie}});
    kismetTriggers_.push_back("MovieStopped:" + movie);
    if (!movieQueue_.empty()) {
        kismetMovie_ = movieQueue_.front();
        movieQueue_.erase(movieQueue_.begin());
        FlowTrace::emit("movie.play", {{"movie", kismetMovie_}});
    } else {
        kismetMovie_.clear();
        frontEndStart();
    }
}

void GameFlow::scriptMovieStopped() {
    // EndMovieMode -> OnFullScreenMovieStop: MovieEnded on the current UI when it is the FrontEnd UI.
    FlowTrace::emit("movie.stopped", {{"movie", scriptMovie_}, {"by", "Game.PlayMovie"}});
    scriptMovie_.clear();
    if (ui_.cls() == UIControllerClass::FrontEnd && !ui_.openMovie().empty())
        uiInvokes_.push_back({ui_.openMovie(), "_global.MovieEnded"});
}

void GameFlow::frontEndStart() {
    if (frontEndStarted_) return;
    frontEndStarted_ = true;
    // [FRONTEND START] - the same targets from both MovieLoader paths (RE 1.2).
    FlowTrace::emit("frontend.kismet", {{"action", "Interp Fade In / Primary Camera / Camera Orbiter, RemoteEvent StartFireworks, "
                                                   "Reverb REVERB_TRANS_FRONT_END, PlayMusic FRONTEND_MX_ORBIT_01, PlaySound FRONTEND_WHSH_REVEAL"}});
    loadingMovieName_ = "UI_GFxLoading_p.LoadScreen_GFX";   // SeqAct_SetLoadingMovieFilename
    FlowTrace::emit("frontend.kismet", {{"action", "SetLoadingMovieFilename UI_GFxLoading_p.LoadScreen_GFX"}});
    // SeqAct_ToggleHUD -> ToggleHidden(Player) -> TnSeqAct_OpenFrontEnd -> TnPlayerController.OnOpenFrontEnd ->
    // UIController.OnUIEvent(0) -> FrontEnd -> OpenUI(FrontEnd_GFX_1).
    ui_.onUIEvent((int)UIEvent::FrontEnd);
    // SeqAct_InstallGame (Finished) -> 3 x SetMatInstScalarParam (Energon DownScaleUVs 12, ring Opacity).
    if (pendingWatchedWrite_) {
        pendingWatchedWrite_ = false;
        // Game.SetHasWatchedIntroMovie: session flag, not part of the profile.
        watchedIntro_ = true;
        FlowTrace::emit("profile", {{"HasWatchedIntroMovie", "true"}});
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Bridge (ExternalInterface.call)

BridgeValue GameFlow::call(const std::string& fn, const std::vector<std::string>& args) {
    auto arg = [&](size_t i) { return i < args.size() ? args[i] : std::string(); };
    auto argi = [&](size_t i) { return std::atoi(arg(i).c_str()); };
    std::string joined;
    for (const std::string& a : args) joined += (joined.empty() ? "" : ",") + a;
    // Per-frame state polls of the movies are not traced (noise); every action call is.
    static const char* const kPolls[] = {"Online.IsInPartyChatSession", "Online.IsHost", "Online.GetLoginStatus"};
    bool poll = false;
    for (const char* q : kPolls) poll |= fn == q;
    if (!poll) FlowTrace::emit("bridge", {{"fn", fn}, {"args", joined}, {"level", levelKindName(level_)}});

    // ---- TnGameActionScriptBinding ----
    if (fn == "Game.HasWatchedIntroMovie") return watchedIntro_;
    // Settings: the movie wrote the <OnlinePlayerData:ProfileData.*> fields; apply / save pushes them to their owners
    // and persists the profile (TnProfileSettings) [CONFIRMED call names].
    if (fn == "Game.ApplyProfileSettings" || fn == "Console.SaveProfileSettings") { profile_.apply(); return true; }
    if (fn == "Game.PlayMovie") {
        // TnGameActionScriptBinding.PlayMovie (native) -> HmPlayerController.ClientPlayMovie -> BeginMovieMode
        // (OnFullScreenMovieStart: UI event 12, HUD / UI hidden) ... EndMovieMode (OnFullScreenMovieStop: UI event 13,
        // then InvokeOnCurrentUIConditional(FrontEndUI, "_global.MovieEnded")) [CONFIRMED script]. The Extras menu
        // removes its own input in _global.MovieStarted and gets it back only from MovieEnded.
        scriptMovie_ = arg(0);
        FlowTrace::emit("movie.play", {{"movie", scriptMovie_}, {"by", "Game.PlayMovie"}});
        if (scriptMovie_.empty()) scriptMovieStopped();
        return {};
    }
    if (fn == "Game.QuitToMainMenu") { quitToMainMenu(); return {}; }
    if (fn == "Game.ExitGame") {
        // TnGameActionScriptBinding.ExitGame: GenericWarningTitle / ExitGameMessage, A Continue (ConfirmExitGame:
        // ConsoleCommand "quit", the box stays with the waiting icon), B Cancel [CONFIRMED script].
        Popup p;
        p.title = "$UIText.MessagePrompts.GenericWarningTitle";
        p.message = "$UIText.MessagePrompts.ExitGameMessage";
        p.a = {"$UIText.MessagePrompts.GenericContinueButton", "exit", false};
        p.b = {"$UIText.MessagePrompts.GenericCancelButton", "", true};
        p.showDefaultButtons = false;
        showPopup(p);
        return {};
    }
    if (fn.rfind("MessageBox.On", 0) == 0 && fn.size() == 14) { popupButton(fn[13]); return {}; }

    // ---- TnOnlineActionScriptBinding ----
    if (fn == "Online.CheckCanPlayOnlineModes" || fn == "Online.CanPlayOnlineModes") return true;   // no profile gate offline
    if (fn == "Online.CheckIsProfileReady" || fn == "Online.IsProfileReady") return true;
    // TnOnlineActionScriptBinding.ShouldShowStartScreen [CONFIRMED script]: true while the player is not signed in, has
    // no assigned controller or no storage device. Press START (mc_menuPressStart) calls ShowDeviceSelectionUI, which
    // assigns all three; afterwards every return to UI_FrontEnd_m goes straight to the main menu. The offline rebuild
    // has one local profile, so "signed in" = Press START passed once this session [HIGH].
    if (fn == "Online.ShouldShowStartScreen") return !startScreenPassed_;
    if (fn == "Online.ShowDeviceSelectionUI") { startScreenPassed_ = true; return {}; }   // device / profile UI (360): bypassed
    if (fn == "Online.OpenPartyLobby") {
        // StringToGameTeamStatus: FFA 1, SingleTeam 2, Team 3, Campaign 4, default 3 [CONFIRMED script].
        std::string s = arg(0);
        int gts = s == "GTS_FreeForAllGame" ? 1 : s == "GTS_SingleTeamGame" ? 2 : s == "GTS_TeamGame" ? 3 : s == "GTS_CampaignGame" ? 4 : 3;
        openPartyLobby(gts);
        return {};
    }
    if (fn == "Online.EditGameMode") { editGameMode(arg(0)); return {}; }
    if (fn == "Online.EditPlaylist") { editPlaylist(argi(0)); return {}; }
    if (fn == "Online.PlayPrivateGame") { playPrivateGame(arg(0)); return {}; }
    if (fn == "Online.PlayPlaylist") { playPlaylist(argi(0), arg(1) == "true" || arg(1) == "1"); return {}; }
    if (fn == "Online.SetSelectedMapID") {
        // -> TnGameLobbyGame.HostRequestMapID -> GRI.HostRequestMapID (ignored unless MapSelectionMethod 1).
        if (level_ == LevelKind::GameLobby && lobby_.mapSelectionMethod == 1) setMapId(argi(0));
        else FlowTrace::emit("lobby.mapRequestIgnored", {{"mapId", arg(0)}, {"method", std::to_string(lobby_.mapSelectionMethod)}});
        return {};
    }
    if (fn == "Online.BeginLobbyExitCountdown") { hostRequestsGameStart(); return {}; }
    if (fn == "Online.IsHost") return true;   // the local player hosts the private match
    if (fn == "Online.GetLoginStatus") return 2;   // PARTIAL: LS_LoggedIn; no online service
    if (fn == "Online.IsInPartyChatSession") return false;
    if (fn == "Online.CheckCampaignCompleteMessage") return {};   // no campaign-complete prompt (no campaign progress offline)
    if (fn == "Online.SetPartyLobbyType") { FlowTrace::emit("partylobby.type", {{"type", arg(0)}}); return {}; }
    if (fn == "Online.SubmitMapVeto") { FlowTrace::emit("lobby.veto", {}); return {}; }
    if (fn == "Online.SwitchTeam") {
        if (level_ == LevelKind::GameLobby && lobby_.localTeam >= 0) lobby_.localTeam = 1 - lobby_.localTeam;
        FlowTrace::emit("lobby.team", {{"team", std::to_string(lobby_.localTeam)}});
        return {};
    }
    FlowTrace::emit("bridge.unhandled", {{"fn", fn}});
    return {};
}

void GameFlow::openPartyLobby(int gts) {
    // CheckOnlineWarningPart0..2 (profile / CanCommunicate / RecommendPlayingCampaignFirst message boxes) are online
    // and first-time prompts: PARTIAL (not shown). -> ConfirmOpenPartyLobby: GRI.SetGameTeamStatus(GTS),
    // URL = BuildPartyLobbyURL(), TnGame.ClientTravelToMap(URL).
    gameTeamStatus_ = gts;
    FlowTrace::emit("online.ConfirmOpenPartyLobby", {{"gameTeamStatus", std::to_string(gts)}});
    travel(kPartyLobbyUrl, false);
}

const GameSettings* GameFlow::settingsByConfigName(const std::string& name, bool privateMatch) const {
    // SettingsDataStore.SetCurrentByName(ConfigurationName). PARTIAL: the GameSettingsCfgList names are an authored
    // CDO list not in the manifests yet; the AS passes the mode's SettingsConfigName ("TDM") or a class name.
    if (const GameSettings* s = cat_->settings(name)) return s;
    if (const GameSettings* s = cat_->settings("TnOnlineGameSettings" + name)) {
        if (const GameSettings* v = cat_->settingsForTag(s->tag, privateMatch)) return v;
        return s;
    }
    return cat_->settingsForTag(name, privateMatch);
}

void GameFlow::editGameMode(const std::string& configName) {
    const GameSettings* gs = settingsByConfigName(configName, true);
    if (!gs) { LOG_WARN("FLOW ERROR: Failed to find settings for %s", configName.c_str()); return; }
    currentSettings_ = gs;
    // GRI.PublishGameInfo(GameSettings.GetGameModeTag(), GameSettings) -> <CurrentGame:GameModeFriendlyName/Rules/Tag>.
    lobby_.settings = gs;
    lobby_.gameModeTag = gs->tag;
    FlowTrace::emit("lobby.publishGameInfo", {{"tag", gs->tag}, {"settings", gs->className},
                                              {"friendlyName", cat_->modeFriendlyName(gs->tag)}});
}

void GameFlow::editPlaylist(int playlistId) {
    const Playlist* p = cat_->playlistById(playlistId);
    const GameSettings* gs = p ? cat_->settings(p->settingsClass) : nullptr;
    if (!gs) { LOG_WARN("FLOW ERROR: Failed to get game settings for playlist (%d)", playlistId); return; }
    lobby_.settings = gs;
    lobby_.gameModeTag = gs->tag;
    lobby_.playlistId = playlistId;
    FlowTrace::emit("lobby.publishGameInfo", {{"tag", gs->tag}, {"settings", gs->className}, {"playlist", std::to_string(playlistId)}});
}

void GameFlow::playPrivateGame(const std::string& settingsName) {
    // SettingsDataStore.SetCurrentByName(SettingsName); GetGame().HostOnlineGame(current settings).
    if (const GameSettings* gs = settingsByConfigName(settingsName, true)) currentSettings_ = gs;
    if (!currentSettings_) { LOG_WARN("FLOW PlayPrivateGame: no settings for %s", settingsName.c_str()); return; }
    lobby_.playlistId = -1;
    hostOnlineGame(currentSettings_);
}

void GameFlow::playPlaylist(int playlistId, bool forceHost) {
    // PlayPlaylistInternal: settings = PlaylistManager.GetGameSettings(PlaylistId, 0); HostType 1 -> host, otherwise a
    // VERSUS search -> FindingGames -> join or ReservingSpace.HostPlaylist. No online search exists offline: the
    // rebuild hosts the playlist game (the search's no-result path) [PARTIAL].
    const Playlist* p = cat_->playlistById(playlistId);
    const GameSettings* gs = p ? cat_->settings(p->settingsClass) : nullptr;
    if (!gs) { LOG_WARN("FLOW PlayPlaylist: unknown playlist %d", playlistId); return; }
    FlowTrace::emit("online.PlayPlaylist", {{"playlist", std::to_string(playlistId)}, {"settings", gs->className},
                                            {"forceHost", FlowTrace::boolean(forceHost)}, {"search", "bypassed"}});
    lobby_.playlistId = playlistId;
    hostOnlineGame(gs);
}

void GameFlow::hostOnlineGame(const GameSettings* gs) {
    // TnPartyLobbyGame.HostOnlineGameInternal: CheckCanPlayGameType (party size 1 <= allowed);
    // StartOnlineGame('Party'); HostingGame: presence session, CreateReservationHost, CreateOnlineGame
    // [online - bypassed]; OnCreateOnlineGameComplete: GameSettings.BuildLobbyURL(URL) $ "?listen";
    // TnGame.ServerTravelToMap(URL, true, false).
    currentSettings_ = gs;
    std::string url = buildLobbyUrl(*gs) + "?listen";
    FlowTrace::emit("online.HostOnlineGame", {{"settings", gs->className}, {"lobbyUrl", url}});
    travel(url, true);
}

std::string GameFlow::buildLobbyUrl(const GameSettings& gs) const {
    // TnOnlineGameSettingsBase.BuildLobbyURL: LobbyMapName ?Game=LobbyGameClass ?GameModeTag=<tag>
    // ?GameTeamStatus=<TeamType> ?MaxPlayers=<Public+Private> ?IconicMode=<0|1> [CONFIRMED script].
    Url u(gs.lobbyMapName);
    u.setOption("Game", gs.lobbyGameClass);
    u.setOption("GameModeTag", gs.tag);
    u.setOption("GameTeamStatus", std::to_string(gs.teamTypeValue));
    u.setOption("MaxPlayers", std::to_string(gs.numPublicConnections + gs.numPrivateConnections));
    u.setOption("IconicMode", "0");
    return u.toString();
}

GameFlow::BotRows GameFlow::botRows() const {
    const int gts = lobby_.settings ? lobby_.settings->teamTypeValue : lobby_.gameTeamStatus;
    return gts == 3 ? BotRows::Teams : gts == 1 ? BotRows::FreeForAll : BotRows::None;
}

int GameFlow::botMax(const std::string& field) const {
    if (field == "difficulty") return 2;
    if (field == "extended") return 1;
    const bool ext = profile_.bots.extended;
    // players per side incl. the human: original 5; extended min(17, Gameplay's maxPerTeam)
    const int perSide = ext ? std::min(17, gpPerTeam_) : 5;
    const int players = ext ? gpMaxPlayers_ : 10;
    if (botRows() == BotRows::Teams) {
        if (field == "autobot") return perSide - (humanFaction() == 0 ? 1 : 0);
        if (field == "decepticon") return perSide - (humanFaction() == 1 ? 1 : 0);
        return 0;
    }
    if (botRows() == BotRows::FreeForAll) return field == "enemy" ? players - 1 : 0;
    return 0;
}

void GameFlow::setBotSetting(const std::string& field, int value) {
    LocalProfile::Bots& b = profile_.bots;
    if (field == "extended") b.extended = value != 0;
    else {
        int& v = field == "autobot" ? b.autobot : field == "decepticon" ? b.decepticon : field == "enemy" ? b.enemy : b.difficulty;
        v = std::clamp(value, 0, std::max(0, botMax(field)));
    }
    // a smaller limit clamps the counts
    b.autobot = std::min(b.autobot, std::max(0, botMax("autobot")));
    b.decepticon = std::min(b.decepticon, std::max(0, botMax("decepticon")));
    if (botRows() == BotRows::FreeForAll) b.enemy = std::min(b.enemy, std::max(0, botMax("enemy")));
    profile_.save();
    FlowTrace::emit("lobby.bots", {{"field", field}, {"autobot", std::to_string(b.autobot)}, {"decepticon", std::to_string(b.decepticon)},
                                   {"enemy", std::to_string(b.enemy)}, {"difficulty", std::to_string(b.difficulty)},
                                   {"extended", FlowTrace::boolean(b.extended)}, {"provenance", "PC ADAPTATION"}});
}

std::string GameFlow::buildMatchUrl(const GameSettings& gs) const {
    // TnOnlineGameSettingsBase.BuildURL [CONFIRMED script; the natively appended properties are HIGH]:
    //   AppendPropertiesToURL -> ?PlaylistId=-1?GamerRegion=0?PointsToWin=40 (score setting)
    //   AppendContextsToURL   -> localized contexts (exact text PARTIAL; not emitted)
    //   ?Game ?GameModeTag ?GameTeamStatus ?GameRules=(empty) ?MaxPlayers ?StatsWriters ?LobbyGameClassName ?IconicMode
    //   [?TimeLimit=<TimeLimits[idx]>] [?listen]
    Url u("");
    u.setOption("PlaylistId", std::to_string(lobby_.playlistId));
    u.setOption("GamerRegion", "0");
    // Host options (TnGameSettings values) feed the natively appended properties / contexts [HIGH].
    int score = gs.pointsToWin;
    if (const SettingField* f = gs.field(gs.scoreOptionName)) {
        int i = settingIndex(&gs, f->name);
        if (i >= 0 && i < (int)f->numeric.size()) score = (int)f->numeric[(size_t)i];
    }
    if (score >= 0) u.setOption(gs.scoreOptionName, std::to_string(score));
    u.setOption("Game", gs.gameClass);
    u.setOption("GameModeTag", gs.tag);
    u.setOption("GameTeamStatus", std::to_string(gs.teamTypeValue));
    u.setOption("GameRules", "");
    u.setOption("MaxPlayers", std::to_string(gs.numPublicConnections + gs.numPrivateConnections));
    u.setOption("StatsWriters", "");
    u.setOption("LobbyGameClassName", gs.lobbyGameClass);
    u.setOption("IconicMode", "0");
    if (!gs.timeLimits.empty()) {
        int ti = settingIndex(&gs, "TimeLimit");
        size_t idx = std::min((size_t)(ti >= 0 ? ti : gs.timeLimitDefaultIndex), gs.timeLimits.size() - 1);
        char b[32]; std::snprintf(b, sizeof b, "%.2f", (double)gs.timeLimits[idx]);   // float -> string
        u.setOption("TimeLimit", b);
    }
    if (gs.numPublicConnections + gs.numPrivateConnections > 0) u.addFlag("listen");
    std::string s = u.toString();
    return s;   // "?PlaylistId=..." (the map filename is prefixed by LobbyGRI.ModifyURL)
}

void GameFlow::returnToGameLobby() {
    // TnGame.ReturnToGameLobby [RE M05 blockers F4 / F6, CONFIRMED]: after MatchOver's 15 s the host ServerTravels to
    // the game lobby with the match's MapId. The rebuilt lobby movie re-sends map index 0 (C7, HIGH).
    const GameSettings* gs = currentSettings_ ? currentSettings_ : (match_.settings ? match_.settings : nullptr);
    if (!gs) { FlowTrace::emit("match.returnToLobby", {{"error", "no lobby settings"}}); travel(kFrontEndMap, false); return; }
    Url u = Url::parse(buildLobbyUrl(*gs));
    if (match_.mapId >= 0) u.setOption("MapId", std::to_string(match_.mapId));
    u.addFlag("listen");
    FlowTrace::emit("match.returnToLobby", {{"url", u.toString()}});
    travel(u.toString(), true);
}

void GameFlow::selectCharacter(const SelectedCharacter& c) {
    selected_ = c;
    selected_.valid = true;
    ++selectionSerial_;
    auto list = [](const std::vector<std::string>& v) { std::string o; for (const auto& x : v) o += (o.empty() ? "" : ",") + x; return o; };
    auto col = [](const CharacterColorSel& k) {
        char b[48];
        if (k.useDefault()) std::snprintf(b, sizeof b, "default(p%d %.0f,%.0f)", k.palette, k.x, k.y);
        else std::snprintf(b, sizeof b, "%02X%02X%02X", k.r, k.g, k.b);
        return std::string(b);
    };
    FlowTrace::emit("character.selected", {{"name", c.name}, {"type", std::to_string(c.type)}, {"specialty", c.specialty},
                                           {"autobot", c.chassis[0]}, {"decepticon", c.chassis[1]},
                                           {"autobotBody", c.bodyAvailable[0] ? "ok" : "MISSING"},
                                           {"decepticonBody", c.bodyAvailable[1] ? "ok" : "MISSING"},
                                           {"weapons", list(c.weapons)}, {"vehicleWeapon", list(c.vehicleWeapons)}, {"melee", list(c.melee)},
                                           {"abilities", list(c.abilities)}, {"skills", list(c.skills)},
                                           {"colorsA", col(c.primary[0]) + "/" + col(c.secondary[0])},
                                           {"colorsD", col(c.primary[1]) + "/" + col(c.secondary[1])}});
    for (int f = 0; f < 2; ++f)
        if (!c.chassis[f].empty() && !c.bodyAvailable[f])
            LOG_WARN("FRONTEND selection %s: no %s body for chassis %s (%s): Gameplay cannot spawn it", c.name.c_str(),
                     f == 0 ? "Autobot" : "Decepticon", c.chassis[f].c_str(), c.robotGltf[f].c_str());
    // In the match, the pre-game screen follows (OnCharacterSelected -> GameStartUI); spawn waits for the selection.
    if (level_ == LevelKind::Match) characterSelected();
}

void GameFlow::clearSelectedCharacter() {
    selected_ = SelectedCharacter{};
    FlowTrace::emit("character.cleared", {});
}

void GameFlow::quitToMainMenu() {
    // Game.QuitToMainMenu -> UIController.ShowCustomPopupUI(TnQuitMessageBox): TnGame.GetQuitGameMessage
    // (LeaveLobby / LeaveLobbyConfirm), A Yes -> ConfirmQuitToMainMenu (waiting icon, "Quitting..." post-confirm
    // message, then QuitGame(0) once TnGame.IsSafeToQuit), B No [CONFIRMED script].
    Popup p;
    p.title = "$UIText.MessagePrompts.LeaveLobby";
    p.message = "$UIText.MessagePrompts.LeaveLobbyConfirm";
    p.postConfirm = "$UIText.MessagePrompts.LeaveLobbyInProgressMessage";
    p.a = {"$UIText.ButtonHints.Yes", "quit", false};
    p.b = {"$UIText.ButtonHints.No", "", true};
    p.showDefaultButtons = false;
    showPopup(p);
}

void GameFlow::quitGame() {
    // TnPlayerController.QuitGame(0) [CONFIRMED script]: a standalone world -> QuitToFrontEnd; a listen server (the
    // lobbies and private matches) -> QuitToFrontEnd from the party lobby (TnPlayerControllerPartyLobby.IsInPartyLobby),
    // otherwise QuitToPartyLobby: TellClientsToReturnToPartyHost -> ClientReturnToParty -> BuildPartyLobbyURL.
    bool toFrontEnd = level_ == LevelKind::PartyLobby || level_ == LevelKind::FrontEnd || level_ == LevelKind::None;
    if (level_ == LevelKind::Match) {
        // TnUIController OnGotoMainMenu (11) from the pause state; OnQuitGame -> RecordSessionComplete("Quit").
        ui_.onUIEvent((int)UIEvent::GotoMainMenu);
        FlowTrace::emit("match.quit", {{"reason", "QuitGame"}});
    }
    FlowTrace::emit("quit.route", {{"from", levelKindName(level_)}, {"to", toFrontEnd ? "FrontEnd" : "PartyLobby"},
                                   {"provenance", "CONFIRMED script (TnPlayerController.QuitGame)"}});
    if (toFrontEnd) travel(kFrontEndMap, false);
    else travel(kPartyLobbyUrl, false);   // the party's GameTeamStatus is kept
}

std::string GameFlow::Popup::buttonString() const {
    std::string s;
    s += a.text.empty() ? ",," : a.text + ",MessageBox.OnA,";
    s += b.text.empty() ? ",," : b.text + ",MessageBox.OnB,";
    s += x.text.empty() ? ",," : x.text + ",MessageBox.OnX,";
    s += y.text.empty() ? "," : y.text + ",MessageBox.OnY,";
    if (showDefaultButtons && s == ",,,,,,,") s = "$UIText.ButtonHints.Ok,MessageBox.OnA,,,,,,";
    return s;
}

void GameFlow::showPopup(const Popup& p) {
    uint64_t serial = popup_.serial + 1;
    popup_ = p;
    popup_.open = true;
    popup_.serial = serial;
    FlowTrace::emit("popup.show", {{"title", p.title}, {"message", p.message}, {"buttons", popup_.buttonString()}});
}

void GameFlow::closePopup() {
    if (!popup_.open) return;
    popup_.open = false;
    ++popup_.serial;
    FlowTrace::emit("popup.close", {{"title", popup_.title}});
}

void GameFlow::popupClosedByMovie() {
    if (popup_.open && !quitPending_) closePopup();
}

void GameFlow::popupButton(char which) {
    if (!popup_.open) return;
    PopupButton btn = which == 'A' ? popup_.a : which == 'B' ? popup_.b : which == 'X' ? popup_.x : popup_.y;
    FlowTrace::emit("popup.button", {{"button", std::string(1, which)}, {"action", btn.action}});
    // HandleButtonPress: close on press, or keep the box with the waiting icon and no buttons.
    if (btn.closeOnPress) closePopup();
    else {
        popup_.icon = 1;
        popup_.a = popup_.b = popup_.x = popup_.y = PopupButton{};
        popup_.showDefaultButtons = false;
        if (!popup_.postConfirm.empty()) popup_.message = popup_.postConfirm;
        ++popup_.serial;
    }
    if (btn.action == "quit") quitPending_ = true;
    else if (btn.action == "exit") { quit_ = true; FlowTrace::emit("exit", {}); }
}

// ---------------------------------------------------------------------------------------------------------------
// UI_PartyLobby_m (TnPartyLobbyGame)

void GameFlow::partyLobbyBegin() {
    // InitGame: PlaylistManager.DownloadPlaylist(). PostLogin (local) -> WaitingForTasksToIdle -> CreatingPartySessions
    // [online - bypassed] -> Ready.
    FlowTrace::emit("partylobby.game", {{"state", "Ready"}, {"gameTeamStatus", std::to_string(gameTeamStatus_)},
                                        {"playlists", std::to_string(cat_->playlists().size())}});
    lobby_ = LobbyState{};
    lobby_.gameTeamStatus = gameTeamStatus_;
    // TnUIControllerPartyLobby: StartingState InLobby -> OpenUI(PartyLobby_GFX_1).
    setUIController(UIControllerClass::PartyLobby);
}

// ---------------------------------------------------------------------------------------------------------------
// UI_Lobby_m (TnGameLobbyGame -> TnGameLobbyGameTeamBase -> TnGameLobbyGameTeam)

void GameFlow::gameLobbyBegin() {
    const GameSettings* gs = currentSettings_;
    std::string tag = levelUrl_.option("GameModeTag");
    if (!gs || gs->tag != tag) gs = cat_->settingsForTag(tag, true);
    lobby_.settings = gs;
    lobby_.gameModeTag = tag;
    lobby_.gameTeamStatus = levelUrl_.intOption("GameTeamStatus", gs ? gs->teamTypeValue : 3);
    // InitGame: NumRequiredPlayers = settings.NumRequiredPlayers (TDM 4) [RE 2.4].
    lobby_.numRequiredPlayers = gs && gs->numRequiredPlayers > 0 ? gs->numRequiredPlayers : 4;
    // InitGameReplicationInfo: AutostartCountdown = !IsPrivateGame(); MapSelectionMethod = setting 0x40000014
    // (Public 0 Rotate / Private 1 Host's Choice); AllowMapVeto = (method == 0).
    bool isPrivate = gs && gs->isPrivate;
    lobby_.autostartCountdown = !isPrivate;
    int msm = settingIndex(gs, "MapSelectionMethod");
    lobby_.mapSelectionMethod = msm >= 0 ? msm : (gs ? gs->mapSelectionMethod : 1);
    lobby_.allowMapVeto = lobby_.mapSelectionMethod == 0;
    lobby_.countingDown = lobby_.countdownRubicon = lobby_.finalCountdown = false;
    lobby_.countdown = 0;
    lobby_.localTeam = -1;
    FlowTrace::emit("gamelobby.game", {{"gameClass", levelUrl_.option("Game")}, {"tag", tag}, {"settings", gs ? gs->className : "?"},
                                       {"private", FlowTrace::boolean(isPrivate)}, {"mapSelectionMethod", std::to_string(lobby_.mapSelectionMethod)}});
    // GRI.OnEnterLobbyFromMap(GRI.GetMapID()): MapId == -1 or Rotate or campaign -> ChooseNextMap, else SetMapId.
    int prev = levelUrl_.intOption("MapId", -1);
    if (prev == -1 || lobby_.mapSelectionMethod == 0) chooseNextMap(prev);
    else setMapId(prev);
    // Private: LobbyStatus 3 ("Waiting for host to start game"); public: 1 ("Finding `p more players").
    lobby_.lobbyStatus = isPrivate ? 3 : 1;
    if (!isPrivate) FlowTrace::emit("gamelobby.autostart", {{"requiredPlayers", std::to_string(lobby_.numRequiredPlayers)},
                                                            {"intermission", std::to_string(kLobbyIntermissionTime)}});
    FlowTrace::emit("gamelobby.status", {{"lobbyStatus", std::to_string(lobby_.lobbyStatus)},
                                         {"text", cat_->localize("TransGame", "TnGameReplicationInfoGameLobby",
                                                                 isPrivate ? "WaitingForHostToStartStatus" : "WaitingForPlayersStatus")}});
    // TnUIControllerGameLobby: StartingState InLobby -> OpenUI(GameLobby_GFX_1).
    setUIController(UIControllerClass::GameLobby);
}

void GameFlow::chooseNextMap(int prevMapId) {
    // SelectRandomMap(prev): GetCompatibleMaps() (CompatibleGameTypes contains GRI.GameModeTag, skipping
    // IsProviderDisabled = !HasRequiredAssets); RandomMap(cur, n): cur == -1 -> RandRange(0, n), else
    // (cur + RandRange(1, n)) % n - a uniformly random DIFFERENT compatible map [CONFIRMED script].
    std::vector<const MapInfo*> maps = cat_->compatibleMaps(lobby_.gameModeTag, true);
    if (maps.empty()) {
        FlowTrace::emit("gamelobby.map", {{"mapId", "-1"}, {"why", "no compatible map with rebuild runtime data"}});
        lobby_.mapId = -1;
        return;
    }
    int n = (int)maps.size(), cur = -1;
    for (int i = 0; i < n; ++i) if (maps[(size_t)i]->mapId == prevMapId) cur = i;
    // RandRange(0, n) on an int: 0..n-1 [HIGH: UE3 RandRange is a float range truncated to the int index].
    int pick = cur == -1 ? std::uniform_int_distribution<int>(0, n - 1)(rng_)
                         : (n > 1 ? (cur + std::uniform_int_distribution<int>(1, n - 1)(rng_)) % n : cur);
    setMapId(maps[(size_t)pick]->mapId);
}

void GameFlow::setMapId(int id) {
    const MapInfo* mi = cat_->mapById(id);
    lobby_.mapId = id;
    // UpdatePrestreaming(): GameEngine.UpdateMapPrestreaming(ConvertMapIdToMapFilename(id)), bHighPriorityLoading.
    lobby_.prestreamMap = mi ? mi->mapFilename : "";
    FlowTrace::emit("gamelobby.map", {{"mapId", std::to_string(id)}, {"map", mi ? mi->mapFilename : "?"},
                                      {"name", mi ? mi->friendlyName : "?"},
                                      {"hasRequiredAssets", FlowTrace::boolean(mi && mi->hasRequiredAssets)}});
    // TnPlayerControllerGameLobby: !HasRequiredMapAssets(MapId) -> ShowContentMissingMessageBox.
    if (mi && !mi->hasRequiredAssets) FlowTrace::emit("gamelobby.contentMissing", {{"mapId", std::to_string(id)}});
}

void GameFlow::hostRequestsGameStart() {
    if (level_ != LevelKind::GameLobby || lobby_.countdownRubicon) return;
    const MapInfo* mi = cat_->mapById(lobby_.mapId);
    if (!mi || !mi->hasRequiredAssets) {
        FlowTrace::emit("gamelobby.startRefused", {{"mapId", std::to_string(lobby_.mapId)}, {"why", "map has no rebuild runtime data"}});
        return;
    }
    // HostRequestsGameStart -> CountdownRubicon = true, BeginShortCountdown = 10 s -> FinalCountdown.
    lobby_.countdownRubicon = true;
    lobby_.countingDown = true;
    lobby_.countdown = kShortCountdown;
    lobby_.countdownTimer = 0.0f;
    FlowTrace::emit("gamelobby.countdown", {{"seconds", std::to_string(kShortCountdown)}, {"kind", "BeginShortCountdown"}});
}

int GameFlow::pickTeam() {
    // TnTeamHandlerTwoTeams.PickTeam: keep a current team < 2; else the smaller team; on a tie RandomInt(2).
    // The local host is the only player: both teams are empty -> RandomInt(2) [HIGH as the offline reduction].
    return std::uniform_int_distribution<int>(0, 1)(rng_);
}

void GameFlow::gameLobbyTick(float dt) {
    if (!lobby_.countingDown) return;
    // GRI.ResetCountdown: a 1 s repeating DecrementCountdown timer.
    lobby_.countdownTimer += dt;
    while (lobby_.countdownTimer >= 1.0f && lobby_.countdown > 0) {
        lobby_.countdownTimer -= 1.0f;
        --lobby_.countdown;
        FlowTrace::emit("gamelobby.countdownTick", {{"value", std::to_string(lobby_.countdown)}});
        if (lobby_.countdown <= 0 && !lobby_.finalCountdown) {
            // FinalCountdown.BeginState: AutobalanceTeams -> TeamHandler.AssignTeams() (from the online reservation,
            // none offline) else ForceToTeam() -> PickTeam(255) for every human without a team.
            lobby_.finalCountdown = true;
            // Teams exist only for GTS_TeamGame lobbies (TnTeamHandlerTwoTeams); FFA has none.
            if (lobby_.localTeam < 0 && lobby_.gameTeamStatus == 3) lobby_.localTeam = pickTeam();
            FlowTrace::emit("gamelobby.finalCountdown", {{"team", std::to_string(lobby_.localTeam)},
                                                         {"teamName", lobby_.localTeam == 0 ? "Autobots" : lobby_.localTeam == 1 ? "Decepticons" : "none (FFA)"}});
        }
    }
    // ShouldStartGame = HasCountdownExpired() && !PC.IsJoiningASession() -> StartLevel().
    if (lobby_.finalCountdown && lobby_.countdown <= 0) {
        lobby_.countingDown = false;
        startLevel();
    }
}

void GameFlow::startLevel() {
    const GameSettings* gs = lobby_.settings;
    const MapInfo* mi = cat_->mapById(lobby_.mapId);
    if (!gs || !mi) { LOG_WARN("FLOW StartLevel: no settings/map"); return; }
    // GameSettings.BuildURL(MapURL); LobbyGRI.ModifyURL(MapURL): GetMapFilename() $ MapURL $ "?MapId=" $ GetMapID().
    std::string url = mi->mapFilename + buildMatchUrl(*gs) + "?MapId=" + std::to_string(mi->mapId);
    {   // Private Match bots (PC ADAPTATION; Gameplay's MatchLaunch::fromURL reads them): only when any are configured.
        // Team modes send the faction counts (?BotsAutobot ?BotsDecepticon) and, for parsers that predate them, the
        // human-relative ?BotsFriendly ?BotsEnemy; ?ExtendedPlayers is the Custom Game player limit.
        const LocalProfile::Bots& b = profile_.bots;
        const BotRows rows = botRows();
        const int au = rows == BotRows::Teams ? std::min(b.autobot, botMax("autobot")) : 0;
        const int de = rows == BotRows::Teams ? std::min(b.decepticon, botMax("decepticon")) : 0;
        const int friendly = rows == BotRows::Teams ? (humanFaction() == 1 ? de : au) : 0;
        const int enemy = rows == BotRows::Teams ? (humanFaction() == 1 ? au : de) : rows == BotRows::FreeForAll ? std::min(b.enemy, botMax("enemy")) : 0;
        if (lobby_.playlistId < 0 && friendly + enemy > 0) {
            if (rows == BotRows::Teams) url += "?BotsAutobot=" + std::to_string(au) + "?BotsDecepticon=" + std::to_string(de);
            url += "?BotsFriendly=" + std::to_string(friendly) + "?BotsEnemy=" + std::to_string(enemy) +
                   "?BotDifficulty=" + std::to_string(std::clamp(b.difficulty, 0, 2)) + "?ExtendedPlayers=" + (b.extended ? "1" : "0");
            FlowTrace::emit("launch.bots", {{"autobot", std::to_string(au)}, {"decepticon", std::to_string(de)}, {"friendly", std::to_string(friendly)},
                                            {"enemy", std::to_string(enemy)}, {"difficulty", std::to_string(b.difficulty)},
                                            {"extended", FlowTrace::boolean(b.extended)}, {"provenance", "PC ADAPTATION"}});
        }
    }
    match_ = MatchLaunch{};
    match_.url = Url::parse(url);
    match_.map = mi;
    match_.settings = gs;
    match_.modeTag = gs->tag;
    match_.mapId = mi->mapId;
    // What the match reads back from the URL (TnMultiplayerGame.InitGame / GameInfo.InitGame).
    match_.goalScore = std::max(0, match_.url.intOption("PointsToWin", 0));
    match_.rounds = std::max(0, match_.url.intOption("Rounds", 0));
    match_.timeLimit = std::max(0, match_.url.intOption("TimeLimit", 0));
    match_.gameTeamStatus = match_.url.intOption("GameTeamStatus", 0);
    match_.teamIndex = lobby_.localTeam;
    FlowTrace::emit("gamelobby.startLevel", {{"url", url}});
    travel(url, true);
}

// ---------------------------------------------------------------------------------------------------------------
// Match map

void GameFlow::matchLoaded() {
    pendingMatch_ = false;
    travelPending_ = false;
    level_ = LevelKind::Match;
    levelUrl_ = match_.url;
    FlowTrace::emit("level.begin", {{"level", "Match"}, {"map", levelUrl_.map()}, {"url", levelUrl_.toString()}});
    // GRI.PostBeginPlay -> OnUIEvent(15); UpdateUiController selects GRI.GameClass.default.UIControllerClass:
    // TnVersusGame / TnMultiplayerGame -> TnUIControllerMultiplayer (StartingState WaitingOnGameStart).
    // The match's PlayerReplicationInfo is new: no character is selected yet, so UpdateUiController initializes the
    // controller with UseInGameLobby = !PRI.HasSelectedCharacter() = true [CONFIRMED script]. WaitingOnGameStart then
    // ignores OnBeginGame (Choose Character stays up until a character is chosen) and the UI enters InGame when the
    // player's pawn spawns (OnRespawn, event 5).
    selected_ = SelectedCharacter{};
    matchHasBegun_ = false;
    setUIController(UIControllerClass::Multiplayer, !selected_.valid);
    ui_.setGRIAvailable(true);
    // PlayerController.CanCloseLoadingMovie: the world is fully loaded before gameplay [HIGH].
    closeLoadingMovie();
}

void GameFlow::matchLoadFailed(const std::string& why) {
    pendingMatch_ = false;
    FlowTrace::emit("match.loadFailed", {{"why", why}});
    travel(kFrontEndMap, false);
}

void GameFlow::showMenu() {
    // Xe-TransInput.ini: Escape / XboxTypeS_Start = "|onrelease showmenu" -> TnPlayerController.ShowMenu ->
    // UIController.OnUIEvent(6) [CONFIRMED].
    if (level_ != LevelKind::Match) return;
    ui_.onUIEvent((int)UIEvent::Pause);
}

void GameFlow::uiClosedItself() { ui_.onCurrentUIClosed(); }

int GameFlow::settingIndex(const GameSettings* gs, const std::string& field) const {
    if (!gs) return -1;
    const SettingField* f = gs->field(field);
    if (!f) return -1;
    auto c = settingValues_.find(gs->className);
    if (c != settingValues_.end()) { auto v = c->second.find(field); if (v != c->second.end()) return v->second; }
    return f->defaultIndex;
}

bool GameFlow::setSettingValue(const std::string& field, const std::string& valueName) {
    if (!currentSettings_) return false;
    const SettingField* f = currentSettings_->field(field);
    if (!f) return false;
    for (size_t i = 0; i < f->values.size(); ++i)
        if (f->values[i] == valueName) {
            settingValues_[currentSettings_->className][field] = (int)i;
            FlowTrace::emit("settings.write", {{"settings", currentSettings_->className}, {"field", field}, {"value", valueName}});
            return true;
        }
    return false;
}

std::string GameFlow::stateSummary() const {
    char b[512];
    const MapInfo* mi = cat_ ? cat_->mapById(lobby_.mapId) : nullptr;
    std::snprintf(b, sizeof b, "level=%s ui=%s/%s movie=%s loading=%s mode=%s map=%s countdown=%d",
                  levelKindName(level_), uiControllerClassName(ui_.cls()), uiStateName(ui_.state()),
                  ui_.openMovie().empty() ? (openMovies_.empty() ? "-" : openMovies_.back().c_str()) : ui_.openMovie().c_str(),
                  loading_.active ? loading_.kind.c_str() : "no", lobby_.gameModeTag.empty() ? "-" : lobby_.gameModeTag.c_str(),
                  mi ? mi->friendlyName.c_str() : "-", lobby_.countdown);
    return b;
}

void GameFlow::traceSnapshot(const char* why) const {
    FlowTrace::emit("snapshot", {{"why", why}, {"level", levelKindName(level_)}, {"map", levelUrl_.map()},
                                 {"uiController", uiControllerClassName(ui_.cls())}, {"uiState", uiStateName(ui_.state())},
                                 {"openMovie", ui_.openMovie()}, {"loading", FlowTrace::boolean(loading_.active)},
                                 {"mode", lobby_.gameModeTag}, {"mapId", std::to_string(lobby_.mapId)},
                                 {"countdown", std::to_string(lobby_.countdown)}, {"team", std::to_string(lobby_.localTeam)},
                                 {"hud", FlowTrace::boolean(ui_.hudVisible())}});
}

} // namespace frontend

namespace frontend {
std::string BridgeValue::str() const {
    switch (kind) {
    case Kind::Bool: return b ? "true" : "false";
    case Kind::Number: { char buf[32]; std::snprintf(buf, sizeof buf, "%g", n); return buf; }
    case Kind::String: return s;
    default: return "";
    }
}
} // namespace frontend
