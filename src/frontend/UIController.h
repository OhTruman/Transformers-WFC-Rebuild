// Clean-room reconstruction — TransGame.TnUIController (StateFullObject) and its subclasses.
// One UI controller exists per PlayerController; travel replaces it. Engine/game code drives it only through
// OnUIEvent(code); each state opens its movie with OpenUI. Source: RE MILESTONE05_FRONTEND_MATCH_BOOTSTRAP.md 1.4
// [CONFIRMED script + authored defaults]. The movie names below are the authored GFxMovie objects.
#pragma once
#include <functional>
#include <string>

namespace frontend {

enum class UIEvent : int {
    FrontEnd = 0, ResetFrontEnd = 1, EnterLobby = 2, BeginGame = 3, Spectating = 4, Respawn = 5, Pause = 6,
    ClearPause = 7, RoundEnd = 8, EndGame = 9, PlayAgain = 10, GotoMainMenu = 11, ShowHudFalse = 12,
    ShowHudTrue = 13, CampaignGameOver = 14, GRIAvailable = 15, GameInviteAccepted = 16, JoinInviteFailed = 17,
};
const char* uiEventName(int code);

enum class UIState {
    NotInGame, FrontEnd, InLobby, WaitingOnGameStart, InGame, Spectating, Paused, PausedSpectating,
    WaitingOnNextRound, GameEnded,
};
const char* uiStateName(UIState s);

enum class UIControllerClass { Base, FrontEnd, PartyLobby, GameLobby, Multiplayer };
const char* uiControllerClassName(UIControllerClass c);

class UIController {
public:
    // OpenUI / CloseUI delegates (TnUIController.Initialize(OpenUIDelegate, CloseUIDelegate, ...)).
    using OpenFn = std::function<void(const std::string& movie)>;
    using CloseFn = std::function<void()>;
    using HudFn = std::function<void(bool visible)>;
    using StateFn = std::function<void(UIState from, UIState to)>;

    void initialize(UIControllerClass cls, OpenFn open, CloseFn close, HudFn hud, StateFn onState, bool inGameLobby = false);
    void onUIEvent(int code);
    // TnUIControllerMultiplayer.WaitingOnGameStart.OnCharacterSelected / global (notification).
    void onCharacterSelected(bool matchHasBegun);
    // The open UI closed itself (e.g. Pause menu "Resume"): Paused -> InGame.
    void onCurrentUIClosed();
    // GRI became available (WaitingOnGameStart opens GameStartUI / InGameLobbyUI only once it exists).
    void setGRIAvailable(bool v);

    UIState state() const { return state_; }
    UIControllerClass cls() const { return cls_; }
    const std::string& openMovie() const { return openMovie_; }
    bool hudVisible() const { return hudVisible_; }

    // Authored movie defaults per subclass (RE 1.4 table).
    std::string frontEndUI() const, lobbyUI() const, inGameLobbyUI() const, gameStartUI() const, pauseUI() const,
        spectatingUI() const, endGameUI() const;
    static const char* messageBoxUI() { return "UI_GFxShared_p.MessagePrompt_GFX_1"; }

private:
    void gotoState(UIState s);
    void openUI(const std::string& movie);
    void closeCurrentUI();
    void setHudVisible(bool v);

    UIControllerClass cls_ = UIControllerClass::Base;
    UIState state_ = UIState::NotInGame;
    OpenFn open_; CloseFn close_; HudFn hud_; StateFn onState_;
    std::string openMovie_;
    bool inGameLobby_ = false, griAvailable_ = false, hudVisible_ = false, hideHud_ = false;
};

} // namespace frontend
