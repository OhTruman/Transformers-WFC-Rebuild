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

void GameFlow::endSession(const char* titleKey, const char* messageKey, const char* why) {
    const bool wasOnline = role_ != SessionRole::Offline || onlinePending_ != OnlinePending::None;
    role_ = SessionRole::Offline;
    onlinePending_ = OnlinePending::None;
    onlineMembers_.clear();
    loadGate_ = false;
    joinCode_.clear();
    joinAddress_.clear();
    pendingEndTitle_ = txt(titleKey);
    pendingEndMessage_ = txt(messageKey);
    FlowTrace::emit("online.sessionEnd", {{"why", why}, {"wasOnline", FlowTrace::boolean(wasOnline)}, {"from", levelKindName(level_)}});
    if (level_ == LevelKind::PartyLobby && !travelPending_) { showMessage(pendingEndTitle_, pendingEndMessage_); pendingEndTitle_.clear(); return; }
    returnToPartyLobby();   // the party lobby, as the original's QuitToPartyLobby (TnPlayerController.QuitGame)
}

void GameFlow::sendMemberInfo(int teamRequest) {
    SessionMemberInfo info;
    info.id = selfId_;
    info.displayName = profile_.playerName();   // D1: the account name
    info.platform = SessionPlatform::PC;
    info.characterSlot = selected_.valid ? selected_.specialty : std::string();
    info.iconicCharacter = selected_.valid && selected_.type == 1 ? selected_.name : std::string();
    // the profile's custom characters (Create a Character) are always there; iconic mode: a claimed iconic character
    info.hasCharacter = !iconicMode() || !info.iconicCharacter.empty();
    info.level = progression::playerLevel(profile_.progression);
    info.teamRequest = teamRequest;
    info.mapVote = myVote_;
    session_->setMemberInfo(info);
}

GameFlow::VoiceState GameFlow::voiceStateOf(const SessionMemberId& m) const {
    auto it = voiceStates_.find(m.fingerprint);
    return it == voiceStates_.end() ? VoiceState{} : it->second;
}

bool GameFlow::isMuted(const SessionMemberId& m) const {
    const auto& v = profile_.voice.muted;
    return !m.fingerprint.empty() && std::find(v.begin(), v.end(), m.fingerprint) != v.end();
}

bool GameFlow::toggleMute(const std::string& displayNameIn) {
    // the row's name carries the platform tag (D11): match the member by the name before it
    std::string name = displayNameIn;
    const size_t tag = name.find("  ");
    if (tag != std::string::npos) name.resize(tag);
    for (const OnlineMember& m : onlineMembers_) {
        if (m.info.displayName != name || m.info.id.fingerprint.empty()) continue;
        auto& v = profile_.voice.muted;
        auto it = std::find(v.begin(), v.end(), m.info.id.fingerprint);
        const bool mute = it == v.end();
        if (mute) v.push_back(m.info.id.fingerprint); else v.erase(it);
        profile_.save();
        if (session_) session_->setMuted(m.info.id, mute);
        FlowTrace::emit("online.mute", {{"member", m.info.id.fingerprint}, {"name", name}, {"muted", FlowTrace::boolean(mute)}});
        return true;
    }
    return false;
}

std::string GameFlow::micMeterText(float level) {
    const int n = (int)(std::clamp(level, 0.0f, 1.0f) * 10.0f + 0.5f);
    return std::string((size_t)n, '|') + std::string((size_t)(10 - n), '.');
}

void GameFlow::beginPttCapture() {
    if (pttCapture_) return;
    pttCapture_ = true;
    Popup p;
    p.title = txt("$WfcPC.Voice.PushToTalkKey");
    p.message = txt("$WfcPC.Voice.PressKeyPrompt");
    p.b = PopupButton{txt("$UIText.MessagePrompts.GenericCancelButton"), "pttCancel", true};
    p.showDefaultButtons = false;
    showPopup(p);
    FlowTrace::emit("settings.pttCapture", {{"state", "waiting"}, {"provenance", "PC EXTENSION"}});
}

void GameFlow::endPttCapture(const std::string& keyName) {
    if (!pttCapture_) return;
    pttCapture_ = false;
    if (popup_.open && popup_.b.action == "pttCancel") closePopup();
    if (!keyName.empty()) { profile_.voice.pttKey = keyName; profile_.save(); }
    FlowTrace::emit("settings.pttCapture", {{"state", keyName.empty() ? "cancelled" : "set"}, {"key", profile_.voice.pttKey}});
}

