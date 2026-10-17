// PC EXTENSION - the online flows of GameFlow (docs/handoff/ONLINE_MP_FLOWS.md, ONLINE_MP_FRONTEND_TESTS.md). Only active
// with a session set (GameFlow::setSession); without one GameFlow is today's offline frontend. The session layer behind
// ISession (Session.h) is Systems'; tests drive these flows with FakeSession. Popup texts are the original's INT texts
// where one exists (ONLINE_MP_UI_STRINGS.md), else the rebuild's own ($WfcPC.Online).
#include "frontend/GameFlow.h"

#include <cctype>

#include "frontend/FlowTrace.h"

namespace frontend {

std::string GameFlow::txt(const char* dollarKey) const { return cat_ ? cat_->localizeKey(dollarKey) : std::string(); }

void GameFlow::onlineTick() {
    for (const SessionEvent& e : session_->poll(clock_)) onSessionEvent(e);
    // Join timeout: the session layer sends JoinFailed{Timeout} at the original InitialConnectTimeout (50 s); this is the
    // frontend's backstop should it never answer.
    if (onlinePending_ == OnlinePending::Joining && clock_ - joinStartedAt_ >= kJoinBackstop) {
        session_->cancelJoin();
        onlineJoinFailed(SessionJoinFailure::Timeout);
    }
    if (role_ == SessionRole::Host && level_ == LevelKind::GameLobby) {   // the host's lobby to every member, on change
        const SessionLobby l = hostLobbySnapshot();
        std::string key = l.gameModeTag + "|" + std::to_string(l.mapId) + "|" + std::to_string(l.mapSelectionMethod) + "|" +
                          std::to_string(l.playerLimit) + "|" + (l.countingDown ? "1" : "0") + "|" + std::to_string(l.countdown);
        for (const auto& [k, v] : l.settings) key += "|" + k + "=" + v;
        if (key != publishedLobbyKey_) { publishedLobbyKey_ = key; session_->publishLobby(l); }
    }
    if (role_ == SessionRole::Member && level_ == LevelKind::GameLobby) applyMemberLobby();
}

SessionLobby GameFlow::hostLobbySnapshot() const {
    SessionLobby l;
    l.gameModeTag = lobby_.gameModeTag;
    l.mapId = lobby_.mapId;
    l.mapSelectionMethod = lobby_.mapSelectionMethod;
    l.playerLimit = profile_.bots.extended ? 1 : 0;
    l.countingDown = lobby_.countingDown;
    l.countdown = lobby_.countdown;
    if (const GameSettings* gs = lobby_.settings)
        for (const SettingField& f : gs->fields) {
            const int i = settingIndex(gs, f.name);
            if (i >= 0 && i < (int)f.values.size()) l.settings[f.name] = f.values[(size_t)i];
        }
    for (const BotOption& o : botOptions()) l.settings[o.name] = botChoiceLabel(o.field, botValue(o.field));
    return l;
}

bool GameFlow::onlineHost() {
    if (!currentSettings_) return false;
    SessionHostSettings hs;
    hs.gameModeTag = currentSettings_->tag;
    hs.mapSelectionMethod = currentSettings_->mapSelectionMethod;
    hs.isPrivate = true;   // phase 1: friends / private sessions only (no XP, decision D8)
    for (const SettingField& f : currentSettings_->fields) {
        const int i = settingIndex(currentSettings_, f.name);
        if (i >= 0 && i < (int)f.values.size()) hs.settings[f.name] = f.values[(size_t)i];
    }
    for (const BotOption& o : botOptions()) hs.settings[o.name] = botChoiceLabel(o.field, botValue(o.field));
    onlinePending_ = OnlinePending::Hosting;
    session_->host(hs);
    Popup p;   // "Hosting Game" / "Creating lobby..." (original texts), no buttons while the session opens
    p.title = txt("$UIText.MessagePrompts.HostGameTitle");
    p.message = txt("$UIText.MessagePrompts.HostingMessage");
    p.icon = 1;
    p.showDefaultButtons = false;
    showPopup(p);
    FlowTrace::emit("online.host", {{"mode", hs.gameModeTag}, {"state", "opening"}});
    return true;
}

namespace {
bool isJoinCode(const std::string& t) {
    // The shape only: "WFC-" + base32 groups separated by '-' (Systems' codes: "WFC-7QK2-M9TD-4H", relay "WFC-R-...").
    // Decoding and the check symbol are the session layer's (JoinFailed{Invalid}).
    if (t.size() < 6 || t.size() > 64) return false;
    for (size_t i = 0; i < 4; ++i) if ((char)std::toupper((unsigned char)t[i]) != "WFC-"[i]) return false;
    bool groupChar = false;
    for (size_t i = 4; i < t.size(); ++i) {
        if (t[i] == '-') { if (!groupChar) return false; groupChar = false; continue; }
        if (!std::isalnum((unsigned char)t[i])) return false;
        groupChar = true;
    }
    return groupChar;
}
bool isAddress(const std::string& t) {    // host:port or [v6]:port, port 1..65535
    const size_t colon = t.rfind(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 >= t.size()) return false;
    const std::string port = t.substr(colon + 1);
    for (char c : port) if (!std::isdigit((unsigned char)c)) return false;
    if (port.size() > 5) return false;
    const long p = std::atol(port.c_str());
    if (p < 1 || p > 65535) return false;
    for (char c : t.substr(0, colon)) if (std::isspace((unsigned char)c)) return false;
    return true;
}
}

BridgeValue GameFlow::onlineJoin(const std::string& targetIn) {
    if (!session_) { FlowTrace::emit("bridge.unhandled", {{"fn", "Online.JoinGame"}, {"why", "offline"}}); return false; }
    std::string target = targetIn;
    while (!target.empty() && std::isspace((unsigned char)target.back())) target.pop_back();
    while (!target.empty() && std::isspace((unsigned char)target.front())) target.erase(target.begin());
    if (!isJoinCode(target) && !isAddress(target)) {
        showMessage(txt("$UIText.MessagePrompts.GenericErrorTitle"), txt("$WfcPC.Online.JoinInvalid"));
        FlowTrace::emit("online.join", {{"target", target}, {"result", "invalid"}});
        return false;
    }
    onlinePending_ = OnlinePending::Joining;
    joinStartedAt_ = clock_;
    session_->join(target);
    Popup p;   // "Joining..." / "Joining game..." with Cancel (original texts)
    p.title = txt("$UIText.MessagePrompts.JoinFriendGameTitle");
    p.message = txt("$UIText.MessagePrompts.JoiningMessage");
    p.icon = 1;
    p.showDefaultButtons = false;
    p.b = PopupButton{txt("$UIText.MessagePrompts.GenericCancelButton"), "cancelJoin", true};
    showPopup(p);
    FlowTrace::emit("online.join", {{"target", target}, {"result", "joining"}});
    return true;
}

void GameFlow::onlineJoinFailed(SessionJoinFailure why) {
    onlinePending_ = OnlinePending::None;
    closePopup();
    if (why == SessionJoinFailure::Invalid) {   // the code failed the session layer's decode: the entry popup's own text
        showMessage(txt("$UIText.MessagePrompts.GenericErrorTitle"), txt("$WfcPC.Online.JoinInvalid"));
        FlowTrace::emit("online.join", {{"result", "failed"}, {"why", "invalid code"}});
        return;
    }
    const char* title = why == SessionJoinFailure::MapMissing ? "$UIText.MessagePrompts.LobbyInvalidMapTitle"
                                                              : "$UIText.MessagePrompts.JoinGameInviteFailedTitle";
    const char* msg = why == SessionJoinFailure::Version      ? "$Engine.Errors.ClientOutdated"
                      : why == SessionJoinFailure::MapMissing ? "$UIText.MessagePrompts.LobbyInvalidMapMessage"
                                                              : "$UIText.MessagePrompts.JoinGameInviteFailedMessage";
    showMessage(txt(title), txt(msg));
    static const char* const kWhy[] = {"full", "timeout", "version", "map", "invalid"};
    FlowTrace::emit("online.join", {{"result", "failed"}, {"why", kWhy[(int)why]}});
}

void GameFlow::enterMemberLobby(const SessionLobby& lobby) {
    onlinePending_ = OnlinePending::None;
    closePopup();
    role_ = SessionRole::Member;
    memberLobby_ = lobby;
    const GameSettings* gs = settingsByConfigName(lobby.gameModeTag, true);
    if (!gs) gs = cat_->settingsForTag(lobby.gameModeTag, true);
    if (!gs) { onlineJoinFailed(SessionJoinFailure::Version); role_ = SessionRole::Offline; return; }
    currentSettings_ = gs;
    lobby_.playlistId = -1;
    std::string url = buildLobbyUrl(*gs);
    if (lobby.mapId >= 0) url += "?MapId=" + std::to_string(lobby.mapId);
    FlowTrace::emit("online.join", {{"result", "joined"}, {"mode", lobby.gameModeTag}, {"mapId", std::to_string(lobby.mapId)},
                                    {"matchRunning", FlowTrace::boolean(lobby.matchRunning)}});
    travel(url, false);
}

void GameFlow::applyMemberLobby() {   // the host's choices on the member's lobby (map, countdown); options are display-only
    if (memberLobby_.mapId >= 0 && memberLobby_.mapId != lobby_.mapId) setMapId(memberLobby_.mapId);
    lobby_.countingDown = memberLobby_.countingDown;
    lobby_.countdown = memberLobby_.countdown;
}

void GameFlow::onSessionEvent(const SessionEvent& e) {
    FlowTrace::emit("online.event", {{"type", sessionEventName(e.type)}, {"role", sessionRoleName(role_)}});
    using T = SessionEvent::Type;
    switch (e.type) {
    case T::HostOpened:
        if (onlinePending_ != OnlinePending::Hosting) break;
        onlinePending_ = OnlinePending::None;
        closePopup();
        role_ = SessionRole::Host;
        joinCode_ = e.joinCode;
        joinAddress_ = e.address;
        publishedLobbyKey_.clear();
        FlowTrace::emit("online.host", {{"state", "open"}, {"code", joinCode_}, {"address", joinAddress_}});
        hostOnlineGame(currentSettings_);
        break;
    case T::HostFailed:
        if (onlinePending_ != OnlinePending::Hosting) break;
        onlinePending_ = OnlinePending::None;
        closePopup();
        showMessage(txt("$UIText.MessagePrompts.StrictNATTitle"),
                    txt(e.hostFailure == SessionHostFailure::StrictNat ? "$UIText.MessagePrompts.StrictNATMessage" : "$WfcPC.Online.HostNoPath"));
        FlowTrace::emit("online.host", {{"state", "failed"}});
        break;
    case T::JoinProgress: break;   // the joining popup stays; the lobby's own connecting animation follows the join
    case T::Joined:
    case T::MatchInProgress:
        if (onlinePending_ != OnlinePending::Joining) break;
        enterMemberLobby(e.lobby);
        if (e.type == T::MatchInProgress) memberLobby_.matchRunning = true;
        break;
    case T::JoinFailed:
        if (onlinePending_ == OnlinePending::Joining) onlineJoinFailed(e.joinFailure);
        break;
    case T::LobbyState:
        if (role_ == SessionRole::Member) memberLobby_ = e.lobby;
        break;
    default: break;   // the later flow groups (start / load gate / leave / session end / voice)
    }
}

} // namespace frontend
