#include "frontend/UIController.h"
#include "core/Log.h"

namespace frontend {

const char* uiEventName(int code) {
    static const char* n[] = {"OnFrontEnd", "OnResetFrontEnd", "OnEnterLobby", "OnBeginGame", "OnSpectating", "OnRespawn",
                              "OnPause", "OnClearPause", "OnRoundEnd", "OnEndGame", "OnPlayAgain", "OnGotoMainMenu",
                              "OnShowHud(false)", "OnShowHud(true)", "OnCampaignGameOver", "OnGRIAvailable",
                              "OnGameInviteAccepted", "OnJoinGameInviteFailed", "OnJoinInviteFailedSecondary",
                              "OnJoinInviteFailedPrimary"};
    return code >= 0 && code < (int)(sizeof(n) / sizeof(n[0])) ? n[code] : "?";
}

const char* uiStateName(UIState s) {
    switch (s) {
    case UIState::NotInGame: return "NotInGame";
    case UIState::FrontEnd: return "FrontEnd";
    case UIState::InLobby: return "InLobby";
    case UIState::WaitingOnGameStart: return "WaitingOnGameStart";
    case UIState::InGame: return "InGame";
    case UIState::Spectating: return "Spectating";
    case UIState::Paused: return "Paused";
    case UIState::PausedSpectating: return "PausedSpectating";
    case UIState::CampaignGameOver: return "CampaignGameOver";
    case UIState::WaitingOnNextRound: return "WaitingOnNextRound";
    case UIState::GameEnded: return "GameEnded";
    }
    return "?";
}

const char* uiControllerClassName(UIControllerClass c) {
    switch (c) {
    case UIControllerClass::Base: return "TnUIController";
    case UIControllerClass::FrontEnd: return "TnUIControllerFrontEnd";
    case UIControllerClass::PartyLobby: return "TnUIControllerPartyLobby";
    case UIControllerClass::GameLobby: return "TnUIControllerGameLobby";
    case UIControllerClass::Multiplayer: return "TnUIControllerMultiplayer";
    case UIControllerClass::Coop: return "TnUIControllerCoop";
    case UIControllerClass::Survival: return "TnUIControllerSurvival";
    }
    return "?";
}

std::string UIController::frontEndUI() const { return cls_ == UIControllerClass::FrontEnd ? "UI_GFxFrontEnd_p.FrontEnd_GFX_1" : ""; }
std::string UIController::lobbyUI() const {
    if (cls_ == UIControllerClass::PartyLobby) return "UI_GFxLobbies_p.PartyLobby_GFX_1";
    if (cls_ == UIControllerClass::GameLobby) return "UI_GFxLobbies_p.GameLobby_GFX_1";
    return "";
}
// TnUIControllerCoop (campaign): InGameLobbyUI = CharacterSelect_GFX (the chapter's iconic trio), no pre-game countdown screen,
// the pause menu, EndGameStats (CCP after a mission), CampaignGameOverUI = CampaignGameOver_GFX [RE CAMPAIGN_RUNTIME_EVENTS_RE].
std::string UIController::inGameLobbyUI() const {
    if (cls_ == UIControllerClass::Coop || cls_ == UIControllerClass::Survival) return "UI_GFxCharacterSelect_p.CharacterSelect_GFX_1";
    return cls_ == UIControllerClass::Multiplayer ? "UI_GFxCustomize_p.CustomTransformers_GFX_1" : "";
}
std::string UIController::campaignGameOverUI() const { return cls_ == UIControllerClass::Coop ? "UI_GFxGameOver_p.CampaignGameOver_GFX_1" : ""; }
std::string UIController::gameStartUI() const { return mp() ? "UI_GFxPreGameCountdown_p.PreGameCountdown_GFX_1" : ""; }
std::string UIController::pauseUI() const { return mp() || cls_ == UIControllerClass::Coop ? "UI_GFxPause_p.PauseMenu_GFX_1" : ""; }
std::string UIController::spectatingUI() const { return mp() ? "UI_GFxRespawn_p.MultiplayerRespawn_GFX_1" : ""; }
std::string UIController::endGameUI() const {
    return mp() || cls_ == UIControllerClass::Coop ? "UI_GFxEndGameStats_p.EndGameStats_GFX_1" : "";
}

void UIController::initialize(UIControllerClass cls, OpenFn open, CloseFn close, HudFn hud, StateFn onState, bool inGameLobby) {
    cls_ = cls; open_ = std::move(open); close_ = std::move(close); hud_ = std::move(hud); onState_ = std::move(onState);
    inGameLobby_ = inGameLobby;
    hudVisible_ = hideHud_ = griAvailable_ = false;
    state_ = UIState::NotInGame;
    openMovie_.clear();
    LOG_INFO("FLOW uicontroller Initialize %s", uiControllerClassName(cls));
    // StartingState: NotInGame (base); InLobby (party/game lobby); WaitingOnGameStart (multiplayer).
    if (cls == UIControllerClass::PartyLobby || cls == UIControllerClass::GameLobby) gotoState(UIState::InLobby);
    else if ((cls == UIControllerClass::Multiplayer || cls == UIControllerClass::Survival) || cls == UIControllerClass::Coop) gotoState(UIState::WaitingOnGameStart);
}

void UIController::openUI(const std::string& movie) {
    if (movie.empty()) return;
    if (!openMovie_.empty()) closeCurrentUI();
    openMovie_ = movie;
    LOG_INFO("FLOW ui OpenUI %s", movie.c_str());
    if (open_) open_(movie);
}

void UIController::closeCurrentUI() {
    if (openMovie_.empty()) return;
    LOG_INFO("FLOW ui CloseUI %s", openMovie_.c_str());
    openMovie_.clear();
    if (close_) close_();
}

void UIController::setHudVisible(bool v) {
    hudVisible_ = v;
    if (hud_) hud_(v);
}

void UIController::setGRIAvailable(bool v) {
    bool was = griAvailable_;
    griAvailable_ = v;
    // WaitingOnGameStart: once the GRI exists, OpenUI(GameStartUI) or InGameLobbyUI (TnUIControllerMultiplayer
    // overrides OpenGameStartUI -> InGameLobbyUI).
    if (!was && v && state_ == UIState::WaitingOnGameStart)
        openUI(cls_ == UIControllerClass::Multiplayer || inGameLobby_ ? inGameLobbyUI() : gameStartUI());
}

void UIController::gotoState(UIState s) {
    UIState from = state_;
    // EndState of the state being left [CONFIRMED TnUIController script]: the states that own a screen close it;
    // InGame / Spectating hide the HUD; Paused (unless entering a paused child state) closes the pause UI and unpauses.
    bool pausedTo = s == UIState::Paused || s == UIState::PausedSpectating;
    switch (from) {
    case UIState::FrontEnd: case UIState::InLobby: case UIState::WaitingOnGameStart: case UIState::WaitingOnNextRound:
    case UIState::GameEnded:
        closeCurrentUI();
        break;
    case UIState::InGame: setHudVisible(false); break;
    case UIState::Spectating: setHudVisible(false); closeCurrentUI(); break;
    case UIState::CampaignGameOver: closeCurrentUI(); break;
    case UIState::Paused: case UIState::PausedSpectating:
        if (!pausedTo) { closeCurrentUI(); LOG_INFO("FLOW ui SetPause(false)"); }
        break;
    default: break;
    }
    state_ = s;
    LOG_INFO("FLOW uistate %s -> %s (%s)", uiStateName(from), uiStateName(s), uiControllerClassName(cls_));
    switch (s) {
    case UIState::FrontEnd: openUI(frontEndUI()); break;
    case UIState::InLobby: openUI(lobbyUI()); break;
    case UIState::WaitingOnGameStart:
        if (griAvailable_) openUI(cls_ == UIControllerClass::Multiplayer || inGameLobby_ ? inGameLobbyUI() : gameStartUI());
        break;
    case UIState::InGame:
        if (!hideHud_) setHudVisible(true);
        break;
    case UIState::Spectating: if (!hideHud_) setHudVisible(true); openUI(spectatingUI()); break;
    case UIState::Paused: case UIState::PausedSpectating:
        LOG_INFO("FLOW ui SetPause(true)");   // TnGame.bPauseable = false: the MP world keeps running [HIGH]
        openUI(pauseUI());
        break;
    case UIState::WaitingOnNextRound: openUI(gameStartUI()); break;
    case UIState::GameEnded: setHudVisible(false); openUI(endGameUI()); break;
    case UIState::CampaignGameOver: setHudVisible(false); openUI(campaignGameOverUI()); break;
    case UIState::NotInGame: closeCurrentUI(); break;
    }
    if (onState_) onState_(from, s);
}

void UIController::onUIEvent(int code) {
    LOG_INFO("FLOW uievent %d %s in %s", code, uiEventName(code), uiStateName(state_));
    // TnUIControllerCoop: OnCampaignGameOver (14) from any in-match state -> CampaignGameOver; a restart's respawn (5) leaves
    // it for InGame; a mission's end (9) for GameEnded.
    if (code == 14 && cls_ == UIControllerClass::Coop && state_ != UIState::CampaignGameOver &&
        (state_ == UIState::InGame || state_ == UIState::Spectating || state_ == UIState::Paused || state_ == UIState::PausedSpectating ||
         state_ == UIState::WaitingOnGameStart)) { gotoState(UIState::CampaignGameOver); return; }
    if (state_ == UIState::CampaignGameOver) {
        if (code == 5) gotoState(UIState::InGame);
        else if (code == 9) gotoState(UIState::GameEnded);
        else if (code == 11) gotoState(UIState::NotInGame);
        return;
    }
    if (code == 12) { hideHud_ = true; setHudVisible(false); return; }
    if (code == 13) { hideHud_ = false; setHudVisible(true); return; }
    switch (state_) {
    case UIState::NotInGame:
        if (code == 0) gotoState(UIState::FrontEnd);
        break;
    case UIState::FrontEnd:
        if (code == 1) { closeCurrentUI(); openUI(frontEndUI()); }
        else if (code == 2) gotoState(UIState::InLobby);
        break;
    case UIState::InLobby:
        break;
    case UIState::WaitingOnGameStart:
        if (code == 3 && !inGameLobby_) gotoState(UIState::InGame);
        else if (code == 4) gotoState(UIState::Spectating);
        else if (code == 5) gotoState(UIState::InGame);
        else if (code == 0) gotoState(UIState::FrontEnd);
        break;
    case UIState::InGame:
        if (code == 6) gotoState(UIState::Paused);
        else if (code == 8) gotoState(UIState::WaitingOnNextRound);
        else if (code == 9) gotoState(UIState::GameEnded);
        else if (code == 4) gotoState(UIState::Spectating);
        else if (code == 0) gotoState(UIState::FrontEnd);
        break;
    case UIState::Spectating:
        if (code == 5) gotoState(UIState::InGame);
        else if (code == 9) gotoState(UIState::GameEnded);
        else if (code == 6) gotoState(UIState::PausedSpectating);
        break;
    case UIState::Paused: case UIState::PausedSpectating:
        // A respawn (5) or a death (4) while the pause menu is up changes the state behind it, so Resume returns to the
        // right one (user playtest 2026-10-23 #2: dead -> pause -> respawned -> resume reopened the respawn timer over live
        // play, and the respawn event was gone). The pause screen itself stays.
        if (code == 5 && state_ == UIState::PausedSpectating) { state_ = UIState::Paused; LOG_INFO("FLOW uistate PausedSpectating -> Paused (respawned)"); if (onState_) onState_(UIState::PausedSpectating, state_); }
        else if (code == 4 && state_ == UIState::Paused) { state_ = UIState::PausedSpectating; LOG_INFO("FLOW uistate Paused -> PausedSpectating (died)"); if (onState_) onState_(UIState::Paused, state_); }
        else if (code == 9) gotoState(UIState::GameEnded);   // the match ended under the pause menu
        else if (code == 11) gotoState(UIState::NotInGame);
        else if (code == 7) gotoState(state_ == UIState::Paused ? UIState::InGame : UIState::Spectating);
        // [RECONSTRUCTION, playtest 2026-10-13 P0 #4] The MP world keeps running under the pause menu (bPauseable false), so the
        // pawn can spawn or die while it is open. OnRespawn / OnSpectating were dropped here: a respawn under the menu (e.g.
        // Change Character while waiting to respawn) resumed into Spectating - a live pawn with input blanked. They now switch
        // the paused child state in place (the open menu stays; resume then reaches InGame / Spectating).
        else if ((code == 5 && state_ == UIState::PausedSpectating) || (code == 4 && state_ == UIState::Paused)) {
            const UIState from = state_;
            state_ = code == 5 ? UIState::Paused : UIState::PausedSpectating;
            LOG_INFO("FLOW uistate %s -> %s (%s) [under the pause menu]", uiStateName(from), uiStateName(state_), uiControllerClassName(cls_));
            if (onState_) onState_(from, state_);
        }
        break;
    case UIState::WaitingOnNextRound:
        if (code == 3) gotoState(UIState::InGame);
        break;
    case UIState::GameEnded:
        if (code == 10) gotoState(UIState::WaitingOnGameStart);
        break;
    }
}

void UIController::onCharacterSelected(bool matchHasBegun) {
    // TnUIControllerMultiplayer: WaitingOnGameStart.OnCharacterSelected -> if !GRI.bMatchHasBegun: CloseCurrentUI(),
    // OpenUI(GameStartUI); global OnCharacterSelected -> notification "CharacterSelectedInGame".
    if (state_ == UIState::WaitingOnGameStart && !matchHasBegun) { closeCurrentUI(); openUI(gameStartUI()); }
    else LOG_INFO("FLOW ui notification CharacterSelectedInGame");
}

void UIController::onCurrentUIClosed() {
    // The open UI closed itself (e.g. pause Resume): Paused.NotifyUIClosed -> CheckCloseCurrentUI(Movie) closes it,
    // then GotoState('InGame') [CONFIRMED script]. The movie must leave the open set, or it stays drawn (with focus)
    // over gameplay that already receives input (stress harness m*.resumed).
    closeCurrentUI();
    if (state_ == UIState::Paused) gotoState(UIState::InGame);
    else if (state_ == UIState::PausedSpectating) gotoState(UIState::Spectating);
}

} // namespace frontend