void GameFlow::setPushToTalkHeld(bool held) {
    if (pttCapture_) held = false;   // the key being chosen does not talk
    if (!session_ || role_ == SessionRole::Offline || profile_.voice.mode != 1 || held == pttHeld_) return;
    pttHeld_ = held;
    session_->setPushToTalk(held);
}

std::vector<std::string> GameFlow::claimedIconicCharacters() const {
    std::vector<std::string> out;
    if (!iconicMode()) return out;
    for (const OnlineMember& m : onlineMembers_) if (!m.info.iconicCharacter.empty()) out.push_back(m.info.iconicCharacter);
    return out;
}

const GameFlow::OnlineMember* GameFlow::memberForPlayer(const MatchValues::Player& p) const {
    for (const OnlineMember& m : onlineMembers_)
        if (p.member.empty() ? m.info.displayName == p.name : m.info.id.fingerprint == p.member) return &m;
    return nullptr;
}

void GameFlow::onlineIdleKick(const std::string& fingerprint) {
    if (!session_ || role_ != SessionRole::Host || fingerprint.empty()) return;
    SessionMemberId m;
    m.fingerprint = fingerprint;
    session_->kick(m, SessionKickReason::Idle);
    FlowTrace::emit("online.idleKick", {{"member", fingerprint}});
}

int GameFlow::localHeadsetState() const {
    // PC ADAPTATION: the original read the Live headset; ours is the Audio rows' mode. Open mic has no voice-activity
    // signal from the session yet, so it shows "voice on" (1); push-to-talk shows "talking" (2) while the key is held.
    const int mode = profile_.voice.mode;
    return mode == 0 ? 0 : (mode == 1 && pttHeld_ ? 2 : 1);
}

void GameFlow::fillOnlineLaunch() {   // the match's participants (WFC_SESSIONTEST / Gameplay's Remote participants)
    // Every world builds the match from the one launch record (sendOnlineLaunch / CountdownStarted): the slots, the names
    // and each side's own team. A later LobbyState (the host keeps publishing through the countdown) must not change it -
    // a host team picked after the record went out desynced the first two-window match (2026-10-23, status 3 at tick 660).
    const bool host = role_ == SessionRole::Host;
    const SessionLobby l = !launchLobby_.slots.empty() ? launchLobby_ : host ? hostLobbySnapshot() : memberLobby_;
    match_.onlineSlots = l.slots;
    match_.self = host ? SessionMemberId{} : selfId_;
    for (const SessionMemberInfo& m : l.members) match_.onlineNames.push_back({m.id, m.displayName});
    for (const SessionSlot& sl : l.slots)
        if (sl.kind == SessionSlot::Kind::Human && sl.member == match_.self && (sl.team == 0 || sl.team == 1)) { match_.teamIndex = sl.team; break; }
    int humans = 0;
    for (const SessionSlot& sl : match_.onlineSlots) humans += sl.kind == SessionSlot::Kind::Human;
    FlowTrace::emit("online.launchSlots", {{"slots", std::to_string(match_.onlineSlots.size())}, {"humans", std::to_string(humans)},
                                           {"self", match_.self.fingerprint}, {"team", std::to_string(match_.teamIndex)}});
}

void GameFlow::syncVoiceSettings() {   // the Audio rows to the session, whenever they change (and when the session starts)
    const LocalProfile::Voice& v = profile_.voice;
    const std::string key = std::to_string(v.mode) + "|" + std::to_string(v.audience) + "|" + v.device + "|" + std::to_string(v.volume);
    if (key == voiceSent_) return;
    voiceSent_ = key;
    SessionVoiceSettings s;
    s.mode = (SessionVoiceMode)std::clamp(v.mode, 0, 2);
    s.audience = (SessionVoiceAudience)std::clamp(v.audience, 0, 3);
    s.device = v.device;
    s.volume = v.volume;
    session_->setVoiceSettings(s);
    FlowTrace::emit("online.voiceSettings", {{"mode", std::to_string(v.mode)}, {"audience", std::to_string(v.audience)}});
}

