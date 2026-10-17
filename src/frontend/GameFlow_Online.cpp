// PC EXTENSION - the online flows of GameFlow (docs/handoff/ONLINE_MP_FLOWS.md, ONLINE_MP_FRONTEND_TESTS.md). Only active
// with a session set (GameFlow::setSession); without one GameFlow is today's offline frontend. The session layer behind
// ISession (Session.h) is Systems'; tests drive these flows with FakeSession. Popup texts are the original's INT texts
// where one exists (ONLINE_MP_UI_STRINGS.md), else the rebuild's own ($WfcPC.Online).
#include "frontend/GameFlow.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

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
    if (level_ == LevelKind::GameLobby && role_ != SessionRole::Offline && !lobby_.countingDown) lobby_.lobbyStatus = onlineGateStatus();
    if (role_ == SessionRole::Host) applyBrowserCap("tick");
    if (role_ == SessionRole::Host && level_ == LevelKind::GameLobby) {   // the host's lobby to every member, on change
        const SessionLobby l = hostLobbySnapshot();
        std::string key = l.gameModeTag + "|" + std::to_string(l.mapId) + "|" + std::to_string(l.mapSelectionMethod) + "|" +
                          std::to_string(l.playerLimit) + "|" + (l.countingDown ? "1" : "0") + "|" + std::to_string(l.countdown);
        for (const auto& [k, v] : l.settings) key += "|" + k + "=" + v;
        for (size_t i = 0; i < l.slots.size(); ++i)
            key += "|" + l.slots[i].member.fingerprint + ":" + std::to_string(l.slots[i].team) + (l.slots[i].connecting ? "c" : "") +
                   (i < l.members.size() && l.members[i].hasCharacter ? "h" : "");
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
    // the members (the host first) and the human slots; bots fill the rest per team (onlineBotsFor)
    SessionMemberInfo self;
    self.displayName = profile_.playerName();   // D1: the account name
    self.hasCharacter = true;
    l.members.push_back(self);
    SessionSlot hs; hs.kind = SessionSlot::Kind::Human; hs.team = lobby_.localTeam >= 0 ? lobby_.localTeam : 0;
    l.slots.push_back(hs);
    for (const OnlineMember& m : onlineMembers_) {
        l.members.push_back(m.info);
        SessionSlot sl; sl.kind = SessionSlot::Kind::Human; sl.member = m.info.id; sl.team = m.team; sl.connecting = m.connecting;
        l.slots.push_back(sl);
    }
    return l;
}

int GameFlow::pickOnlineTeam() {
    // TnTeamHandlerTwoTeams.PickTeam: the smaller team, random on a tie (Autobots 0, Decepticons 1) [RE MP addendum 3].
    int n[2] = {0, 0};
    n[lobby_.localTeam == 1 ? 1 : 0]++;
    for (const OnlineMember& m : onlineMembers_) if (m.team == 0 || m.team == 1) n[m.team]++;
    if (n[0] != n[1]) return n[0] < n[1] ? 0 : 1;
    return std::uniform_int_distribution<int>(0, 1)(rng_);
}

int GameFlow::onlineBotsFor(int team) const {
    const int want = team == 0 ? profile_.bots.autobot : profile_.bots.decepticon;
    const int side = profile_.bots.extended ? gpPerTeam_ : 5;   // players per side incl. humans
    int humans = (role_ == SessionRole::Member ? (selfTeam() == team ? 1 : 0) : ((lobby_.localTeam >= 0 ? lobby_.localTeam : 0) == team ? 1 : 0));
    for (const OnlineMember& m : onlineMembers_) humans += m.team == team ? 1 : 0;
    return std::max(0, std::min(want, side - humans));
}

int GameFlow::onlineGateStatus() const {
    // The original lobby gate (D4): every human has a character, then balanced teams, then the host starts: LobbyStatus 6
    // WaitingForCharacterChoice / 4 WaitingForBalancedTeams / 3 WaitingForHostToStart. Teams must be balanced only with Team
    // Balancing "Manual": "Autobalanced" (the default) balances them itself at the final countdown (AssignTeams).
    for (const OnlineMember& m : onlineMembers_) if (!m.info.hasCharacter || m.connecting) return 6;
    bool manual = false;     // AutobalanceTeams: 0 Manual, 1 Autobalanced (the default)
    if (const GameSettings* gs = lobby_.settings) manual = settingIndex(gs, "AutobalanceTeams") == 0;
    if (manual && botRows() == BotRows::Teams && !onlineMembers_.empty()) {
        int n[2] = {0, 0};
        n[role_ == SessionRole::Member ? (selfTeam() == 1 ? 1 : 0) : (lobby_.localTeam == 1 ? 1 : 0)]++;
        for (const OnlineMember& m : onlineMembers_) if (m.team == 0 || m.team == 1) n[m.team]++;
        if (std::abs(n[0] - n[1]) > 1) return 4;
    }
    return 3;
}

