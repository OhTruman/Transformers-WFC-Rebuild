// PC EXTENSION - the online session boundary (docs/handoff/ONLINE_MP_FRONTEND_TESTS.md section 1). Contract between the
// frontend flow (GameFlow) and the session layer (Systems' ONLINE_MP_ARCHITECTURE.md: transport, join codes, lobby
// relay, lockstep). The frontend calls out; the session layer answers with events, polled once per frontend update, in
// order. Nothing here blocks. Every timeout the frontend shows (InitialConnectTimeout 50 s, ConnectionTimeout 8 s, the
// load gate) counts frontend update time, so tests advance it by updating with a fixed dt (FakeSession.h).
//
// Header only, no behaviour yet: GameFlow has no session set, so the offline flow is unchanged. The original's model
// (RE NETWORK_ARCHITECTURE_2026-10-01): a player-hosted listen server, sessions 'Party' and 'Game', no host migration;
// the reservation beacon's packets (request / response 4 = accepted / tell clients to join / host ready / cancelled /
// heartbeat / return to session) map onto host / join / Joined / JoinFailed / CountdownStarted / HostLost below.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace frontend {

// A peer's identity in a session (CROSSPLAY_PROFILE_STATS.md section 3): the public-key fingerprint plus the display
// name (decision D1: the Accounts screen's account name).
struct SessionMemberId {
    std::string fingerprint;   // "WFC-7QK2-M9TD"
    bool operator==(const SessionMemberId& o) const { return fingerprint == o.fingerprint; }
    bool operator<(const SessionMemberId& o) const { return fingerprint < o.fingerprint; }
};

enum class SessionPlatform { PC, Web };   // decision D11: shown per member; D10: a Web member caps Player Limit to 5 v 5

// What every member publishes about itself (setMemberInfo). The loadout is trusted in phase 1 (decision D3).
struct SessionMemberInfo {
    SessionMemberId id;
    std::string displayName;
    SessionPlatform platform = SessionPlatform::PC;
    int level = 0;                          // displayed level (self-reported)
    bool hasCharacter = false;              // the original lobby gate (D4): "Waiting for all players to choose a character"
    std::string characterSlot;              // custom character slot / specialty ("Soldier", ...)
    std::vector<std::string> loadout;       // weapons / abilities as the lobby message carries them
    int teamRequest = -1;                   // Online.SwitchTeam from this member (-1 none); the host decides
};

// One participant slot of the lobby / match: a human (local or remote) or a bot. Bot counts from Game Mode Options are
// maxima; humans take bot slots (ONLINE_MP_FLOWS.md 3.4).
struct SessionSlot {
    enum class Kind { Bot, Human } kind = Kind::Bot;
    SessionMemberId member;                 // humans only
    int team = 255;                         // 0 Autobots, 1 Decepticons, 255 none (FFA)
    bool connecting = false;                // PlayerList_GFX IsConnecting
};

// The host's lobby, as members receive it (the <CurrentGame:*> / <TnGameSettings:*> answers on a member come from it).
struct SessionLobby {
    std::string gameModeTag;                // "TDM" ...
    std::map<std::string, std::string> settings;   // TnGameSettings field -> value label (incl. the Wfc* bot rows)
    int mapId = -1;
    int mapSelectionMethod = 1;             // 0 rotate / vote, 1 Host's Choice
    int playerLimit = 0;                    // 0 Original, 1 Extended (WfcPlayerLimit)
    std::vector<SessionSlot> slots;
    std::vector<SessionMemberInfo> members;
    bool matchRunning = false;              // decision D6: a joiner waits in the lobby ("Match in progress")
    bool countingDown = false;
    int countdown = 0;                      // lobby exit countdown, seconds
};

// The values Create Game hands to host() (Game Mode Options as the host left them).
struct SessionHostSettings {
    std::string gameModeTag;
    std::map<std::string, std::string> settings;
    int mapSelectionMethod = 1;
    bool isPrivate = true;                  // phase 1: friends / private only (no XP, decision D8)
};