void GameFlow::showFirstRunPrompt() {
    firstRunAsked_ = true;
    Popup p;
    p.title = txt("$WfcPC.FirstRun.Title");
    p.showDefaultButtons = false;
    if (profile_.loggedInAccount.empty()) {
        p.message = txt("$WfcPC.FirstRun.ChooseName");
        p.a = PopupButton{txt("$WfcPC.FirstRun.ChooseNameButton"), "frName", true};
        p.b = PopupButton{txt("$UIText.ButtonHints.Back"), "frBack", true};
    } else {
        const LocalProfile::Voice& v = profile_.voice;
        const std::string voice = v.mode == 0 ? txt("$WfcPC.Voice.VoiceOff")
                                : v.mode == 2 ? txt("$WfcPC.Voice.VoiceOpenMic") : txt("$WfcPC.Voice.VoicePushToTalk") + " (" + v.pttKey + ")";
        std::string m = txt("$WfcPC.FirstRun.Message");
        size_t at = m.find("`p");
        if (at != std::string::npos) m.replace(at, 2, profile_.playerName());
        at = m.find("`v");
        if (at != std::string::npos) m.replace(at, 2, voice);
        p.message = m;
        p.a = PopupButton{txt("$WfcPC.FirstRun.Continue"), "frContinue", true};
        p.x = PopupButton{txt("$WfcPC.FirstRun.ChangeName"), "frName", true};
        p.y = PopupButton{txt("$WfcPC.FirstRun.VoiceSettings"), "frVoice", true};
    }
    showPopup(p);
    FlowTrace::emit("online.firstRun", {{"state", "shown"}, {"account", FlowTrace::boolean(!profile_.loggedInAccount.empty())}});
}

void GameFlow::answerFirstRun(const std::string& action) {
    if (action == "frName") firstRunUi_ = FirstRunUi::NamePrompt;
    if (action == "frVoice") firstRunUi_ = FirstRunUi::VoiceSettings;
    if (action == "frContinue" || action == "frVoice") {   // answered: never again for this profile
        profile_.onlineFirstRunShown = true;
        profile_.save();
    }
    FlowTrace::emit("online.firstRun", {{"state", action}});
}

void GameFlow::firstRunNameEntered() { firstRunReshow_ = true; }