void GameFlow::applyBrowserCap(const char* why) {
    bool web = false;
    for (const OnlineMember& m : onlineMembers_) web = web || m.info.platform == SessionPlatform::Web;
    if (!web || !profile_.bots.extended) return;
    setBotSetting("extended", 0);   // D10: 5 v 5 while a browser peer is in (the counts clamp with it)
    FlowTrace::emit("online.browserCap", {{"why", why}, {"playerLimit", "Original (5 v 5)"}});
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

void GameFlow::applyMemberLobby() {   // the host's choices on the member's lobby (map, countdown, teams); options are display-only
    if (memberLobby_.mapId >= 0 && memberLobby_.mapId != lobby_.mapId) setMapId(memberLobby_.mapId);
    lobby_.countingDown = memberLobby_.countingDown;
    lobby_.countdown = memberLobby_.countdown;
    // the other humans (the host's list minus this player) and this player's own team from its slot
    onlineMembers_.clear();
    for (size_t i = 0; i < memberLobby_.slots.size(); ++i) {
        const SessionSlot& sl = memberLobby_.slots[i];
        if (sl.kind != SessionSlot::Kind::Human) continue;
        if (!selfId_.fingerprint.empty() && sl.member == selfId_) { lobby_.localTeam = sl.team; continue; }
        OnlineMember m;
        if (i < memberLobby_.members.size()) m.info = memberLobby_.members[i];
        m.info.id = sl.member;
        m.team = sl.team;
        m.connecting = sl.connecting;
        onlineMembers_.push_back(m);
    }
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
        selfId_ = e.member;
        enterMemberLobby(e.lobby);
        if (e.type == T::MatchInProgress) memberLobby_.matchRunning = true;
        break;
    case T::JoinFailed:
        if (onlinePending_ == OnlinePending::Joining) onlineJoinFailed(e.joinFailure);
        break;
    case T::LobbyState:
        if (role_ == SessionRole::Member) memberLobby_ = e.lobby;
        break;
    case T::MemberJoined: {   // host: a new member (connecting until it has a character), or its updated info
        if (role_ != SessionRole::Host) break;
        auto it = std::find_if(onlineMembers_.begin(), onlineMembers_.end(), [&](const OnlineMember& m) { return m.info.id == e.member; });
        if (it == onlineMembers_.end()) {
            OnlineMember m;
            m.info = e.info;
            m.info.id = e.member;
            m.team = botRows() == BotRows::Teams ? pickOnlineTeam() : 255;
            m.connecting = !e.info.hasCharacter && e.info.characterSlot.empty();
            onlineMembers_.push_back(m);
            FlowTrace::emit("online.memberJoined", {{"member", e.member.fingerprint}, {"name", e.info.displayName}, {"team", std::to_string(m.team)},
                                                    {"platform", e.info.platform == SessionPlatform::Web ? "Web" : "PC"}});
            applyBrowserCap("member joined");
        } else {
            const int team = it->team;
            it->info = e.info;
            it->info.id = e.member;
            it->connecting = false;
            if (e.info.teamRequest == 0 || e.info.teamRequest == 1) it->team = e.info.teamRequest;   // Change Team (the host decides: allowed)
            else it->team = team;
        }
        break;
    }
    case T::MemberLeft: {
        if (role_ != SessionRole::Host) break;
        onlineMembers_.erase(std::remove_if(onlineMembers_.begin(), onlineMembers_.end(), [&](const OnlineMember& m) { return m.info.id == e.member; }),
                             onlineMembers_.end());
        FlowTrace::emit("online.memberLeft", {{"member", e.member.fingerprint}});
        break;
    }
    default: break;   // the later flow groups (start / load gate / leave / session end / voice)
    }
}

} // namespace frontend