enum class SessionHostFailure { NoPath, StrictNat };                     // D2: PC text / the original NAT text
enum class SessionJoinFailure { Full, Timeout, Version, MapMissing };    // the original texts (ONLINE_MP_UI_STRINGS.md 3)
enum class SessionLeaveReason { Left, Timeout, Kicked, LoadTimeout };
enum class SessionKickReason { Idle, Host };                             // D5: the original 180 s idle kick

// Session -> frontend, in order. `time` is the session's frontend-update time in seconds.
struct SessionEvent {
    enum class Type {
        HostOpened,        // joinCode, address
        HostFailed,        // hostFailure
        JoinProgress,      // connecting
        Joined,            // lobby
        JoinFailed,        // joinFailure
        MatchInProgress,   // lobby (matchRunning true)
        LobbyState,        // lobby (members: replaces the lobby)
        MemberJoined,      // member, info
        MemberLeft,        // member, leaveReason
        CountdownStarted,  // value = start time
        PeerLoaded,        // member
        AllLoaded,         // value = start tick
        PeerDropped,       // member, leaveReason (in a match: kill-feed line + bot swap)
        HostLost,          // D9: end of session
        ConnectionLost,    // member side; timedOut picks ConnectionTimeout vs ConnectionFailed text
        Desync,            // value = tick
        Kicked,            // kickReason
    } type = Type::LobbyState;
    double time = 0.0;
    std::string joinCode, address;
    SessionHostFailure hostFailure = SessionHostFailure::NoPath;
    SessionJoinFailure joinFailure = SessionJoinFailure::Full;
    SessionLeaveReason leaveReason = SessionLeaveReason::Left;
    SessionKickReason kickReason = SessionKickReason::Idle;
    SessionMemberId member;
    SessionMemberInfo info;
    SessionLobby lobby;
    int64_t value = 0;
    bool connecting = false, timedOut = false;
};
inline const char* sessionEventName(SessionEvent::Type t) {
    static const char* const k[] = {"HostOpened", "HostFailed", "JoinProgress", "Joined", "JoinFailed", "MatchInProgress",
                                    "LobbyState", "MemberJoined", "MemberLeft", "CountdownStarted", "PeerLoaded", "AllLoaded",
                                    "PeerDropped", "HostLost", "ConnectionLost", "Desync", "Kicked"};
    const int i = (int)t;
    return i >= 0 && i < (int)(sizeof k / sizeof k[0]) ? k[i] : "?";
}

// Frontend -> session. One instance per frontend runtime; set on GameFlow (setSession), absent = offline.
class ISession {
public:
    virtual ~ISession() = default;
    virtual void host(const SessionHostSettings& settings) = 0;              // Create Game, online
    virtual void join(const std::string& target) = 0;                         // a join code or an address, as typed
    virtual void cancelJoin() = 0;                                            // Escape / Back while joining
    virtual void leave() = 0;                                                 // Quit Game / Quit Match / Back from the game lobby
    virtual void publishLobby(const SessionLobby& lobby) = 0;                 // host: every LobbyState change
    virtual void setMemberInfo(const SessionMemberInfo& info) = 0;            // every member: name, character, team request
    virtual void requestStart() = 0;                                          // host: Start Game
    virtual void reportLoaded(uint64_t simId, uint64_t loadHash) = 0;         // after the map load (load gate)
    virtual void kick(const SessionMemberId& member, SessionKickReason why) = 0;   // host: the idle kick
    // Events since the last poll, in order. `now` = the frontend's update time (seconds).
    virtual std::vector<SessionEvent> poll(double now) = 0;
};

// Match-side plumbing for WFC_SESSIONTEST (Gameplay's harness, ONLINE_MP_FRONTEND_TESTS.md section 4); declared here so
// both halves use one name. A remote human is a match participant of game::ParticipantKind::Remote (GameplayEvents.h),
// fed one command per sim tick; the flow turns PeerDropped / a late join into a slot swap at an agreed tick.
struct SessionSlotSwap {
    int matchPlayer = -1;                   // the participant slot (its score row, team and index stay)
    enum class To { Bot, Remote } to = To::Bot;
    SessionMemberId member;                 // To::Remote: who takes the slot
    int64_t atTick = 0;                     // the agreed tick (the same on every peer)
};

} // namespace frontend