void GameFlow::onlineTick() {
    for (const SessionEvent& e : session_->poll(clock_)) onSessionEvent(e);
    // The one-time "Online Play" prompt: an online session exists (never offline), the party lobby is up and quiet
    if (level_ != LevelKind::PartyLobby || loading_.active) firstRunAsked_ = false;   // a new visit asks again (until answered)
    else if (!travelPending_ && !loading_.active && !popup_.open && ui_.state() == UIState::InLobby && !profile_.onlineFirstRunShown &&
             (!firstRunAsked_ || firstRunReshow_)) {
        firstRunReshow_ = false;
        showFirstRunPrompt();
    }
    if (role_ != SessionRole::Offline) syncVoiceSettings();
    else voiceSent_.clear();
    if (!lobbyServiceSent_) { session_->setLobbyService(profile_.lobbyService); lobbyServiceSent_ = true; }
    if (!pendingEndTitle_.empty() && !travelPending_ && !loading_.active) {   // the session-end message, after the travel
        showMessage(pendingEndTitle_, pendingEndMessage_);
        pendingEndTitle_.clear();
    }
    // Join timeout: the session layer sends JoinFailed{Timeout} at the original InitialConnectTimeout (50 s); this is the
    // frontend's backstop should it never answer.
    if (onlinePending_ == OnlinePending::Joining && clock_ - joinStartedAt_ >= kJoinBackstop) {
        session_->cancelJoin();
        onlineJoinFailed(SessionJoinFailure::Timeout);
    }
    if (level_ == LevelKind::GameLobby && role_ != SessionRole::Offline && !lobby_.countingDown) lobby_.lobbyStatus = onlineGateStatus();
    if (role_ == SessionRole::Host) applyBrowserCap("tick");
    if (role_ == SessionRole::Host && level_ == LevelKind::GameLobby) voteTick();   // public lobbies (4.1)
    if (role_ == SessionRole::Host && level_ == LevelKind::GameLobby) {   // the host's lobby to every member, on change
        const SessionLobby l = hostLobbySnapshot();
        std::string key = l.gameModeTag + "|" + std::to_string(l.mapId) + "|" + std::to_string(l.mapSelectionMethod) + "|" +
                          std::to_string(l.playerLimit) + "|" + (l.countingDown ? "1" : "0") + "|" + std::to_string(l.countdown);
        for (const auto& [k, v] : l.settings) key += "|" + k + "=" + v;
        key += "|v" + std::to_string(l.voteRound) + (l.voteOpen ? "o" : "c") + std::to_string(l.voteWinner);
        for (size_t i = 0; i < l.voteMaps.size(); ++i) key += ":" + std::to_string(l.voteMaps[i]);
        for (int c : l.voteCounts) key += "," + std::to_string(c);
        for (size_t i = 0; i < l.slots.size(); ++i)
            key += "|" + l.slots[i].member.fingerprint + ":" + std::to_string(l.slots[i].team) + (l.slots[i].connecting ? "c" : "") +
                   (i < l.members.size() && l.members[i].hasCharacter ? "h" : "") + (i < l.members.size() ? l.members[i].iconicCharacter : "");
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
    l.voteMaps = vote_.maps;
    l.voteCounts = vote_.counts;
    l.voteRound = vote_.round;
    l.voteOpen = vote_.open;
    l.voteWinner = vote_.winner;
    if (const GameSettings* gs = lobby_.settings)
        for (const SettingField& f : gs->fields) {
            const int i = settingIndex(gs, f.name);
            if (i >= 0 && i < (int)f.values.size()) l.settings[f.name] = f.values[(size_t)i];
        }
    for (const BotOption& o : botOptions()) l.settings[o.name] = botChoiceLabel(o.field, botValue(o.field));
    // the members (the host first) and the human slots; bots fill the rest per team (onlineBotsFor)
    SessionMemberInfo self;
    self.displayName = profile_.playerName();   // D1: the account name
    self.iconicCharacter = selected_.valid && selected_.type == 1 ? selected_.name : std::string();
    self.hasCharacter = !iconicMode() || !self.iconicCharacter.empty();
    self.mapVote = myVote_;
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
    if (iconicMode()) {   // every human holds an iconic character, each one at most once (the host's pick first, then join order)
        if (role_ == SessionRole::Host && !(selected_.valid && selected_.type == 1)) return 6;
        std::vector<std::pair<int, std::string>> held;
        if (role_ == SessionRole::Host) held.push_back({lobby_.localTeam, selected_.name});
        for (const OnlineMember& m : onlineMembers_) {
            for (const auto& h : held)
                if (h.second == m.info.iconicCharacter) return 6;   // another player already holds it (any team)
            held.push_back({m.team, m.info.iconicCharacter});
        }
    }
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
    hs.isPublic = false;   // a private lobby, joined by code (no XP, decision D8); public lobbies come from Quick Match
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
bool isJoinCode(const std::string& in) {
    // The shape only (Systems' decodeTarget parses and checks; a bad code returns JoinFailed{Invalid}): Crockford base32
    // in dash-separated groups - a LAN code (2 groups of 5), an internet code (4 groups), the lobby service's 6 letters -
    // at most 24 characters, dashes optional; an optional "WFC-" prefix is accepted (Systems strips it).
    std::string t = in;
    if (t.size() >= 4 && std::toupper((unsigned char)t[0]) == 'W' && std::toupper((unsigned char)t[1]) == 'F' &&
        std::toupper((unsigned char)t[2]) == 'C' && t[3] == '-') t = t.substr(4);
    if (t.empty() || t.size() > 24) return false;
    bool groupChar = false;
    int chars = 0;
    for (char c : t) {
        if (c == '-') { if (!groupChar) return false; groupChar = false; continue; }
        if (!std::isalnum((unsigned char)c)) return false;
        groupChar = true;
        ++chars;
    }
    return groupChar && chars >= 4;
}
bool isIpv4(const std::string& t) {   // a plain "a.b.c.d" (the session uses its default port)
    int parts = 0, digits = 0;
    for (char c : t) {
        if (c == '.') { if (digits == 0) return false; ++parts; digits = 0; continue; }
        if (!std::isdigit((unsigned char)c) || ++digits > 3) return false;
    }
    return parts == 3 && digits > 0;
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
    if (!isJoinCode(target) && !isAddress(target) && !isIpv4(target)) {
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
    sendMemberInfo();
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

void GameFlow::applyMemberLobby() {   // the host's choices on the member's lobby (map, teams); options are display-only
    // (the lobby exit countdown runs on the member itself from CountdownStarted, as on the host)
    if (memberLobby_.voteRound != vote_.round) myVote_ = -1;   // a new round (None of these re-roll): the vote resets
    vote_.maps = memberLobby_.voteMaps;
    vote_.counts = memberLobby_.voteCounts;
    vote_.round = memberLobby_.voteRound;
    vote_.open = memberLobby_.voteOpen;
    vote_.winner = memberLobby_.voteWinner;
    if (memberLobby_.mapId >= 0 && memberLobby_.mapId != lobby_.mapId) setMapId(memberLobby_.mapId);
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

void GameFlow::updateLoadGateText() {
    int waiting = 0;
    for (const OnlineMember& m : onlineMembers_)
        if (std::find(loadedPeers_.begin(), loadedPeers_.end(), m.info.id) == loadedPeers_.end()) ++waiting;
    std::string t = txt(waiting == 1 ? "$WfcPC.Online.WaitingForOnePlayer" : "$WfcPC.Online.WaitingForPlayers");
    const size_t at = t.find("`n");
    if (at != std::string::npos) t.replace(at, 2, std::to_string(waiting));
    loading_.message = t;
}

void GameFlow::onSessionEvent(const SessionEvent& e) {
    FlowTrace::emit("online.event", {{"type", sessionEventName(e.type)}, {"role", sessionRoleName(role_)}});
    using T = SessionEvent::Type;
    switch (e.type) {
    case T::HostOpened:   // our host session opened (Private Match), or a Quick Match found nothing: we host a public lobby
        if (onlinePending_ != OnlinePending::Hosting && onlinePending_ != OnlinePending::Searching) break;
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
        if (onlinePending_ != OnlinePending::Hosting && onlinePending_ != OnlinePending::Searching) break;
        onlinePending_ = OnlinePending::None;
        closePopup();
        showMessage(txt("$UIText.MessagePrompts.StrictNATTitle"),
                    txt(e.hostFailure == SessionHostFailure::StrictNat ? "$UIText.MessagePrompts.StrictNATMessage" : "$WfcPC.Online.HostNoPath"));
        FlowTrace::emit("online.host", {{"state", "failed"}});
        break;
    case T::JoinProgress: break;   // the joining popup stays; the lobby's own connecting animation follows the join
    case T::Joined:
    case T::MatchInProgress:   // a code join, or a Quick Match reservation accepted
        if (onlinePending_ != OnlinePending::Joining && onlinePending_ != OnlinePending::Searching) break;
        if (onlinePending_ == OnlinePending::Searching) { onlinePending_ = OnlinePending::Joining; closePopup(); }
        selfId_ = e.member;
        enterMemberLobby(e.lobby);
        if (e.type == T::MatchInProgress) memberLobby_.matchRunning = true;
        break;
    case T::JoinFailed:
        if (onlinePending_ == OnlinePending::Joining) onlineJoinFailed(e.joinFailure);
        else if (role_ != SessionRole::Offline && e.joinFailure == SessionJoinFailure::Version)   // a sim id / load hash mismatch
            endSession("$UIText.MessagePrompts.JoinGameInviteFailedTitle", "$Engine.Errors.ClientOutdated", "version");
        break;
    case T::HostLost:   // D9: the original's end of session
        if (role_ != SessionRole::Offline) endSession("$Engine.Errors.ConnectionFailed_Title", "$Engine.Errors.ConnectionFailedHostQuit", "host quit");
        break;
    case T::ConnectionLost:
        if (role_ != SessionRole::Offline)
            endSession("$Engine.Errors.ConnectionFailed_Title", e.timedOut ? "$Engine.Errors.ConnectionTimeout" : "$Engine.Errors.ConnectionFailed",
                       e.timedOut ? "connection timed out" : "connection lost");
        break;
    case T::Kicked:
        if (role_ != SessionRole::Offline)
            endSession("$UIText.MessagePrompts.GenericErrorTitle",
                       e.kickReason == SessionKickReason::Idle ? "$Engine.Errors.KickedForIdling" : "$Engine.AccessControl.KickedMsg",
                       e.kickReason == SessionKickReason::Idle ? "kicked (idle)" : "kicked");
        break;
    case T::Desync:   // everyone back to the game lobby (architecture section 5); the tick goes to the log, not the popup
        if (role_ == SessionRole::Offline) break;
        FlowTrace::emit("online.desync", {{"tick", std::to_string(e.value)}});
        pendingEndTitle_ = txt("$Engine.Errors.ConnectionFailed_Title");
        pendingEndMessage_ = txt("$WfcPC.Online.Desync");
        loadGate_ = false;
        if (level_ == LevelKind::Match) returnToGameLobby();
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
            if (isMuted(e.member)) session_->setMuted(e.member, true);   // a remembered mute (profile)
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
    case T::SearchProgress:   // the original "Reserving space in game `i of `n"
        if (onlinePending_ == OnlinePending::Searching && popup_.open) {
            std::string m = txt("$UIText.MessagePrompts.ReservingMessage");
            size_t at = m.find("`i"); if (at != std::string::npos) m.replace(at, 2, std::to_string(e.searchTried));
            at = m.find("`n"); if (at != std::string::npos) m.replace(at, 2, std::to_string(e.searchOf));
            popup_.message = m;
            ++popup_.serial;
        }
        break;
    case T::SearchCancelled:
        if (onlinePending_ == OnlinePending::Searching) { onlinePending_ = OnlinePending::None; closePopup(); lobby_.playlistId = -1; }
        FlowTrace::emit("online.quickMatch", {{"state", "cancelled"}});
        break;
    case T::MemberVoice:
        voiceStates_[e.member.fingerprint] = VoiceState{e.headsetState, e.voiceVia};
        break;
    case T::CountdownStarted:   // member: the host started the game - the same lobby exit countdown here
        if (role_ == SessionRole::Member) {   // the host's launch record (map, options, seed, roster) for StartLevel
            memberLaunchUrl_ = e.launchUrl;
            launchSeed_ = e.seed;
            if (!e.lobby.slots.empty()) { memberLobby_.slots = e.lobby.slots; memberLobby_.members = e.lobby.members; launchLobby_ = e.lobby; }
        }
        if (role_ == SessionRole::Member && level_ == LevelKind::GameLobby && !lobby_.countingDown) hostRequestsGameStart();
        break;
    case T::PeerLoaded:
        if (loadGate_ && std::find(loadedPeers_.begin(), loadedPeers_.end(), e.member) == loadedPeers_.end()) {
            loadedPeers_.push_back(e.member);
            updateLoadGateText();
        }
        break;
    case T::AllLoaded:
        if (!loadGate_) break;
        loadGate_ = false;
        FlowTrace::emit("online.loadGate", {{"state", "all loaded"}, {"startTick", std::to_string(e.value)}});
        closeLoadingMovie();   // the match countdown (MatchAutoStartCountdown, in sim ticks) follows on every peer
        break;
    case T::PeerDropped: {   // a human left (quit, timeout, kick, load timeout): its slot becomes a bot
        auto it = std::find_if(onlineMembers_.begin(), onlineMembers_.end(), [&](const OnlineMember& m) { return m.info.id == e.member; });
        const std::string name = it != onlineMembers_.end() && !it->info.displayName.empty() ? it->info.displayName : std::string("Player");
        if (it != onlineMembers_.end()) onlineMembers_.erase(it);
        if (loadGate_) updateLoadGateText();
        if (level_ == LevelKind::Match) {
            if (!loadGate_) {   // the kill-feed line once the HUD is up (a drop while loading only swaps the slot)
                std::string line = cat_ ? cat_->localize("TransGame", "TnGameMessage", "LeftGameString") : std::string();
                const size_t at = line.find("`k");
                if (at != std::string::npos) line.replace(at, 2, name);
                pendingGameMessages_.push_back(line);
            }
            SessionSlotSwap sw;
            sw.to = SessionSlotSwap::To::Bot;
            sw.member = e.member;   // Gameplay maps the member to its match player (World::applySlotSwap)
            sw.atTick = e.value;    // the session's agreed tick in World::simTick units (0 = the next tick)
            pendingSwaps_.push_back(sw);
        }
        static const char* const kWhy[] = {"left", "timeout", "kicked", "load timeout"};
        FlowTrace::emit("online.peerDropped", {{"member", e.member.fingerprint}, {"name", name}, {"why", kWhy[(int)e.leaveReason]},
                                               {"inMatch", FlowTrace::boolean(level_ == LevelKind::Match)}});
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
